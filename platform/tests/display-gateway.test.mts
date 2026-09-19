// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { TestContext } from 'node:test';
import { createServer, request as httpRequest } from 'node:http';
import { EventEmitter, once } from 'node:events';
import { WebSocket } from 'ws';
import { DisplayAccess } from '../server/chat/display-access.mts';
import { DisplayGateway, displayFrame, MAX_DISPLAY_FRAME_BYTES } from '../server/chat/display-gateway.mts';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { startProbe } from '../tools/chzzk-connect.mts';
import { connectNativeChat } from '../web/native-chat.js';

const empty = () => ({ state: 'idle', received: 0, messages: [] });
const message = () => ({ nickname: '한글 😀', content: '<img onerror=alert(1)> 안녕', messageTime: 1700000000000 });

async function fixture(t: TestContext, ownerLifetime = 3_600_000) {
  const started = performance.now();
  let time = 0; let owner = true; let snapshot: unknown = empty(); let origin = '';
  const access = new DisplayAccess(() => owner ? { id: 'creator', expiresAt: started + ownerLifetime } : undefined, () => performance.now() + time);
  const gateway = new DisplayGateway(access, () => origin, () => snapshot);
  const server = createServer((req, res) => { if (!gateway.handle(req, res)) { res.writeHead(404); res.end(); } });
  server.on('upgrade', (req, socket, head) => gateway.upgrade(req, socket, head));
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address(); assert.ok(address && typeof address !== 'string');
  origin = `http://127.0.0.1:${address.port}`;
  t.after(async () => { gateway.close(); const done = new Promise<void>(resolve => server.close(() => resolve())); server.closeAllConnections(); await done; });
  const exchange = (ticket: string, headers: Record<string, string> = {}, body?: string) => fetch(`${origin}/display/exchange`, {
    method: 'POST', headers: { Authorization: `ChatView-Ticket ${ticket}`, ...headers }, body, signal: AbortSignal.timeout(3000),
  });
  return { origin, access, gateway, exchange,
    snapshot: (value: unknown) => { snapshot = value; gateway.changed(); },
    expire: () => { time = 300_000; gateway.changed(); },
    signOut: () => { owner = false; gateway.changed(); },
  };
}

async function reader(t: TestContext, origin: string, token: string) {
  const socket = new WebSocket(origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 2000,
  });
  const frames: string[] = [];
  const signal = new EventEmitter();
  socket.on('error', () => {});
  socket.on('message', (data, binary) => { assert.equal(binary, false); frames.push(data.toString()); signal.emit('frame'); });
  t.after(() => socket.terminate());
  await once(socket, 'open');
  return { socket, async next() {
    while (!frames.length) await once(signal, 'frame', { signal: AbortSignal.timeout(3000) });
    return JSON.parse(frames.shift()!) as { type: string; version: number; snapshot: { state: string; received: number; messages: ReturnType<typeof message>[] } };
  } };
}

async function rejected(origin: string, headers: Record<string, string>, expected: number, path = '/display/events') {
  const socket = new WebSocket(origin.replace('http:', 'ws:') + path, { headers, handshakeTimeout: 2000 });
  socket.on('error', () => {});
  await new Promise<void>((resolve, reject) => {
    socket.once('unexpected-response', (_req, res) => { res.resume(); socket.terminate(); try { assert.equal(res.statusCode, expected); resolve(); } catch (e) { reject(e); } });
    socket.once('open', () => { socket.terminate(); reject(new Error('Unexpected authorized socket')); });
    socket.once('error', reject);
  });
}

