// SPDX-License-Identifier: GPL-2.0-or-later
// Real controls, WebView2, WinHTTP, HTTP/SQLite/ws. Synthetic provider and HTML.
import assert from 'node:assert/strict';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { fixture } from '../tests/fixtures/service.mts';

const executable = process.argv[2]; assert(executable, 'native switch executable required');
const configExecutable = process.argv[3]; assert(configExecutable, 'Control Center executable required');
for (const mode of ['memory', 'remembered', 'unrelated', 'revoked', 'mismatch', 'logout-failure']) {
  const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  let child: ReturnType<typeof spawn> | undefined;
  try {
    const observer = await f.connect('alice', 'gaming'), other = await f.connect('bob', 'gaming');
    let pending: { id: string; verificationPath: string; verifier: string } | undefined;
    let loginStarts = 0, refreshes = 0, signouts = 0, target = '', connectionId = '';
    const start = f.app.login.start.bind(f.app.login);
    f.app.login.start = (challenge, remember, role) => {
      assert.equal(role, 'streaming'); assert.equal(remember, mode === 'remembered');
      loginStarts++; const value = start(challenge, remember, role);
      pending = { ...value, verifier: '' }; return value;
    };
    const poll = f.app.login.poll.bind(f.app.login);
    f.app.login.poll = (id, verifier) => {
      const result = poll(id, verifier);
      if (result.status === 'approved') {
        assert.equal(target, ''); target = result.lease.sessionToken;
        connectionId = result.lease.membership!.connectionId;
      }
      return result;
    };
    const handle = f.app.handle.bind(f.app);
    f.app.handle = async (request, response) => {
      if (request.url === '/display/refresh' || request.url === '/display/signout') {
        assert(target); assert.equal(request.headers.authorization, `ChatView-Session ${target}`,
          'return/signout stays bound to the original account, not the stored other account');
        if (request.url === '/display/refresh') {
          refreshes++; assert.equal(request.headers['x-chatview-role'], 'streaming');
          if (mode === 'mismatch') {
            const lease = await f.creators.resume(target);
            response.writeHead(200, { 'Content-Type': 'application/json' });
            response.end(JSON.stringify({ ...lease, membership: { ...lease.membership, connectionId: randomUUID() } }));
            return;
          }
        } else {
          signouts++;
          if (mode === 'logout-failure' && signouts === 1) { response.writeHead(503).end(); return; }
        }
      }
      await handle(request, response);
    };
    child = spawn(executable, [mode, configExecutable], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
    const exited = once(child, 'exit'); child.stdin!.write(`${f.origin}\n${other.lease.sessionToken}\n`);
    let buffer = '', errors = '', failure: unknown, externalSeen = false;
    let browserTask: Promise<void> | undefined;
    child.stdout!.setEncoding('utf8');
    child.stdout!.on('data', (chunk: string) => {
      buffer = (buffer + chunk).slice(-4096);
      if (!browserTask && /(?:^|\n)browser-opened\r?\n/u.test(buffer)) {
        browserTask = (async () => {
          assert(pending); await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
          f.chats.get('alice')!.publish('Native return fixture');
        })();
        void browserTask.catch(error => { failure = error; child?.kill(); });
      }
      if (!externalSeen && /(?:^|\n)external-ready\r?\n/u.test(buffer)) {
        try {
          externalSeen = true;
          assert(target && f.store.find(target));
          assert.equal(signouts, 0, 'changing displayed pages is not logout');
          assert.equal(loginStarts, 1);
          if (mode === 'revoked') f.creators.removeConnection('alice', connectionId);
          else f.chats.get('alice')!.publish('Native return fixture');
          child?.stdin?.write('continue\n');
        } catch (error) { failure = error; child?.kill(); }
      }
    });
    child.stderr!.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
    const deadline = setTimeout(() => child?.kill(), 45000);
    try {
      const [code] = await exited; await browserTask;
      if (failure) throw failure;
      assert.equal(code, 0, `${mode}: ${errors}`);
    } finally { clearTimeout(deadline); }
    assert(externalSeen); assert.equal(loginStarts, 1, 'return never issues a new browser approval');
    assert.equal(refreshes, ['revoked', 'mismatch'].includes(mode) ? 1 : 2,
      'one renewal per explicit return, duplicate button commands do not replay');
    assert.equal(signouts, ['revoked', 'mismatch'].includes(mode) ? 0 : mode === 'logout-failure' ? 2 : 1);
    assert(f.store.find(observer.lease.sessionToken)); assert(f.store.find(other.lease.sessionToken));
    if (mode !== 'mismatch') assert.equal(f.store.find(target), undefined);
    console.log(`native page switch ${mode}: passed`);
  } finally { child?.kill(); await f.close(); }
}
