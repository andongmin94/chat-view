// SPDX-License-Identifier: GPL-2.0-or-later
// Real HTTP/SQLite restarts with synthetic CHZZK/chat. The separate wire test
// additionally uses the actual display gateway; neither certifies live OBS.
import test from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { ChzzkError } from '../server/chzzk/api.mts';
import type { Channel, Tokens } from '../server/chzzk/api.mts';
import { fixture, deferred, denied, ownerOf, tokens } from './fixtures/service.mts';

type Service = Awaited<ReturnType<typeof fixture>>;
type Lease = { token: string; membership: { role: string; connectionId: string; broadcastSessionId: string } };
async function resumed(f: Service, token: string): Promise<Lease> {
  const response = await f.native('/display/refresh', 'ChatView-Session', token);
  assert.equal(response.status, 200);
  const body = await response.text();
  assert.doesNotMatch(body, /access:|refresh:|accessToken|refreshToken/u);
  return JSON.parse(body) as Lease;
}
async function disk() {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-restart-'));
  const path = join(dir, 'sessions.sqlite'), key = randomBytes(32);
  let now = Date.now(), f = await fixture({ path, key, now: () => now });
  return { get f() { return f; }, advance(ms: number) { now += ms; },
    async restart() { await f.close(); f = await fixture({ path, key, now: () => now }); return f; },
    async close() { await f.close(); key.fill(0); rmSync(dir, { recursive: true, force: true }); } };
}

test('two approved PCs resume after restart with identical roles, one upstream and no OAuth reconsent', async () => {
  const d = await disk();
  try {
    const gaming = await d.f.connect('alice', 'gaming'), streaming = await d.f.connect('alice', 'streaming');
    const bob = await d.f.connect('bob', 'gaming');
    const f = await d.restart();
    const [game, stream, other] = await Promise.all([
      resumed(f, gaming.lease.sessionToken), resumed(f, streaming.lease.sessionToken), resumed(f, bob.lease.sessionToken),
    ]);
    assert.deepEqual(game.membership, gaming.lease.membership);
    assert.deepEqual(stream.membership, streaming.lease.membership);
    assert.deepEqual(other.membership, bob.lease.membership);
    assert.equal(f.exchangeCalls, 0); assert.equal(f.refreshCalls, 0); assert.equal(f.userCalls, 2);
    assert.equal(f.active.get('alice'), 1); assert.equal(f.active.get('bob'), 1);
    assert.equal(f.creators.gateway(game.token), f.creators.gateway(stream.token));
    assert.notEqual(f.creators.gateway(game.token), f.creators.gateway(other.token));
    assert.throws(() => f.creators.gateway(gaming.lease.token), denied(401));
    const account = await f.request('/account', { headers: { Cookie: gaming.b.cookie } });
    assert.doesNotMatch(await account.text(), /channel &lt;alice&gt;|connectionId/u);
    const second = await d.restart();
    assert.deepEqual((await resumed(second, gaming.lease.sessionToken)).membership, game.membership);
    assert.equal(second.exchangeCalls, 0);
  } finally { await d.close(); }
});

test('downtime does not extend token expiry; simultaneous two-PC resume rotates exactly once', async () => {
  const d = await disk();
  try {
    const game = await d.f.connect('alice', 'gaming'), stream = await d.f.connect('alice', 'streaming');
    const old = d.f.grants.load('alice')!;
    d.advance(3_600_001);
    const f = await d.restart();
    const [a, b] = await Promise.all([resumed(f, game.lease.sessionToken), resumed(f, stream.lease.sessionToken)]);
    assert.equal(f.refreshCalls, 1); assert.equal(f.exchangeCalls, 0); assert.equal(f.userCalls, 1);
    assert.equal(f.active.get('alice'), 1);
    assert.deepEqual(a.membership, game.lease.membership); assert.deepEqual(b.membership, stream.lease.membership);
    assert.ok(f.grants.load('alice')!.expiresAt > old.expiresAt);
    assert.equal(f.grants.load('alice')!.tokens.accessToken, tokens('alice', 1).accessToken);
    const next = await d.restart();
    await resumed(next, game.lease.sessionToken);
    assert.equal(next.refreshCalls, 0); assert.deepEqual(next.startedWith, [tokens('alice', 1).accessToken]);
  } finally { await d.close(); }
});

test('shutdown during refresh never replays a claimed token; explicit reconsent restores existing PC roles', async () => {
  const d = await disk();
  try {
    const game = await d.f.connect('alice', 'gaming'), stream = await d.f.connect('alice', 'streaming');
    const pending = deferred<Tokens>(); d.f.setRefresh(() => pending.promise);
    const refresh = d.f.creators.refresh('alice'); void refresh.catch(() => {});
    assert.equal(d.f.grants.load('alice'), undefined);
    const f = await d.restart();
    pending.resolve(tokens('alice', 1)); await assert.rejects(refresh, denied(503));
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', game.lease.sessionToken)).status, 503);
    assert.equal(f.refreshCalls, 0); assert.equal(f.exchangeCalls, 0); assert.equal(f.grants.load('alice'), undefined);
    assert.equal(f.store.connections('alice').length, 2);
    const request = await f.start(); await f.authenticate('alice', request);
    const [a, b] = await Promise.all([resumed(f, game.lease.sessionToken), resumed(f, stream.lease.sessionToken)]);
    assert.deepEqual(a.membership, game.lease.membership); assert.deepEqual(b.membership, stream.lease.membership);
    assert.equal(f.store.connections('alice').length, 2);
  } finally { await d.close(); }
});

