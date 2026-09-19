// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { TestContext } from 'node:test';
import { EventEmitter } from 'node:events';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { startProbe } from '../tools/chzzk-connect.mts';
import { renderChat } from '../web/chat.js';

async function fixture(t: TestContext, expiresIn = '86400') {
  const credentials = { clientId: 'app', clientSecret: 'PRIVATE_CLIENT' };
  const ok = (content: unknown) => Response.json({ code: 200, content });
  class Socket extends EventEmitter {
    connect() { queueMicrotask(() => this.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } })); }
    disconnect() {}
  }
  let socket!: Socket; let sockets = 0;
  const calls: string[] = [];
  const api = new ChzzkApi(credentials, async url => {
    calls.push(url);
    if (url.endsWith('/token')) return ok({ accessToken: 'PRIVATE_ACCESS', refreshToken: 'PRIVATE_REFRESH', tokenType: 'Bearer', expiresIn });
    if (url.endsWith('/users/me')) return ok({ channelId: 'mine', channelName: '내 채널' });
    if (url.endsWith('/sessions/auth')) return ok({ url: 'https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET' });
    if (url.includes('/subscribe/chat?')) queueMicrotask(() => socket.emit('SYSTEM', { type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    return ok(null);
  });
  const probe = await startProbe({ credentials, port: 0, api, socketFactory: () => { sockets++; socket = new Socket(); return socket; } });
  t.after(() => probe.close());
  const first = await fetch(probe.origin); const cookie = first.headers.get('set-cookie')!.split(';')[0]!; await first.text();
  const get = (path: string, headers: Record<string, string> = {}) => fetch(probe.origin + path, { headers: { Cookie: cookie, ...headers }, redirect: 'manual', signal: AbortSignal.timeout(5000) });
  const page = await (await get('/')).text(); const csrf = page.match(/csrf=([a-f0-9]{64})/u)![1];
  const post = (path: string) => fetch(`${probe.origin}${path}?csrf=${csrf}`, { method: 'POST', headers: { Cookie: cookie, Origin: probe.origin }, redirect: 'manual' });
  const auth = await post('/connect'); const state = new URL(auth.headers.get('location')!).searchParams.get('state');
  assert.equal((await get(`/callback?code=code&state=${state}`)).status, 303);
  async function events() {
    const response = await get('/chat/events'); assert.equal(response.status, 200);
    assert.equal(response.headers.get('cache-control'), 'no-store');
    const reader = response.body!.getReader(); let buffer = '';
    t.after(() => reader.cancel().catch(() => {}));
    return async () => {
      while (!buffer.includes('\n\n')) {
        const { value, done } = await reader.read(); assert.equal(done, false);
        buffer += new TextDecoder().decode(value);
      }
      const boundary = buffer.indexOf('\n\n'); const raw = buffer.slice(0, boundary); buffer = buffer.slice(boundary + 2);
      assert.doesNotMatch(raw, /PRIVATE_/u);
      return JSON.parse(raw.slice('data: '.length)) as { state: string; received: number; messages: { content: string }[] };
    };
  }
  const send = () => socket.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender', profile: { nickname: '한글', verifiedMark: false }, userRoleCode: 'common_user', content: '<img onerror=alert(1)>', messageTime: 1700000000000 }));
  return { probe, get, post, events, send, calls, socket: () => socket, sockets: () => sockets };
}

test('authenticated browser receives own chat stream; duplicate start does not duplicate upstream; stop clears it', async t => {
  const f = await fixture(t); const next = await f.events(); assert.equal((await next()).state, 'idle');
  assert.equal((await f.post('/chat/start')).status, 303);
  let frame = await next(); while (frame.state !== 'subscribed') frame = await next();
  assert.equal(frame.received, 0); f.send(); frame = await next();
  assert.equal(frame.messages[0]?.content, '<img onerror=alert(1)>');
  await f.post('/chat/start'); assert.equal(f.sockets(), 1);
  await f.post('/chat/stop'); frame = await next();
  assert.equal(frame.state, 'stopped'); assert.deepEqual(frame.messages, []);
  await f.post('/chat/start'); assert.equal(f.sockets(), 2);
  assert.equal(f.calls.filter(url => url.endsWith('/sessions/auth')).length, 2);
});

test('revocation clears the stream and local authorization; no automatic global revoke', async t => {
  const f = await fixture(t); await f.post('/chat/start'); f.send();
  const next = await f.events(); assert.equal((await next()).messages.length, 1);
  f.socket().emit('SYSTEM', { type: 'revoked', data: { channelId: 'mine', eventType: 'CHAT' } });
  assert.equal((await next()).state, 'revoked');
  assert.equal((await f.post('/chat/start')).status, 400);
  assert.match(await (await f.get('/')).text(), /치지직 로그인/u);
  assert.equal(f.calls.some(url => url.endsWith('/revoke')), false);
});

test('stream and assets reject unauthenticated/cross-origin access, and old issuance-only action is gone', async t => {
  const f = await fixture(t);
  for (const path of ['/chat', '/chat.js', '/chat.css', '/chat/events']) {
    assert.equal((await fetch(f.probe.origin + path)).status, 403);
    assert.equal((await f.get(path, { Origin: 'https://attacker.invalid' })).status, 403);
    assert.equal((await f.get(path, { 'Sec-Fetch-Site': 'cross-site' })).status, 403);
  }
  const page = await f.get('/chat'); assert.equal(page.status, 200);
  assert.match(page.headers.get('content-security-policy')!, /script-src 'self'/u);
  assert.doesNotMatch(page.headers.get('content-security-policy')!, /unsafe-inline/u);
  assert.doesNotMatch(await page.text(), /PRIVATE_/u);
  assert.equal((await f.post('/session')).status, 400);
});

test('preview caps simultaneous readers instead of opening another upstream socket', async t => {
  const f = await fixture(t);
  for (let i = 0; i < 4; i++) { const next = await f.events(); await next(); }
  assert.equal((await f.get('/chat/events')).status, 429); assert.equal(f.sockets(), 0);
});

test('token expiry closes chat and requires reauthorization', async t => {
  const f = await fixture(t, '1'); await f.post('/chat/start');
  const next = await f.events(); await next();
  let frame = await next(); while (frame.state !== 'stopped') frame = await next();
  assert.deepEqual(frame.messages, []); assert.equal((await f.post('/chat/start')).status, 400);
});

test('renderer uses text nodes for malicious strings and clears on disconnect', () => {
  class Element {
    textContent = ''; dir = ''; className = ''; children: Element[] = [];
    scrollHeight = 0; scrollTop = 0; clientHeight = 0;
    set innerHTML(_value: string) { throw new Error('HTML injection'); }
    append(...elements: Element[]) { this.children.push(...elements); }
    replaceChildren(...elements: Element[]) { this.children = elements; }
  }
  const status = new Element(); const list = new Element();
  const document = { getElementById: (id: string) => id === 'status' ? status : list, createElement: () => new Element() };
  renderChat(document, { state: 'subscribed', received: 1, messages: [{ nickname: '<script>', content: '<img onerror=evil()>', messageTime: 1700000000000 }] });
  assert.equal(list.children[0]?.children[1]?.textContent, '<script>');
  assert.equal(list.children[0]?.children[2]?.textContent, '<img onerror=evil()>');
  renderChat(document, { state: 'subscribed', received: 0, messages: [] });
  assert.match(status.textContent, /첫 메시지 대기/u);
  renderChat(document, { state: 'revoked', received: 1, messages: [{ content: 'must not show' }] });
  assert.equal(list.children.length, 0);
});
