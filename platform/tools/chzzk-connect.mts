// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only loopback probe, NOT the public multi-user platform server.
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { pathToFileURL } from 'node:url';
import { ChzzkChatSession } from '../server/chzzk/session.mts';
import type { SocketFactory } from '../server/chzzk/socket.mts';
import { ChatPreview } from './chat-preview.mts';
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
  socketFactory?: SocketFactory;
};

export async function startProbe({ credentials, port = 47831, api = new ChzzkApi(credentials), now = () => performance.now(), socketFactory }: Options) {
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
  let chat: ChzzkChatSession | undefined;
  const preview = new ChatPreview(() => chat?.snapshot() ?? { state: 'idle', received: 0, messages: [] });
  let tokenExpiry: ReturnType<typeof setTimeout> | undefined;
  const expireTokens = () => {
    current = undefined; channel = undefined; attempt = undefined;
    void chat?.stop(); notice = '인증이 만료됐습니다. 다시 로그인하세요.';
  };
  const armExpiry = (tokens: Tokens) => {
    clearTimeout(tokenExpiry);
    tokenExpiry = setTimeout(expireTokens, Math.min(tokens.expiresIn * 1000, 2147483647));
    tokenExpiry.unref();
  };

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
${!current ? action('/connect', '치지직 로그인') : action('/chat/start', '채팅 수신 시작 / 재연결') + action('/chat/stop', '채팅 수신 중지') + action('/refresh', '토큰 갱신 확인') + action('/revoke', '이 앱의 치지직 연결 권한 철회')}
<p><a href="/chat">챗뷰 자체 채팅 화면 열기</a></p>
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
    if (request.method === 'GET' && preview.handles(url.pathname)) {
      if (request.headers['sec-fetch-site'] === 'cross-site' ||
          (request.headers.origin && request.headers.origin !== origin)) {
        reply(response, 403, 'Invalid preview origin'); return;
      }
      if (url.pathname === '/chat/events') { preview.open(response); return; }
      if (await preview.asset(url.pathname, response)) return;
    }
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
        current = issued; channel = identified; armExpiry(issued);
        notice = '인증과 본인 채널 조회 성공. 채팅 수신을 시작한 뒤 자체 채팅 화면을 여세요.';
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
    if (!current || !['/chat/start', '/chat/stop', '/refresh', '/revoke'].includes(url.pathname)) {
      reply(response, 400, 'Connect first'); return;
    }
    busy = true;
    try {
      if (url.pathname === '/chat/start') {
        if (chat && ['connecting', 'subscribing', 'subscribed'].includes(chat.snapshot().state)) {
          notice = '이 채널의 채팅 연결이 이미 실행 중입니다.';
        } else {
          const candidate = new ChzzkChatSession(api, () => {
            if (chat !== candidate) return;
            if (candidate.snapshot().state === 'revoked') {
              current = undefined; channel = undefined; clearTimeout(tokenExpiry);
            }
            preview.changed();
          }, socketFactory);
          chat = candidate;
          await candidate.start(current.accessToken, channel!.channelId, lifetime.signal);
          notice = '채팅 구독 확인 완료. 실제 수신 여부는 채팅 화면에서 별도로 표시합니다.';
        }
      } else if (url.pathname === '/chat/stop') {
        const acknowledged = await chat?.stop();
        notice = acknowledged === false ? '로컬 채팅 수신은 중지했습니다. 원격 구독 취소는 확인하지 못했습니다.' : '채팅 수신을 중지했습니다.';
      } else if (url.pathname === '/refresh') {
        await chat?.stop();
        // No parallel requests and no replay after an ambiguous one-use refresh.
        const previous = current;
        current = undefined;
        const refreshed = await api.refresh(previous.refreshToken, lifetime.signal);
        if (!closed) { current = refreshed; armExpiry(refreshed); }
        notice = 'Access/Refresh Token 갱신 성공. 새 토큰은 서버 메모리에만 보관합니다.';
      } else {
        await chat?.stop();
        await api.revoke(current.accessToken, lifetime.signal);
        clearTimeout(tokenExpiry);
        current = undefined; channel = undefined; attempt = undefined;
        notice = '이 앱과 해당 계정의 치지직 토큰을 철회했습니다.';
      }
    } catch (error) {
      notice = error instanceof ChzzkError ? error.message : '요청 실패';
      if (url.pathname === '/refresh' || (error instanceof ChzzkError && [401, 403].includes(error.status))) {
        current = undefined; channel = undefined; clearTimeout(tokenExpiry);
        await chat?.stop();
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
      closed = true;
      const chatStopped = chat?.stop();
      lifetime.abort(); clearTimeout(tokenExpiry); preview.close();
      attempt = undefined; current = undefined; channel = undefined;
      const done = new Promise<void>(resolve => server.close(() => resolve()));
      server.closeAllConnections();
      await done; await chatStopped;
    },
  };
}

async function main() {
  const credentials = { clientId: process.env.CHZZK_CLIENT_ID ?? '', clientSecret: process.env.CHZZK_CLIENT_SECRET ?? '' };
  if (!credentials.clientId || !credentials.clientSecret) throw new Error('Set CHZZK_CLIENT_ID and CHZZK_CLIENT_SECRET in your private environment');
  const probe = await startProbe({ credentials });
  console.log(`Open ${probe.origin}/ in your system browser.`);
  console.log(`Register exactly ${probe.origin}/callback as the CHZZK login redirect URL.`);
  console.log('Developer-only chat preview. Messages stay in bounded memory; no advertising credit.');
  const stop = () => { void probe.close(); };
  process.once('SIGINT', stop); process.once('SIGTERM', stop);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  void main().catch(error => {
    console.error(error instanceof ChzzkError ? error.message : 'Cannot start the local CHZZK probe; check credentials and port 47831.');
    process.exitCode = 1;
  });
}
