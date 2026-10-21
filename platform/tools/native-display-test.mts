// SPDX-License-Identifier: GPL-2.0-or-later
// Test runner only: actual gateway/ws -> actual WinHTTP -> actual HUD/WebView2.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { randomBytes, randomUUID } from 'node:crypto';
import { WebSocketServer } from 'ws';
import { DisplayAccess } from '../server/chat/display-access.mts';
import { DisplayGateway, displayFrame } from '../server/chat/display-gateway.mts';
import { DisplaySessionGateway } from '../server/chat/display-session-gateway.mts';

const executable = process.argv[2];
assert(executable, 'native test executable required');
const snapshot = { state: 'subscribed', received: 1, messages: [
  { nickname: '검증 사용자', content: '안녕 😀 <img onerror=evil()>', messageTime: 1700000000000 },
] };
for (const mode of ['gateway', 'gateway-gaming', 'gateway-provider-loss', 'gateway-provider-loss-gaming', 'gateway-provider-burst', 'login-cancel', 'login-cancel-unconfirmed', 'login-expiry', 'login-denied', 'cancel', 'local-expiry', 'redirect', 'wrong-scope', 'wrong-role',
  'wrong-session', 'bad-count', 'claimed-capture', 'missing-connection', 'invalid-json', 'binary', 'oversize', 'idle']) {
  const ui = mode === 'gateway' || mode === 'gateway-gaming' ||
    mode.startsWith('gateway-provider-loss') || mode === 'gateway-provider-burst' || mode.startsWith('login-');
  const role = mode === 'gateway-gaming' || mode === 'gateway-provider-loss-gaming' ? 'gaming' : 'streaming';
  let origin = '';
  const access = new DisplayAccess(() => ({ id: 'synthetic-owner', expiresAt: performance.now() + 60000 }));
  const issued = access.issue();
  const token = randomBytes(32).toString('hex');
  const renewal = randomBytes(32).toString('hex');
  const membership = { role: 'gaming' as const, connectionId: randomUUID(), broadcastSessionId: randomUUID() };
  const frameConnection = { ...membership, gamingConnections: 1, streamingConnections: 0, captureState: 'unverified' as const };
  let currentSnapshot = snapshot;
  const gateway = new DisplayGateway(access, () => origin, () => currentSnapshot);
  const sessionGateway = new DisplaySessionGateway(access, () => origin, () => gateway.changed());
  let loginId = '', grantId = '', browserOpened = false, loginStarts = 0, loginPolls = 0, loginCancels = 0;
  let approved = false, denied = false;
  const ids: string[] = [];
  const startLogin = sessionGateway.login.start.bind(sessionGateway.login), pollLogin = sessionGateway.login.poll.bind(sessionGateway.login);
  sessionGateway.login.start = (challenge, remember, requested) => {
    assert.equal(requested, role);
    const restartedAfterCancel = mode.startsWith('login-cancel') && loginStarts === 2;
    assert.equal(remember, restartedAfterCancel, 'only the explicit new request can choose remembrance');
    const result = startLogin(challenge, remember, requested); loginId = result.id; ids.push(loginId);
    // Shorten only this test response so the actual client's original deadline
    // can expire without waiting five minutes or modifying production budgets.
    return { ...result, expiresInMs: mode === 'login-expiry' && loginStarts === 1 ? 2000 : result.expiresInMs };
  };
  sessionGateway.login.poll = (id, verifier) => {
    const result = pollLogin(id, verifier);
    if (result.status === 'approved') {
      assert.equal(result.lease.membership?.role, role);
      grantId = result.lease.id;
    }
    return result;
  };
  const peers = new WebSocketServer({ noServer: true, perMessageDeflate: false });
  const timers: ReturnType<typeof setInterval>[] = [];
  let requests = 0, upgrades = 0, redirected = 0, rendered = false;
  let providerLost = false, providerReturned = false, providerBurst = false;
  let child: ReturnType<typeof spawn> | undefined;
  const server = createServer((request, response) => {
    if (ui) {
      assert.equal(request.headers.cookie, undefined);
      if (request.url === '/display/login') {
        assert.equal(request.headers['x-chatview-role'], role); loginStarts++;
      } else if (request.url === `/display/login/${loginId}`) loginPolls++;
      else if (request.url === `/display/login/${loginId}/cancel`) {
        loginCancels++;
        assert(request.headers.authorization?.startsWith('ChatView-Login '));
        if (mode === 'login-cancel-unconfirmed') {
          // Apply the actual server cancellation but lose the HTTP response.
          // The app must not interpret a transport failure as acknowledgement.
          sessionGateway.login.cancel(loginId, request.headers.authorization!.slice('ChatView-Login '.length));
          response.destroy(); return;
        }
      } else if (request.url === '/display/refresh') assert.equal(request.headers['x-chatview-role'], role);
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
      membership: { ...membership, role: mode === 'wrong-role' ? 'streaming' : 'gaming' },
      sessionToken: renewal, sessionExpiresInMs: 60000, sessionScope: 'chat:renew' }));
  });
  server.on('upgrade', (request, socket, head) => {
    upgrades++;
    if (ui) { gateway.upgrade(request, socket, head); return; }
    assert.equal(request.url, '/display/events');
    assert.equal(request.headers.authorization, `Bearer ${token}`);
    assert.equal(request.headers.cookie, undefined);
    peers.handleUpgrade(request, socket, head, peer => {
      peer.on('error', () => peer.terminate());
      if (mode === 'binary') peer.send(Buffer.from('{}'));
      else if (mode === 'oversize') peer.send('x'.repeat(2 * 1024 * 1024 + 1));
      else if (mode === 'invalid-json') peer.send('{invalid');
      else if (mode === 'local-expiry') {
        peer.send(displayFrame(snapshot, frameConnection));
        const timer = setInterval(() => { if (peer.readyState === 1) peer.send(displayFrame(snapshot, frameConnection)); }, 100);
        timers.push(timer); peer.once('close', () => clearInterval(timer));
      } else if (['wrong-session', 'bad-count', 'claimed-capture', 'missing-connection'].includes(mode)) {
        const frame = JSON.parse(displayFrame(snapshot, frameConnection));
        if (mode === 'wrong-session') frame.connection.broadcastSessionId = randomUUID();
        if (mode === 'bad-count') frame.connection.streamingConnections = 2;
        if (mode === 'claimed-capture') frame.connection.captureState = 'verified';
        if (mode === 'missing-connection') delete frame.connection;
        peer.send(JSON.stringify(frame));
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
        assert(loginId); browserOpened = true;
        assert.equal(sessionGateway.login.view(loginId).role, role);
      }
      if (/(^|\n)deny-login\r?\n/u.test(output) && !denied) {
        assert(ui && mode === 'login-denied' && loginStarts === 1);
        denied = true; sessionGateway.login.deny(loginId);
      }
      if (/(^|\n)approve-login\r?\n/u.test(output) && !approved) {
        assert(ui); approved = true;
        assert.equal(loginStarts, mode.startsWith('login-') ? 2 : 1);
        sessionGateway.login.approve(loginId);
      }
      if (/(^|\n)provider-loss\r?\n/u.test(output) && !providerLost) {
        assert(mode.startsWith('gateway-provider-loss'));
        providerLost = true;
        currentSnapshot = { state: 'disconnected', received: 0, messages: [] };
        gateway.changed();
      }
      if (/(^|\n)provider-return\r?\n/u.test(output) && !providerReturned) {
        assert(mode.startsWith('gateway-provider-loss'));
        assert(providerLost);
        providerReturned = true;
        currentSnapshot = { state: 'subscribed', received: 1, messages: [
          { nickname: '복구 사용자', content: '새 구독의 첫 채팅 😀', messageTime: 1700000001000 },
        ] };
        gateway.changed();
      }
      if (/(^|\n)provider-burst\r?\n/u.test(output) && !providerBurst) {
        assert.equal(mode, 'gateway-provider-burst');
        providerBurst = true;
        void (async () => {
          currentSnapshot = { state: 'disconnected', received: 0, messages: [] };
          gateway.changed();
          // The gateway batches within 50ms. Send both *separate* server
          // frames while the native HUD owner deliberately does not poll.
          await new Promise<void>(resolve => setTimeout(resolve, 90));
          currentSnapshot = { state: 'subscribed', received: 1, messages: [
            { nickname: '복구 사용자', content: '새 구독의 첫 채팅 😀', messageTime: 1700000001000 },
          ] };
          gateway.changed();
        })().catch(() => child?.kill());
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
    if (ui) {
      assert(browserOpened && approved); assert.equal(loginStarts, mode.startsWith('login-') ? 2 : 1);
      assert.equal(new Set(ids).size, loginStarts, 'only explicit restarts create new requests');
      assert(loginPolls >= 1); assert.equal(requests, 0);
      if (mode === 'login-cancel' || mode === 'login-cancel-unconfirmed') {
        assert.equal(loginCancels, 1, 'exactly one server cancellation for the old browser request');
        assert.throws(() => sessionGateway.login.view(ids[0]!), { status: 410 },
          'old browser request remains closed after an explicit new login');
      } else {
        assert.equal(loginCancels, 0, 'hide, reopen, expiry and denial are not explicit cancel requests');
        if (mode === 'login-expiry')
          assert.equal(sessionGateway.login.view(ids[0]!).role, role, 'uncollected timed-out client has no approval');
      }
      assert.equal(denied, mode === 'login-denied');
    } else assert.equal(requests, 1, `${mode}: no replay of the one-use exchange`);
    assert.equal(redirected, 0, 'credentials never follow a redirect');
    if (ui) assert(rendered, 'native DOM acknowledgement before revocation');
    if (mode.startsWith('gateway-provider-loss')) assert(providerLost && providerReturned,
      'the native HUD reported both the upstream interruption and its fresh first chat');
    if (mode === 'gateway-provider-burst')
      assert(providerBurst, 'two ordered provider frames arrived before the HUD owner resumed polling');
    if (['wrong-scope', 'wrong-role', 'redirect', 'cancel'].includes(mode)) assert.equal(upgrades, 0);
    console.log(`${mode}: passed`);
  } finally {
    child?.kill(); for (const timer of timers) clearInterval(timer);
    sessionGateway.close(); gateway.close(); for (const peer of peers.clients) peer.terminate(); peers.close();
    server.closeAllConnections(); server.close();
  }
}
