// SPDX-License-Identifier: GPL-2.0-or-later
// Actual Socket.IO transport to a local simulated provider, not NAVER.
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
      peer.emit('SYSTEM', JSON.stringify({ type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    },
    async unsubscribeChat() { unsubscribed++; },
  };
  const session = new ChzzkChatSession(api, () => {});
  t.after(() => session.stop());
  await session.start('TEST_ACCESS', 'mine'); assert.equal(subscribed, 1);
  assert.equal(session.snapshot().received, 0);
  peer.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender', profile: { nickname: '실제 소켓 / 모의 제공자', verifiedMark: false }, userRoleCode: 'common_user', content: '한글 😀', messageTime: 1700000000000 }));
  for (let i = 0; session.snapshot().received === 0 && i < 100; i++) await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(session.snapshot().messages[0]?.content, '한글 😀');
  await session.stop(); assert.equal(unsubscribed, 1); assert.deepEqual(session.snapshot().messages, []);
});

test('production factory rejects arbitrary endpoints and debug logging before connecting', () => {
  for (const url of ['http://127.0.0.1:123/', 'https://evil.invalid/?auth=x', 'https://nchat.naver.com.evil.invalid/?auth=x']) {
    assert.throws(() => createChzzkSocket(url));
  }
  const debug = process.env.DEBUG; process.env.DEBUG = '*';
  try { assert.throws(() => createChzzkSocket('https://ssio08.nchat.naver.com/?auth=TEST_TICKET')); }
  finally { if (debug === undefined) delete process.env.DEBUG; else process.env.DEBUG = debug; }
});