test('display frame strips private fields and is accepted by the existing native receiver', () => {
  const raw = displayFrame({ state: 'subscribed', received: 1, messages: [{ ...message(), senderChannelId: 'PRIVATE_SENDER', token: 'PRIVATE_TOKEN' }], token: 'PRIVATE_ACCESS' });
  assert.doesNotMatch(raw, /PRIVATE_/u); assert.ok(Buffer.byteLength(raw) < MAX_DISPLAY_FRAME_BYTES);
  let listener: ((event: unknown) => void) | undefined; let rendered: unknown;
  const bridge = { addEventListener(_event: string, fn: (event: unknown) => void) { listener = fn; }, removeEventListener() {}, postMessage() {} };
  const document = { getElementById() { return { children: { length: 1 } }; } };
  const close = connectNativeChat(document, bridge, (_doc: unknown, snapshot: unknown) => { rendered = snapshot; }, 'nonce');
  listener!({ source: bridge, data: JSON.parse(raw) });
  assert.deepEqual(rendered, { state: 'subscribed', received: 1, messages: [message()] }); close();
});

test('invalid/oversized DTO is rejected and non-subscribed frames contain no previous chat', () => {
  assert.throws(() => displayFrame({ state: 'subscribed', received: 1, messages: [{ ...message(), content: '\0' }] }));
  assert.throws(() => displayFrame({ state: 'subscribed', received: 100, messages: Array.from({ length: 100 }, () => ({ ...message(), content: '가'.repeat(10000) })) }));
  assert.deepEqual(JSON.parse(displayFrame({ state: 'revoked', received: 1, messages: [message()] })).snapshot.messages, []);
});

test('real HTTP ticket exchange and WebSocket deliver only the bounded native display envelope', { timeout: 5000 }, async t => {
  const f = await fixture(t); const ticket = f.access.issue(); const response = await f.exchange(ticket.ticket);
  assert.equal(response.status, 200); assert.equal(response.headers.get('cache-control'), 'no-store');
  const lease = await response.json() as { token: string; scope: string }; assert.equal(lease.scope, 'chat:read');
  assert.equal((await f.exchange(ticket.ticket)).status, 401);
  const client = await reader(t, f.origin, lease.token); assert.equal((await client.next()).snapshot.state, 'idle');
  f.snapshot({ state: 'subscribed', received: 1, messages: [message()] });
  const frame = await client.next(); assert.equal(frame.type, 'chat-snapshot'); assert.equal(frame.version, 1);
  assert.deepEqual(frame.snapshot.messages, [message()]);
  f.snapshot({ state: 'stopped', received: 1, messages: [] }); assert.deepEqual((await client.next()).snapshot.messages, []);
});

for (const [headers, status] of [
  [{ Origin: 'https://attacker.invalid' }, 403], [{ 'Sec-Fetch-Site': 'cross-site' }, 403],
  [{ Authorization: 'Bearer ' + 'a'.repeat(64) }, 401],
] as const) {
  test(`exchange rejects forged context ${Object.keys(headers)[0]}`, async t => {
    const f = await fixture(t); const ticket = f.access.issue(); const response = await f.exchange(ticket.ticket, headers);
    assert.equal(response.status, status); assert.doesNotMatch(await response.text(), new RegExp(ticket.ticket));
    assert.equal((await f.exchange(ticket.ticket)).status, 200); // Failed validation never consumes a valid ticket.
  });
}

test('exchange rejects an actual forged Host header before consuming the ticket', async t => {
  const f = await fixture(t); const ticket = f.access.issue();
  // fetch may replace Host. Use HTTP directly so the wire request actually
  // carries the forged authority we intend to test, rather than testing fetch.
  await new Promise<void>((resolve, reject) => {
    const req = httpRequest(f.origin + '/display/exchange', { method: 'POST', headers: {
      Host: 'attacker.invalid', Authorization: `ChatView-Ticket ${ticket.ticket}`,
    } }, res => { res.resume(); try { assert.equal(res.statusCode, 403); resolve(); } catch (e) { reject(e); } });
    req.on('error', reject); req.end();
  });
  assert.equal((await f.exchange(ticket.ticket)).status, 200);
});

test('exchange rejects request bodies and duplicate authorization headers', async t => {
  const f = await fixture(t); const ticket = f.access.issue();
  assert.equal((await f.exchange(ticket.ticket, {}, 'not accepted')).status, 400);
  await new Promise<void>((resolve, reject) => {
    const req = httpRequest(f.origin + '/display/exchange', { method: 'POST', headers: {
      Authorization: [`ChatView-Ticket ${ticket.ticket}`, `ChatView-Ticket ${ticket.ticket}`],
    } }, res => { res.resume(); try { assert.equal(res.statusCode, 401); resolve(); } catch (e) { reject(e); } });
    req.on('error', reject); req.end();
  });
  assert.equal((await f.exchange(ticket.ticket)).status, 200);
});

