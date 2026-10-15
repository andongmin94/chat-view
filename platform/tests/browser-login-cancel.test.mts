// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import type { TestContext } from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { BrowserLogin, LOGIN_POLL_MS, LOGIN_WINDOW_MS } from '../server/chat/browser-login.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { SessionStore, hashSecret } from '../server/chat/session-store.mts';
import type { ConnectionRole } from '../server/chat/session-store.mts';

const secret = () => randomBytes(32).toString('hex');
const denied = (status: number) => (error: unknown) =>
  error instanceof DisplayAccessError && error.status === status;
function setup(t: TestContext) {
  let time = 10_000;
  const now = () => time;
  const sessions = new SessionStore(':memory:', now);
  const access = new DisplayAccess(() => ({ id: 'cancel-owner', expiresAt: time + 600_000 }), now, sessions);
  const login = new BrowserLogin(access, now);
  t.after(() => { login.clear(); access.close(); sessions.close(); });
  const start = (role: ConnectionRole = 'gaming', keep = false) => {
    const proof = secret(); return { ...login.start(hashSecret(proof), keep, role), proof };
  };
  return { login, access, sessions, start, advance: (ms: number) => { time += ms; } };
}

test('initiating proof cancels only its pending request; old browser actions cannot revive it', t => {
  const { login, sessions, start } = setup(t), stopped = start(), other = start();
  assert.deepEqual(login.cancel(stopped.id, stopped.proof), { cancelled: true });
  assert.deepEqual(login.cancel(stopped.id, stopped.proof), { cancelled: true });
  assert.throws(() => login.view(stopped.id), denied(410));
  assert.throws(() => login.approve(stopped.id), denied(410));
  assert.throws(() => login.deny(stopped.id), denied(410));
  assert.throws(() => login.poll(stopped.id, stopped.proof), denied(410));
  assert.equal(login.collectionState(stopped.id, 'cancel-owner'), undefined);
  assert.deepEqual(login.poll(other.id, other.proof), { status: 'pending' });
  assert.equal(sessions.connections('cancel-owner').length, 0);
  const fresh = start(); login.approve(fresh.id);
  assert.equal(login.poll(fresh.id, fresh.proof).status, 'approved');
});

for (const role of ['gaming', 'streaming'] as const) {
  test(`cancellation after ${role} browser consent but before app collection issues no approval`, t => {
    const { login, sessions, start } = setup(t), pending = start(role, true);
    login.approve(pending.id);
    assert.equal(login.collectionState(pending.id, 'cancel-owner'), 'awaiting-app');
    assert.deepEqual(login.cancel(pending.id, pending.proof), { cancelled: true });
    assert.throws(() => login.poll(pending.id, pending.proof), denied(410));
    assert.equal(login.collectionState(pending.id, 'cancel-owner'), undefined);
    assert.equal(sessions.connections('cancel-owner').length, 0);
  });
}

test('URL id, challenge and foreign proofs cannot cancel or alter poll cadence', t => {
  const { login, start, advance } = setup(t), pending = start(), other = start();
  assert.deepEqual(login.poll(pending.id, pending.proof), { status: 'pending' });
  for (const bad of [undefined, '', pending.id, hashSecret(pending.proof), other.proof, 'A'.repeat(64)])
    assert.throws(() => login.cancel(pending.id, bad), denied(401));
  assert.throws(() => login.poll(pending.id, pending.proof), denied(429));
  advance(LOGIN_POLL_MS); login.approve(pending.id);
  assert.equal(login.poll(pending.id, pending.proof).status, 'approved');
});

test('collection winning the race preserves its approval, output scope and other creators', t => {
  const { login, access, sessions, start } = setup(t), pending = start('streaming');
  const foreign = sessions.create('other-owner', 'streaming');
  login.approve(pending.id);
  const result = login.poll(pending.id, pending.proof); assert.equal(result.status, 'approved');
  if (result.status !== 'approved') assert.fail('approval required');
  assert.throws(() => login.cancel(pending.id, pending.proof), denied(410));
  assert.equal(access.authenticate(result.lease.token).id, result.lease.id);
  assert.equal(access.authenticateOutput(result.lease.outputToken).id, result.lease.id);
  assert(sessions.find(result.lease.sessionToken)); assert(sessions.find(foreign.token));
  assert.deepEqual(access.resume(result.lease.sessionToken, 'streaming').membership, result.lease.membership);
});

test('a cancelled losing streaming request cannot use a later free slot or disturb the winner', t => {
  const { login, access, sessions, start, advance } = setup(t);
  const gaming = access.exchange(access.issue().ticket, false, 'gaming');
  const first = start('streaming'), second = start('streaming');
  login.approve(first.id); login.approve(second.id);
  const winner = login.poll(first.id, first.proof); assert.equal(winner.status, 'approved');
  if (winner.status !== 'approved') assert.fail('winner required');
  assert.equal(login.poll(second.id, second.proof).status, 'pending');
  assert.equal(login.collectionState(second.id, 'cancel-owner'), 'retry-required');
  login.cancel(second.id, second.proof);
  assert.equal(access.authenticate(winner.lease.token).id, winner.lease.id);
  assert.equal(access.authenticate(gaming.token).id, gaming.id);
  access.signout(winner.lease.sessionToken); advance(LOGIN_POLL_MS);
  assert.throws(() => login.poll(second.id, second.proof), denied(410));
  assert.throws(() => login.approve(second.id), denied(410));
  assert.deepEqual(sessions.connections('cancel-owner'), [gaming.membership]);
});

test('denied requests can be retired without converting denial to an approval', t => {
  const { login, start, sessions } = setup(t), pending = start(); login.deny(pending.id);
  assert.deepEqual(login.cancel(pending.id, pending.proof), { cancelled: true });
  assert.throws(() => login.poll(pending.id, pending.proof), denied(410));
  assert.equal(sessions.connections('cancel-owner').length, 0);
});

test('cancellation acknowledgement is bounded by the original expiry and pending cap', t => {
  const { login, start, advance } = setup(t);
  const requests = Array.from({ length: 16 }, () => start());
  for (const pending of requests) login.cancel(pending.id, pending.proof);
  assert.throws(() => start(), denied(429));
  const pending = requests[0]!;
  advance(LOGIN_WINDOW_MS - 1);
  assert.deepEqual(login.cancel(pending.id, pending.proof), { cancelled: true });
  advance(1);
  assert.throws(() => login.cancel(pending.id, pending.proof), denied(410));
  const fresh = start(); login.clear();
  assert.throws(() => login.cancel(fresh.id, fresh.proof), denied(410));
  assert.throws(() => login.cancel('invalid-id', fresh.proof), denied(404));
});
