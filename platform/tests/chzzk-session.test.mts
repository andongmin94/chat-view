// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { test } from 'node:test';
import { inspect } from 'node:util';
import { ChzzkChatSession, MAX_MESSAGES } from '../server/chzzk/session.mts';
import type { ChatApi } from '../server/chzzk/session.mts';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { parseChatMessage, parseSystemEvent } from '../server/chzzk/events.mts';
import { nativeChatSnapshot } from '../web/native-chat.js';

const tick = () => new Promise<void>(resolve => setImmediate(resolve));
const message = (content = '안녕하세요 <script>alert(1)</script>') => ({
  channelId: 'mine', senderChannelId: 'sender', chatChannelId: 'chat-channel',
  profile: { nickname: '<img onerror=alert(1)>', verifiedMark: true, badges: [], userRoleCode: 'common_user' },
  content, emojis: {}, messageTime: 1_700_000_000_000,
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

test('provider first text waits privately for both HTTP and SYSTEM subscription confirmations', async () => {
  let releaseHttp!: () => void;
  const f = fixture({
    async subscribeChat() {
      await new Promise<void>(resolve => { releaseHttp = resolve; });
    },
  });
  const ready = f.session.start('PRIVATE_ACCESS', 'mine');
  await tick();
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  f.socket.emit('CHAT', message('first chat before confirmation'));
  f.socket.emit('CHAT', { ...message('wrong channel'), channelId: 'other' });
  f.socket.emit('SYSTEM', system('subscribed'));
  f.socket.emit('CHAT', JSON.stringify(message('second chat while HTTP is pending')));
  const pending = f.session.snapshot();
  assert.equal(pending.state, 'subscribing');
  assert.equal(pending.received, 0, 'unconfirmed chat is never exposed');
  assert.deepEqual(pending.messages, [], 'native display sees no pre-approval chat');
  releaseHttp(); await ready;
  const delivered = f.session.snapshot();
  assert.equal(delivered.state, 'subscribed');
  assert.equal(delivered.received, 2);
  assert.deepEqual(delivered.messages.map(item => item.content),
    ['first chat before confirmation', 'second chat while HTTP is pending']);
  f.socket.emit('CHAT', message('regular chat after confirmation'));
  assert.equal(f.session.snapshot().received, 3);
  await f.session.stop();
});

test('early CHAT remains private until SYSTEM acknowledgement, even after HTTP success', async () => {
  const f = fixture({ async subscribeChat() {} });
  const ready = f.session.start('PRIVATE_ACCESS', 'mine');
  await tick();
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  await tick();
  f.socket.emit('CHAT', message('early before system ack'));
  f.socket.emit('SYSTEM', system('subscribed', 'other'));
  assert.equal(f.session.snapshot().state, 'subscribing');
  assert.equal(f.session.snapshot().received, 0);
  assert.equal(f.session.snapshot().messages.length, 0);
  f.socket.emit('SYSTEM', system('subscribed'));
  await ready;
  assert.equal(f.session.snapshot().received, 1);
  assert.equal(f.session.snapshot().messages[0]?.content, 'early before system ack');
  await f.session.stop();
});

test('pre-confirmation burst is bounded; subscription failure discards all buffered private chat', async () => {
  let resolveHttp!: () => void;
  const f = fixture({
    async subscribeChat() {
      await new Promise<void>(resolve => { resolveHttp = resolve; });
    },
  });
  const ready = f.session.start('PRIVATE_ACCESS', 'mine');
  await tick();
  f.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  for (let i = 0; i < MAX_MESSAGES + 50; i++) f.socket.emit('CHAT', message(`buffered ${i}`));
  assert.equal(f.session.snapshot().received, 0);
  assert.equal(f.session.snapshot().messages.length, 0);
  f.socket.emit('SYSTEM', system('subscribed'));
  resolveHttp(); await ready;
  const snapshot = f.session.snapshot();
  assert.equal(snapshot.received, MAX_MESSAGES + 50);
  assert.equal(snapshot.messages.length, MAX_MESSAGES);
  assert.equal(snapshot.messages[0]?.content, 'buffered 50');
  assert.equal(snapshot.messages.at(-1)?.content, 'buffered 149');
  await f.session.stop();

  const broken = fixture({ async subscribeChat() { throw new Error('PRIVATE_UPSTREAM_ERROR'); } });
  const rejected = assert.rejects(broken.session.start('PRIVATE_ACCESS', 'mine'));
  await tick();
  broken.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
  broken.socket.emit('CHAT', message('never authorized'));
  await rejected;
  assert.equal(broken.session.snapshot().state, 'error');
  assert.equal(broken.session.snapshot().received, 0);
  assert.deepEqual(broken.session.snapshot().messages, []);
  assert.equal(broken.socket.connected, false);
  await broken.session.stop();
});

// The production service creates a fresh ChzzkChatSession for every upstream
// restart. An unconfirmed first chat, an old SYSTEM ack and a slow HTTP reply
// from the retired session must not become the new native HUD's first text.
for (const closing of ['disconnect', 'revoked'] as const) {
  test(`unconfirmed first chat from ${closing} never enters a replacement session`, async () => {
    let finishOldSubscribe!: () => void;
    const old = fixture({
      async subscribeChat() { await new Promise<void>(resolve => { finishOldSubscribe = resolve; }); },
    });
    const first = old.session.start('PRIVATE_ACCESS', 'mine');
    const rejected = assert.rejects(first);
    await tick();
    old.socket.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } });
    const oldChatListener = old.socket.listeners('CHAT')[0]!;
    old.socket.emit('CHAT', message('PRIVATE_OLD_FIRST_TEXT'));
    old.socket.emit('SYSTEM', system('subscribed'));
    assert.equal(old.session.snapshot().state, 'subscribing');
    assert.deepEqual(old.session.snapshot().messages, []);
    assert.equal(old.session.snapshot().received, 0);

    if (closing === 'disconnect') old.socket.emit('disconnect', 'PRIVATE_OLD_SOCKET');
    else old.socket.emit('SYSTEM', system('revoked'));
    assert.equal(old.session.snapshot().state, closing === 'disconnect' ? 'disconnected' : 'revoked');
    assert.equal(old.socket.listenerCount('CHAT'), 0);
    assert.deepEqual(old.session.snapshot().messages, []);
    assert.equal(nativeChatSnapshot(old.session.snapshot()).received, 0);
    assert.equal(old.session.snapshot().received, 0);

    // A replacement uses a separate provider ticket/socket and starts empty.
    const current = fixture();
    await current.begin();
    assert.equal(current.session.snapshot().received, 0);
    current.socket.emit('CHAT', message('fresh authorized first text'));
    assert.equal(current.session.snapshot().state, 'subscribed');
    assert.deepEqual(current.session.snapshot().messages.map(m => m.content),
      ['fresh authorized first text']);
    assert.equal(nativeChatSnapshot(current.session.snapshot()).received, 1);

    // Provider I/O and even a previously captured listener may complete late.
    finishOldSubscribe();
    await rejected;
    oldChatListener(message('PRIVATE_LATE_MESSAGE'));
    old.socket.emit('SYSTEM', system('subscribed'));
    assert.equal(old.session.snapshot().received, 0);
    assert.deepEqual(old.session.snapshot().messages, []);
    assert.deepEqual(current.session.snapshot().messages.map(m => m.content),
      ['fresh authorized first text']);
    assert.doesNotMatch(JSON.stringify(current.session.snapshot()), /PRIVATE_/u);
    await old.session.stop(); await current.session.stop();
    assert.equal(current.session.snapshot().received, 0);
    assert.deepEqual(current.session.snapshot().messages, []);
  });
}

