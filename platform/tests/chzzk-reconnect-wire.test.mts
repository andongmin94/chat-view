// SPDX-License-Identifier: GPL-2.0-or-later
// Real Socket.IO v2/EIO3 transport -> Creators -> SQLite approvals ->
// two real private display WebSockets. Provider identity/events are simulated;
// no live CHZZK account, physical PCs or native WebView2 are exercised.
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import { Server } from 'socket.io';
import type { Socket } from 'socket.io';
import { WebSocket } from 'ws';
import type { RawData } from 'ws';
import { ChzzkChatSession } from '../server/chzzk/session.mts';
import type { ChatApi } from '../server/chzzk/session.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { SessionStore } from '../server/chat/session-store.mts';
import type { ConnectionRole } from '../server/chat/session-store.mts';
import { Creators } from '../server/service/creators.mts';
import { ProviderGrants } from '../server/service/provider-grants.mts';

const require = createRequire(import.meta.url);
type Owner = 'alice' | 'bob';
type Frame = {
  type: string; version: number;
  snapshot: { state: string; received: number; messages: { content: string }[] };
  connection: { role: ConnectionRole; broadcastSessionId: string; captureState: string };
};
function receive(peer: WebSocket, matches: (frame: Frame) => boolean): Promise<Frame> {
  return new Promise((resolve, reject) => {
    const clear = () => {
      clearTimeout(timeout);
      peer.off('message', message); peer.off('error', failed); peer.off('close', closed);
    };
    const failed = (error: Error) => { clear(); reject(error); };
    const closed = () => failed(new Error('Private display closed before matching frame'));
    const message = (data: RawData, binary: boolean) => {
      try {
        assert.equal(binary, false);
        const frame = JSON.parse(data.toString()) as Frame;
        assert.equal(frame.type, 'chat-snapshot'); assert.equal(frame.version, 1);
        if (!matches(frame)) return;
        clear(); resolve(frame);
      } catch (error) { failed(error as Error); }
    };
    const timeout = setTimeout(() => failed(new Error('Expected private display frame missing')), 5000);
    peer.on('message', message); peer.once('error', failed); peer.once('close', closed);
  });
}

