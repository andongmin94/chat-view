// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { test } from 'node:test';
import { inspect } from 'node:util';
import { ChzzkChatSession, MAX_MESSAGES } from '../server/chzzk/session.mts';
import type { ChatApi } from '../server/chzzk/session.mts';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { parseChatMessage, parseSystemEvent } from '../server/chzzk/events.mts';

const tick = () => new Promise<void>(resolve => setImmediate(resolve));
const message = (content = '안녕하세요 <script>alert(1)</script>') => ({
  channelId: 'mine', senderChannelId: 'sender', chatChannelId: 'chat-channel',
  profile: { nickname: '<img onerror=alert(1)>', verifiedMark: true, badges: [] },
  userRoleCode: 'common_user', content, emojis: {}, messageTime: 1_700_000_000_000,
});
const system = (type: string, channelId = 'mine', eventType = 'CHAT') => ({ type, data: { channelId, eventType } });
class FakeSocket extends EventEmitter {
  connected = false;
  connect() { this.connected = true; return this; }
  disconnect() { this.connected = false; this.emit('disconnect', 'private reason'); return this; }
}
function fixture(overrides: Partial<ChatApi> = {}) {
  const socket = new FakeSocket(); let subscribed = 0; let unsubscribed = 0; let notifications = 0;
  const api: ChatApi = {
    async createUserSession() { return 'https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET'; },
    async subscribeChat(token, key) {
      subscribed++; assert.equal(token, 'PRIVATE_ACCESS'); assert.equal(key, 'PRIVATE_KEY');
      socket.emit('SYSTEM', JSON.stringify(system('subscribed')));
    },
    async unsubscribeChat() { unsubscribed++; assert.equal(socket.connected, true); },
    ...overrides,
  };
  const session = new ChzzkChatSession(api, () => { notifications++; }, () => socket);
  async function begin() {
    const ready = session.start('PRIVATE_ACCESS', 'mine'); await tick();
    socket.emit('SYSTEM', JSON.stringify({ type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } }));
    await ready;
  }
  return { socket, session, begin, api, counts: () => ({ subscribed, unsubscribed, notifications }) };
}

test('connected -> HTTP subscription + SYSTEM acknowledgement -> first message, not socket-only ready', async () => {
  const f = fixture(); await f.begin();
  assert.equal(f.session.snapshot().state, 'subscribed');
  assert.equal(f.session.snapshot().received, 0);
  f.socket.emit('CHAT', JSON.stringify(message()));
  assert.equal(f.session.snapshot().received, 1);
  assert.equal(f.session.snapshot().messages[0]?.content, message().content);
  assert.doesNotMatch(inspect(f.session) + JSON.stringify(f.session.snapshot()), /PRIVATE_/u);
  await f.session.stop(); assert.equal(f.counts().unsubscribed, 1);
});

test('HTTP success without matching acknowledgement is not subscribed', async () => {
  const f = fixture({ async subscribeChat() {} });
  const ready = f.session.start('PRIVATE_ACCESS', 'mine'); const rejected = assert.rejects(ready);
  await tick(); f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  await tick(); assert.equal(f.session.snapshot().state, 'subscribing');
  f.socket.emit('SYSTEM', system('subscribed', 'other'));
  f.socket.emit('SYSTEM', system('subscribed', 'mine', 'DONATION'));
  f.socket.emit('CHAT', message()); assert.equal(f.session.snapshot().received, 0);
  await f.session.stop(); await rejected;
});

test('acknowledgement before HTTP completion waits, and duplicate connected never resubscribes', async () => {
  let release!: () => void;
  const f = fixture({ async subscribeChat() { await new Promise<void>(resolve => { release = resolve; }); } });
  const ready = f.session.start('PRIVATE_ACCESS', 'mine'); await tick();
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  f.socket.emit('SYSTEM', system('subscribed')); assert.equal(f.session.snapshot().state, 'subscribing');
  release(); await ready;
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'OTHER_KEY' } });
  assert.equal(f.session.snapshot().state, 'subscribed'); await f.session.stop();
});

for (const end of ['revoked', 'unsubscribed', 'disconnect', 'connect_error', 'error']) {
  test(`${end} closes delivery, clears history, suppresses raw reasons and late messages`, async () => {
    const f = fixture(); await f.begin(); f.socket.emit('CHAT', message());
    const late = f.socket.listeners('CHAT')[0]!;
    if (end === 'revoked' || end === 'unsubscribed') f.socket.emit('SYSTEM', system(end));
    else f.socket.emit(end, new Error('PRIVATE_REASON'));
    late(message());
    assert.equal(f.session.snapshot().messages.length, 0);
    assert.equal(f.socket.connected, false);
    assert.doesNotMatch(JSON.stringify(f.session.snapshot()), /PRIVATE_/u);
    assert.equal(f.socket.listenerCount('CHAT'), 0);
    await f.session.stop(); assert.equal(f.counts().unsubscribed, 0);
  });
}

