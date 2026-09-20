// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { DisplayAccess } from '../server/chat/display-access.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { DisplaySessionGateway } from '../server/chat/display-session-gateway.mts';

test('existing connect remembers without enrollment; refresh and signout are separate from stream access', async () => {
  const access = new DisplayAccess(() => ({ id: 'owner', expiresAt: performance.now() + 600000 }));
  let origin = '';
  const gateway = new DisplayGateway(access, () => origin, () => ({ state: 'idle', received: 0, messages: [] }));
  const sessions = new DisplaySessionGateway(access, () => origin, () => gateway.changed());
  const server = createServer((request, response) => {
    if (!sessions.handle(request, response) && !gateway.handle(request, response)) response.writeHead(404).end();
  });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const address = server.address(); assert(address && typeof address !== 'string');
  origin = `http://127.0.0.1:${address.port}`;
  const post = (path: string, scheme: string, token: string, extra: Record<string, string> = {}) =>
    fetch(origin + path, { method: 'POST', headers: { Authorization: `${scheme} ${token}`, ...extra } });
  try {
    const ticket = access.issue();
    const response = await post('/display/exchange', 'ChatView-Ticket', ticket.ticket);
    assert.equal(response.status, 200);
    const login = await response.json();
    assert.equal(login.scope, 'chat:read'); assert.equal(login.sessionScope, 'chat:renew');
    assert.equal((await post('/display/exchange', 'ChatView-Ticket', ticket.ticket)).status, 401);
    assert.equal((await post('/display/refresh', 'Bearer', login.token)).status, 401);
    assert.equal((await post('/display/refresh', 'ChatView-Session', login.token)).status, 401);
    const refreshed = await post('/display/refresh', 'ChatView-Session', login.sessionToken);
    assert.equal(refreshed.status, 200);
    const lease = await refreshed.json();
    assert.equal(access.active(login.id), false); assert.equal(access.active(lease.id), true);
    const signedOut = await post('/display/signout', 'ChatView-Session', login.sessionToken);
    assert.deepEqual(await signedOut.json(), { signedOut: true });
    assert.equal(access.active(lease.id), false);
    assert.equal((await post('/display/refresh', 'ChatView-Session', login.sessionToken)).status, 401);
    assert.equal((await post('/display/signout', 'ChatView-Session', login.sessionToken)).status, 200);
    assert.equal((await post('/display/enroll', 'ChatView-Ticket', ticket.ticket)).status, 404);
    const invalid = access.issue();
    assert.equal((await post('/display/exchange', 'ChatView-Ticket', invalid.ticket, { 'X-ChatView-Remember': '1' })).status, 400);
    const foreign = await post('/display/exchange', 'ChatView-Ticket', invalid.ticket, { Origin: 'https://elsewhere.invalid' });
    assert.equal(foreign.status, 403);
  } finally {
    sessions.close(); gateway.close(); server.closeAllConnections();
    await new Promise<void>(resolve => server.close(() => resolve()));
  }
});
