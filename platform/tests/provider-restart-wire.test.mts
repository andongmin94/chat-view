// SPDX-License-Identifier: GPL-2.0-or-later
// Actual ws + display gateway + HTTP/SQLite. CHZZK/chat remain synthetic, and
// this is neither certificate/Windows acceptance nor a hardware/video test.
import test from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import type { RawData } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { fixture } from './fixtures/service.mts';

type Frame = { type: string; version: number; snapshot: { messages: { content: string }[] } };
function receive(peer: WebSocket, content?: string): Promise<Frame> {
  return new Promise((resolve, reject) => {
    const clean = () => { clearTimeout(timer); peer.off('message', message); peer.off('error', failed); peer.off('close', closed); };
    const failed = (error: Error) => { clean(); reject(error); };
    const closed = () => failed(new Error('Display closed before expected frame'));
    const message = (data: RawData, binary: boolean) => {
      try {
        assert.equal(binary, false);
        const frame = JSON.parse(data.toString()) as Frame;
        assert.equal(frame.type, 'chat-snapshot'); assert.equal(frame.version, 1);
        if (content !== undefined && !frame.snapshot.messages.some(m => m.content === content)) return;
        clean(); resolve(frame);
      } catch (error) { failed(error as Error); }
    };
    const timer = setTimeout(() => failed(new Error('Expected display frame missing')), 5000);
    peer.on('message', message); peer.once('error', failed); peer.once('close', closed);
  });
}

test('both PCs receive isolated real display frames again after service restart; signout closes only its peer', { timeout: 20000 }, async () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-restart-wire-')), path = join(dir, 'sessions.sqlite');
  const key = randomBytes(32), peers: WebSocket[] = [];
  const options = { path, key, createDisplay: (access: ConstructorParameters<typeof DisplayGateway>[0],
    origin: () => string, snapshot: () => unknown) => new DisplayGateway(access, origin, snapshot) };
  let f = await fixture(options);
  const open = async (token: string) => {
    const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', { headers: { Authorization: `Bearer ${token}` } });
    peers.push(peer);
    const seen: string[] = []; peer.on('message', data => seen.push(data.toString()));
    await receive(peer); return { peer, seen };
  };
  try {
    const game = await f.connect('alice', 'gaming'), stream = await f.connect('alice', 'streaming');
    const bob = await f.connect('bob', 'gaming');
    const initial = await open(game.lease.token);
    const first = receive(initial.peer, 'before-restart'); f.chats.get('alice')!.publish('before-restart'); await first;
    const ended = once(initial.peer, 'close', { signal: AbortSignal.timeout(5000) });
    await f.close(); await ended; f = await fixture(options);
    const renew = async (sessionToken: string) => {
      const response = await f.native('/display/refresh', 'ChatView-Session', sessionToken);
      assert.equal(response.status, 200); return (await response.json() as { token: string }).token;
    };
    const leases = await Promise.all([game, stream, bob].map(value => renew(value.lease.sessionToken)));
    const [a, b, other] = await Promise.all(leases.map(open));
    const messages = [receive(a!.peer, 'alice-only'), receive(b!.peer, 'alice-only'), receive(other!.peer, 'bob-only')];
    f.chats.get('alice')!.publish('alice-only'); f.chats.get('bob')!.publish('bob-only'); await Promise.all(messages);
    for (const consumer of [a!, b!]) {
      assert.doesNotMatch(consumer.seen.join(''), /bob-only|access:|refresh:|senderChannelId/u);
    }
    assert.doesNotMatch(other!.seen.join(''), /alice-only|access:|refresh:/u);
    assert.equal(f.exchangeCalls, 0); assert.equal(f.active.get('alice'), 1);
    const closed = once(a!.peer, 'close', { signal: AbortSignal.timeout(5000) });
    assert.equal((await f.native('/display/signout', 'ChatView-Session', game.lease.sessionToken)).status, 200);
    await closed;
    const continued = receive(b!.peer, 'streaming-still-connected');
    f.chats.get('alice')!.publish('streaming-still-connected'); await continued;
    assert.equal(other!.peer.readyState, WebSocket.OPEN);
  } finally {
    for (const peer of peers) peer.terminate();
    await f.close(); key.fill(0); rmSync(dir, { recursive: true, force: true });
  }
});
