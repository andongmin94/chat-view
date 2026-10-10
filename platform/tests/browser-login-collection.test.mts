// SPDX-License-Identifier: GPL-2.0-or-later
// Real BrowserLogin, DisplayAccess and SQLite. No provider, native UI or OBS claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { BrowserLogin, LOGIN_POLL_MS, LOGIN_WINDOW_MS } from '../server/chat/browser-login.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { SessionStore, hashSecret } from '../server/chat/session-store.mts';

const denied = (status: number) => (error: unknown) => error instanceof DisplayAccessError && error.status === status;
function setup() {
  let now = 1000;
  const sessions = new SessionStore(':memory:', () => now);
  const access = new DisplayAccess(() => ({ id: 'alice', expiresAt: now + 600000 }), () => now, sessions);
  const other = new DisplayAccess(() => ({ id: 'bob', expiresAt: now + 600000 }), () => now, sessions);
  const login = new BrowserLogin(undefined, () => now);
  const start = (remember = false, role: 'gaming' | 'streaming' = 'streaming') => {
    const verifier = randomBytes(32).toString('hex');
    return { ...login.start(hashSecret(verifier), remember, role), verifier };
  };
  const collect = (r: ReturnType<typeof start>) => login.poll(r.id, r.verifier);
  return { sessions, access, other, login, start, collect, advance: (ms = LOGIN_POLL_MS) => { now += ms; },
    close() { login.clear(); access.close(); other.close(); sessions.close(); } };
}

for (const winnerIndex of [0, 1]) {
  test(`two consented senders collect in order ${winnerIndex}; the loser retains only a reconsent request`, () => {
    const s = setup();
    try {
      const game = s.sessions.create('alice', 'gaming');
      const requests = [s.start(false), s.start(true)];
      for (const r of requests) s.login.approve(r.id, s.access);
      const winner = s.collect(requests[winnerIndex]!);
      assert.equal(winner.status, 'approved');
      if (winner.status !== 'approved') throw Error('missing winner');
      const loser = requests[1 - winnerIndex]!;
      assert.deepEqual(s.collect(loser), { status: 'pending' }, 'collection conflict must not consume the request');
      assert.equal(s.login.collectionState(loser.id, 'alice'), 'retry-required');
      assert.equal(s.login.collectionState(loser.id, 'bob'), undefined);
      assert.equal(s.login.view(loser.id).remember, loser === requests[1]);
      assert.equal(s.login.view(loser.id).role, 'streaming');
      assert.throws(() => s.collect(loser), denied(429), 'a conflict does not reset poll throttling');
      assert(s.sessions.find(game.token));
      assert(s.access.authenticate(winner.lease.token));
      assert.equal(s.sessions.connections('alice').length, 2, 'no orphan approval from the losing transaction');
      assert.throws(() => s.collect(requests[winnerIndex]!), denied(410), 'successful collection is still one-use');
      s.access.signout(winner.lease.sessionToken);
      for (let i = 0; i < 2; i++) {
        s.advance(); assert.deepEqual(s.collect(loser), { status: 'pending' });
        assert.equal(s.sessions.connections('alice').length, 1, 'freeing the slot never replays consent');
      }
      s.login.approve(loser.id, s.access);
      assert.equal(s.login.collectionState(loser.id, 'alice'), 'awaiting-app');
      s.advance(); const replacement = s.collect(loser);
      assert.equal(replacement.status, 'approved');
      if (replacement.status !== 'approved') throw Error('missing replacement');
      assert.notEqual(replacement.lease.membership?.connectionId, winner.lease.membership?.connectionId);
      assert.equal(replacement.lease.membership?.broadcastSessionId, game.membership?.broadcastSessionId);
      assert(s.access.authenticate(replacement.lease.token));
      assert.throws(() => s.collect(loser), denied(410));
    } finally { s.close(); }
  });
}

