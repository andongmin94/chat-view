// SPDX-License-Identifier: GPL-2.0-or-later
// Real HTTP and SQLite. The creator identity is synthetic; no live provider.
import test from 'node:test';
import type { TestContext } from 'node:test';
import assert from 'node:assert/strict';
import { createServer, request as httpRequest } from 'node:http';
import type { OutgoingHttpHeaders } from 'node:http';
import { once } from 'node:events';
import { randomBytes } from 'node:crypto';
import { DisplaySessionGateway } from '../server/chat/display-session-gateway.mts';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { hashSecret } from '../server/chat/session-store.mts';

async function fixture(t: TestContext) {
  let origin = '', expectedOrigin = '', changed = 0, dropCancellationReply = false;
  const access = new DisplayAccess(() => ({ id: 'http-cancel-owner', expiresAt: performance.now() + 600_000 }));
  const gateway = new DisplaySessionGateway(access, () => expectedOrigin, () => { changed++; });
  const server = createServer((request, response) => {
    if (dropCancellationReply && request.url?.endsWith('/cancel')) {
      dropCancellationReply = false;
      // Execute the real handler but lose only the acknowledgement on the wire.
      response.end = (() => { response.destroy(); return response; }) as typeof response.end;
    }
    if (!gateway.handle(request, response)) response.writeHead(404).end();
  });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const address = server.address(); assert(address && typeof address !== 'string');
  origin = `http://127.0.0.1:${address.port}`; expectedOrigin = origin;
  t.after(async () => {
    gateway.close();
    server.closeAllConnections();
    await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  });
  const call = (path: string, headers: OutgoingHttpHeaders = {}, method = 'POST', body = '') =>
    new Promise<{ status: number; data: unknown; headers: import('node:http').IncomingHttpHeaders }>((resolve, reject) => {
      const request = httpRequest(`${origin}${path}`, {
        method, headers: { 'Content-Length': Buffer.byteLength(body), ...headers },
      }, response => {
        let text = '';
        response.setEncoding('utf8');
        response.on('data', (chunk: string) => { text += chunk; if (text.length > 4096) response.destroy(new Error('oversized fixture response')); });
        response.on('error', reject);
        response.on('end', () => {
          try { resolve({ status: response.statusCode!, data: text ? JSON.parse(text) as unknown : undefined, headers: response.headers }); }
          catch (error) { reject(error); }
        });
      });
      request.on('error', reject); request.end(body);
    });
  const start = async () => {
    const proof = randomBytes(32).toString('hex');
    const result = await call('/display/login', {
      Authorization: `ChatView-Challenge ${hashSecret(proof)}`, 'X-ChatView-Role': 'gaming',
    });
    assert.equal(result.status, 200);
    const id = (result.data as { id: string }).id; assert.match(id, /^[a-f0-9]{32}$/u);
    return { id, proof, headers: { Authorization: `ChatView-Login ${proof}` }, path: `/display/login/${id}/cancel` };
  };
  return { call, start, gateway, access, origin, changes: () => changed,
    dropReply: () => { dropCancellationReply = true; },
    requireHttps: () => { expectedOrigin = origin.replace('http:', 'https:'); } };
}
const gone = (error: unknown) => error instanceof DisplayAccessError && error.status === 410;

test('HTTP cancellation retires only the initiating request, never signs out existing chat', { timeout: 10_000 }, async t => {
  const f = await fixture(t), pending = await f.start();
  const existing = f.access.exchange(f.access.issue().ticket, false, 'streaming');
  const before = f.changes();
  const cancelled = await f.call(pending.path, pending.headers);
  assert.equal(cancelled.status, 200); assert.deepEqual(cancelled.data, { cancelled: true });
  assert.equal(cancelled.headers['cache-control'], 'no-store');
  assert.equal(cancelled.headers['referrer-policy'], 'no-referrer');
  assert.equal(f.changes(), before, 'cancelling no lease must not broadcast a fake state change');
  assert.throws(() => f.gateway.login.view(pending.id), gone);
  assert.equal((await f.call(`/display/login/${pending.id}`, pending.headers)).status, 410);
  assert.equal(f.access.authenticate(existing.token).id, existing.id);
  assert.equal(f.access.authenticateOutput(existing.outputToken).id, existing.id);
  assert.deepEqual(f.access.resume(existing.sessionToken, 'streaming').membership, existing.membership);
  const fresh = await f.start(); f.gateway.login.approve(fresh.id);
  assert.equal((await f.call(`/display/login/${fresh.id}`, fresh.headers)).status, 200);
});

