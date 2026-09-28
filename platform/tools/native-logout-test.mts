// SPDX-License-Identifier: GPL-2.0-or-later
// Real native controls/WinHTTP -> real application HTTP/SQLite/display gateway.
// Provider/chat are synthetic; no live CHZZK, OBS output or capture qualification.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import type { ServerResponse } from 'node:http';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { fixture } from '../tests/fixtures/service.mts';

const executable = process.argv[2]; assert(executable, 'native logout executable required');
for (const mode of ['memory', 'remembered', 'stopped', 'retry', 'unrelated', 'pending', 'storage-failure', 'stored-only', 'cancel']) {
  const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  let child: ReturnType<typeof spawn> | undefined;
  let held: ServerResponse | undefined;
  try {
    const observer = await f.connect('alice', 'gaming');
    const other = await f.connect('bob', 'gaming');
    const stored = mode === 'stored-only' ? await f.connect('alice', 'streaming') : undefined;
    let pending: { id: string; verificationPath: string; verifier: string } | undefined;
    let loginStarts = 0, signouts = 0, revoked = false, retrySeen = false;
    let target: string | undefined = stored?.lease.sessionToken;
    const start = f.app.login.start.bind(f.app.login);
    f.app.login.start = (challenge, remember, role) => {
      assert.equal(role, 'streaming');
      assert.equal(remember, ['remembered', 'storage-failure', 'cancel'].includes(mode));
      loginStarts++;
      const value = start(challenge, remember, role);
      // The browser never sees the native verifier. authenticate() does not poll.
      pending = { ...value, verifier: '' }; return value;
    };
    const handle = f.app.handle.bind(f.app);
    f.app.handle = async (request, response) => {
      if (request.url === '/display/signout') {
        signouts++;
        const authorization = request.headers.authorization ?? '';
        assert(authorization.startsWith('ChatView-Session '));
        const credential = authorization.slice('ChatView-Session '.length);
        const session = f.store.find(credential);
        assert.equal(session?.owner, 'alice', 'logout must not revoke the unrelated saved creator');
        assert.equal(session?.membership?.role, 'streaming');
        if (target) assert.equal(credential, target, 'explicit retry uses the same approval');
        target = credential;
        if (mode === 'retry' && signouts === 1) { response.writeHead(503).end(); return; }
        if (mode === 'cancel') {
          held = response; child?.stdin?.write('signout-requested\n'); return;
        }
        await handle(request, response);
        assert.equal(f.store.find(credential), undefined);
        revoked = true; return;
      }
      await handle(request, response);
    };
    child = spawn(executable, [mode], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
    const exited = once(child, 'exit');
    child.stdin!.write(`${f.origin}\n${stored?.lease.sessionToken ?? other.lease.sessionToken}\n`);
    if (mode !== 'cancel') child.stdin!.end();
    let output = '', errors = '', failure: unknown;
    let browserTask: Promise<void> | undefined;
    child.stdout!.setEncoding('utf8');
    child.stdout!.on('data', (chunk: string) => {
      output = (output + chunk).slice(-4096);
      if (!browserTask && /(?:^|\n)browser-opened\r?\n/u.test(output)) {
        browserTask = (async () => {
          assert(pending);
          if (mode === 'pending') return; // No provider or role consent in this case.
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
          f.chats.get('alice')!.publish('로그아웃 검증 😀');
        })();
        void browserTask.catch(error => { failure = error; child?.kill(); });
      }
      if (!retrySeen && /(?:^|\n)retry-ready\r?\n/u.test(output)) retrySeen = true;
    });
    child.stderr!.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
    const deadline = setTimeout(() => child?.kill(), 45000);
    try {
      const [code] = await exited;
      await browserTask;
      if (failure) throw failure;
      assert.equal(code, 0, `${mode}: ${errors}`);
    } finally { clearTimeout(deadline); }
    assert.equal(loginStarts, mode === 'stored-only' ? 0 : 1, 'logout never opens another approval');
    assert.equal(signouts, mode === 'pending' ? 0 : mode === 'retry' ? 2 : 1);
    assert.equal(retrySeen, mode === 'retry' || mode === 'storage-failure');
    assert(f.store.find(observer.lease.sessionToken), 'same-creator gaming approval survives');
    assert(f.store.find(other.lease.sessionToken), 'other creator survives');
    if (mode === 'pending') {
      assert.equal(f.store.connections('alice').filter(c => c.role === 'streaming').length, 0);
      assert.equal(revoked, false);
    } else if (mode === 'cancel') {
      assert(target && f.store.find(target)); assert.equal(revoked, false);
    } else {
      assert(revoked && target && !f.store.find(target));
      assert.equal(f.store.connections('alice').filter(c => c.role === 'streaming').length, 0);
      // The released streaming role can be approved immediately, without deleting
      // the gaming approval, replacing the creator, or waiting for 30-day expiry.
      f.app.login.start = start;
      const replacement = await f.start('streaming');
      const replacementLease = await f.approve(replacement, 'streaming', stored?.b ?? f.browser);
      assert.equal(replacementLease.membership.broadcastSessionId, observer.lease.membership.broadcastSessionId);
    }
    console.log(`native logout ${mode}: passed`);
  } finally {
    child?.kill(); held?.destroy(); await f.close();
  }
}
