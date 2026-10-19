// SPDX-License-Identifier: GPL-2.0-or-later
// Actual Socket.IO transport to a local simulated provider, not NAVER.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import { ChzzkChatSession } from '../server/chzzk/session.mts';
import type { ChatApi } from '../server/chzzk/session.mts';
import { createChzzkSocket } from '../server/chzzk/socket.mts';
import { nativeChatSnapshot } from '../web/native-chat.js';
import { SessionStore } from '../server/chat/session-store.mts';
import { DisplayAccess } from '../server/chat/display-access.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { WebSocket } from 'ws';
import type { RawData } from 'ws';
import { Server } from 'socket.io';
import type { Socket } from 'socket.io';
const require = createRequire(import.meta.url);

type NativeFrame = { type: string; version: number; snapshot: {
  state: string; received: number; messages: { nickname: string; content: string }[];
}; connection: { role: string; broadcastSessionId: string } };
function receive(peer: WebSocket, matches: (value: NativeFrame) => boolean): Promise<NativeFrame> {
  return new Promise((resolve, reject) => {
    const clean = () => {
      clearTimeout(timer); peer.off('message', message); peer.off('error', error); peer.off('close', closed);
    };
    const error = (reason: Error) => { clean(); reject(reason); };
    const closed = () => error(new Error('Private HUD closed before expected frame'));
    const message = (raw: RawData, binary: boolean) => {
      try {
        assert.equal(binary, false);
        const frame = JSON.parse(raw.toString()) as NativeFrame;
        assert.equal(frame.type, 'chat-snapshot'); assert.equal(frame.version, 1);
        if (!matches(frame)) return;
        clean(); resolve(frame);
      } catch (reason) { error(reason as Error); }
    };
    const timer = setTimeout(() => error(new Error('Expected private HUD frame missing')), 3000);
    peer.on('message', message); peer.once('error', error); peer.once('close', closed);
  });
}

