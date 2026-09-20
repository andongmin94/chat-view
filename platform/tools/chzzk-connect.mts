// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only loopback probe, NOT the public multi-user platform server.
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { ChzzkChatSession } from '../server/chzzk/session.mts';
import type { SocketFactory } from '../server/chzzk/socket.mts';
import { ChatPreview } from './chat-preview.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { SessionStore } from '../server/chat/session-store.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { DisplaySessionGateway } from '../server/chat/display-session-gateway.mts';
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
  sessionDatabase?: string;
};

export async function startProbe({ credentials, port = 47831, api = new ChzzkApi(credentials), now = () => performance.now(), socketFactory, sessionDatabase = ':memory:' }: Options) {
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('Invalid loopback port');
  const sessions = new SessionStore(sessionDatabase);
  const browserSecret = nonce();
  const csrf = nonce();
  const lifetime = new AbortController();
  let attempt: { state: string; expires: number; loginId?: string } | undefined;
  let current: Tokens | undefined;
  let channel: Channel | undefined;
  let notice = '먼저 치지직에 연결하세요. 아직 실시간 채팅을 수신하지 않습니다.';
  let busy = false;
  let origin = '';
  let closed = false;
  let chat: ChzzkChatSession | undefined;
  const preview = new ChatPreview(() => chat?.snapshot() ?? { state: 'idle', received: 0, messages: [] });
  let tokenDeadline = 0;
  const displayAccess = new DisplayAccess(() => current && channel && now() < tokenDeadline
    ? { id: channel.channelId, expiresAt: tokenDeadline } : undefined, now, sessions);
  const display = new DisplayGateway(displayAccess, () => origin,
    () => chat?.snapshot() ?? { state: 'idle', received: 0, messages: [] });
  const displaySession = new DisplaySessionGateway(displayAccess, () => origin, () => display.changed());
  const revokeDisplays = () => { displayAccess.clear(); display.changed(); };
  const signoutDisplays = () => { displaySession.login.clear(); if (channel) sessions.removeOwner(channel.channelId); revokeDisplays(); };
  let tokenExpiry: ReturnType<typeof setTimeout> | undefined;
  const expireTokens = () => {
    current = undefined; channel = undefined; attempt = undefined; revokeDisplays();
    void chat?.stop(); notice = '인증이 만료됐습니다. 다시 로그인하세요. 저장한 연결은 같은 채널 승인 뒤 다시 연결됩니다.';
  };
  const armExpiry = (tokens: Tokens) => {
    clearTimeout(tokenExpiry); tokenDeadline = now() + tokens.expiresIn * 1000;
    tokenExpiry = setTimeout(expireTokens, Math.min(tokens.expiresIn * 1000, 2147483647));
    tokenExpiry.unref();
  };
  async function startChat() {
    if (!current || !channel || now() >= tokenDeadline) throw new DisplayAccessError(401);
    if (chat?.snapshot().state === 'subscribed') return;
    if (chat && ['connecting', 'subscribing'].includes(chat.snapshot().state)) throw new DisplayAccessError(409);
    const candidate = new ChzzkChatSession(api, () => {
      if (chat !== candidate) return;
      if (candidate.snapshot().state === 'revoked') {
        signoutDisplays(); current = undefined; channel = undefined; clearTimeout(tokenExpiry);
      }
      preview.changed(); display.changed();
    }, socketFactory);
    chat = candidate;
    await candidate.start(current.accessToken, channel!.channelId, lifetime.signal);
    if (chat?.snapshot().state !== 'subscribed') throw new DisplayAccessError(503);
  }
  const authenticated = (request: IncomingMessage) => {
    const cookies = (request.headers.cookie ?? '').split(';').map(value => value.trim());
    return cookies.some(value => value.startsWith(`${COOKIE}=`) && equal(value.slice(COOKIE.length + 1), browserSecret));
  };
  const reply = (response: ServerResponse, status: number, body: string) => {
    response.writeHead(status, {
      'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
      'X-Content-Type-Options': 'nosniff',
      'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; form-action 'self' https://chzzk.naver.com; frame-ancestors 'none'; base-uri 'none'",
    }); response.end(body);
  };
  const redirect = (response: ServerResponse, location: string) => {
    response.writeHead(303, { Location: location, 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer' }); response.end();
  };
  const page = () => {
    const action = (path: string, title: string) => `<form method="post" action="${path}?csrf=${csrf}"><button>${title}</button></form>`;
    return `<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>ChatView · 치지직 연결 검증</title>
<style>body{font:16px/1.7 system-ui;max-width:720px;margin:48px auto;padding:0 24px}h1{font-size:28px}form{display:inline-block;margin:8px 12px 8px 0}button{font:inherit;padding:8px 14px;cursor:pointer}small{display:block}section{border:1px solid;padding:20px;margin:24px 0}</style>
<h1>ChatView · 치지직 연결 검증</h1><p>개발자용 로컬 연동입니다. 배포된 계정 서비스·투컴 영상·광고 실적 검증이 아닙니다.</p>
<section><p>${escape(notice)}</p>${channel ? `<p>채널: <strong>${escape(channel.channelName)}</strong><br>채널 ID: ${escape(channel.channelId)}</p>` : ''}</section>
${!current ? action('/connect', '치지직 로그인') : action('/chat/start', '채팅 수신 시작 / 재연결') + action('/chat/stop', '채팅 수신 중지') + action('/refresh', '토큰 갱신 확인') + action('/revoke', '이 앱의 치지직 연결 권한 철회')}
<p><a href="/chat">챗뷰 자체 채팅 화면 열기</a></p>
${current ? action('/display/ticket', '표시 연결용 1회 키 발급') + action('/display/revoke', '표시 연결 모두 해제') : ''}
<small>HUD에서 ‘이 PC에서 연결 유지’를 선택하면 다음부터 다시 키를 넣지 않고 연결합니다. 별도 기기 등록은 없습니다.</small>
<small>‘표시 연결 모두 해제’는 저장한 연결 승인도 취소합니다. 치지직 권한 철회와는 별개입니다.</small>
<p>이 도구는 개발용입니다. 서비스 재시작 후에는 치지직 채널을 다시 승인해야 합니다. 표시 승인은 해시로만 저장하고, 메시지는 저장하지 않습니다.</p></html>`;
  };
  const loginPage = (id: string) => {
    const intent = displaySession.login.view(id);
    const action = (name: string, title: string) => `<form method="post" action="/login/${id}/${name}?csrf=${csrf}"><button>${title}</button></form>`;
    return `<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>ChatView · 로그인</title>
<style>body{font:16px/1.7 system-ui;max-width:600px;margin:48px auto;padding:24px}button{font:inherit;padding:12px 18px}form{display:inline-block;margin:12px 12px 0 0}</style>
<h1>챗뷰에 채팅 연결</h1><p>앱에서 직접 시작한 요청인지 확인하세요. 확인 번호: <strong>${intent.code}</strong></p>
<p>${intent.remember ? '이 PC에서 연결을 유지합니다. 공용 PC라면 취소하세요.' : '이번 실행에서만 연결합니다.'}</p>
${current && channel ? `<p>연결할 채널: <strong>${escape(channel.channelName)}</strong></p>${action('approve', '이 채널의 채팅 연결')}` : action('connect', '치지직으로 로그인')}
${action('deny', '취소')}<p>개발 연동 단계입니다. 광고 수익이나 투컴 영상 검증이 아닙니다.</p></html>`;
  };
  async function handle(request: IncomingMessage, response: ServerResponse) {
    if (closed) { reply(response, 503, 'Probe closed'); return; }
    if (request.headers.host !== new URL(origin).host) { reply(response, 403, 'Invalid host'); return; }
    if ((request.url ?? '').length > 12_000) { reply(response, 414, 'Request too long'); return; }
    const url = new URL(request.url!, origin);
    if (url.origin !== origin) { reply(response, 403, 'Invalid origin'); return; }
    if (displaySession.handle(request, response)) return;
    if (display.handle(request, response)) return;
    if (request.method === 'GET' && url.pathname === '/') {
      if (request.headers['sec-fetch-site'] === 'cross-site' && !authenticated(request)) { reply(response, 403, 'Open the printed local address directly'); return; }
      response.setHeader('Set-Cookie', `${COOKIE}=${browserSecret}; HttpOnly; SameSite=Lax; Path=/`);
      reply(response, 200, authenticated(request) ? page() : '<!doctype html><html lang="ko"><meta charset="utf-8"><title>ChatView</title><p><a href="/">치지직 연결 검증 열기</a></p></html>');
      return;
    }
    const loginLanding = /^\/login\/([a-f0-9]{32})$/u.exec(url.pathname);
    if (request.method === 'GET' && loginLanding) {
      if (url.search || (request.headers['sec-fetch-site'] === 'cross-site' && !authenticated(request)) ||
          (request.headers.origin && request.headers.origin !== origin)) { reply(response, 403, 'Open login from ChatView'); return; }
      try {
        displaySession.login.view(loginLanding[1]!);
        if (!authenticated(request)) {
          response.setHeader('Set-Cookie', `${COOKIE}=${browserSecret}; HttpOnly; SameSite=Lax; Path=/`);
          redirect(response, url.pathname); return;
        }
        reply(response, 200, loginPage(loginLanding[1]!));
      } catch (error) { reply(response, error instanceof DisplayAccessError ? error.status : 500, 'Login request unavailable'); }
      return;
    }
    if (!authenticated(request)) { reply(response, 403, 'Open the local probe in the same browser'); return; }
    if (request.method === 'GET' && preview.handles(url.pathname)) {
      if (request.headers['sec-fetch-site'] === 'cross-site' || (request.headers.origin && request.headers.origin !== origin)) {
        reply(response, 403, 'Invalid preview origin'); return;
      }
      if (url.pathname === '/chat/events') { preview.open(response); return; }
      if (await preview.asset(url.pathname, response)) return;
    }
    if (request.method === 'GET' && url.pathname === '/callback') {
      const state = url.searchParams.get('state') ?? '', code = url.searchParams.get('code') ?? '';
      if (!attempt || now() >= attempt.expires || url.searchParams.getAll('state').length !== 1 ||
          url.searchParams.getAll('code').length !== 1 || !equal(state, attempt.state) || !code || code.length > 8192 || busy) {
        reply(response, 400, 'Invalid, expired, or already used authorization'); return;
      }
      const loginId = attempt.loginId;
      attempt = undefined; busy = true;
      if (loginId) {
        try { displaySession.login.view(loginId); } catch { busy = false; reply(response, 410, 'Login request expired'); return; }
      }
      try {
        const issued = await api.exchangeCode(code, state, lifetime.signal);
        const identified = await api.getUser(issued.accessToken, lifetime.signal);
        if (closed) return;
        revokeDisplays(); current = issued; channel = identified; armExpiry(issued);
        notice = '인증과 본인 채널 조회 성공. 채팅 수신을 시작한 뒤 자체 채팅 화면을 여세요.';
      } catch (error) {
        current = undefined; channel = undefined;
        notice = error instanceof ChzzkError ? error.message : '연결 실패. 다시 로그인하세요.';
      } finally { busy = false; }
      if (!closed) redirect(response, loginId ? `/login/${loginId}` : '/'); return;
    }
    if (request.method !== 'POST') { reply(response, 404, 'Not found'); return; }
    if (request.headers.origin !== origin || url.searchParams.getAll('csrf').length !== 1 ||
        !equal(url.searchParams.get('csrf') ?? '', csrf)) { reply(response, 403, 'Invalid action'); return; }
    if (busy) { reply(response, 409, 'Another action is in progress'); return; }
    request.resume();
    const loginAction = /^\/login\/([a-f0-9]{32})\/(connect|approve|deny)$/u.exec(url.pathname);
    if (loginAction) {
      const id = loginAction[1]!, action = loginAction[2]!;
      try {
        displaySession.login.view(id);
        if (action === 'deny') {
          displaySession.login.deny(id);
          if (attempt?.loginId === id) attempt = undefined;
          reply(response, 200, '<!doctype html><meta charset="utf-8"><p>연결을 취소했습니다. 챗뷰로 돌아가세요.</p>'); return;
        }
        if (action === 'connect') {
          if (current) { redirect(response, `/login/${id}`); return; }
          if (attempt && now() < attempt.expires) throw new DisplayAccessError(409);
          attempt = { state: nonce(), expires: now() + 300000, loginId: id };
          redirect(response, authorizationUrl(credentials.clientId, `${origin}/callback`, attempt.state)); return;
        }
        busy = true;
        try { await startChat(); displaySession.login.approve(id); }
        finally { busy = false; }
        reply(response, 200, '<!doctype html><meta charset="utf-8"><h1>채팅 연결 승인 완료</h1><p>챗뷰로 돌아가세요. 표시 키를 복사할 필요 없이 자동으로 연결됩니다.</p>');
      } catch (error) { reply(response, error instanceof DisplayAccessError ? error.status : 502, 'Login request could not be completed'); }
      return;
    }
    if (url.pathname === '/connect' && !current) {
      attempt = { state: nonce(), expires: now() + 300_000 };
      redirect(response, authorizationUrl(credentials.clientId, `${origin}/callback`, attempt.state)); return;
    }
    if (['/display/ticket', '/display/revoke'].includes(url.pathname)) {
      try {
        if (url.pathname === '/display/ticket') {
          const ticket = displayAccess.issue();
          response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store',
            'Referrer-Policy': 'no-referrer', 'X-Content-Type-Options': 'nosniff' });
          response.end(JSON.stringify(ticket)); return;
        }
        displayAccess.revokeSessions(); display.changed();
        notice = '표시 연결과 저장된 연결 승인을 해제했습니다. 치지직 권한은 유지됩니다.';
        redirect(response, '/');
      } catch (error) {
        reply(response, error instanceof DisplayAccessError ? error.status : 500, 'Display access unavailable');
      }
      return;
    }
    if (!current || !['/chat/start', '/chat/stop', '/refresh', '/revoke'].includes(url.pathname)) { reply(response, 400, 'Connect first'); return; }
    busy = true;
    try {
      if (url.pathname === '/chat/start') {
        if (chat && ['connecting', 'subscribing', 'subscribed'].includes(chat.snapshot().state)) {
          notice = '이 채널의 채팅 연결이 이미 실행 중입니다.';
        } else {
          await startChat();
          notice = '채팅 구독 확인 완료. 실제 수신 여부는 채팅 화면에서 별도로 표시합니다.';
        }
      } else if (url.pathname === '/chat/stop') {
        const acknowledged = await chat?.stop();
        notice = acknowledged === false ? '로컬 채팅 수신은 중지했습니다. 원격 구독 취소는 확인하지 못했습니다.' : '채팅 수신을 중지했습니다.';
      } else if (url.pathname === '/refresh') {
        revokeDisplays(); await chat?.stop();
        const previous = current; current = undefined;
        const refreshed = await api.refresh(previous.refreshToken, lifetime.signal);
        if (!closed) { current = refreshed; armExpiry(refreshed); }
        notice = 'Access/Refresh Token 갱신 성공. 새 토큰은 서버 메모리에만 보관합니다.';
      } else {
        signoutDisplays(); await chat?.stop(); await api.revoke(current.accessToken, lifetime.signal);
        clearTimeout(tokenExpiry); current = undefined; channel = undefined; attempt = undefined;
        notice = '이 앱과 해당 계정의 치지직 토큰을 철회했습니다.';
      }
    } catch (error) {
      notice = error instanceof ChzzkError ? error.message : '요청 실패';
      if (url.pathname === '/refresh' || (error instanceof ChzzkError && [401, 403].includes(error.status))) {
        current = undefined; channel = undefined; clearTimeout(tokenExpiry); revokeDisplays();
        await chat?.stop(); notice += ' · 다시 로그인하세요. 토큰을 자동 재사용하지 않습니다.';
      }
    } finally { busy = false; }
    if (!closed) redirect(response, '/');
  }
  const server = createServer({ maxHeaderSize: 16_384, requestTimeout: 15_000, headersTimeout: 10_000 }, (request, response) => {
    void handle(request, response).catch(() => {
      if (!response.headersSent) reply(response, 500, 'Local probe failure'); else response.end();
    });
  });
  server.on('upgrade', (request, socket, head) => display.upgrade(request, socket, head));
  try {
    await new Promise<void>((resolve, reject) => {
      server.once('error', reject);
      server.listen(port, '127.0.0.1', () => { server.off('error', reject); resolve(); });
    });
  } catch (error) { display.close(); sessions.close(); throw error; }
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Loopback address unavailable');
  origin = `http://127.0.0.1:${address.port}`;
  return {
    origin,
    async close() {
      if (closed) return;
      closed = true; displaySession.close(); display.close();
      const chatStopped = chat?.stop();
      lifetime.abort(); clearTimeout(tokenExpiry); preview.close();
      attempt = undefined; current = undefined; channel = undefined;
      const done = new Promise<void>(resolve => server.close(() => resolve()));
      server.closeAllConnections(); await done; await chatStopped; sessions.close();
    },
  };
}
async function main() {
  const credentials = { clientId: process.env.CHZZK_CLIENT_ID ?? '', clientSecret: process.env.CHZZK_CLIENT_SECRET ?? '' };
  if (!credentials.clientId || !credentials.clientSecret) throw new Error('Set CHZZK_CLIENT_ID and CHZZK_CLIENT_SECRET in your private environment');
  const probe = await startProbe({ credentials,
    sessionDatabase: fileURLToPath(new URL('../.local/chat-sessions.sqlite', import.meta.url)) });
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