test('other channel/event revocation does not revoke this chat; other channel messages are dropped', async () => {
  const f = fixture(); await f.begin();
  f.socket.emit('SYSTEM', system('revoked', 'other'));
  f.socket.emit('SYSTEM', system('revoked', 'mine', 'DONATION'));
  f.socket.emit('CHAT', { ...message(), channelId: 'other' });
  assert.equal(f.session.snapshot().state, 'subscribed'); assert.equal(f.session.snapshot().received, 0);
  await f.session.stop();
});

test('bounded history preserves repeated legitimate messages rather than inventing provider IDs', async () => {
  const f = fixture(); await f.begin();
  for (let i = 0; i < MAX_MESSAGES + 50; i++) f.socket.emit('CHAT', message(`same ${i}`));
  const snapshot = f.session.snapshot();
  assert.equal(snapshot.messages.length, 100); assert.equal(snapshot.messages[0]?.content, 'same 50');
  f.socket.emit('CHAT', message('repeat')); f.socket.emit('CHAT', message('repeat'));
  assert.equal(f.session.snapshot().received, 152); await f.session.stop();
});

test('cancellation during ticket issuance cannot create a late socket', async () => {
  let release!: (value: string) => void; let sockets = 0;
  const api: ChatApi = { createUserSession: () => new Promise(resolve => { release = resolve; }),
    async subscribeChat() {}, async unsubscribeChat() {} };
  const session = new ChzzkChatSession(api, () => {}, () => { sockets++; return new FakeSocket(); });
  const ready = session.start('PRIVATE_ACCESS', 'mine'); const rejected = assert.rejects(ready);
  await session.stop(); release('https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET');
  await rejected; assert.equal(sockets, 0); assert.equal(session.snapshot().state, 'stopped');
});

test('cancellation remains active after subscription, and stop is idempotent', async () => {
  const f = fixture(); const controller = new AbortController();
  const ready = f.session.start('PRIVATE_ACCESS', 'mine', controller.signal); await tick();
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } }); await ready;
  controller.abort(); assert.equal(f.session.snapshot().state, 'stopped');
  await f.session.stop(); await f.session.stop(); assert.equal(f.counts().unsubscribed, 0);
});

test('handshake deadline cannot leave a half-open socket indefinitely', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const f = fixture(); const ready = f.session.start('PRIVATE_ACCESS', 'mine');
  const rejected = assert.rejects(ready); await tick(); t.mock.timers.tick(15000);
  await rejected; assert.equal(f.socket.connected, false); assert.equal(f.session.snapshot().state, 'error');
});

test('explicit stop clears locally even on unsubscribe error, without global token revoke', async () => {
  const f = fixture({ async unsubscribeChat() { throw new Error('PRIVATE_RESPONSE'); } }); await f.begin();
  f.socket.emit('CHAT', message()); assert.equal(await f.session.stop(), false);
  assert.equal(f.session.snapshot().messages.length, 0); assert.equal(f.socket.connected, false);
  assert.equal(await f.session.stop(), true);
});

for (const bad of [null, [], '{', 'x'.repeat(65537), { ...message(), messageTime: 1.1 },
  { ...message(), messageTime: Number.MAX_SAFE_INTEGER + 1 }, { ...message(), content: 'x'.repeat(10001) },
  { ...message(), profile: { nickname: 'x', verifiedMark: 'true' } }, { ...message(), senderChannelId: 'bad\nidentifier' }]) {
  test('malformed chat is dropped without throwing or retaining unknown fields', () => {
    assert.equal(parseChatMessage(bad, 'mine'), undefined);
  });
}
test('native identifiers and Unicode text retained; remote profile objects and scripts stay inert', () => {
  const parsed = parseChatMessage(message('한글 日本語 😀 <script>'), 'mine')!;
  assert.equal(parsed.chatChannelId, 'chat-channel'); assert.match(parsed.content, /<script>/u);
  assert.equal('profile' in parsed, false); assert.equal('messageId' in parsed, false);
  assert.equal(parseSystemEvent('{PRIVATE_KEY'), undefined);
});
for (const verb of ['subscribeChat', 'unsubscribeChat'] as const) {
  test(`${verb} uses POST query parameters, Bearer, no credentials in body, no redirects`, async () => {
    let calls = 0;
    const api = new ChzzkApi({ clientId: 'client', clientSecret: 'secret' }, async (raw, init) => {
      calls++; const url = new URL(raw);
      assert.equal(url.searchParams.get('sessionKey'), 'key&part');
      assert.equal(url.pathname, `/open/v1/sessions/events/${verb === 'subscribeChat' ? 'subscribe' : 'unsubscribe'}/chat`);
      assert.equal(init.method, 'POST'); assert.equal(init.body, undefined); assert.equal(init.redirect, 'error');
      assert.equal(new Headers(init.headers).get('Authorization'), 'Bearer token');
      return Response.json({ code: 200, content: null });
    });
    await api[verb]('token', 'key&part'); assert.equal(calls, 1);
    await assert.rejects(api[verb]('token', 'bad\nkey')); assert.equal(calls, 1);
  });
}