test('real Socket.IO disconnect and fresh subscribe deliver only new first text to both PCs; last logout stops upstream and revoke is scoped',
  { timeout: 20000 }, async t => {
  const providerHttp = createServer();
  const io = new Server(providerHttp, {
    allowEIO3: true, transports: ['websocket'], serveClient: false, perMessageDeflate: false,
  });
  await new Promise<void>(resolve => providerHttp.listen(0, '127.0.0.1', resolve));
  const port = providerHttp.address();
  assert.ok(port && typeof port !== 'string');

  // The production socket constructor/URL validation is unchanged. Substitute
  // only its destination, keeping the real Socket.IO v2 Manager and EIO3 wire.
  const actual = require('socket.io-client');
  const clientModule = require.cache[require.resolve('socket.io-client')]!;
  clientModule.exports = { Manager: class extends actual.Manager {
    constructor(options: Record<string, unknown>) {
      assert.equal(options.hostname, 'ssio08.nchat.naver.com');
      assert.equal(options.port, 443);
      assert.equal(options.reconnection, false);
      assert.deepEqual(options.transports, ['websocket']);
      super({ ...options, hostname: '127.0.0.1', port: port.port, secure: false });
    }
  } };

  const key = randomBytes(32), sessions = new SessionStore();
  const grants = new ProviderGrants(sessions.database, key);
  const issued = new Map<Owner, number>();
  const providerPeers = new Map<string, { owner: Owner; serial: number; peer: Socket }>();
  const latest = new Map<Owner, { owner: Owner; serial: number; peer: Socket }>();
  const revoked: Owner[] = [];
  let unsubscribeCount = 0;
  const identity = (value: string): Owner => {
    const match = /^TEST_(?:ACCESS|REFRESH)_(alice|bob)$/u.exec(value);
    assert.ok(match);
    return match[1] as Owner;
  };
  io.on('connection', peer => {
    assert.equal(peer.conn.protocol, 3);
    const ticket = String(peer.handshake.query.auth);
    const match = /^TEST_(alice|bob)_(\d+)$/u.exec(ticket);
    assert.ok(match);
    const owner = match[1] as Owner, serial = Number(match[2]);
    const record = { owner, serial, peer };
    const sessionKey = 'SESSION_' + owner + '_' + serial;
    providerPeers.set(sessionKey, record);
    latest.set(owner, record);
    peer.emit('SYSTEM', JSON.stringify({ type: 'connected', data: { sessionKey } }));
  });
  const chatApi: ChatApi = {
    async createUserSession(token) {
      const owner = identity(token);
      const serial = (issued.get(owner) ?? 0) + 1;
      issued.set(owner, serial);
      return 'https://ssio08.nchat.naver.com/?auth=TEST_' + owner + '_' + serial;
    },
    async subscribeChat(token, key) {
      const record = providerPeers.get(key);
      assert.ok(record);
      assert.equal(identity(token), record.owner);
      // Real Socket.IO may deliver the first frame before the HTTP result.
      record.peer.emit('CHAT', JSON.stringify({ channelId: record.owner,
        senderChannelId: 'viewer', profile: {
          nickname: 'First viewer', verifiedMark: false, userRoleCode: 'common_user',
        }, content: 'first-' + record.owner + '-' + record.serial, messageTime: 1700000000000 }));
      record.peer.emit('SYSTEM', JSON.stringify({ type: 'subscribed',
        data: { eventType: 'CHAT', channelId: record.owner } }));
    },
    async unsubscribeChat() { unsubscribeCount++; },
  };
  let displayOrigin = '';
  const creators = new Creators({
    api: {
      async exchangeCode(owner: string) {
        assert.ok(owner === 'alice' || owner === 'bob');
        return { accessToken: 'TEST_ACCESS_' + owner,
          refreshToken: 'TEST_REFRESH_' + owner, expiresIn: 3600 };
      },
      async getUser(token: string) {
        const owner = identity(token);
        return { channelId: owner, channelName: 'Simulated ' + owner };
      },
      async refresh(token: string) {
        const owner = identity(token);
        return { accessToken: 'TEST_ACCESS_' + owner,
          refreshToken: 'TEST_REFRESH_' + owner, expiresIn: 3600 };
      },
      async revoke(token: string) { revoked.push(identity(token)); },
    },
    sessions, grants,
    createChat: changed => new ChzzkChatSession(chatApi, changed),
    createDisplay: (access, snapshot) => new DisplayGateway(access, () => displayOrigin, snapshot),
  });
  const displayHttp = createServer();
  displayHttp.on('upgrade', (request, socket, head) => {
    try {
      const header = request.headers.authorization;
      if (!header?.startsWith('Bearer ')) { socket.destroy(); return; }
      creators.gateway(header.slice(7)).upgrade(request, socket, head);
    } catch { socket.destroy(); }
  });
  await new Promise<void>(resolve => displayHttp.listen(0, '127.0.0.1', resolve));
  const address = displayHttp.address();
  assert.ok(address && typeof address !== 'string');
  displayOrigin = 'http://127.0.0.1:' + address.port;
  const clients: WebSocket[] = [];
  t.after(async () => {
    for (const client of clients) client.terminate();
    await creators.close();
    displayHttp.closeAllConnections();
    await new Promise<void>(resolve => displayHttp.close(() => resolve()));
    await new Promise<void>(resolve => io.close(() => resolve()));
    grants.close(); sessions.close(); key.fill(0);
    clientModule.exports = actual;
  });

  const approve = (owner: Owner, role: ConnectionRole) => {
    const access = creators.access(owner);
    const lease = access.exchange(access.issue().ticket, false, role);
    creators.track(lease, owner);
    return lease;
  };
  const open = async (token: string, owner: Owner, serial: number, role: ConnectionRole) => {
    const client = new WebSocket(displayOrigin.replace('http:', 'ws:') + '/display/events', {
      headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000,
    });
    client.on('error', () => {});
    clients.push(client);
    const first = await receive(client, frame => frame.snapshot.state === 'subscribed' &&
      frame.snapshot.messages[0]?.content === 'first-' + owner + '-' + serial);
    assert.equal(first.connection.role, role);
    assert.equal(first.connection.captureState, 'unverified');
    assert.equal(first.snapshot.received, 1);
    assert.deepEqual(first.snapshot.messages.map(row => row.content), ['first-' + owner + '-' + serial]);
    assert.doesNotMatch(JSON.stringify(first), /TEST_ACCESS|TEST_REFRESH|senderChannelId|userRoleCode/u);
    return client;
  };
  const publish = (owner: Owner, content: string) => {
    const record = latest.get(owner);
    assert.ok(record);
    record.peer.emit('CHAT', JSON.stringify({ channelId: owner, senderChannelId: 'viewer',
      profile: { nickname: 'Viewer', verifiedMark: false, userRoleCode: 'common_user' },
      content, messageTime: 1700000000000 }));
  };

  await creators.authorize('alice', 'STATE');
  await creators.authorize('bob', 'STATE');
  const gaming = approve('alice', 'gaming'), streaming = approve('alice', 'streaming');
  const other = approve('bob', 'gaming');
  assert.ok(gaming.membership && streaming.membership && other.membership);
  assert.equal(gaming.membership.broadcastSessionId, streaming.membership.broadcastSessionId);
  assert.notEqual(gaming.membership.broadcastSessionId, other.membership.broadcastSessionId);
  await Promise.all([creators.ensureChat('alice'), creators.ensureChat('bob')]);
  assert.equal(issued.get('alice'), 1);
  assert.equal(issued.get('bob'), 1);
  const [game, sender, bob] = await Promise.all([
    open(gaming.token, 'alice', 1, 'gaming'),
    open(streaming.token, 'alice', 1, 'streaming'),
    open(other.token, 'bob', 1, 'gaming'),
  ]);
  const initial = [receive(game, frame => frame.snapshot.received === 2),
    receive(sender, frame => frame.snapshot.received === 2)];
  publish('alice', 'alice-before-disconnect');
  for (const frame of await Promise.all(initial)) {
    assert.deepEqual(frame.snapshot.messages.map(m => m.content),
      ['first-alice-1', 'alice-before-disconnect']);
  }

  const previous = latest.get('alice')!;
  const cleared = [receive(game, frame => frame.snapshot.state === 'disconnected'),
    receive(sender, frame => frame.snapshot.state === 'disconnected')];
  previous.peer.disconnect(true);
  for (const frame of await Promise.all(cleared)) {
    assert.equal(frame.snapshot.received, 0);
    assert.deepEqual(frame.snapshot.messages, []);
  }
  assert.equal(bob.readyState, WebSocket.OPEN);
  const returned = [receive(game, frame => frame.snapshot.state === 'subscribed' &&
    frame.snapshot.messages[0]?.content === 'first-alice-2'),
  receive(sender, frame => frame.snapshot.state === 'subscribed' &&
    frame.snapshot.messages[0]?.content === 'first-alice-2')];
  for (const frame of await Promise.all(returned)) {
    assert.deepEqual(frame.snapshot.messages.map(m => m.content), ['first-alice-2']);
    assert.equal(frame.snapshot.received, 1);
  }
  assert.equal(issued.get('alice'), 2, 'one new ticket/upstream serves both original approvals');
  assert.equal(issued.get('bob'), 1);
  assert.equal(creators.describe('alice').connections.length, 2);
  assert.equal(game.readyState, WebSocket.OPEN);
  assert.equal(sender.readyState, WebSocket.OPEN);

  const bobFrame = receive(bob, frame => frame.snapshot.received === 2);
  publish('bob', 'bob-only');
  assert.deepEqual((await bobFrame).snapshot.messages.map(m => m.content), ['first-bob-1', 'bob-only']);
  const stillStreaming = receive(sender, frame => frame.snapshot.received === 2);
  const gameClosed = once(game, 'close', { signal: AbortSignal.timeout(3000) });
  creators.signout(gaming.sessionToken);
  await gameClosed;
  publish('alice', 'sender-survives-gaming-logout');
  const surviving = await stillStreaming;
  assert.equal(surviving.snapshot.messages[1]?.content, 'sender-survives-gaming-logout');

  const senderClosed = once(sender, 'close', { signal: AbortSignal.timeout(3000) });
  creators.signout(streaming.sessionToken);
  await senderClosed;
  assert.equal(creators.describe('alice').chatState, 'stopped');
  assert.equal(sessions.connections('alice').length, 0);
  assert.ok(grants.load('alice'), 'last-device signout preserves provider grant without global revocation');
  assert.equal(revoked.length, 0);
  // A pending reconnect must not restart an upstream after the last device signs out.
  await new Promise(resolve => setTimeout(resolve, 1150));
  assert.equal(issued.get('alice'), 2);
  assert.ok(unsubscribeCount > 0);

  // A new explicit role approval can use the existing grant again.
  const gamingAgain = approve('alice', 'gaming'), streamingAgain = approve('alice', 'streaming');
  await creators.ensureChat('alice');
  const [gameAgain, senderAgain] = await Promise.all([
    open(gamingAgain.token, 'alice', 3, 'gaming'),
    open(streamingAgain.token, 'alice', 3, 'streaming'),
  ]);
  const closed = [once(gameAgain, 'close', { signal: AbortSignal.timeout(3000) }),
    once(senderAgain, 'close', { signal: AbortSignal.timeout(3000) })];
  assert.equal(await creators.revoke('alice'), true);
  await Promise.all(closed);
  assert.deepEqual(revoked, ['alice']);
  assert.equal(sessions.connections('alice').length, 0);
  assert.equal(bob.readyState, WebSocket.OPEN);
  const remaining = receive(bob, frame => frame.snapshot.received === 3);
  publish('bob', 'bob-after-revoke');
  assert.equal((await remaining).snapshot.messages[2]?.content, 'bob-after-revoke');
  assert.equal(issued.get('bob'), 1);
});
