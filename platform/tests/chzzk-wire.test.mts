// SPDX-License-Identifier: GPL-2.0-or-later
// A real Socket.IO server, NOT hand-written Engine.IO/Socket.IO framing.
// Only the HTTPS API is simulated. This still does not attest NAVER's service.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import { ChzzkChatSession } from '../server/chzzk/session.mts';
import type { ChatApi } from '../server/chzzk/session.mts';
import { createChzzkSocket } from '../server/chzzk/socket.mts';
import { Server } from 'socket.io';
import type { Socket } from 'socket.io';
const require = createRequire(import.meta.url);

test('documented client 2.0.3 + patched dependencies negotiates EIO3 over real WebSocket', { timeout: 10000 }, async t => {
  assert.equal(require('socket.io-client/package.json').version, '2.0.3');
  const clientRequire = createRequire(require.resolve('socket.io-client'));
  assert.equal(clientRequire('engine.io-client/package.json').version, '3.5.6');
  assert.equal(clientRequire('socket.io-parser/package.json').version, '3.3.6');
  assert.equal(clientRequire('parseuri/package.json').version, '2.0.0');
  const engineRequire = createRequire(clientRequire.resolve('engine.io-client'));
  assert.equal(engineRequire('parseuri/package.json').version, '2.0.0');
  const http = createServer();
  const io = new Server(http, { allowEIO3: true, transports: ['websocket'], serveClient: false, perMessageDeflate: false });
  await new Promise<void>(resolve => http.listen(0, '127.0.0.1', resolve));
  const address = http.address(); assert.ok(address && typeof address !== 'string');
  const origin = `http://127.0.0.1:${address.port}`;
  t.after(() => new Promise<void>(resolve => io.close(() => resolve())));
  let peer!: Socket;
  io.on('connection', socket => {
    peer = socket;
    assert.equal(socket.conn.protocol, 3); assert.equal(socket.conn.transport.name, 'websocket');
    assert.equal(socket.handshake.query.auth, 'PRIVATE_TICKET');
    socket.emit('SYSTEM', JSON.stringify({ type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } }));
  });
  const actual = require('socket.io-client');
  const module = require.cache[require.resolve('socket.io-client')]!;
  // Substitute only the origin in this test. Preserve the actual ticket query
  // so URI parsing/encoding is exercised by both client and transport packages.
  // Production has no loopback-bypass option.
  module.exports = (url: string, options: Record<string, unknown>) => {
    const ticket = new URL(url);
    assert.equal(ticket.hostname, 'ssio08.nchat.naver.com');
    assert.equal(options.reconnection, false); assert.equal(options.forceNew, true);
    assert.equal(options.forceNode, true); assert.equal(options.rejectUnauthorized, true);
    assert.equal(options.autoConnect, false); assert.deepEqual(options.transports, ['websocket']);
    return actual(`${origin}${ticket.pathname}${ticket.search}`, options);
  };
  t.after(() => { module.exports = actual; });
  let subscribed = 0; let unsubscribed = 0;
  const api: ChatApi = {
    async createUserSession() { return 'https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET'; },
    async subscribeChat(_token, key) {
      assert.equal(key, 'PRIVATE_KEY'); subscribed++;
      peer.emit('SYSTEM', JSON.stringify({ type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    },
    async unsubscribeChat() { unsubscribed++; },
  };
  const session = new ChzzkChatSession(api, () => {});
  t.after(() => session.stop());
  await session.start('PRIVATE_ACCESS', 'mine'); assert.equal(subscribed, 1);
  assert.equal(session.snapshot().received, 0);
  peer.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender', profile: { nickname: '실제 소켓 / 모의 제공자', verifiedMark: false }, userRoleCode: 'common_user', content: '한글 😀', messageTime: 1700000000000 }));
  for (let i = 0; session.snapshot().received === 0 && i < 100; i++) await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(session.snapshot().messages[0]?.content, '한글 😀');
  await session.stop(); assert.equal(unsubscribed, 1); assert.deepEqual(session.snapshot().messages, []);
});

test('production socket factory rejects arbitrary endpoints and unsafe debug logging before connecting', () => {
  for (const url of ['http://127.0.0.1:123/', 'https://evil.invalid/?auth=x', 'https://nchat.naver.com.evil.invalid/?auth=x']) {
    assert.throws(() => createChzzkSocket(url));
  }
  const debug = process.env.DEBUG; process.env.DEBUG = '*';
  try { assert.throws(() => createChzzkSocket('https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET')); }
  finally { if (debug === undefined) delete process.env.DEBUG; else process.env.DEBUG = debug; }
});
