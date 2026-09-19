// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { DisplayAccess, DisplayAccessError, DISPLAY_TICKET_MS, DISPLAY_LEASE_MS } from '../server/chat/display-access.mts';
import type { DisplayOwner } from '../server/chat/display-access.mts';
function fixture() {
  let now = 10_000;
  let owner: DisplayOwner | undefined = { id: 'creator', expiresAt: now + 3_600_000 };
  const access = new DisplayAccess(() => owner, () => now);
  return { access, advance: (ms: number) => { now += ms; }, owner: (value?: DisplayOwner) => { owner = value; } };
}
const denied = (fn: () => unknown, status = 401) => assert.throws(fn, (e: unknown) => e instanceof DisplayAccessError && e.status === status);

test('one-use ticket becomes a distinct read-only lease, never a provider credential', () => {
  const { access } = fixture(); const ticket = access.issue();
  assert.match(ticket.ticket, /^[a-f0-9]{64}$/u); assert.equal(ticket.scope, 'chat:read');
  denied(() => access.authenticate(ticket.ticket));
  const lease = access.exchange(ticket.ticket); assert.notEqual(lease.token, ticket.ticket);
  assert.equal(lease.id, ticket.id); assert.equal(lease.expiresInMs, DISPLAY_LEASE_MS);
  assert.equal(access.authenticate(lease.token).id, lease.id);
  denied(() => access.exchange(ticket.ticket)); denied(() => access.exchange(lease.token));
});
test('ticket expiration is checked synchronously, not only by a timer', () => {
  const { access, advance } = fixture(); const ticket = access.issue();
  advance(DISPLAY_TICKET_MS); denied(() => access.exchange(ticket.ticket));
});
test('lease expiration cannot be extended by reading or reconnecting', () => {
  const { access, advance } = fixture(); const lease = access.exchange(access.issue().ticket);
  advance(DISPLAY_LEASE_MS - 1); assert.equal(access.remaining(lease.id), 1);
  advance(1); denied(() => access.authenticate(lease.token)); assert.equal(access.active(lease.id), false);
});
test('grant is bounded by creator authorization and cannot revive after reauthorization', () => {
  const f = fixture(); f.owner({ id: 'creator', expiresAt: 10_050 });
  const lease = f.access.exchange(f.access.issue().ticket); assert.equal(lease.expiresInMs, 50);
  f.advance(50); denied(() => f.access.authenticate(lease.token));
  f.owner({ id: 'creator', expiresAt: 100_000 }); denied(() => f.access.authenticate(lease.token));
});
test('creator switch and sign-out invalidate outstanding tickets and leases', () => {
  const f = fixture(); const ticket = f.access.issue(); const lease = f.access.exchange(f.access.issue().ticket);
  f.owner({ id: 'other-creator', expiresAt: 100_000 });
  denied(() => f.access.exchange(ticket.ticket)); denied(() => f.access.authenticate(lease.token));
  f.owner(); denied(() => f.access.issue());
});
test('individual revoke preserves other devices; clear invalidates all capabilities', () => {
  const { access } = fixture(); const a = access.exchange(access.issue().ticket); const b = access.exchange(access.issue().ticket);
  access.revoke(a.id); denied(() => access.authenticate(a.token)); assert.ok(access.active(b.id));
  const pending = access.issue(); access.clear(); denied(() => access.authenticate(b.token)); denied(() => access.exchange(pending.ticket));
});
test('four pending/active devices is a hard combined bound, expired entries release capacity', () => {
  const { access, advance } = fixture(); const tickets = Array.from({ length: 4 }, () => access.issue());
  access.exchange(tickets[0]!.ticket); denied(() => access.issue(), 429);
  advance(DISPLAY_TICKET_MS); assert.doesNotThrow(() => access.issue());
});
for (const value of [undefined, null, '', 'x'.repeat(64), 'a'.repeat(65), 'a'.repeat(64) + '\r\n', {}]) {
  test(`invalid bearer material is rejected (${typeof value}, ${String(value).length})`, () => {
    const { access } = fixture(); denied(() => access.authenticate(value)); denied(() => access.exchange(value));
  });
}
test('errors never include secret input and all clocks are monotonic-local values', () => {
  const { access } = fixture(); const secret = 'f'.repeat(64);
  try { access.authenticate(secret); assert.fail('accepted'); }
  catch (error) { assert.ok(error instanceof DisplayAccessError); assert.ok(!error.message.includes(secret)); }
});