test('provider revocation while offline removes only that creator approvals and cannot revive on reconsent', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming'), bob = await d.f.connect('bob', 'streaming');
    const f = await d.restart();
    f.setUser(async access => {
      if (ownerOf(access) === 'alice') throw new ChzzkError('http', 401);
      return { channelId: 'bob', channelName: 'Bob' };
    });
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 401);
    assert.equal(f.grants.load('alice'), undefined); assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    await resumed(f, bob.lease.sessionToken);
    const next = await d.restart();
    const request = await next.start(); await next.authenticate('alice', request);
    assert.equal((await next.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 401);
    await resumed(next, bob.lease.sessionToken);
  } finally { await d.close(); }
});

test('confirmed refresh rejection also revokes approvals rather than treating it as a transient outage', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'streaming');
    d.advance(3_600_001); const f = await d.restart();
    f.setRefresh(async () => { throw new ChzzkError('http', 403); });
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 401);
    assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    assert.equal(f.grants.load('alice'), undefined);
  } finally { await d.close(); }
});

test('a temporary identity lookup outage preserves the sealed grant for safe later retry', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming');
    const saved = d.f.grants.load('alice'); const f = await d.restart();
    f.setUser(async () => { throw new ChzzkError('transport'); });
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 503);
    assert.deepEqual(f.grants.load('alice'), saved); assert.ok(f.store.find(alice.lease.sessionToken));
    f.setUser(undefined); await resumed(f, alice.lease.sessionToken);
    assert.equal(f.refreshCalls, 0); assert.equal(f.exchangeCalls, 0);
  } finally { await d.close(); }
});

test('chat failure after a successful refresh does not discard the durable replacement', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming');
    d.f.failChat(); await assert.rejects(d.f.creators.refresh('alice'));
    assert.equal(d.f.grants.load('alice')!.tokens.accessToken, tokens('alice', 1).accessToken);
    const f = await d.restart(); await resumed(f, alice.lease.sessionToken);
    assert.equal(f.refreshCalls, 0); assert.equal(f.exchangeCalls, 0);
    assert.deepEqual(f.startedWith, [tokens('alice', 1).accessToken]);
  } finally { await d.close(); }
});

test('failure to persist a refresh reply cannot expose new credentials or replay old ones after restart', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming');
    d.f.store.database.exec(`CREATE TRIGGER reject_rotation BEFORE UPDATE ON provider_grants
      WHEN NEW.sealed IS NOT NULL BEGIN SELECT RAISE(ABORT, 'synthetic disk failure'); END;`);
    await assert.rejects(d.f.creators.refresh('alice'));
    assert.throws(() => d.f.creators.gateway(alice.lease.token), denied(401));
    const f = await d.restart();
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 503);
    assert.equal(f.refreshCalls, 0); assert.equal(f.grants.load('alice'), undefined);
  } finally { await d.close(); }
});

test('revoking a restored account defeats an in-flight identity lookup without starting a chat', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming'); const f = await d.restart();
    const user = deferred<Channel>(), called = deferred<void>();
    f.setUser(async () => { called.resolve(); return user.promise; });
    const resuming = f.creators.resume(alice.lease.sessionToken); void resuming.catch(() => {});
    await called.promise; await f.creators.revoke('alice');
    user.resolve({ channelId: 'alice', channelName: 'Alice' });
    await assert.rejects(resuming, denied(503)); assert.equal(f.active.get('alice'), undefined);
    const next = await d.restart();
    assert.equal(next.store.find(alice.lease.sessionToken), undefined); assert.equal(next.grants.load('alice'), undefined);
  } finally { await d.close(); }
});

test('corruption blocks the affected stored creator without falling back to another account', async () => {
  const d = await disk();
  try {
    const alice = await d.f.connect('alice', 'gaming'), bob = await d.f.connect('bob', 'gaming');
    d.f.store.database.prepare("UPDATE provider_grants SET sealed = ? WHERE owner = 'alice'").run(randomBytes(100));
    const f = await d.restart();
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 503);
    await resumed(f, bob.lease.sessionToken); assert.equal(f.userCalls, 1);
    assert.equal(f.active.get('alice'), undefined); assert.equal(f.active.get('bob'), 1);
  } finally { await d.close(); }
});

test('selective and full revocations remain effective across service restart', async () => {
  const d = await disk();
  try {
    const game = await d.f.connect('alice', 'gaming'), stream = await d.f.connect('alice', 'streaming');
    await d.f.native('/display/signout', 'ChatView-Session', game.lease.sessionToken);
    const f = await d.restart();
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', game.lease.sessionToken)).status, 401);
    await resumed(f, stream.lease.sessionToken);
    f.failRevoke(); assert.equal(await f.creators.revoke('alice'), false);
    const next = await d.restart();
    assert.equal((await next.native('/display/refresh', 'ChatView-Session', stream.lease.sessionToken)).status, 401);
    assert.equal(next.grants.load('alice'), undefined);
  } finally { await d.close(); }
});
