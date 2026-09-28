// SPDX-License-Identifier: GPL-2.0-or-later
// Actual HTTP/WebSocket delivery with synthetic provider/chat. No OBS output,
// capture safety, remote hardware attestation or audience measurement is tested.
import test from 'node:test';
import type { TestContext } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter, once } from 'node:events';
import { request } from 'node:http';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import type { DisplayConnectionState } from '../server/chat/display-gateway.mts';
import { fixture, nonce } from './fixtures/service.mts';

type Frame = { connection: DisplayConnectionState; snapshot: { messages: { content: string }[] } };
async function reader(t: TestContext, origin: string, token: string) {
  const peer = new WebSocket(origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 2000,
  });
  let latest: Frame | undefined;
  const changes = new EventEmitter();
  peer.on('error', () => {});
  peer.on('message', (data, binary) => {
    assert.equal(binary, false); latest = JSON.parse(String(data)); changes.emit('frame');
  });
  t.after(() => peer.terminate());
  await once(peer, 'open');
  return { peer, async until(predicate: (frame: Frame) => boolean): Promise<Frame> {
    const signal = AbortSignal.timeout(3000);
    while (!latest || !predicate(latest)) await once(changes, 'frame', { signal });
    return latest;
  } };
}

test('same-session display counts are owner-scoped and change on connect, disconnect and revoke', { timeout: 15000 }, async t => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  try {
    const game = await f.connect('alice', 'gaming'), stream = await f.connect('alice', 'streaming');
    const other = await f.connect('bob', 'streaming');
    const gaming = await reader(t, f.origin, game.lease.token);
    const initial = await gaming.until(frame => frame.connection.streamingConnections === 0);
    assert.deepEqual(initial.connection, { ...game.lease.membership,
      gamingConnections: 1, streamingConnections: 0, captureState: 'unverified', output: { state: 'unknown' } });
    const streaming = await reader(t, f.origin, stream.lease.token);
    const both = await gaming.until(frame => frame.connection.streamingConnections === 1);
    assert.deepEqual(both.connection, { ...game.lease.membership,
      gamingConnections: 1, streamingConnections: 1, captureState: 'unverified', output: { state: 'unknown' } });
    const second = await streaming.until(frame => frame.connection.gamingConnections === 1);
    assert.deepEqual(second.connection, { ...stream.lease.membership,
      gamingConnections: 1, streamingConnections: 1, captureState: 'unverified', output: { state: 'unknown' } });
    const bob = await reader(t, f.origin, other.lease.token);
    const isolated = await bob.until(() => true);
    assert.deepEqual(isolated.connection, { ...other.lease.membership,
      gamingConnections: 0, streamingConnections: 1, captureState: 'unverified', output: { state: 'unknown' } });
    assert(!JSON.stringify(isolated).includes(game.lease.membership.broadcastSessionId));
    assert(!JSON.stringify(both).includes(other.lease.membership.broadcastSessionId));
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', game.lease.sessionToken, 'streaming')).status, 409);
    f.chats.get('alice')!.publish('still authorized');
    await gaming.until(frame => frame.snapshot.messages[0]?.content === 'still authorized');
    const closed = once(streaming.peer, 'close');
    assert.equal((await f.native('/display/signout', 'ChatView-Session', stream.lease.sessionToken)).status, 200);
    await closed;
    const remaining = await gaming.until(frame => frame.connection.streamingConnections === 0);
    assert.equal(remaining.connection.gamingConnections, 1);
    assert.equal(remaining.connection.broadcastSessionId, game.lease.membership.broadcastSessionId);
    assert.equal(bob.peer.readyState, WebSocket.OPEN);
    const gameClosed = once(gaming.peer, 'close');
    await f.creators.revoke('alice'); await gameClosed;
    f.chats.get('bob')!.publish('other creator unaffected');
    const liveBob = await bob.until(frame => frame.snapshot.messages[0]?.content === 'other creator unaffected');
    assert.equal(liveBob.connection.streamingConnections, 1);
    assert.equal(liveBob.connection.gamingConnections, 0);
    assert.equal(liveBob.connection.captureState, 'unverified');
  } finally { await f.close(); }
});

test('duplicate launch-role headers are rejected on the wire without creating an approval', async () => {
  const f = await fixture();
  try {
    await new Promise<void>((resolve, reject) => {
      const req = request(f.origin + '/display/login', { method: 'POST', headers: {
        Authorization: `ChatView-Challenge ${nonce()}`,
        'X-ChatView-Role': ['gaming', 'streaming'],
      } }, response => {
        response.resume();
        try { assert.equal(response.statusCode, 400); resolve(); } catch (error) { reject(error); }
      });
      req.on('error', reject); req.end();
    });
    assert.equal(f.store.connections('alice').length, 0);
    assert.equal(f.exchangeCalls, 0);
  } finally { await f.close(); }
});
