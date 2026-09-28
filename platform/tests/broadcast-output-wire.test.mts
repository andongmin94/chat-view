// SPDX-License-Identifier: GPL-2.0-or-later
// Real application HTTP/WebSocket with synthetic provider/chat, not live OBS.
import test from 'node:test';
import type { TestContext } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter, once } from 'node:events';
import { request as httpRequest } from 'node:http';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import type { DisplayConnectionState } from '../server/chat/display-gateway.mts';
import { BroadcastOutput } from '../server/chat/broadcast-output.mts';
import { fixture, nonce } from './fixtures/service.mts';

type Frame = { connection: DisplayConnectionState; snapshot: { messages: { content: string }[] } };
async function reader(t: TestContext, origin: string, token: string) {
  const peer = new WebSocket(origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 2000,
  });
  let latest: Frame | undefined;
  const changes = new EventEmitter();
  peer.on('error', () => {});
  peer.on('message', data => { latest = JSON.parse(String(data)); changes.emit('frame'); });
  t.after(() => peer.terminate());
  await once(peer, 'open');
  return { peer, async until(predicate: (frame: Frame) => boolean): Promise<Frame> {
    const signal = AbortSignal.timeout(4000);
    while (!latest || !predicate(latest)) await once(changes, 'frame', { signal });
    return latest;
  } };
}
const report = (sequence = 1, streaming = true) => ({ sequence, streaming, recording: false, sampleAgeMs: 0 });
type Service = Awaited<ReturnType<typeof fixture>>;
const send = (f: Service, token: string, value: unknown, scheme = 'ChatView-Output') => f.request('/broadcast/output', {
  method: 'POST', headers: { Authorization: `${scheme} ${token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(value),
});

test('only live streaming report scope updates its own session; chat remains read-only', { timeout: 15000 }, async t => {
  let now = 1000;
  const f = await fixture({ createDisplay: (a, o, s) => new DisplayGateway(a, o, s, new BroadcastOutput(() => now)) });
  try {
    const game = await f.connect('alice', 'gaming'), stream = await f.connect('alice', 'streaming');
    const other = await f.connect('bob', 'gaming');
    assert.equal(game.lease.outputToken, undefined); assert.ok(stream.lease.outputToken);
    assert.equal((await send(f, stream.lease.outputToken, report())).status, 409, 'approval alone is not a live reporter');
    const gaming = await reader(t, f.origin, game.lease.token);
    const streaming = await reader(t, f.origin, stream.lease.token);
    const bob = await reader(t, f.origin, other.lease.token);
    await gaming.until(frame => frame.connection.streamingConnections === 1);
    for (const token of [game.lease.token, game.lease.sessionToken, stream.lease.token, stream.lease.sessionToken, nonce()])
      assert.equal((await send(f, token, report())).status, 401);
    assert.equal((await send(f, stream.lease.outputToken, report(), 'Bearer')).status, 401);
    const result = await send(f, stream.lease.outputToken, report());
    assert.deepEqual(await result.json(), { accepted: true, sequence: 1 });
    const frame = await gaming.until(v => v.connection.output.state === 'reported');
    assert.equal(frame.connection.captureState, 'unverified');
    assert(frame.connection.output.state === 'reported' && frame.connection.output.expiresInMs > 0 &&
      frame.connection.output.expiresInMs <= 15000);
    assert.deepEqual(frame.connection.output, { state: 'reported', streaming: true, recording: false,
      expiresInMs: frame.connection.output.state === 'reported' ? frame.connection.output.expiresInMs : 0 });
    assert.equal((await streaming.until(v => v.connection.output.state === 'reported')).connection.output.state, 'reported');
    assert.deepEqual((await bob.until(() => true)).connection.output, { state: 'unknown' });
    assert.equal((await send(f, stream.lease.outputToken, report())).status, 409);
    assert.equal((await send(f, stream.lease.outputToken, report(2))).status, 429);
    now += 500;
    assert.equal((await send(f, stream.lease.outputToken, report(2, false))).status, 200);
    await gaming.until(v => v.connection.output.state === 'reported' && !v.connection.output.streaming);
    now += 16000; f.chats.get('alice')!.publish('chat survives output timeout');
    const expired = await gaming.until(v => v.snapshot.messages[0]?.content === 'chat survives output timeout');
    assert.deepEqual(expired.connection.output, { state: 'unknown' });
    assert.equal((await send(f, stream.lease.outputToken, report(3))).status, 200);
    await gaming.until(v => v.connection.output.state === 'reported');
    const gone = once(streaming.peer, 'close'); streaming.peer.close(); await gone;
    await gaming.until(v => v.connection.streamingConnections === 0 && v.connection.output.state === 'unknown');
    assert.equal((await send(f, stream.lease.outputToken, report(4))).status, 409);
    assert.equal(bob.peer.readyState, WebSocket.OPEN);
  } finally { await f.close(); }
});

test('renewal and revoke fence late report bodies without changing another session', { timeout: 15000 }, async t => {
  const f = await fixture({ createDisplay: (a, o, s) => new DisplayGateway(a, o, s) });
  try {
    const game = await f.connect('alice', 'gaming'), stream = await f.connect('alice', 'streaming');
    const gaming = await reader(t, f.origin, game.lease.token);
    await reader(t, f.origin, stream.lease.token);
    assert.ok(stream.lease.outputToken);
    assert.equal((await send(f, stream.lease.outputToken, report())).status, 200);
    await gaming.until(v => v.connection.output.state === 'reported');
    const refreshed = await f.native('/display/refresh', 'ChatView-Session', stream.lease.sessionToken);
    const lease = await refreshed.json() as { token: string; outputToken: string };
    await gaming.until(v => v.connection.output.state === 'unknown');
    assert.equal((await send(f, stream.lease.outputToken, report(2))).status, 401);
    await reader(t, f.origin, lease.token);
    assert.equal((await send(f, lease.outputToken, report())).status, 200);
    await gaming.until(v => v.connection.output.state === 'reported');
    const body = JSON.stringify(report(2));
    const pending = new Promise<number>((resolve, reject) => {
      const req = httpRequest(f.origin + '/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${lease.outputToken}`, 'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(body),
      } }, res => { res.resume(); resolve(res.statusCode!); });
      req.on('error', reject); req.write(body.slice(0, -1));
      // The request exists before revocation; completing its body cannot restore authority.
      void f.native('/display/signout', 'ChatView-Session', stream.lease.sessionToken)
        .then(res => { assert.equal(res.status, 200); req.end(body.slice(-1)); }).catch(reject);
    });
    assert.equal(await pending, 401);
    await gaming.until(v => v.connection.streamingConnections === 0 && v.connection.output.state === 'unknown');
    assert.equal((await send(f, lease.outputToken, report(3))).status, 401);
    const replacement = await f.start('streaming');
    const reapproved = await f.approve(replacement, 'streaming', stream.b);
    assert.ok(reapproved.outputToken);
    assert.notEqual(reapproved.outputToken, lease.outputToken);
  } finally { await f.close(); }
});

test('output route rejects client-selected authority, malformed body and foreign origin', async t => {
  const f = await fixture({ createDisplay: (a, o, s) => new DisplayGateway(a, o, s) });
  try {
    const stream = await f.connect('alice', 'streaming'); await reader(t, f.origin, stream.lease.token);
    assert.ok(stream.lease.outputToken);
    for (const body of [{ ...report(), owner: 'bob' }, { ...report(), captureState: 'verified' },
      { ...report(), sequence: 0 }, { ...report(), sampleAgeMs: 3000 }])
      assert.equal((await send(f, stream.lease.outputToken, body)).status, 400);
    assert.equal((await f.request('/broadcast/output')).status, 405);
    assert.equal((await f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: `ChatView-Output ${stream.lease.outputToken}`, Origin: 'https://foreign.invalid',
      'Content-Type': 'application/json',
    }, body: JSON.stringify(report()) })).status, 403);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', stream.lease.outputToken)).status, 401);
    assert.throws(() => f.creators.gateway(stream.lease.outputToken));
    assert.equal((await send(f, stream.lease.outputToken, report())).status, 200);
  } finally { await f.close(); }
});
