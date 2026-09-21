// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes, timingSafeEqual } from 'node:crypto';
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Duplex } from 'node:stream';
import { BrowserLogin } from '../chat/browser-login.mts';
import { DisplayAccessError } from '../chat/display-access.mts';
import { hashSecret, validSecret } from '../chat/session-store.mts';
import { Creators } from './creators.mts';

const nonce = () => randomBytes(32).toString('hex');
const escape = (value: string) => value.replace(/[&<>"']/gu,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]!));
const COOKIE = '__Host-chatview';
const BROWSER_MS = 60 * 60 * 1000;
type Browser = { key: string; csrf: string; expires: number; controller: AbortController; owner?: string;
  attempt?: { digest: string; loginId: string; expires: number } };

function auth(request: IncomingMessage, scheme: string): string {
  let count = 0;
  for (let i = 0; i < request.rawHeaders.length; i += 2)
    if (request.rawHeaders[i]?.toLowerCase() === 'authorization') count++;
  const value = request.headers.authorization;
  if (count !== 1 || !value?.startsWith(`${scheme} `) || !validSecret(value.slice(scheme.length + 1)))
    throw new DisplayAccessError(401);
  return value.slice(scheme.length + 1);
}
function empty(request: IncomingMessage) {
  if (request.headers['transfer-encoding'] !== undefined ||
      (request.headers['content-length'] !== undefined && request.headers['content-length'] !== '0'))
    throw new DisplayAccessError(400);
}
async function csrfForm(request: IncomingMessage, expected: string) {
  if (!/^application\/x-www-form-urlencoded(?:;\s*charset=utf-8)?$/iu.test(request.headers['content-type'] ?? ''))
    throw new DisplayAccessError(400);
  let body = '';
  for await (const chunk of request) {
    body += Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
    if (Buffer.byteLength(body) > 2048) throw new DisplayAccessError(413);
  }
  const form = new URLSearchParams(body), value = form.get('csrf');
  if (form.size !== 1 || !validSecret(value) ||
      !timingSafeEqual(Buffer.from(value), Buffer.from(expected))) throw new DisplayAccessError(403);
}

// HTTP routes shared by the HTTPS listener and synthetic HTTP-flow tests.
// No proxy-header trust, local-probe cookie, manual ticket issuance, or public
// channel selector. Native secrets appear only in authenticated JSON responses.
export class PlatformApplication {
  readonly login = new BrowserLogin();
  #creators: Creators;
  #origin: string;
  #authorizeUrl: (state: string) => string;
  #browsers = new Map<string, Browser>();
  #closed = false;
  constructor(creators: Creators, origin: string, authorizeUrl: (state: string) => string) {
    const url = new URL(origin);
    if (url.origin !== origin || url.username || url.password || url.pathname !== '/' ||
        (url.protocol !== 'https:' && !(url.protocol === 'http:' && url.hostname === '127.0.0.1')))
      throw new Error('Invalid ChatView origin');
    this.#creators = creators; this.#origin = origin; this.#authorizeUrl = authorizeUrl;
  }
  #check(request: IncomingMessage) {
    if (this.#closed) throw new DisplayAccessError(503);
    const origin = new URL(this.#origin);
    if (origin.protocol === 'https:' &&
        (request.socket as typeof request.socket & { encrypted?: boolean }).encrypted !== true) throw new DisplayAccessError(403);
    if (request.headers.host !== origin.host ||
        (request.headers.origin !== undefined && request.headers.origin !== origin.origin)) throw new DisplayAccessError(403);
    if (!request.url?.startsWith('/') || request.url.startsWith('//') || request.url.length > 12_000)
      throw new DisplayAccessError(400);
  }
  #headers(response: ServerResponse) {
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Referrer-Policy', 'no-referrer');
    response.setHeader('X-Content-Type-Options', 'nosniff');
    response.setHeader('Content-Security-Policy', "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
    if (this.#origin.startsWith('https:')) response.setHeader('Strict-Transport-Security', 'max-age=31536000');
  }
  #json(response: ServerResponse, value: unknown) {
    response.writeHead(200, { 'Content-Type': 'application/json' }); response.end(JSON.stringify(value));
  }
  #page(response: ServerResponse, title: string, content: string) {
    response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    response.end(`<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>ChatView · ${title}</title>
<style>body{font:16px/1.7 system-ui;max-width:720px;margin:48px auto;padding:0 24px}h1{font-size:28px}form{margin:12px 0}button{font:inherit;padding:10px 16px;cursor:pointer}small{display:block}</style>
<h1>${title}</h1>${content}<p><small>개발 서비스입니다. PC 역할 연결은 HUD 없는 영상 전달이나 실제 광고 노출의 증거가 아닙니다.</small></p></html>`);
  }
  #redirect(response: ServerResponse, location: string) {
    response.writeHead(303, { Location: location }); response.end();
  }
  #prune() {
    for (const b of this.#browsers.values()) if (b.expires <= Date.now()) this.#drop(b);
  }
  #drop(b: Browser) { b.controller.abort(); this.#browsers.delete(b.key); }
  #browser(request: IncomingMessage): Browser | undefined {
    this.#prune();
    const values = (request.headers.cookie ?? '').split(';').map(v => v.trim()).filter(v => v.startsWith(`${COOKIE}=`));
    if (values.length !== 1) return undefined;
    const token = values[0]!.slice(COOKIE.length + 1);
    return validSecret(token) ? this.#browsers.get(hashSecret(token)) : undefined;
  }
  #createBrowser(response: ServerResponse, owner?: string): Browser {
    this.#prune();
    if (this.#browsers.size >= 256) throw new DisplayAccessError(429);
    const token = nonce(), b: Browser = { key: hashSecret(token), csrf: nonce(),
      expires: Date.now() + BROWSER_MS, controller: new AbortController(), owner };
    this.#browsers.set(b.key, b);
    response.setHeader('Set-Cookie', `${COOKIE}=${token}; Secure; HttpOnly; SameSite=Lax; Path=/; Max-Age=3600`);
    return b;
  }
  #live(b: Browser) {
    if (this.#closed || b.expires <= Date.now() || this.#browsers.get(b.key) !== b) throw new DisplayAccessError(401);
  }
  #form(b: Browser, path: string, label: string) {
    return `<form method="post" action="${path}"><input type="hidden" name="csrf" value="${b.csrf}"><button>${label}</button></form>`;
  }
  async handle(request: IncomingMessage, response: ServerResponse): Promise<void> {
    this.#headers(response);
    try {
      this.#check(request);
      const url = new URL(request.url!, this.#origin);
      if (url.origin !== this.#origin) throw new DisplayAccessError(403);
      if (url.pathname === '/callback' && request.method === 'GET') {
        const b = this.#browser(request), state = url.searchParams.get('state'), code = url.searchParams.get('code');
        if (!b?.attempt || b.attempt.expires <= Date.now() || !validSecret(state) || !code || code.length > 8192 ||
            url.searchParams.getAll('state').length !== 1 || url.searchParams.getAll('code').length !== 1 ||
            !timingSafeEqual(Buffer.from(hashSecret(state)), Buffer.from(b.attempt.digest))) throw new DisplayAccessError(400);
        const id = b.attempt.loginId;
        b.attempt = undefined; // Consume before upstream I/O, even on failure.
        this.login.view(id);
        const channel = await this.#creators.authorize(code, state,
          AbortSignal.any([b.controller.signal, AbortSignal.timeout(Math.max(1, b.expires - Date.now()))]));
        this.#live(b); this.login.view(id);
        this.#drop(b); this.#createBrowser(response, channel.channelId);
        this.#redirect(response, `/login/${id}`); return;
      }
      if (request.headers['sec-fetch-site'] === 'cross-site' || url.search) throw new DisplayAccessError(403);
      if (url.pathname.startsWith('/display/')) {
        if (request.method !== 'POST') throw new DisplayAccessError(405);
        empty(request);
        const remember = request.headers['x-chatview-remember'];
        if (remember !== undefined && (url.pathname !== '/display/login' || remember !== '1')) throw new DisplayAccessError(400);
        const polling = /^\/display\/login\/([a-f0-9]{32})$/u.exec(url.pathname);
        if (url.pathname === '/display/login') {
          this.#json(response, this.login.start(auth(request, 'ChatView-Challenge'), remember === '1')); return;
        }
        if (polling) {
          const result = this.login.poll(polling[1]!, auth(request, 'ChatView-Login'));
          if (result.status === 'approved') {
            const session = this.#creators.sessions.find(result.lease.sessionToken);
            if (!session) throw new DisplayAccessError(401);
            this.#creators.track(result.lease, session.owner);
          }
          this.#json(response, result); return;
        }
        if (url.pathname === '/display/refresh') {
          this.#json(response, await this.#creators.resume(auth(request, 'ChatView-Session'))); return;
        }
        if (url.pathname === '/display/signout') {
          this.#creators.signout(auth(request, 'ChatView-Session'));
          this.#json(response, { signedOut: true }); return;
        }
        // The public service intentionally has no manual display-ticket route.
        throw new DisplayAccessError(404);
      }
      if (url.pathname === '/broadcast/session' && request.method === 'POST') {
        empty(request);
        const session = this.#creators.sessions.find(auth(request, 'ChatView-Session'));
        if (!session?.membership) throw new DisplayAccessError(403);
        this.#json(response, { membership: session.membership, captureState: 'unverified' }); return;
      }
      const landing = /^\/login\/([a-f0-9]{32})$/u.exec(url.pathname);
      let b = this.#browser(request);
      if (request.method === 'GET' && landing) {
        const id = landing[1]!, intent = this.login.view(id);
        if (!b) { this.#createBrowser(response); this.#redirect(response, url.pathname); return; }
        const account = b.owner ? this.#creators.describe(b.owner) : undefined;
        this.#page(response, '챗뷰에 채팅 연결', `<p>앱에서 직접 시작한 요청인지 확인하세요. 확인 번호: <strong>${intent.code}</strong></p>
<p>${intent.remember ? '이 PC에서 연결을 유지합니다. 공용 PC에서는 취소하세요.' : '이번 실행에서만 연결합니다.'}</p>
${account?.authorized ? `<p>채널: <strong>${escape(account.channel.channelName)}</strong></p>
${this.#form(b, `/login/${id}/approve/gaming`, '게임 PC · 개인 HUD로 연결')}
${this.#form(b, `/login/${id}/approve/streaming`, '송출 PC · OBS 역할로 연결')}
<p>송출 역할은 한 연결에만 승인됩니다. 다른 PC로 옮길 때는 기존 연결을 먼저 해제하세요.</p><p><a href="/account">내 연결 관리</a></p>`
: this.#form(b, `/login/${id}/connect`, '치지직으로 로그인')}
${this.#form(b, `/login/${id}/deny`, '취소')}`); return;
      }
      if (request.method === 'GET' && url.pathname === '/healthz') { response.writeHead(204); response.end(); return; }
      if (request.method === 'GET' && (url.pathname === '/' || url.pathname === '/account')) {
        if (!b?.owner) { this.#page(response, 'ChatView', '<p>챗뷰 앱에서 로그인 버튼을 눌러 채팅을 연결하세요.</p>'); return; }
        const account = this.#creators.describe(b.owner);
        this.#page(response, '내 채팅과 연결', `<p>채널: <strong>${escape(account.channel.channelName)}</strong></p>
<p>채팅 상태: ${escape(account.chatState)} · ${account.authorized ? '치지직 승인 유효' : '앱에서 치지직 재로그인 필요'}</p>
${account.connections.map(connection => `<section><p>${connection.role === 'gaming' ? '게임 PC · 개인 HUD' : '송출 PC · OBS 역할'}<br>
연결: ${connection.connectionId}</p>${this.#form(b!, `/connections/${connection.connectionId}/revoke`, '이 연결 해제')}</section>`).join('')}
${this.#form(b, '/account/reconnect', '채팅 다시 연결')}
${this.#form(b, '/account/revoke', '모든 PC 연결 및 치지직 권한 철회')}
${this.#form(b, '/logout', '이 브라우저만 로그아웃')}
<p>서비스 재시작 뒤에는 앱에서 치지직을 다시 승인해야 합니다. 저장한 PC 연결은 같은 채널 승인 뒤 이어집니다.</p>`); return;
      }
      if (request.method !== 'POST') throw new DisplayAccessError(404);
      if (!b || request.headers.origin !== this.#origin) throw new DisplayAccessError(403);
      await csrfForm(request, b.csrf); this.#live(b);
      const action = /^\/login\/([a-f0-9]{32})\/(connect|deny|approve\/(gaming|streaming))$/u.exec(url.pathname);
      if (action) {
        const id = action[1]!; this.login.view(id);
        if (action[2] === 'connect') {
          const state = nonce();
          b.attempt = { digest: hashSecret(state), loginId: id, expires: Date.now() + 300_000 };
          this.#redirect(response, this.#authorizeUrl(state)); return;
        }
        if (action[2] === 'deny') this.login.deny(id);
        else {
          if (!b.owner) throw new DisplayAccessError(401);
          const role = action[3] as 'gaming' | 'streaming';
          if (role === 'streaming' && this.#creators.sessions.connections(b.owner).some(c => c.role === 'streaming'))
            throw new DisplayAccessError(409);
          await this.#creators.ensureChat(b.owner); this.#live(b);
          this.login.approve(id, this.#creators.access(b.owner), role);
        }
        this.#page(response, '연결 요청 처리됨', '<p>챗뷰 앱으로 돌아가세요.</p><p><a href="/account">내 연결 관리</a></p>'); return;
      }
      if (url.pathname === '/logout') {
        this.#drop(b);
        response.setHeader('Set-Cookie', `${COOKIE}=; Secure; HttpOnly; SameSite=Lax; Path=/; Max-Age=0`);
        this.#redirect(response, '/'); return;
      }
      if (!b.owner) throw new DisplayAccessError(401);
      const revoke = /^\/connections\/([a-f0-9-]{36})\/revoke$/u.exec(url.pathname);
      if (revoke) { this.#creators.removeConnection(b.owner, revoke[1]!); this.#redirect(response, '/account'); return; }
      if (url.pathname === '/account/reconnect') {
        await this.#creators.ensureChat(b.owner); this.#redirect(response, '/account'); return;
      }
      if (url.pathname === '/account/revoke') {
        const owner = b.owner;
        for (const browser of this.#browsers.values()) if (browser.owner === owner) this.#drop(browser);
        const upstream = await this.#creators.revoke(owner);
        this.#page(response, '연결 해제', `<p>챗뷰의 해당 계정 PC 연결을 모두 해제했습니다.</p><p>${upstream
          ? '치지직 권한 철회도 완료했습니다.' : '치지직 권한 철회는 확인하지 못했습니다. 치지직의 연결 관리에서 확인하세요.'}</p>`); return;
      }
      throw new DisplayAccessError(404);
    } catch (error) {
      if (response.headersSent) { response.destroy(); return; }
      response.setHeader('Connection', 'close');
      response.writeHead(error instanceof DisplayAccessError ? error.status : 503, { 'Content-Type': 'application/json' });
      response.end(JSON.stringify({ error: 'ChatView request unavailable' }));
    }
  }
  upgrade(request: IncomingMessage, socket: Duplex, head: Buffer): void {
    socket.once('error', () => socket.destroy());
    try {
      this.#check(request);
      if (request.url !== '/display/events' || request.headers['sec-fetch-site'] === 'cross-site') throw new DisplayAccessError(403);
      this.#creators.gateway(auth(request, 'Bearer')).upgrade(request, socket, head);
    } catch (error) {
      const status = error instanceof DisplayAccessError ? error.status : 503;
      socket.end(`HTTP/1.1 ${status} Rejected\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
    }
  }
  close() {
    if (this.#closed) return;
    this.#closed = true; this.login.clear();
    for (const b of this.#browsers.values()) this.#drop(b);
  }
}