test('cancellation response loss stays distinguishable from unknown or collected requests', { timeout: 10_000 }, async t => {
  const f = await fixture(t), pending = await f.start();
  f.dropReply(); await assert.rejects(f.call(pending.path, pending.headers));
  assert.throws(() => f.gateway.login.view(pending.id), gone);
  const retry = await f.call(pending.path, pending.headers);
  assert.equal(retry.status, 200); assert.deepEqual(retry.data, { cancelled: true });
  const missing = await f.call(`/display/login/${'0'.repeat(32)}/cancel`, pending.headers);
  assert.equal(missing.status, 410); assert.deepEqual(missing.data, { error: 'ChatView display request denied' });
  const fresh = await f.start(); f.gateway.login.approve(fresh.id);
  const collected = await f.call(`/display/login/${fresh.id}`, fresh.headers);
  assert.equal(collected.status, 200);
  const lease = (collected.data as { lease: { id: string; token: string } }).lease;
  assert.equal((await f.call(fresh.path, fresh.headers)).status, 410);
  assert.equal(f.access.authenticate(lease.token).id, lease.id);
});

test('HTTP cancel and poll race leaves either no approval or the original collected approval', { timeout: 10_000 }, async t => {
  const f = await fixture(t);
  for (const cancelFirst of [true, false]) {
    const pending = await f.start(); f.gateway.login.approve(pending.id);
    const cancel = () => f.call(pending.path, pending.headers);
    const poll = () => f.call(`/display/login/${pending.id}`, pending.headers);
    const results = await Promise.all(cancelFirst ? [cancel(), poll()] : [poll(), cancel()]);
    const cancelled = results[cancelFirst ? 0 : 1]!, collected = results[cancelFirst ? 1 : 0]!;
    assert.deepEqual([cancelled.status, collected.status].sort(), [200, 410]);
    if (cancelled.status === 200) {
      assert.deepEqual(cancelled.data, { cancelled: true });
      assert.throws(() => f.gateway.login.approve(pending.id), gone);
    } else {
      const lease = (collected.data as { lease: { id: string; token: string; sessionToken: string } }).lease;
      assert.equal(f.access.authenticate(lease.token).id, lease.id);
      f.access.signout(lease.sessionToken);
    }
  }
});

test('cancel route retains method, origin, proof, duplicate-header and empty-body guards', { timeout: 10_000 }, async t => {
  const f = await fixture(t), pending = await f.start();
  const badCalls: [OutgoingHttpHeaders, string, string, number][] = [
    [{}, 'POST', '', 401],
    [{ Authorization: `ChatView-Login ${hashSecret(pending.proof)}` }, 'POST', '', 401],
    [{ Authorization: `ChatView-Session ${pending.proof}` }, 'POST', '', 401],
    [{ Authorization: [pending.headers.Authorization, pending.headers.Authorization] }, 'POST', '', 401],
    [{ ...pending.headers, Host: 'elsewhere.invalid' }, 'POST', '', 403],
    [{ ...pending.headers, Origin: 'https://elsewhere.invalid' }, 'POST', '', 403],
    [{ ...pending.headers, 'Sec-Fetch-Site': 'cross-site' }, 'POST', '', 403],
    [{ ...pending.headers, 'X-ChatView-Remember': '1' }, 'POST', '', 400],
    [pending.headers, 'GET', '', 405],
    [pending.headers, 'DELETE', '', 405],
    [pending.headers, 'POST', '{}', 400],
  ];
  for (const [headers, method, body, status] of badCalls) {
    const result = await f.call(pending.path, headers, method, body);
    assert.equal(result.status, status); assert.deepEqual(result.data, { error: 'ChatView display request denied' });
    assert.equal(f.gateway.login.view(pending.id).role, 'gaming');
  }
  assert.equal((await f.call(`${pending.path}?proof=not-a-capability`, pending.headers)).status, 404);
  assert.equal((await f.call('/display/login/not-an-id/cancel', pending.headers)).status, 404);
  assert.equal((await f.call(pending.path, pending.headers)).status, 200);
});

test('plain HTTP and forwarded headers cannot cancel a production HTTPS request', { timeout: 10_000 }, async t => {
  const f = await fixture(t), pending = await f.start(); f.requireHttps();
  const result = await f.call(pending.path, { ...pending.headers, 'X-Forwarded-Proto': 'https' });
  assert.equal(result.status, 403); assert.equal(f.gateway.login.view(pending.id).role, 'gaming');
});

test('closed gateway refuses cancellation instead of inventing an acknowledgement', { timeout: 10_000 }, async t => {
  const f = await fixture(t), pending = await f.start(); f.gateway.close();
  assert.equal((await f.call(pending.path, pending.headers)).status, 503);
});