for (const end of ['revoked', 'unsubscribed', 'disconnect', 'connect_error', 'error']) {
  test(`${end} closes delivery, clears history, suppresses raw reasons and late messages`, async () => {
    const f = fixture(); await f.begin(); f.socket.emit('CHAT', message());
    const late = f.socket.listeners('CHAT')[0]!;
    if (end === 'revoked' || end === 'unsubscribed') f.socket.emit('SYSTEM', system(end));
    else f.socket.emit(end, new Error('PRIVATE_REASON'));
    late(message());
    assert.equal(f.session.snapshot().messages.length, 0);
    assert.equal(f.session.snapshot().received, 0, 'terminal state cannot retain the old HUD count');
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
test('observed CHZZK profile role reaches the own native text snapshot; top-level role alone is not accepted', () => {
  const original = message('실제 수신 첫 글자 😀');
  const raw = { ...original, eventSentAt: '2026-07-26T03:14:18.629843820',
    profile: { ...original.profile, userRoleCode: 'streamer' } };
  const parsed = parseChatMessage(JSON.stringify(raw), 'mine')!;
  assert.equal(parsed.userRoleCode, 'streamer');
  assert.deepEqual(nativeChatSnapshot({ state: 'subscribed', received: 1, messages: [parsed] }), {
    state: 'subscribed', received: 1,
    messages: [{ nickname: original.profile.nickname, content: raw.content, messageTime: raw.messageTime }],
  });
  assert.equal(parseChatMessage({ ...raw,
    profile: { nickname: 'fixture', verifiedMark: false }, userRoleCode: 'streamer' }, 'mine'), undefined,
  'a role outside the observed profile cannot substitute for a missing profile role');
  assert.equal(parseChatMessage(raw, 'someone-else'), undefined);
});

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
