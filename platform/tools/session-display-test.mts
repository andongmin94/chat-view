// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import type { Duplex } from 'node:stream';
import { DisplayAccess, DisplayAccessError } from '../server/chat/display-access.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';

const executable = process.argv[2]; assert(executable, 'native executable required');
const access = new DisplayAccess(() => ({ id: 'fixture-owner', expiresAt: performance.now() + 120000 }));
const ticket = access.issue();
let origin = '', deliveries = 0, exchanges = 0, unavailable = 0, dropped = false, revoked = false;
const connections = new Set<Duplex>();
const gateway = new DisplayGateway(access, () => origin, () => ({ state: 'subscribed', received: deliveries,
  messages: [{ nickname: '검증 사용자', content: '연결 유지 😀', messageTime: 1700000000000 }] }));
const server = createServer((request, response) => {
  if (!['/display/exchange', '/display/refresh'].includes(request.url ?? '')) {
    if (!gateway.handle(request, response)) response.writeHead(404).end(); return;
  }
  assert.equal(request.method, 'POST'); assert.equal(request.headers.cookie, undefined);
  if (unavailable) { unavailable--; response.writeHead(503).end(); return; }
  try {
    const header = request.headers.authorization ?? '';
    let lease;
    if (request.url === '/display/exchange') {
      exchanges++; assert.equal(header, `ChatView-Ticket ${ticket.ticket}`);
      assert.equal(request.headers['x-chatview-remember'], undefined);
      lease = access.exchange(ticket.ticket);
    } else {
      assert(header.startsWith('ChatView-Session '));
      lease = access.resume(header.slice('ChatView-Session '.length));
    }
    deliveries++; gateway.changed();
    response.setHeader('Content-Type', 'application/json');
    response.end(JSON.stringify({ ...lease, expiresInMs: 2500 }));
  } catch (error) { response.writeHead(error instanceof DisplayAccessError ? error.status : 500).end(); }
});
server.on('upgrade', (request, socket, head) => {
  connections.add(socket); socket.once('close', () => connections.delete(socket));
  gateway.upgrade(request, socket, head);
});
server.listen(0, '127.0.0.1'); await once(server, 'listening');
const address = server.address(); assert(address && typeof address !== 'string');
origin = `http://127.0.0.1:${address.port}`;
const child = spawn(executable, [], { stdio: ['pipe', 'pipe', 'pipe'] });
const exited = once(child, 'exit');
child.stdin.write(`${origin}\n${ticket.ticket}\n`);
let output = '', errors = '';
child.stdout.setEncoding('utf8');
child.stdout.on('data', (chunk: string) => {
  output = (output + chunk).slice(-4096);
  if (!dropped && /(?:^|\n)restored\r?\n/u.test(output)) {
    dropped = true; unavailable = 1;
    for (const socket of connections) socket.destroy();
    child.stdin.write('dropped\n');
  }
  if (!revoked && /(?:^|\n)revoke\r?\n/u.test(output)) {
    revoked = true; access.revokeSessions(); gateway.changed();
  }
});
child.stderr.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
const deadline = setTimeout(() => child.kill(), 60000);
try {
  const [code] = await exited;
  assert.equal(code, 0, errors);
  assert.equal(exchanges, 1, 'one-use approval is not replayed on renewal/reconnect');
  assert(deliveries >= 4 && dropped && revoked);
  console.log('Native remembered chat flow passed');
} finally {
  clearTimeout(deadline); child.kill(); gateway.close();
  for (const socket of connections) socket.destroy();
  server.closeAllConnections(); server.close();
}