test('socket refuses ticket/cookie credentials, foreign origins, query tokens and duplicate readers', { timeout: 5000 }, async t => {
  const f = await fixture(t); const ticket = f.access.issue();
  await rejected(f.origin, { Authorization: `Bearer ${ticket.ticket}` }, 401);
  const lease = f.access.exchange(ticket.ticket);
  await rejected(f.origin, { Cookie: `chatview_probe=${lease.token}` }, 401);
  await rejected(f.origin, { Authorization: `Bearer ${lease.token}`, Origin: 'https://evil.invalid' }, 403);
  await rejected(f.origin, { Authorization: `Bearer ${lease.token}` }, 403, `/display/events?token=${lease.token}`);
  const first = await reader(t, f.origin, lease.token); await first.next();
  await rejected(f.origin, { Authorization: `Bearer ${lease.token}` }, 409);
});

for (const action of ['revoke', 'expire', 'signout'] as const) {
  test(`${action} terminates an already-connected reader, not only future handshakes`, { timeout: 5000 }, async t => {
    const f = await fixture(t); const lease = f.access.exchange(f.access.issue().ticket);
    const client = await reader(t, f.origin, lease.token); await client.next();
    const closed = once(client.socket, 'close');
    if (action === 'revoke') { f.access.revoke(lease.id); f.gateway.changed(); }
    else if (action === 'expire') f.expire(); else f.signOut();
    await closed; await rejected(f.origin, { Authorization: `Bearer ${lease.token}` }, 401);
  });
}

for (const payload of ['{"command":"payout"}', 'a'.repeat(2048)]) {
  test(`read-only socket closes on client data (${payload.length} bytes)`, { timeout: 5000 }, async t => {
    const f = await fixture(t); const lease = f.access.exchange(f.access.issue().ticket);
    const client = await reader(t, f.origin, lease.token); await client.next();
    const closed = once(client.socket, 'close'); client.socket.send(payload); await closed;
  });
}

test('invalid outgoing data fails closed instead of exposing unknown fields', { timeout: 5000 }, async t => {
  const f = await fixture(t); const lease = f.access.exchange(f.access.issue().ticket);
  const client = await reader(t, f.origin, lease.token); await client.next();
  const closed = once(client.socket, 'close'); f.snapshot({ secret: 'DO_NOT_SEND' }); await closed;
});

