// SPDX-License-Identifier: GPL-2.0-or-later
// Real native controls/WebView2/WinHTTP/WGC + HTTP/SQLite/ws; synthetic provider.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { DisplayGateway } from '../platform/server/chat/display-gateway.mts';
import { fixture } from '../platform/tests/fixtures/service.mts';

const executable = process.argv[2], source = process.argv[3], mode = process.argv[4];
assert(executable && source && ['return', 'revoked'].includes(mode ?? ''), 'native flow, source and explicit mode required');
const revoked = mode === 'revoked';
const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
let child: ReturnType<typeof spawn> | undefined;
try {
  const other = await f.connect('bob', 'gaming');
  let pending: Awaited<ReturnType<typeof f.start>> | undefined;
  let target = '', connectionId = '', loginStarts = 0, refreshes = 0, signouts = 0, denials = 0;
  let removed = false;
  const start = f.app.login.start.bind(f.app.login);
  f.app.login.start = (challenge, remember, role) => {
    assert.equal(role, 'gaming'); assert.equal(remember, false); loginStarts++;
    const value = start(challenge, remember, role); pending = { ...value, verifier: '' }; return value;
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
      assert(target); assert.equal(request.headers.authorization, `ChatView-Session ${target}`);
      if (request.url === '/display/refresh') {
        refreshes++; assert.equal(request.headers['x-chatview-role'], 'gaming');
      } else signouts++;
    }
    await handle(request, response);
    if (request.url === '/display/refresh' && removed) {
      assert([401, 403].includes(response.statusCode), 'real application rejects the removed approval');
      denials++;
    }
  };
  const account = async (online: number, approved = 1) => {
    const due = Date.now() + 2000;
    for (;;) {
      const html = await f.page('/account');
      const row = /<tr data-pc-role="gaming">([\s\S]*?)<\/tr>/u.exec(html)?.[1];
      assert(row); assert(row.includes(`data-approved-count="${approved}"`));
      assert.equal(f.store.find(target)?.membership?.connectionId, approved ? connectionId : undefined);
      if (row.includes(`data-online-count="${online}"`)) return;
      assert(Date.now() < due, `server socket count did not reach ${online}`);
      await new Promise<void>(resolve => setTimeout(resolve, 25));
    }
  };
  child = spawn(executable, [source, mode!], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
  const exited = once(child, 'exit'); child.stdin!.write(`${f.origin}\n${other.lease.sessionToken}\n`);
  let errors = '', phase = 0;
  child.stderr!.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
  const phases = ['browser-opened', 'chat-empty-ready', 'pattern-paused', 'pattern-returned',
    'capture-paused', 'capture-returned', 'video-stopped', 'black-paused', 'black-returned', 'reselected',
    ...(revoked ? ['revoke-requested', 'revoked', 'revoked-video-restarted'] : ['signed-out'])];
  const drive = (async () => {
    for await (const line of createInterface({ input: child!.stdout! })) {
      assert.equal(line, phases[phase++], 'fixed native flow order');
      if (line === 'browser-opened') {
        assert(pending); await f.authenticate('alice', pending);
        assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200); continue;
      }
      assert.equal(loginStarts, 1, 'panel navigation never starts a second login');
      if (line === 'signed-out') {
        assert.equal(signouts, 1); assert.equal(f.store.find(target), undefined);
      } else if (line === 'revoked' || line === 'revoked-video-restarted') {
        assert(removed); assert.equal(signouts, 0); assert.equal(denials, 1);
        await account(0, 0);
      } else {
        assert.equal(signouts, 0, 'chat/video stops do not revoke the approval');
        await account(line.endsWith('-paused') ? 0 : 1);
      }
      const expectedRefreshes = line === 'chat-empty-ready' || line === 'pattern-paused' ? 0
        : line === 'pattern-returned' || line === 'capture-paused' ? 1
        : ['capture-returned', 'video-stopped', 'black-paused'].includes(line) ? 2
        : line === 'revoked' || line === 'revoked-video-restarted' ? 4 : 3;
      assert.equal(refreshes, expectedRefreshes, 'three explicit returns, then at most one rejected renewal after revocation');
      if (line === 'revoke-requested') {
        removed = true; f.creators.removeConnection('alice', connectionId);
        assert.equal(f.store.find(target), undefined);
      }
      const marker = line === 'chat-empty-ready' ? 'initial-chat'
        : line === 'video-stopped' ? 'video-stopped-chat-live'
        : line.endsWith('-paused') ? line.replace('-paused', '-return') : undefined;
      if (marker) { assert(f.chats.has('alice')); f.chats.get('alice')!.publish(marker); }
      if (line !== 'chat-empty-ready') child!.stdin!.write('continue\n');
    }
  })();
  void drive.catch(() => child?.kill());
  const deadline = setTimeout(() => child?.kill(), 45000);
  try {
    const [[code]] = await Promise.all([exited, drive]);
    assert.equal(code, 0, errors); assert.equal(phase, phases.length);
  } finally { clearTimeout(deadline); }
  assert(f.store.find(other.lease.sessionToken), 'another creator approval is unchanged');
  assert.equal(loginStarts, 1); assert.equal(refreshes, revoked ? 4 : 3);
  assert.equal(signouts, revoked ? 0 : 1); assert.equal(denials, revoked ? 1 : 0);
  console.log(`companion chat/video ${mode}: independent video and exact chat authority passed`);
} finally { child?.kill(); await f.close(); }
