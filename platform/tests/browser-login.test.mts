// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { randomBytes } from 'node:crypto';
import { BrowserLogin, LOGIN_POLL_MS, LOGIN_WINDOW_MS } from '../server/chat/browser-login.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { hashSecret } from '../server/chat/session-store.mts';
const denied = (code: number) => (e: unknown) => e instanceof DisplayAccessError && e.status === code;
function setup() {
  let now = 0, owner: { id: string; expiresAt: number } | undefined;
  const access = new DisplayAccess(() => owner, () => now);
  const login = new BrowserLogin(access, () => now);
  const verifier = randomBytes(32).toString('hex');
  return { access, login, verifier, advance: (ms = LOGIN_POLL_MS) => { now += ms; },
    identify: (id = 'creator') => { owner = { id, expiresAt: now + 600000 }; },
    expire: () => { owner = undefined; } };
}
test('browser approval returns a one-use, remembered chat lease only to the initiating app', () => {
  const s = setup();
  try {
    const request = s.login.start(hashSecret(s.verifier), true);
    assert.match(request.verificationPath, /^\/login\/[a-f0-9]{32}$/u);
    assert(!JSON.stringify(request).includes(s.verifier));
    assert.equal(s.login.view(request.id).remember, true);
    assert.deepEqual(s.login.poll(request.id, s.verifier), { status: 'pending' });
    assert.throws(() => s.login.approve(request.id), denied(401));
    s.identify(); s.login.approve(request.id); s.advance();
    const result = s.login.poll(request.id, s.verifier);
    assert.equal(result.status, 'approved');
    if (result.status !== 'approved') throw Error('approval missing');
    assert.equal(result.lease.sessionScope, 'chat:renew');
    assert(s.access.authenticate(result.lease.token));
    assert(s.access.resume(result.lease.sessionToken));
    assert.throws(() => s.login.poll(request.id, s.verifier), denied(410));
  } finally { s.access.close(); }
});
test('unrelated app, public URL id and malformed verifier cannot claim or consume approval', () => {
  const s = setup();
  try {
    const request = s.login.start(hashSecret(s.verifier), false);
    s.identify(); s.login.approve(request.id);
    for (const invalid of [request.id, '0'.repeat(64), '', undefined])
      assert.throws(() => s.login.poll(request.id, invalid), denied(401));
    const result = s.login.poll(request.id, s.verifier);
    assert.equal(result.status, 'approved');
    if (result.status === 'approved') assert.equal(result.lease.sessionToken, undefined);
  } finally { s.access.close(); }
});
test('denial, expiry and changed channel never deliver private chat', () => {
  const s = setup();
  try {
    let r = s.login.start(hashSecret(s.verifier), false); s.login.deny(r.id);
    assert.throws(() => s.login.poll(r.id, s.verifier), denied(403));
    assert.throws(() => s.login.poll(r.id, s.verifier), denied(410));
    r = s.login.start(hashSecret(s.verifier), false); s.advance(LOGIN_WINDOW_MS);
    assert.throws(() => s.login.view(r.id), denied(410));
    r = s.login.start(hashSecret(s.verifier), false); s.identify('a'); s.login.approve(r.id); s.identify('b');
    assert.throws(() => s.login.poll(r.id, s.verifier), denied(401));
    r = s.login.start(hashSecret(s.verifier), false); s.login.approve(r.id); s.expire();
    assert.throws(() => s.login.poll(r.id, s.verifier), denied(401));
  } finally { s.access.close(); }
});
test('approval is explicit and single-use; poll cadence and pending storage are bounded', () => {
  const s = setup();
  try {
    const r = s.login.start(hashSecret(s.verifier), false);
    s.login.poll(r.id, s.verifier);
    assert.throws(() => s.login.poll(r.id, s.verifier), denied(429));
    s.advance(); s.identify(); s.login.approve(r.id);
    assert.throws(() => s.login.approve(r.id), denied(409));
    assert.throws(() => s.login.deny(r.id), denied(409));
    s.login.poll(r.id, s.verifier);
    for (let i = 0; i < 16; i++) s.login.start(hashSecret(s.verifier), false);
    assert.throws(() => s.login.start(hashSecret(s.verifier), false), denied(429));
    s.advance(LOGIN_WINDOW_MS); assert(s.login.start(hashSecret(s.verifier), false));
    s.login.clear(); assert.throws(() => s.login.view(r.id), denied(410));
  } finally { s.access.close(); }
});

test('revoking display access invalidates browser approvals not yet collected by the app', () => {
  const s = setup();
  try {
    const request = s.login.start(hashSecret(s.verifier), true);
    s.identify(); s.login.approve(request.id);
    s.access.revokeSessions();
    assert.throws(() => s.login.poll(request.id, s.verifier), denied(401));
    assert.throws(() => s.login.poll(request.id, s.verifier), denied(410));
    const fresh = s.login.start(hashSecret(s.verifier), true);
    s.login.approve(fresh.id);
    assert.equal(s.login.poll(fresh.id, s.verifier).status, 'approved');
  } finally { s.access.close(); }
});
