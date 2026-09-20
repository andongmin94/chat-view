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
import { DisplaySessionGateway } from '../server/chat/display-session-gateway.mts';

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
  const renewal = randomBytes(32).toString('hex');
  const gateway = new DisplayGateway(access, () => origin, () => snapshot);
  const sessionGateway = new DisplaySessionGateway(access, () => origin, () => gateway.changed());
  let loginId = '', grantId = '', browserOpened = false, loginStarts = 0, loginPolls = 0;
  const startLogin = sessionGateway.login.start.bind(sessionGateway.login), pollLogin = sessionGateway.login.poll.bind(sessionGateway.login);
  sessionGateway.login.start = (challenge, remember) => {
    const result = startLogin(challenge, remember); loginId = result.id; return result;
  };
  sessionGateway.login.poll = (id, verifier) => {
    const result = pollLogin(id, verifier);
    if (result.status === 'approved') grantId = result.lease.id;
    return result;
  };
  const peers = new WebSocketServer({ noServer: true, perMessageDeflate: false });
  const timers: ReturnType<typeof setInterval>[] = [];
  let requests = 0, upgrades = 0, redirected = 0, rendered = false;
  let child: ReturnType<typeof spawn> | undefined;
  const server = createServer((request, response) => {
    if (mode === 'gateway') {
      assert.equal(request.headers.cookie, undefined);
      if (request.url === '/display/login') loginStarts++;
      else if (request.url === `/display/login/${loginId}`) loginPolls++;
      else if (request.url === '/display/refresh') { /* revoked renewal must fail through the session gateway */ }
      else assert.fail('native login must not submit a manual display ticket');
      assert(sessionGateway.handle(request, response)); return;
    }
    if (request.url === '/leak') { redirected++; response.end(); return; }
    if (request.url !== '/display/exchange') { response.writeHead(404).end(); return; }
    requests++;
    assert.equal(request.headers.authorization, `ChatView-Ticket ${issued.ticket}`);
    assert.equal(request.headers.cookie, undefined);
    if (mode === 'cancel') { child?.stdin?.write('requested\n'); return; }
    if (mode === 'redirect') { response.writeHead(307, { Location: `${origin}/leak` }).end(); return; }
    response.setHeader('Content-Type', 'application/json');
    response.end(JSON.stringify({ id: issued.id, token,
      expiresInMs: mode === 'local-expiry' ? 1000 : 30000,
      scope: mode === 'wrong-scope' ? 'account:admin' : 'chat:read',
      sessionToken: renewal, sessionExpiresInMs: 60000, sessionScope: 'chat:renew' }));
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
      if (/(^|\n)browser-opened\r?\n/u.test(output) && !browserOpened) {
        assert(loginId); browserOpened = true; sessionGateway.login.approve(loginId);
      }
      if (/(^|\n)rendered\r?\n/u.test(output) && !rendered) {
        rendered = true; assert(grantId); access.revokeSessions(); gateway.changed();
      }
    });
    let errors = '';
    child.stderr!.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
    const timeout = setTimeout(() => child?.kill(), 60000);
    try {
      const [code] = await exited;
      assert.equal(code, 0, `${mode}: ${errors}`);
    } finally { clearTimeout(timeout); }
    if (mode === 'gateway') {
      assert(browserOpened); assert.equal(loginStarts, 1); assert(loginPolls >= 1); assert.equal(requests, 0);
    } else assert.equal(requests, 1, `${mode}: no replay of the one-use exchange`);
    assert.equal(redirected, 0, 'credentials never follow a redirect');
    if (mode === 'gateway') assert(rendered, 'native DOM acknowledgement before revocation');
    if (mode === 'wrong-scope' || mode === 'redirect' || mode === 'cancel') assert.equal(upgrades, 0);
    console.log(`${mode}: passed`);
  } finally {
    child?.kill(); for (const timer of timers) clearInterval(timer);
    sessionGateway.close(); gateway.close(); for (const peer of peers.clients) peer.terminate(); peers.close();
    server.closeAllConnections(); server.close();
  }
}