test('creator-authorized probe joins CHZZK session to separate display access without exporting provider secrets', { timeout: 5000 }, async t => {
  const credentials = { clientId: 'app', clientSecret: 'PRIVATE_CLIENT' };
  const ok = (content: unknown) => Response.json({ code: 200, content });
  class Socket extends EventEmitter {
    connect() { queueMicrotask(() => this.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } })); }
    disconnect() {}
  }
  let upstream!: Socket; let upstreams = 0; let globalRevokes = 0;
  const api = new ChzzkApi(credentials, async url => {
    if (url.endsWith('/token')) return ok({ accessToken: 'PRIVATE_ACCESS', refreshToken: 'PRIVATE_REFRESH', tokenType: 'Bearer', expiresIn: '86400' });
    if (url.endsWith('/users/me')) return ok({ channelId: 'mine', channelName: '내 채널' });
    if (url.endsWith('/sessions/auth')) return ok({ url: 'https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET' });
    if (url.endsWith('/revoke')) globalRevokes++;
    if (url.includes('/subscribe/chat?')) queueMicrotask(() => upstream.emit('SYSTEM', { type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    return ok(null);
  });
  const probe = await startProbe({ credentials, port: 0, api, socketFactory: () => { upstreams++; upstream = new Socket(); return upstream; } });
  t.after(() => probe.close());
  const first = await fetch(probe.origin); const cookie = first.headers.get('set-cookie')!.split(';')[0]!; await first.text();
  const get = (path: string) => fetch(probe.origin + path, { headers: { Cookie: cookie }, redirect: 'manual' });
  const page = await (await get('/')).text(); const csrf = page.match(/csrf=([a-f0-9]{64})/u)![1]!;
  const post = (path: string) => fetch(`${probe.origin}${path}?csrf=${csrf}`, { method: 'POST', headers: { Cookie: cookie, Origin: probe.origin }, redirect: 'manual' });
  assert.equal((await post('/display/ticket')).status, 401);
  const auth = await post('/connect'); const state = new URL(auth.headers.get('location')!).searchParams.get('state');
  assert.equal((await get(`/callback?code=code&state=${state}`)).status, 303);
  assert.equal((await fetch(probe.origin + '/display/ticket', { method: 'POST', headers: { Cookie: cookie, Origin: probe.origin } })).status, 403);
  const ticket = await (await post('/display/ticket')).json() as { ticket: string };
  const response = await fetch(probe.origin + '/display/exchange', { method: 'POST', headers: { Authorization: `ChatView-Ticket ${ticket.ticket}` } });
  const raw = await response.text(); assert.doesNotMatch(raw, /PRIVATE_/u);
  const lease = JSON.parse(raw) as { token: string };
  for (const action of ['/display/ticket', '/display/revoke', '/revoke', '/chat/start']) {
    assert.equal((await fetch(probe.origin + action, { method: 'POST', headers: { Authorization: `Bearer ${lease.token}`, Origin: probe.origin }, redirect: 'manual' })).status, 403);
  }
  const client = await reader(t, probe.origin, lease.token); await client.next();
  assert.equal((await post('/chat/start')).status, 303);
  let frame = await client.next(); while (frame.snapshot.state !== 'subscribed') frame = await client.next();
  upstream.emit('CHAT', { channelId: 'mine', senderChannelId: 'PRIVATE_SENDER', profile: { nickname: '한글 😀', verifiedMark: false }, userRoleCode: 'common_user', content: '연결 성공', messageTime: 1700000000000 });
  frame = await client.next(); assert.equal(frame.snapshot.messages[0]?.content, '연결 성공'); assert.doesNotMatch(JSON.stringify(frame), /PRIVATE_/u);
  const closed = once(client.socket, 'close'); await post('/display/revoke'); await closed;
  assert.equal(globalRevokes, 0); assert.equal(upstreams, 1);
  assert.match(await (await get('/')).text(), /채팅 수신 중지/u);
});


test('lease deadline closes an idle socket without requiring a new chat event', { timeout: 5000 }, async t => {
  const f = await fixture(t, 1000); const lease = f.access.exchange(f.access.issue().ticket);
  const client = await reader(t, f.origin, lease.token); await client.next();
  await once(client.socket, 'close');
  assert.throws(() => f.access.authenticate(lease.token));
});

test('backpressure terminates the reader instead of accumulating additional frames', { timeout: 5000 }, async t => {
  const descriptor = Object.getOwnPropertyDescriptor(WebSocket.prototype, 'bufferedAmount')!;
  const send = WebSocket.prototype.send;
  let serverPeer: WebSocket | undefined; let blocked = false;
  Object.defineProperty(WebSocket.prototype, 'bufferedAmount', { ...descriptor, get(this: WebSocket) {
    return blocked && this === serverPeer ? 1 : descriptor.get!.call(this);
  } });
  Object.defineProperty(WebSocket.prototype, 'send', { configurable: true, writable: true,
    value: function(this: WebSocket, ...args: unknown[]) { serverPeer = this; return Reflect.apply(send, this, args); } });
  t.after(() => { Object.defineProperty(WebSocket.prototype, 'bufferedAmount', descriptor); WebSocket.prototype.send = send; });
  const f = await fixture(t); const lease = f.access.exchange(f.access.issue().ticket);
  const client = await reader(t, f.origin, lease.token); await client.next();
  const closed = once(client.socket, 'close'); blocked = true;
  f.snapshot({ state: 'subscribed', received: 1, messages: [message()] }); await closed;
});
