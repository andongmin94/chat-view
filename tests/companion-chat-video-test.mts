// SPDX-License-Identifier: GPL-2.0-or-later
// Real native controls/WebView2/WinHTTP/WGC + HTTP/SQLite/ws; synthetic provider.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { DisplayGateway } from '../platform/server/chat/display-gateway.mts';
import { fixture } from '../platform/tests/fixtures/service.mts';

const executable = process.argv[2], source = process.argv[3], mode = process.argv[4];
assert(executable && source && ['early-first', 'return', 'revoked', 'reapproved', 'redraw'].includes(mode ?? ''), 'native flow, source and explicit mode required');
const earlyFirst = mode === 'early-first', reapproved = mode === 'reapproved', redraw = mode === 'redraw';
const revoked = mode === 'revoked' || reapproved;
const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
let child: ReturnType<typeof spawn> | undefined;
try {
  const other = await f.connect('bob', 'gaming');
  let pending: Awaited<ReturnType<typeof f.start>> | undefined;
  let target = '', connectionId = '', loginStarts = 0, refreshes = 0, signouts = 0, denials = 0;
  let removed = false, oldTarget = '', oldConnection = '', freshApproval = false, cancelled = 0;
  const start = f.app.login.start.bind(f.app.login);
  f.app.login.start = (challenge, remember, role) => {
    assert.equal(role, 'gaming'); loginStarts++;
    assert(loginStarts <= (reapproved ? 4 : earlyFirst ? 2 : 1), 'no automatic or duplicate login');
    assert.equal(remember, reapproved && (loginStarts === 2 || loginStarts === 3));
    const value = start(challenge, remember, role); pending = { ...value, verifier: '' }; return value;
  };
  const poll = f.app.login.poll.bind(f.app.login);
  f.app.login.poll = (id, verifier) => {
    const result = poll(id, verifier);
    if (result.status === 'approved') {
      if (target) {
        assert(removed && !freshApproval && id === pending?.id && !f.store.find(oldTarget),
          'a new gaming connection needs a fresh approved browser request after revocation');
        assert(result.lease.sessionToken !== oldTarget && result.lease.sessionToken !== other.lease.sessionToken,
          'new consent is neither the revoked grant nor the unrelated saved account');
        assert(result.lease.membership!.role === 'gaming' &&
          result.lease.membership!.connectionId !== oldConnection,
          'new approval receives a new gaming connection');
        assert(earlyFirst ? loginStarts === 2 : reapproved && cancelled === 2 && loginStarts === 4,
          'only the explicitly approved final request can replace the denied context');
        freshApproval = true;
      }
      target = result.lease.sessionToken; connectionId = result.lease.membership!.connectionId;
      if (earlyFirst && !freshApproval) {
        // Publish BEFORE returning the approved lease to the native worker:
        // its very first authenticated WebSocket snapshot must include text.
        const chat = f.chats.get('alice'); assert(chat, 'authorized provider subscription is live');
        chat.publish('initial-chat');
      }
    }
    return result;
  };
  const handle = f.app.handle.bind(f.app);
  f.app.handle = async (request, response) => {
    const deniedRefresh = request.url === '/display/refresh' && removed && !freshApproval;
    if (request.url === '/display/refresh' || request.url === '/display/signout') {
      assert(target && request.headers.authorization === `ChatView-Session ${target}`,
        'native request must use only its current exact approval');
      if (request.url === '/display/refresh') {
        refreshes++; assert.equal(request.headers['x-chatview-role'], 'gaming');
      } else signouts++;
    }
    await handle(request, response);
    if (deniedRefresh) {
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
  const phases = earlyFirst
    ? ['browser-opened', 'early-first-visible', 'early-stopped',
      'early-returned', 'early-returned-text', 'early-final-stopped',
      'early-revoked', 'browser-opened', 'early-reapproval-pending',
      'early-reapproval-empty', 'early-reapproved', 'early-signed-out']
    : ['browser-opened',
    ...(redraw ? ['live-redraw-started', 'live-redraw-ready', 'black-redraw-started', 'black-redraw-ready'] : []),
    'chat-empty-ready', 'pattern-paused', 'pattern-returned',
    'capture-paused', 'capture-returned', 'video-stopped', 'black-paused', 'black-returned', 'reselected',
    ...(revoked ? ['revoke-requested', 'revoked', 'revoked-video-restarted'] : ['signed-out']),
    ...(reapproved ? ['browser-opened', 'reapproval-pending', 'reapproval-cancelled',
      'browser-opened', 'reapproval-pending', 'reapproval-cancelled', 'browser-opened', 'reapproval-approve',
      'reapproval-empty-ready', 'reapproved', 'reapproved-paused', 'reapproved-returned',
      'reapproved-video-restarted', 'reapproved-signed-out'] : [])];
  const drive = (async () => {
    for await (const line of createInterface({ input: child!.stdout! })) {
      assert.equal(line, phases[phase++], 'fixed native flow order');
      if (line === 'browser-opened') {
        assert(pending);
        if (loginStarts === 1) {
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
        } else assert(removed && !freshApproval &&
          (earlyFirst ? loginStarts === 2 : reapproved),
          'the new browser challenge stays pending until explicit owner consent');
        continue;
      }
      if (earlyFirst && line.startsWith('early-')) {
        const hasNewConsent = ['early-reapproval-empty', 'early-reapproved', 'early-signed-out'].includes(line);
        const afterRevocation = ['early-revoked', 'early-reapproval-pending'].includes(line) || hasNewConsent;
        const beforeFirstReturn = ['early-first-visible', 'early-stopped'].includes(line);
        const stopped = ['early-stopped', 'early-final-stopped', 'early-revoked',
          'early-reapproval-pending', 'early-signed-out'].includes(line);
        assert.equal(loginStarts, hasNewConsent || line === 'early-reapproval-pending' ? 2 : 1,
          'only the user opens a second browser challenge after the revoked grant');
        assert.equal(refreshes, afterRevocation ? 2 : beforeFirstReturn ? 0 : 1,
          'the original approval is renewed once, then denied exactly once after revocation');
        assert.equal(denials, afterRevocation ? 1 : 0,
          'server denial is mandatory before new channel/role consent');
        assert.equal(signouts, line === 'early-signed-out' ? 1 : 0,
          'only an explicit logout of the new grant revokes its approval');
        assert.equal(freshApproval, hasNewConsent);
        assert.equal(f.store.find(oldTarget || target)?.membership?.connectionId,
          !afterRevocation ? connectionId : undefined,
          'revocation removes only the exact obsolete gaming connection');
        const approvedCount = line === 'early-signed-out' || (afterRevocation && !hasNewConsent) ? 0 : 1;
        await account(stopped ? 0 : 1, approvedCount);
        assert(f.store.find(other.lease.sessionToken), 'Bob gaming approval survives Alice revoke/consent');
        if (line === 'early-stopped' || line === 'early-returned') {
          const foreign = f.chats.get('bob');
          assert(foreign, 'unrelated creator subscription stays independent');
          foreign.publish(line === 'early-stopped' ? 'bob-only-during-stop' : 'bob-only-after-return');
        }
        if (line === 'early-returned') {
          const chat = f.chats.get('alice');
          assert(chat, 'the original creator chat remains subscribed across local Stop');
          chat.publish('returned-chat');
        }
        if (line === 'early-final-stopped') {
          oldTarget = target; oldConnection = connectionId;
          f.creators.removeConnection('alice', oldConnection);
          removed = true;
          assert.equal(f.store.find(oldTarget), undefined,
            'server revoked Alice while her private display was locally stopped');
          await account(0, 0);
        }
        if (line === 'early-revoked') {
          assert(removed && !freshApproval && loginStarts === 1 &&
            !f.store.find(oldTarget), 'the denied old refresh never creates consent');
        }
        if (line === 'early-reapproval-pending') {
          assert(removed && pending && !freshApproval &&
            f.app.login.view(pending.id).role === 'gaming',
            'second browser challenge is new, fixed-role and not yet approved');
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200,
            'the creator explicitly reapproves the original gaming role');
        }
        if (line === 'early-reapproval-empty') {
          assert(freshApproval && connectionId !== oldConnection && !f.store.find(oldTarget),
            'fresh approval is distinct from the revoked connection');
          const chat = f.chats.get('alice'); assert(chat);
          chat.publish('fresh-consent-chat');
        }
        if (line === 'early-signed-out') {
          assert.equal(f.store.find(target), undefined,
            'explicit logout retires only the newly approved game connection');
        }
        child!.stdin!.write('continue\n'); continue;
      }
      if (line.startsWith('reapproval-') || line.startsWith('reapproved')) {
        assert(reapproved && removed && !f.store.find(oldTarget));
        assert.equal(denials, 1, 'no reuse of the old denied approval');
        if (line === 'reapproval-pending') assert.equal(loginStarts, 2 + cancelled);
        else if (line === 'reapproval-cancelled') { cancelled++; assert.equal(loginStarts, 1 + cancelled); }
        else { assert.equal(loginStarts, 4); assert.equal(cancelled, 2); }
        const returned = ['reapproved-returned', 'reapproved-video-restarted', 'reapproved-signed-out'].includes(line);
        assert.equal(refreshes, returned ? 5 : 4, 'only explicit return renews the new approval');
        assert.equal(signouts, line === 'reapproved-signed-out' ? 1 : 0);
        const hasApproval = freshApproval && line !== 'reapproved-signed-out';
        await account(hasApproval && line !== 'reapproved-paused' ? 1 : 0, hasApproval ? 1 : 0);
        if (line === 'reapproval-approve') {
          assert(pending && !freshApproval);
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
        }
        if (line === 'reapproval-empty-ready' || line === 'reapproved-paused') {
          assert(freshApproval);
          f.chats.get('alice')!.publish(line === 'reapproval-empty-ready' ? 'reapproved-chat' : 'reapproved-return-chat');
        }
        child!.stdin!.write('continue\n'); continue;
      }
      assert.equal(loginStarts, 1, 'panel navigation never starts a second login');
      if (line.includes('-redraw-')) {
        assert(redraw && !removed && !freshApproval && !reapproved);
        assert.equal(signouts, 0, 'manual redraw never signs out the same approval');
        assert.equal(f.store.find(target)?.membership?.connectionId, connectionId,
          'one browser consent keeps the exact same gaming-role approval across both redraws');
        const expected = line.startsWith('live-') ? 1 : 2;
        if (line.endsWith('-ready')) {
          assert.equal(refreshes, expected, 'one explicit renewed chat connection per redraw, no login');
          await account(1);
        } else {
          assert(refreshes >= expected - 1 && refreshes <= expected,
            'explicit redraw may finish asynchronously but never duplicates renewal');
        }
        child!.stdin!.write('continue\n'); continue;
      }
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
      assert.equal(refreshes, expectedRefreshes + (redraw ? 2 : 0),
        'three ordinary returns plus exactly two explicit empty-DOM redraws, or one denied renewal');
      if (line === 'revoke-requested') {
        oldTarget = target; oldConnection = connectionId;
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
  if (earlyFirst) {
    assert(removed && freshApproval && oldTarget !== target &&
      oldConnection !== connectionId && !f.store.find(oldTarget) && !f.store.find(target),
      'old grant denied, new gaming grant explicitly approved and then logged out');
    assert.equal(loginStarts, 2); assert.equal(refreshes, 2); assert.equal(signouts, 1);
    assert.equal(denials, 1); assert.equal(cancelled, 0);
    console.log('companion early first chat: Stop -> original approval denied -> explicit same-owner gaming consent -> new private DOM -> logout passed');
  } else {
    assert.equal(loginStarts, reapproved ? 4 : 1);
    assert.equal(refreshes, reapproved || redraw ? 5 : revoked ? 4 : 3);
    assert.equal(signouts, reapproved || !revoked ? 1 : 0); assert.equal(denials, revoked ? 1 : 0);
    assert.equal(freshApproval, reapproved); assert.equal(cancelled, reapproved ? 2 : 0);
    console.log(`companion chat/video ${mode}: independent video and exact chat authority passed`);
  }
} finally { child?.kill(); await f.close(); }
