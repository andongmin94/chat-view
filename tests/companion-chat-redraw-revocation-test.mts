// SPDX-License-Identifier: GPL-2.0-or-later
// Real companion HUD/WebView2/WinHTTP/WGC + HTTP/SQLite/ws.
// Revoke the exact gaming grant before owner-thread redraw renews it;
// both live game output and user-stopped black output are independent.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { DisplayGateway } from '../platform/server/chat/display-gateway.mts';
import { fixture } from '../platform/tests/fixtures/service.mts';

const executable = process.argv[2], source = process.argv[3], mode = process.argv[4];
assert(executable && source && ['redraw-revoked-live', 'redraw-revoked-black',
  'redraw-reapproved-live', 'redraw-reapproved-black'].includes(mode ?? ''),
  'native executable, source and explicit revocation/reapproval mode required');
const reapproved = mode?.startsWith('redraw-reapproved-') ?? false;
const black = mode?.endsWith('-black') ?? false;

const f = await fixture({ createDisplay: (access, origin, snapshot) =>
  new DisplayGateway(access, origin, snapshot) });
let child: ReturnType<typeof spawn> | undefined;
try {
  const other = await f.connect('bob', 'gaming');
  let pending: Awaited<ReturnType<typeof f.start>> | undefined;
  let target = '', connectionId = '', renewedTarget = '', renewedConnection = '';
  let loginStarts = 0, renewals = 0, signouts = 0, denials = 0;
  const start = f.app.login.start.bind(f.app.login);
  f.app.login.start = (challenge, remember, role) => {
    assert.equal(role, 'gaming', 'only the companion may request its fixed gaming role');
    assert.equal(remember, false, 'the revoked approval is in-memory only');
    loginStarts++;
    assert(loginStarts <= (reapproved ? 2 : 1),
      'there is exactly one additional, explicitly clicked browser login only for reapproval');
    const value = start(challenge, remember, role);
    pending = { ...value, verifier: '' }; return value;
  };
  const poll = f.app.login.poll.bind(f.app.login);
  f.app.login.poll = (id, verifier) => {
    const result = poll(id, verifier);
    if (result.status === 'approved') {
      assert.equal(id, pending?.id);
      assert.equal(result.lease.membership?.role, 'gaming');
      if (target) {
        assert(reapproved && loginStarts === 2 && !renewedTarget,
          'the user must explicitly consent to the only replacement grant');
        renewedTarget = result.lease.sessionToken;
        renewedConnection = result.lease.membership!.connectionId;
        assert(renewedTarget !== target && renewedTarget !== other.lease.sessionToken &&
          renewedConnection !== connectionId && f.store.find(target) === undefined,
          'new approval cannot reuse the revoked connection or another creator');
      } else {
        target = result.lease.sessionToken;
        connectionId = result.lease.membership!.connectionId;
      }
    }
    return result;
  };
  const handle = f.app.handle.bind(f.app);
  f.app.handle = async (request, response) => {
    const refresh = request.url === '/display/refresh';
    if (refresh || request.url === '/display/signout') {
      assert(request.headers.authorization === `ChatView-Session ${refresh ? target : renewedTarget}`,
        'old revoked grant is used only for the one denied refresh, new exact grant for logout');
      if (refresh) {
        assert.equal(request.headers['x-chatview-role'], 'gaming');
        renewals++;
      } else signouts++;
    }
    await handle(request, response);
    if (refresh) {
      assert([401, 403].includes(response.statusCode),
        'the real server must reject renewal after exact membership revocation');
      denials++;
    }
  };

  const account = async (online: number, approved: number) => {
    const deadline = Date.now() + 2000;
    for (;;) {
      const html = await f.page('/account');
      const row = /<tr data-pc-role="gaming">([\s\S]*?)<\/tr>/u.exec(html)?.[1];
      assert(row, 'same creator has a gaming role row');
      assert(row.includes(`data-approved-count="${approved}"`));
      if (approved) assert.equal(f.store.find(renewedTarget || target)?.membership?.connectionId,
        renewedConnection || connectionId, 'current native peer uses only the explicitly approved gaming grant');
      else assert.equal(f.store.find(renewedTarget || target), undefined,
        'native user has no valid grant after server revoke or explicit logout');
      if (row.includes(`data-online-count="${online}"`)) return;
      assert(Date.now() < deadline, 'revoked native socket must not retain stale online counts');
      await new Promise<void>(resolve => setTimeout(resolve, 25));
    }
  };

  child = spawn(executable, [source, mode!], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
  const exited = once(child, 'exit');
  child.stdin!.write(`${f.origin}\n${other.lease.sessionToken}\n`);
  let phase = 0, failure: unknown, stderr = '';
  child.stderr!.on('data', chunk => { stderr = (stderr + String(chunk)).slice(-4096); });
  const phases = ['browser-opened', 'redraw-revoke-ready',
    'redraw-revoke-started', 'redraw-revoke-validated',
    ...(reapproved ? ['browser-opened', 'redraw-reapproval-pending', 'redraw-reapproval-empty-ready',
      'redraw-reapproved', ...(black ? ['redraw-reapproved-video-restarted'] : []),
      'redraw-reapproved-signed-out'] : [])];
  const drive = (async () => {
    for await (const line of createInterface({ input: child!.stdout! })) {
      assert.equal(line, phases[phase++], 'the native test owns each revoke/redraw/reapproval phase');
      if (line === 'browser-opened') {
        assert(pending);
        if (loginStarts === 1) {
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
        } else {
          assert(reapproved && loginStarts === 2 && f.store.find(target) === undefined &&
            !renewedTarget, 'second browser page is an unapproved user-requested challenge');
        }
      } else if (line === 'redraw-revoke-ready') {
        assert(target && connectionId);
        assert.equal(loginStarts, 1);
        assert.equal(renewals, 0);
        assert.equal(signouts, 0);
        await account(1, 1);
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-revoke-started') {
        assert.equal(renewals, 0,
          'the owner thread cannot refresh until the parent revokes this exact connection');
        f.creators.removeConnection('alice', connectionId);
        assert(f.store.find(target) === undefined, 'only the original approved membership was removed');
        assert(f.store.find(other.lease.sessionToken), 'another creator approval is intact');
        await account(0, 0);
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-revoke-validated') {
        assert.equal(renewals, 1, 'one rejected explicit same-approval refresh');
        assert.equal(denials, 1, 'server denied the old role before any new private rendering');
        assert.equal(signouts, 0, 'a revoked chat redraw is not another account logout');
        assert.equal(loginStarts, 1, 'server denial never opens a new browser automatically');
        await account(0, 0);
        assert(f.store.find(other.lease.sessionToken), 'unrelated browser/device approval remains intact');
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-reapproval-pending') {
        assert(reapproved && loginStarts === 2 && !renewedTarget && pending);
        assert.equal(renewals, 1); assert.equal(denials, 1);
        await account(0, 0);
        await f.authenticate('alice', pending);
        assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200,
          'new current gaming role needs a deliberate browser consent');
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-reapproval-empty-ready') {
        assert(reapproved && renewedTarget && renewedConnection && loginStarts === 2);
        await account(1, 1);
        assert(f.store.find(target) === undefined && f.store.find(other.lease.sessionToken),
          'new empty DOM belongs to the new grant and leaves other creator intact');
        f.chats.get('alice')!.publish('redraw-new-chat');
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-reapproved' || line === 'redraw-reapproved-video-restarted') {
        assert(reapproved && loginStarts === 2 && renewedTarget);
        assert.equal(renewals, 1, 'successful new first text never renews the revoked grant');
        assert.equal(signouts, 0, 'new display does not implicitly sign out');
        await account(1, 1);
        child!.stdin!.write('continue\n');
      } else if (line === 'redraw-reapproved-signed-out') {
        assert(reapproved && renewedTarget && loginStarts === 2);
        assert.equal(signouts, 1, 'only the user explicitly logs out the newly approved grant');
        assert.equal(renewals, 1); assert.equal(denials, 1);
        await account(0, 0);
        assert(f.store.find(target) === undefined && f.store.find(renewedTarget) === undefined &&
          f.store.find(other.lease.sessionToken),
          'logout revokes new grant alone, and preserves another creator plus old revocation');
        child!.stdin!.write('continue\n');
      }
    }
  })();
  void drive.catch(error => { failure = error; child?.kill(); });
  const deadline = setTimeout(() => child?.kill(), 45000);
  try {
    const [[code]] = await Promise.all([exited, drive]);
    if (failure) throw failure;
    assert.equal(code, 0, `${mode}: ${stderr}`);
    assert.equal(phase, phases.length, 'no phase may be skipped or repeated');
    assert.equal(loginStarts, reapproved ? 2 : 1); assert.equal(renewals, 1);
    assert.equal(denials, 1); assert.equal(signouts, reapproved ? 1 : 0);
    assert(f.store.find(target) === undefined);
    if (reapproved) assert(renewedTarget && f.store.find(renewedTarget) === undefined);
    assert(f.store.find(other.lease.sessionToken));
    console.log(`${mode}: revoked first-display redraw and explicit reapproval preserved independent video and other creator`);
  } finally { clearTimeout(deadline); }
} finally {
  child?.kill();
  await f.close();
}
