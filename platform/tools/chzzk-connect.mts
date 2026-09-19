// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only loopback probe, NOT the public multi-user platform server.
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { pathToFileURL } from 'node:url';
import { authorizationUrl, ChzzkApi, ChzzkError } from '../server/chzzk/api.mts';
import type { Channel, Credentials, Tokens } from '../server/chzzk/api.mts';

const nonce = () => randomBytes(32).toString('hex');
const equal = (a: string, b: string) => /^[a-f0-9]{64}$/u.test(a) &&
  timingSafeEqual(Buffer.from(a), Buffer.from(b));
const escape = (value: string) => value.replace(/[&<>"']/gu, c => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
}[c]!));
const COOKIE = 'chatview_probe';

type Options = {
  credentials: Credentials;
  port?: number;
  api?: ChzzkApi;
  now?: () => number;
};

export async function startProbe({ credentials, port = 47831, api = new ChzzkApi(credentials), now = () => performance.now() }: Options) {
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('Invalid loopback port');
  const browserSecret = nonce();
  const csrf = nonce();
  const lifetime = new AbortController();
  let attempt: { state: string; expires: number } | undefined;
  let current: Tokens | undefined;
  let channel: Channel | undefined;
  let notice = '먼저 치지직에 연결하세요. 아직 실시간 채팅을 수신하지 않습니다.';
  let busy = false;
  let origin = '';
  let closed = false;

  const authenticated = (request: IncomingMessage) => {
    const cookies = (request.headers.cookie ?? '').split(';').map(value => value.trim());
    return cookies.some(value => value.startsWith(`${COOKIE}=`) &&
      equal(value.slice(COOKIE.length + 1), browserSecret));
  };

  const reply = (response: ServerResponse, status: number, body: string) => {
    response.writeHead(status, {
      'Content-Type': 'text/html; charset=utf-8',
      'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
      'X-Content-Type-Options': 'nosniff',
      'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; form-action 'self' https://chzzk.naver.com; frame-ancestors 'none'; base-uri 'none'",
    });
    response.end(body);
  };
  const redirect = (response: ServerResponse, location: string) => {
    response.writeHead(303, {
      Location: location, 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
    });
    response.end();
  };
  const page = () => {
    const action = (path: string, title: string) => `<form method="post" action="${path}?csrf=${csrf}"><button>${title}</button></form>`;
    return `<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>ChatView · 치지직 연결 검증</title>
<style>body{font:16px/1.7 system-ui;max-width:720px;margin:48px auto;padding:0 24px}h1{font-size:28px}form{display:inline-block;margin:8px 12px 8px 0}button{font:inherit;padding:8px 14px;cursor:pointer}small{display:block}section{border:1px solid;padding:20px;margin:24px 0}</style>
<h1>ChatView · 치지직 연결 검증</h1><p>개발자용 로컬 인증 검증입니다. 챗뷰 계정 서비스·채팅 HUD·광고 실적 검증이 아닙니다.</p>
<section><p>${escape(notice)}</p>${channel ? `<p>채널: <strong>${escape(channel.channelName)}</strong><br>채널 ID: ${escape(channel.channelId)}</p>` : ''}</section>
${!current ? action('/connect', '치지직 로그인') : action('/session', '소켓용 세션 발급 확인') + action('/refresh', '토큰 갱신 확인') + action('/revoke', '이 앱의 치지직 연결 권한 철회')}
<small>권한 철회는 이 앱과 해당 계정의 모든 Access/Refresh Token을 취소합니다. 다른 기기의 연결도 영향을 받을 수 있습니다.</small>
<p>비밀키·토큰·소켓 URL은 브라우저나 로그에 출력하지 않습니다. 종료 시 로컬 메모리만 비우며 원격 권한을 자동으로 철회하지 않습니다.</p></html>`;
  };

  async function handle(request: IncomingMessage, response: ServerResponse) {
    if (closed) { reply(response, 503, 'Probe closed'); return; }
    if (request.headers.host !== new URL(origin).host) { reply(response, 403, 'Invalid host'); return; }
    if ((request.url ?? '').length > 12_000) { reply(response, 414, 'Request too long'); return; }
    const url = new URL(request.url!, origin);
    if (url.origin !== origin) { reply(response, 403, 'Invalid origin'); return; }
    if (request.method === 'GET' && url.pathname === '/') {
      // Allow the authenticated OAuth return redirect, but never mint browser
      // state for an unsolicited cross-site navigation.
      if (request.headers['sec-fetch-site'] === 'cross-site' && !authenticated(request)) { reply(response, 403, 'Open the printed local address directly'); return; }
      response.setHeader('Set-Cookie', `${COOKIE}=${browserSecret}; HttpOnly; SameSite=Lax; Path=/`);
      reply(response, 200, authenticated(request) ? page() : '<!doctype html><html lang="ko"><meta charset="utf-8"><title>ChatView</title><p><a href="/">치지직 연결 검증 열기</a></p></html>');
      return;
    }
    if (!authenticated(request)) { reply(response, 403, 'Open the local probe in the same browser'); return; }
    if (request.method === 'GET' && url.pathname === '/callback') {
      const state = url.searchParams.get('state') ?? '';
      const code = url.searchParams.get('code') ?? '';
      if (!attempt || now() >= attempt.expires ||
          url.searchParams.getAll('state').length !== 1 ||
          url.searchParams.getAll('code').length !== 1 ||
          !equal(state, attempt.state) || !code || code.length > 8192 || busy) {
        reply(response, 400, 'Invalid, expired, or already used authorization');
        return;
      }
      // Consume before network I/O: callbacks are single-use even on failure.
      attempt = undefined;
      busy = true;
      try {
        const issued = await api.exchangeCode(code, state, lifetime.signal);
        const identified = await api.getUser(issued.accessToken, lifetime.signal);
        if (closed) return;
        current = issued; channel = identified;
        notice = '치지직 인증과 본인 채널 조회 성공. 채팅 소켓 연결·메시지 수신은 아직 검증하지 않았습니다.';
      } catch (error) {
        current = undefined; channel = undefined;
        notice = error instanceof ChzzkError ? error.message : '연결 실패. 다시 로그인하세요.';
      } finally { busy = false; }
      if (!closed) redirect(response, '/');
      return;
    }
    if (request.method !== 'POST') { reply(response, 404, 'Not found'); return; }
    if (request.headers.origin !== origin || url.searchParams.getAll('csrf').length !== 1 ||
        !equal(url.searchParams.get('csrf') ?? '', csrf)) { reply(response, 403, 'Invalid action'); return; }
    if (busy) { reply(response, 409, 'Another action is in progress'); return; }
    request.resume(); // No POST body is interpreted or retained by this probe.
    if (url.pathname === '/connect' && !current) {
      attempt = { state: nonce(), expires: now() + 300_000 };
      redirect(response, authorizationUrl(credentials.clientId, `${origin}/callback`, attempt.state));
      return;
    }
    if (!current || !['/session', '/refresh', '/revoke'].includes(url.pathname)) {
      reply(response, 400, 'Connect first'); return;
    }
    busy = true;
    try {
      if (url.pathname === '/session') {
        // Issue once per explicit click; never publish the confidential URL.
        await api.createUserSession(current.accessToken, lifetime.signal);
        notice = '소켓 연결용 URL 발급 성공. 소켓 연결·이벤트 구독·실제 메시지 수신은 아직 하지 않았습니다.';
      } else if (url.pathname === '/refresh') {
        // No parallel requests and no replay after an ambiguous one-use refresh.
        const previous = current;
        current = undefined;
        const refreshed = await api.refresh(previous.refreshToken, lifetime.signal);
        if (!closed) current = refreshed;
        notice = 'Access/Refresh Token 갱신 성공. 새 토큰은 서버 메모리에만 보관합니다.';
      } else {
        await api.revoke(current.accessToken, lifetime.signal);
        current = undefined; channel = undefined; attempt = undefined;
        notice = '이 앱과 해당 계정의 치지직 토큰을 철회했습니다.';
      }
    } catch (error) {
      notice = error instanceof ChzzkError ? error.message : '요청 실패';
      if (url.pathname === '/refresh' || (error instanceof ChzzkError && [401, 403].includes(error.status))) {
        current = undefined; channel = undefined;
        notice += ' · 다시 로그인하세요. 토큰을 자동 재사용하지 않습니다.';
      }
    } finally { busy = false; }
    if (!closed) redirect(response, '/');
  }

  const server = createServer({ maxHeaderSize: 16_384, requestTimeout: 15_000, headersTimeout: 10_000 }, (request, response) => {
    void handle(request, response).catch(() => {
      if (!response.headersSent) reply(response, 500, 'Local probe failure');
      else response.end();
    });
  });
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => { server.off('error', reject); resolve(); });
  });
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Loopback address unavailable');
  origin = `http://127.0.0.1:${address.port}`;
  return {
    origin,
    async close() {
      closed = true; lifetime.abort(); attempt = undefined; current = undefined; channel = undefined;
      const done = new Promise<void>(resolve => server.close(() => resolve()));
      server.closeAllConnections();
      await done;
    },
  };
}

async function main() {
  const credentials = { clientId: process.env.CHZZK_CLIENT_ID ?? '', clientSecret: process.env.CHZZK_CLIENT_SECRET ?? '' };
  if (!credentials.clientId || !credentials.clientSecret) throw new Error('Set CHZZK_CLIENT_ID and CHZZK_CLIENT_SECRET in your private environment');
  const probe = await startProbe({ credentials });
  console.log(`Open ${probe.origin}/ in your system browser.`);
  console.log(`Register exactly ${probe.origin}/callback as the CHZZK login redirect URL.`);
  console.log('Local authorization probe only. No live chat or advertising credit is recorded.');
  const stop = () => { void probe.close(); };
  process.once('SIGINT', stop); process.once('SIGTERM', stop);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  void main().catch(error => {
    console.error(error instanceof ChzzkError ? error.message : 'Cannot start the local CHZZK probe; check credentials and port 47831.');
    process.exitCode = 1;
  });
}
