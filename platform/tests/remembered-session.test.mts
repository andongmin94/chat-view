// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { SessionStore, SESSION_MS } from '../server/chat/session-store.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
const denied = (status: number) => (error: unknown) => error instanceof DisplayAccessError && error.status === status;

test('connect once, keep login, renew, restart service, logout; no plaintext token on disk', () => {
  const directory = mkdtempSync(join(tmpdir(), 'chatview-session-'));
  const path = join(directory, 'sessions.sqlite');
  let wall = 1700000000000, clock = 1000;
  const owner = () => ({ id: 'creator-a', expiresAt: clock + 600000 });
  let store = new SessionStore(path, () => wall);
  try {
    let access = new DisplayAccess(owner, () => clock, store);
    const ticket = access.issue();
    const login = access.exchange(ticket.ticket, true);
    assert.equal(login.sessionScope, 'chat:renew');
    assert.equal(login.sessionExpiresInMs, SESSION_MS);
    assert.throws(() => access.exchange(ticket.ticket, true), denied(401));
    assert.throws(() => access.authenticate(login.sessionToken), denied(401));
    assert.throws(() => access.resume(login.token), denied(401));
    const next = access.resume(login.sessionToken);
    assert.equal(access.active(login.id), false);
    assert.equal(access.active(next.id), true);
    access.close(); store.close();
    assert(!readFileSync(path).includes(Buffer.from(login.sessionToken!)));
    store = new SessionStore(path, () => wall);
    access = new DisplayAccess(owner, () => clock, store);
    const restored = access.resume(login.sessionToken);
    assert.equal(access.active(restored.id), true);
    access.signout(login.sessionToken);
    assert.equal(access.active(restored.id), false);
    access.signout(login.sessionToken);
    assert.throws(() => access.resume(login.sessionToken), denied(401));
    store.close(); store = new SessionStore(path, () => wall);
    assert.equal(store.find(login.sessionToken), undefined);
    wall++; clock++;
  } finally { store.close(); rmSync(directory, { recursive: true, force: true }); }
});

test('temporary connect stores no remembered login; stopping a lease does not logout', () => {
  const access = new DisplayAccess(() => ({ id: 'a', expiresAt: 999999 }), () => 0);
  try {
    const temporary = access.exchange(access.issue().ticket);
    assert.equal(temporary.sessionToken, undefined);
    assert.equal(temporary.sessionScope, undefined);
    assert.equal(temporary.expiresInMs, 300000);
    const login = access.exchange(access.issue().ticket, true);
    access.revoke(login.id);
    assert.equal(access.active(login.id), false);
    assert.equal(access.active(access.resume(login.sessionToken).id), true);
  } finally { access.close(); }
});

test('missing upstream waits; a different channel never inherits remembered access', () => {
  let owner: { id: string; expiresAt: number } | undefined = { id: 'a', expiresAt: 999999 };
  const access = new DisplayAccess(() => owner, () => 0);
  try {
    const login = access.exchange(access.issue().ticket, true);
    owner = undefined;
    assert.equal(access.active(login.id), false);
    assert.throws(() => access.resume(login.sessionToken), denied(503));
    owner = { id: 'b', expiresAt: 999999 };
    assert.throws(() => access.resume(login.sessionToken), denied(403));
    owner = { id: 'a', expiresAt: 999999 };
    assert.equal(access.active(access.resume(login.sessionToken).id), true);
    owner = undefined;
    access.signout(login.sessionToken);
    assert.throws(() => access.resume(login.sessionToken), denied(401));
  } finally { access.close(); }
});

test('logout affects one login; owner revocation includes all saved logins', () => {
  const access = new DisplayAccess(() => ({ id: 'a', expiresAt: 999999 }), () => 0);
  try {
    const a = access.exchange(access.issue().ticket, true);
    const b = access.exchange(access.issue().ticket, true);
    const guest = access.exchange(access.issue().ticket);
    access.signout(a.sessionToken);
    assert.equal(access.active(a.id), false);
    assert.equal(access.active(b.id), true);
    assert.equal(access.active(guest.id), true);
    access.revokeSessions();
    assert.equal(access.active(b.id), false);
    assert.equal(access.active(guest.id), false);
    assert.throws(() => access.resume(b.sessionToken), denied(401));
  } finally { access.close(); }
});

test('session and upstream expiry bound each lease; reused/invalid credentials cannot extend it', () => {
  let wall = 1000, clock = 0;
  const store = new SessionStore(':memory:', () => wall);
  const access = new DisplayAccess(() => ({ id: 'a', expiresAt: 999999 }), () => clock, store);
  try {
    const login = access.exchange(access.issue().ticket, true);
    wall += SESSION_MS - 500;
    const last = access.resume(login.sessionToken);
    assert.equal(last.expiresInMs, 500);
    wall += 500;
    assert.equal(access.active(last.id), false);
    assert.throws(() => access.resume(login.sessionToken), denied(401));
    assert.throws(() => access.resume('0'.repeat(64)), denied(401));
    clock = 999998;
    assert.equal(access.exchange(access.issue().ticket).expiresInMs, 1);
    clock++;
    assert.throws(() => access.issue(), denied(401));
  } finally { access.close(); store.close(); }
});
