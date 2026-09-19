// SPDX-License-Identifier: GPL-2.0-or-later
// Test runner only: actual gateway/ws -> actual WinHTTP -> actual HUD/WebView2.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { randomBytes } from 'node:crypto';
import { WebSocketServer } from 'ws';
import { DisplayAccess } from '../server/chat/display-access.mts';
import { DisplayGateway, displayFrame } from '../server/chat/display-gateway.mts';

const executable = process.argv[2];
assert(executable, 'native test executable required');
const snapshot = { state: 'subscribed', received: 1, messages: [
  { nickname: '검증 사용자', content: '안녕 😀 <img onerror=evil()>', messageTime: 1700000000000 },
] };
for (const mode of ['gateway', 'cancel', 'local-expiry', 'redirect', 'wrong-scope', 'invalid-json', 'binary', 'oversize', 'idle']) {
  let origin = '';
  const access = new DisplayAccess(() => ({ id: 'synthetic-owner', expiresAt: performance.now() + 60000 }));
  const issued = access.issue();
  const token = randomBytes(32).toString('hex');
  const gateway = new DisplayGateway(access, () => origin, () => snapshot);
  const peers = new WebSocketServer({ noServer: true, perMessageDeflate: false });
  const timers: ReturnType<typeof setInterval>[] = [];
  let requests = 0, upgrades = 0, redirected = 0, rendered = false;
  let child: ReturnType<typeof spawn> | undefined;
  const server = createServer((request, response) => {
    if (request.url === '/leak') { redirected++; response.end(); return; }
    if (request.url !== '/display/exchange') { response.writeHead(404).end(); return; }
    requests++;
    assert.equal(request.headers.authorization, `ChatView-Ticket ${issued.ticket}`);
    assert.equal(request.headers.cookie, undefined);
    if (mode === 'gateway') { assert(gateway.handle(request, response)); return; }
    if (mode === 'cancel') { child?.stdin?.write('requested\n'); return; }
    if (mode === 'redirect') { response.writeHead(307, { Location: `${origin}/leak` }).end(); return; }
    response.setHeader('Content-Type', 'application/json');
    response.end(JSON.stringify({ id: issued.id, token,
      expiresInMs: mode === 'local-expiry' ? 1000 : 30000,
      scope: mode === 'wrong-scope' ? 'account:admin' : 'chat:read' }));
  });
  server.on('upgrade', (request, socket, head) => {
    upgrades++;
    if (mode === 'gateway') { gateway.upgrade(request, socket, head); return; }
    assert.equal(request.url, '/display/events');
    assert.equal(request.headers.authorization, `Bearer ${token}`);
    assert.equal(request.headers.cookie, undefined);
    peers.handleUpgrade(request, socket, head, peer => {
      peer.on('error', () => peer.terminate());
      if (mode === 'binary') peer.send(Buffer.from('{}'));
      else if (mode === 'oversize') peer.send('x'.repeat(2 * 1024 * 1024 + 1));
      else if (mode === 'invalid-json') peer.send('{invalid');
      else if (mode === 'local-expiry') {
        peer.send(displayFrame(snapshot));
        const timer = setInterval(() => { if (peer.readyState === 1) peer.send(displayFrame(snapshot)); }, 100);
        timers.push(timer); peer.once('close', () => clearInterval(timer));
      }
      // idle deliberately leaves the peer open without application data.
    });
  });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const address = server.address(); assert(address && typeof address !== 'string');
  origin = `http://127.0.0.1:${address.port}`;
  try {
    child = spawn(executable, [mode], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
    const exited = once(child, 'exit');
    child.stdin!.write(`${origin}\n${issued.ticket}\n`);
    if (mode !== 'cancel') child.stdin!.end();
    let output = '';
    child.stdout!.setEncoding('utf8');
    child.stdout!.on('data', (chunk: string) => {
      output = (output + chunk).slice(-4096);
      if (output.includes('rendered\n') && !rendered) {
        rendered = true; access.revoke(issued.id); gateway.changed();
      }
    });
    // Native fixture prints fixed assertion labels only, never input credentials.
    let errors = '';
    child.stderr!.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
    const timeout = setTimeout(() => child?.kill(), 60000);
    try {
      const [code] = await exited;
      assert.equal(code, 0, `${mode}: ${errors}`);
    } finally { clearTimeout(timeout); }
    assert.equal(requests, 1, `${mode}: no replay of the one-use exchange`);
    assert.equal(redirected, 0, 'credentials never follow a redirect');
    if (mode === 'gateway') assert(rendered, 'native DOM acknowledgement before revocation');
    if (mode === 'wrong-scope' || mode === 'redirect' || mode === 'cancel') assert.equal(upgrades, 0);
    console.log(`${mode}: passed`);
  } finally {
    child?.kill(); for (const timer of timers) clearInterval(timer);
    gateway.close(); for (const peer of peers.clients) peer.terminate(); peers.close();
    server.closeAllConnections(); server.close();
  }
}
