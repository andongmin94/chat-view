// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { BroadcastOutput, parseOutputReport, readOutputReport, OUTPUT_LIFETIME_MS } from '../server/chat/broadcast-output.mts';
const status = (code: number) => (e: unknown) => e instanceof DisplayAccessError && e.status === code;
const report = (sequence = 1) => ({ sequence, streaming: true, recording: false, sampleAgeMs: 0 });

test('only a streaming lease receives a distinct, short output-report capability', () => {
  let now = 1000;
  const access = new DisplayAccess(() => ({ id: 'alice', expiresAt: 900000 }), () => now);
  try {
    const gaming = access.exchange(access.issue().ticket, false, 'gaming');
    const streaming = access.exchange(access.issue().ticket, false, 'streaming');
    assert.equal(gaming.outputToken, undefined);
    assert.equal(streaming.outputScope, 'broadcast:report');
    assert.ok(streaming.outputToken);
    assert.notEqual(streaming.outputToken, streaming.token);
    assert.notEqual(streaming.outputToken, streaming.sessionToken);
    assert.equal(access.authenticateOutput(streaming.outputToken).id, streaming.id);
    for (const token of [gaming.token, gaming.sessionToken, streaming.token, streaming.sessionToken])
      assert.throws(() => access.authenticateOutput(token), status(401));
    assert.throws(() => access.authenticate(streaming.outputToken), status(401));
    assert.throws(() => access.resume(streaming.outputToken), status(401));
    const renewed = access.resume(streaming.sessionToken, 'streaming');
    assert.throws(() => access.authenticateOutput(streaming.outputToken), status(401));
    assert.ok(access.authenticateOutput(renewed.outputToken));
    now += renewed.expiresInMs;
    assert.throws(() => access.authenticateOutput(renewed.outputToken), status(401));
  } finally { access.close(); }
});

test('logout, full revoke and another owner cannot reuse report authority', () => {
  const alice = new DisplayAccess(() => ({ id: 'alice', expiresAt: performance.now() + 60000 }));
  const bob = new DisplayAccess(() => ({ id: 'bob', expiresAt: performance.now() + 60000 }));
  try {
    const a = alice.exchange(alice.issue().ticket, false, 'streaming');
    const b = bob.exchange(bob.issue().ticket, false, 'streaming');
    assert.throws(() => bob.authenticateOutput(a.outputToken), status(401));
    alice.signout(a.sessionToken);
    assert.throws(() => alice.authenticateOutput(a.outputToken), status(401));
    assert.ok(bob.authenticateOutput(b.outputToken));
    bob.revokeSessions();
    assert.throws(() => bob.authenticateOutput(b.outputToken), status(401));
  } finally { alice.close(); bob.close(); }
});

test('reports enforce ordering/rate and expire to unknown, never inferred stopped', () => {
  let now = 1000;
  const output = new BroadcastOutput(() => now);
  assert.deepEqual(output.snapshot(() => true), { state: 'unknown' });
  assert.equal(output.record('lease', report()), 1);
  assert.deepEqual(output.snapshot(() => true), { state: 'reported', streaming: true,
    recording: false, expiresInMs: OUTPUT_LIFETIME_MS });
  assert.throws(() => output.record('lease', report()), status(409));
  assert.throws(() => output.record('lease', report(2)), status(429));
  now += 500;
  output.record('lease', { ...report(2), streaming: false, recording: true, sampleAgeMs: 2000 });
  assert.equal(output.snapshot(() => true).state, 'reported');
  now += OUTPUT_LIFETIME_MS - 2000;
  assert.deepEqual(output.snapshot(() => true), { state: 'unknown' });
  assert.throws(() => output.record('lease', report(1)), status(409), 'expiry does not reset sequence');
  output.record('new-lease', report());
  output.clear('lease');
  assert.equal(output.snapshot(() => true).state, 'reported', 'old disconnect does not erase new lease');
  assert.deepEqual(output.snapshot(() => false), { state: 'unknown' });
  output.clear('new-lease');
  assert.deepEqual(output.snapshot(() => true), { state: 'unknown' });
  assert.throws(() => output.record('new-lease', report()), status(409), 'same-lease reconnect cannot replay old report');
});

test('output input excludes owner, capture, scene, audience and noncanonical values', () => {
  for (const value of [null, [], 'true', {}, { ...report(), owner: 'bob' },
    { ...report(), captureState: 'verified' }, { ...report(), viewers: 50 },
    ...[-1, 0, 0.5, 0x100000000, NaN].map(sequence => ({ ...report(), sequence })),
    ...[-1, 0.5, 3000, Infinity].map(sampleAgeMs => ({ ...report(), sampleAgeMs })),
    { ...report(), streaming: 1 }, { ...report(), recording: 'false' }])
    assert.throws(() => parseOutputReport(value), status(400));
  assert.deepEqual(parseOutputReport(report()), report());
});

test('output HTTP body has a tiny exact schema, bounded length and JSON content type', async () => {
  const server = createServer((req, res) => {
    void readOutputReport(req).then(value => res.end(JSON.stringify(value))).catch(error => {
      res.writeHead(error instanceof DisplayAccessError ? error.status : 500).end();
    });
  });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const address = server.address(); assert(address && typeof address !== 'string');
  const origin = `http://127.0.0.1:${address.port}`;
  const send = (body: string, contentType = 'application/json') => fetch(origin, {
    method: 'POST', headers: { 'Content-Type': contentType }, body,
  });
  try {
    const good = await send(JSON.stringify(report())); assert.equal(good.status, 200);
    const parsed = await good.json(); assert.equal(parsed.streaming, true); assert(parsed.sampleAgeMs < 3000);
    for (const body of ['', '{}', '{bad', JSON.stringify({ ...report(), owner: 'bob' }), 'x'.repeat(1025)])
      assert.equal((await send(body)).status, 400);
    assert.equal((await send(JSON.stringify(report()), 'text/plain')).status, 400);
  } finally { server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); }
});