test('structured Manager options negotiate EIO3 and preserve the ticket over real WebSocket', { timeout: 10000 }, async t => {
  assert.equal(require('socket.io-client/package.json').version, '2.0.3');
  const clientRequire = createRequire(require.resolve('socket.io-client'));
  assert.equal(clientRequire('engine.io-client/package.json').version, '3.5.6');
  assert.equal(clientRequire('socket.io-parser/package.json').version, '3.3.6');
  const engineRequire = createRequire(clientRequire.resolve('engine.io-client'));
  assert.equal(typeof clientRequire('parseuri'), 'object');
  assert.equal(typeof engineRequire('parseuri'), 'object');
  const ticket = 'TEST_TICKET+/=%한글';
  const issued = `https://ssio08.nchat.naver.com/?${new URLSearchParams({ auth: ticket })}`;
  const http = createServer();
  const io = new Server(http, { allowEIO3: true, transports: ['websocket'], serveClient: false, perMessageDeflate: false });
  await new Promise<void>(resolve => http.listen(0, '127.0.0.1', resolve));
  const address = http.address(); assert.ok(address && typeof address !== 'string');
  const port = address.port;
  t.after(() => new Promise<void>(resolve => io.close(() => resolve())));
  let peer!: Socket;
  io.on('connection', socket => {
    peer = socket;
    assert.equal(socket.conn.protocol, 3);
    assert.equal(socket.conn.transport.name, 'websocket');
    assert.equal(socket.handshake.query.auth, ticket);
    socket.emit('SYSTEM', JSON.stringify({ type: 'connected', data: { sessionKey: 'TEST_KEY' } }));
  });
  const actual = require('socket.io-client');
  const module = require.cache[require.resolve('socket.io-client')]!;
  // Test-only destination substitution; no endpoint override in production.
  module.exports = { Manager: class extends actual.Manager {
    constructor(options: Record<string, unknown>) {
      assert.equal(options.hostname, 'ssio08.nchat.naver.com');
      assert.equal(options.port, 443); assert.equal(options.secure, true);
      assert.equal(options.host, undefined);
      assert.equal(options.reconnection, false); assert.equal(options.autoConnect, false);
      assert.equal(options.forceNode, true); assert.equal(options.rejectUnauthorized, true);
      assert.deepEqual(options.transports, ['websocket']);
      assert.deepEqual(options.query, { auth: ticket });
      super({ ...options, hostname: '127.0.0.1', port, secure: false });
      assert.equal(this.uri, undefined);
    }
  } };
  t.after(() => { module.exports = actual; });
  let subscribed = 0; let unsubscribed = 0;
  const api: ChatApi = {
    async createUserSession() { return issued; },
    async subscribeChat(_token, key) {
      assert.equal(key, 'TEST_KEY'); subscribed++;
      // Actual EIO3/Socket.IO transport can deliver the first CHAT before
      // the SYSTEM acknowledgement, while the REST response is still pending.
      // Do not lose it, and do not expose it until both have succeeded.
      peer.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender',
        profile: { nickname: '모의 첫 시청자', verifiedMark: false, userRoleCode: 'common_user' },
        content: '첫 채팅', messageTime: 1700000000000 }));
      peer.emit('SYSTEM', JSON.stringify({ type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    },
    async unsubscribeChat() { unsubscribed++; },
  };
  let gateway: DisplayGateway | undefined;
  const session = new ChzzkChatSession(api, () => gateway?.changed());
  t.after(() => session.stop());
  await session.start('TEST_ACCESS', 'mine'); assert.equal(subscribed, 1);
  assert.equal(session.snapshot().received, 1, 'the first transport CHAT survives the HTTP/SYSTEM ordering');
  assert.equal(session.snapshot().messages[0]?.content, '첫 채팅');
  assert.deepEqual(nativeChatSnapshot(session.snapshot()).messages, [{
    nickname: '모의 첫 시청자', content: '첫 채팅', messageTime: 1700000000000,
  }], 'an early real Socket.IO frame becomes ChatView-owned HUD text');
  peer.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender', profile: { nickname: '실제 소켓 / 모의 제공자', verifiedMark: false, userRoleCode: 'common_user' }, content: '한글 😀', messageTime: 1700000000000 }));
  for (let i = 0; session.snapshot().received < 2 && i < 100; i++) await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(session.snapshot().received, 2);
  assert.equal(session.snapshot().messages[1]?.content, '한글 😀');
  const store = new SessionStore();
  const expiry = Date.now() + 120_000;
  const access = new DisplayAccess(() => ({ id: 'mine', expiresAt: expiry }), Date.now, store);
  const displayHttp = createServer();
  let displayOrigin = '';
  gateway = new DisplayGateway(access, () => displayOrigin, () => session.snapshot());
  displayHttp.on('upgrade', (request, socket, head) => gateway!.upgrade(request, socket, head));
  await new Promise<void>(resolve => displayHttp.listen(0, '127.0.0.1', resolve));
  const displayAddress = displayHttp.address();
  assert.ok(displayAddress && typeof displayAddress !== 'string');
  displayOrigin = 'http://127.0.0.1:' + displayAddress.port;
  const readers: WebSocket[] = [];
  t.after(async () => {
    for (const reader of readers) reader.terminate();
    gateway?.close(); store.close();
    displayHttp.closeAllConnections();
    await new Promise<void>(resolve => displayHttp.close(() => resolve()));
  });
  const open = async (role: 'gaming' | 'streaming') => {
    // The existing approved-role/session path is used, without extra upstreams.
    const ticket = access.issue();
    const lease = access.exchange(ticket.ticket, false, role);
    const reader = new WebSocket(displayOrigin.replace('http:', 'ws:') + '/display/events', {
      headers: { Authorization: 'Bearer ' + lease.token }, handshakeTimeout: 3000,
    });
    readers.push(reader);
    const first = await receive(reader, frame => frame.snapshot.state === 'subscribed' &&
      frame.snapshot.received === 2 && frame.snapshot.messages[1]?.content === '한글 😀');
    assert.equal(first.connection.role, role);
    assert.deepEqual(first.snapshot.messages.map(m => m.content), ['첫 채팅', '한글 😀']);
    assert.doesNotMatch(JSON.stringify(first), /TEST_ACCESS|TEST_TICKET|TEST_KEY|senderChannelId|userRoleCode/u);
    return { reader, lease };
  };
  const [game, streaming] = await Promise.all([open('gaming'), open('streaming')]);
  assert.equal(game.lease.membership.broadcastSessionId,
    streaming.lease.membership.broadcastSessionId);
  assert.equal(subscribed, 1, 'two private HUDs share one actual Socket.IO upstream');

  const next = [receive(game.reader, frame => frame.snapshot.received === 3),
    receive(streaming.reader, frame => frame.snapshot.received === 3)];
  peer.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender',
    profile: { nickname: '실제 소켓', verifiedMark: false, userRoleCode: 'common_user' },
    content: '세 번째 실시간 채팅', messageTime: 1700000000000 }));
  const third = await Promise.all(next);
  for (const frame of third) assert.equal(frame.snapshot.messages[2]?.content, '세 번째 실시간 채팅');

  const cleared = [receive(game.reader, frame => frame.snapshot.state === 'stopped'),
    receive(streaming.reader, frame => frame.snapshot.state === 'stopped')];
  await session.stop();
  for (const frame of await Promise.all(cleared)) {
    assert.equal(frame.snapshot.received, 0);
    assert.deepEqual(frame.snapshot.messages, []);
  }
  assert.equal(game.reader.readyState, WebSocket.OPEN);
  assert.equal(streaming.reader.readyState, WebSocket.OPEN);
  assert.equal(unsubscribed, 1);
  assert.deepEqual(session.snapshot().messages, []);
  assert.deepEqual(nativeChatSnapshot(session.snapshot()).messages, []);
});

test('production factory rejects arbitrary endpoints and debug logging before connecting', () => {
  for (const url of ['http://127.0.0.1:123/', 'https://evil.invalid/?auth=x', 'https://nchat.naver.com.evil.invalid/?auth=x']) {
    assert.throws(() => createChzzkSocket(url));
  }
  const debug = process.env.DEBUG; process.env.DEBUG = '*';
  try { assert.throws(() => createChzzkSocket('https://ssio08.nchat.naver.com/?auth=TEST_TICKET')); }
  finally { if (debug === undefined) delete process.env.DEBUG; else process.env.DEBUG = debug; }
});