for (const end of ['deny', 'expire', 'clear'] as const) {
  test(`collection conflict followed by ${end} never replaces the winner`, () => {
    const s = setup();
    try {
      const a = s.start(), b = s.start();
      s.login.approve(a.id, s.access); s.login.approve(b.id, s.access);
      const winner = s.collect(a); assert.equal(winner.status, 'approved');
      assert.deepEqual(s.collect(b), { status: 'pending' });
      if (end === 'deny') s.login.deny(b.id);
      else if (end === 'expire') s.advance(LOGIN_WINDOW_MS - LOGIN_POLL_MS);
      else s.login.clear();
      s.advance();
      assert.throws(() => s.collect(b), denied(end === 'deny' ? 403 : 410));
      assert.throws(() => s.login.view(b.id), denied(410));
      assert.equal(s.sessions.connections('alice').length, 1);
      if (winner.status === 'approved') assert(s.sessions.find(winner.lease.sessionToken));
    } finally { s.close(); }
  });
}

test('collection state is owner-only; a wrong verifier cannot turn consent into recovery or claim the winner', () => {
  const s = setup();
  try {
    const a = s.start(), b = s.start();
    s.login.approve(a.id, s.access); s.login.approve(b.id, s.access);
    s.collect(a);
    assert.throws(() => s.login.poll(b.id, a.verifier), denied(401));
    assert.equal(s.login.collectionState(b.id, 'alice'), 'awaiting-app');
    assert.equal(s.login.collectionState(b.id, 'bob'), undefined);
    assert.deepEqual(s.collect(b), { status: 'pending' });
    assert.throws(() => s.login.approve(b.id, s.other), denied(403), 'recovery cannot change the consented owner');
    assert.equal(s.sessions.connections('bob').length, 0);
    s.access.revokeSessions(); s.advance();
    assert.deepEqual(s.collect(b), { status: 'pending' }, 'old consent is gone even after authority changes');
    assert.equal(s.sessions.connections('alice').length, 0);
    s.login.approve(b.id, s.access); s.advance();
    assert.equal(s.collect(b).status, 'approved', 'a fresh explicit same-owner consent may authorize again');
  } finally { s.close(); }
});

test('repeated conflicts preserve the original deadline and release failed exchange tickets', () => {
  const s = setup();
  try {
    s.sessions.create('alice', 'streaming');
    const r = s.start();
    for (let i = 0; i < 8; i++) {
      s.login.approve(r.id, s.access); s.advance();
      assert.deepEqual(s.collect(r), { status: 'pending' });
      assert.equal(s.sessions.connections('alice').length, 1);
    }
    s.advance(LOGIN_WINDOW_MS - 8 * LOGIN_POLL_MS);
    assert.throws(() => s.login.view(r.id), denied(410), 'reconsent never extends the initial five-minute window');
    const fresh = s.start(); s.login.approve(fresh.id, s.other);
    assert.equal(s.collect(fresh).status, 'approved', 'another owner remains independent');
  } finally { s.close(); }
});

for (const failure of ['closed', 'capacity', 'storage', 'gaming-role'] as const) {
  test(`non-recoverable ${failure} failure is not disguised as a pending streaming request`, () => {
    const s = setup();
    try {
      const r = s.start(false, failure === 'gaming-role' ? 'gaming' : 'streaming');
      s.login.approve(r.id, s.access);
      if (failure === 'closed') s.access.close();
      else if (failure === 'capacity') for (let i = 0; i < 4; i++) s.access.issue();
      else if (failure === 'storage') s.sessions.database.exec(`CREATE TRIGGER reject_fixture_session BEFORE INSERT ON chat_sessions
        BEGIN SELECT RAISE(ABORT, 'fixture storage failure'); END;`);
      else for (let i = 0; i < 4; i++) s.sessions.create('alice', 'gaming');
      assert.throws(() => s.collect(r), failure === 'storage' ? /fixture storage failure/u
        : denied(failure === 'closed' ? 401 : failure === 'capacity' ? 429 : 409));
      assert.throws(() => s.login.view(r.id), denied(410));
    } finally { s.close(); }
  });
}
