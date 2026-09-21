// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { BrowserLogin } from '../server/chat/browser-login.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { SessionStore, SessionRoleError, hashSecret, SESSION_MS } from '../server/chat/session-store.mts';
const nonce = () => randomBytes(32).toString('hex');
const status = (code: number) => (e: unknown) => e instanceof DisplayAccessError && e.status === code;

test('same creator shares a session, distinct creators and roles remain isolated', () => {
  const store = new SessionStore();
  try {
    const game = store.create('alice', 'gaming'), stream = store.create('alice', 'streaming');
    const other = store.create('bob', 'streaming');
    assert.equal(game.membership?.broadcastSessionId, stream.membership?.broadcastSessionId);
    assert.notEqual(other.membership?.broadcastSessionId, game.membership?.broadcastSessionId);
    assert.throws(() => store.create('alice', 'streaming'), SessionRoleError);
    assert.equal(store.connections('alice').length, 2);
    store.removeConnection('bob', game.id);
    assert.ok(store.find(game.token));
    store.remove(game.token);
    assert.equal(store.find(game.token), undefined);
    assert.ok(store.find(stream.token)); assert.ok(store.find(other.token));
    store.remove(stream.token);
    assert.equal(store.create('alice', 'streaming').membership?.role, 'streaming');
  } finally { store.close(); }
});

test('roles and opaque hashed approvals survive restart without hardware identifiers', () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-roles-')), path = join(dir, 'sessions.sqlite');
  let store = new SessionStore(path);
  try {
    const first = store.create('alice', 'gaming');
    store.close();
    assert.equal(readFileSync(path).includes(Buffer.from(first.token)), false);
    store = new SessionStore(path);
    assert.deepEqual(store.find(first.token)?.membership, first.membership);
    assert.equal(store.create('alice', 'streaming').membership?.broadcastSessionId, first.membership?.broadcastSessionId);
    store.removeOwner('alice');
    assert.notEqual(store.create('alice', 'gaming').membership?.broadcastSessionId, first.membership?.broadcastSessionId);
  } finally { store.close(); rmSync(dir, { recursive: true, force: true }); }
});

test('expiry releases the streaming slot and a role cap does not create orphan approvals', () => {
  let now = 1000;
  const store = new SessionStore(':memory:', () => now);
  try {
    const stream = store.create('alice', 'streaming');
    for (let i = 0; i < 3; i++) store.create('alice', 'gaming');
    assert.throws(() => store.create('alice', 'gaming'), SessionRoleError);
    assert.equal(store.connections('alice').length, 4);
    now += SESSION_MS;
    assert.equal(store.find(stream.token), undefined);
    assert.equal(store.create('alice', 'streaming').membership?.role, 'streaming');
    assert.equal(store.connections('alice').length, 1);
  } finally { store.close(); }
});

test('browser approval binds the exact creator and role; renewal cannot upgrade a role', () => {
  const store = new SessionStore();
  const alice = new DisplayAccess(() => ({ id: 'alice', expiresAt: Date.now() + 3600000 }), Date.now, store);
  const bob = new DisplayAccess(() => ({ id: 'bob', expiresAt: Date.now() + 3600000 }), Date.now, store);
  const login = new BrowserLogin();
  try {
    const verifier = nonce(), request = login.start(hashSecret(verifier), true);
    assert.throws(() => login.approve(request.id), status(401));
    login.approve(request.id, alice, 'gaming');
    assert.throws(() => login.poll(request.id, nonce()), status(401));
    const result = login.poll(request.id, verifier);
    assert.equal(result.status, 'approved');
    if (result.status !== 'approved') throw new Error('Expected approval');
    assert.equal(store.find(result.lease.sessionToken)?.owner, 'alice');
    assert.equal(result.lease.membership?.role, 'gaming');
    assert.throws(() => bob.resume(result.lease.sessionToken), status(403));
    assert.throws(() => bob.authenticate(result.lease.token), status(401));
    const renewed = alice.resume(result.lease.sessionToken);
    assert.deepEqual(renewed.membership, result.lease.membership);
    assert.throws(() => alice.authenticate(result.lease.token), status(401));
    assert.throws(() => login.poll(request.id, verifier), status(410));
  } finally { alice.close(); bob.close(); store.close(); }
});

test('revocation invalidates pending approvals and only the revoked creator leases', () => {
  const store = new SessionStore(), login = new BrowserLogin();
  const alice = new DisplayAccess(() => ({ id: 'alice', expiresAt: Date.now() + 3600000 }), Date.now, store);
  const bob = new DisplayAccess(() => ({ id: 'bob', expiresAt: Date.now() + 3600000 }), Date.now, store);
  try {
    const verifier = nonce(), pending = login.start(hashSecret(verifier), false);
    login.approve(pending.id, alice, 'streaming');
    const other = bob.exchange(bob.issue().ticket, false, 'gaming');
    alice.revokeSessions();
    assert.throws(() => login.poll(pending.id, verifier), status(401));
    assert.ok(bob.authenticate(other.token));
    const legacy = bob.exchange(bob.issue().ticket);
    assert.equal(legacy.membership, undefined);
    assert.equal(bob.resume(legacy.sessionToken).scope, 'chat:read');
  } finally { alice.close(); bob.close(); store.close(); }
});
