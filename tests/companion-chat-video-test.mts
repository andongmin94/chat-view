// SPDX-License-Identifier: GPL-2.0-or-later
// Real native controls/WebView2/WinHTTP/WGC + HTTP/SQLite/ws; synthetic provider.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { DisplayGateway } from '../platform/server/chat/display-gateway.mts';
import { fixture } from '../platform/tests/fixtures/service.mts';

const executable = process.argv[2], source = process.argv[3], mode = process.argv[4];
assert(executable && source && ['early-first', 'return', 'revoked', 'reapproved', 'redraw',
  'observe-late-ready', 'observe-never-ready', 'recover-first-ack'].includes(mode ?? ''), 'native flow, source and explicit mode required');
const earlyFirst = mode === 'early-first', reapproved = mode === 'reapproved', redraw = mode === 'redraw';
const observation = mode === 'observe-late-ready' || mode === 'observe-never-ready';
const recoverFirstAck = mode === 'recover-first-ack';
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
  const exited = once(child, 'close'); child.stdin!.write(`${f.origin}\n${other.lease.sessionToken}\n`);
  let errors = '', phase = 0;
  let observationStderr = '', observationOverflow = false;
  child.stderr!.on('data', chunk => {
    const value = String(chunk);
    errors = (errors + value).slice(-4096);
    // Explicit recovery regressions require the complete, fixed diagnostic record.
    // Overflow must fail, rather than silently lose the original failure.
    if ((observation || recoverFirstAck) && !observationOverflow) {
      observationStderr += value;
      if (observationStderr.length > 8192) {
        observationOverflow = true; observationStderr = '';
      }
    }
  });
  const phases = observation ? ['browser-opened'] : recoverFirstAck
    ? ['browser-opened', 'ack-suppressed', 'ack-recovered', 'ack-recovery-stopped'] : earlyFirst
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
          // The real prewarmed document must be ready and its ACK receiver
          // suppressed before the original browser request is approved.
          if (recoverFirstAck) continue;
          // Input condition only: the original native 15-second wait starts
          // before consent, leaving part of its unchanged product deadline.
          if (observation) await new Promise<void>(resolve => setTimeout(resolve, 3000));
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
        } else assert(removed && !freshApproval &&
          (earlyFirst ? loginStarts === 2 : reapproved),
          'the new browser challenge stays pending until explicit owner consent');
        continue;
      }
      if (recoverFirstAck) {
        assert.equal(loginStarts, 1, 'ACK recovery does not start another login');
        assert.equal(refreshes, 0, 'ACK recovery preserves the existing server connection');
        assert.equal(signouts, 0, 'local recovery and Stop never revoke the approval');
        assert.equal(denials, 0); assert.equal(freshApproval, false); assert.equal(cancelled, 0);
        assert(f.store.find(other.lease.sessionToken), 'Bob approval survives local ACK recovery and Stop');
        if (line === 'ack-suppressed') {
          assert(pending && !target, 'the first consent is pending until the actual ACK listener is suppressed');
          await f.authenticate('alice', pending);
          assert.equal((await f.post(`/login/${pending.id}/approve`)).status, 200);
          continue;
        } else {
          assert(line === 'ack-recovered' || line === 'ack-recovery-stopped');
          assert(target && f.store.find(target)?.membership?.connectionId === connectionId,
            'ACK recovery and local Stop preserve the exact original gaming approval');
          await account(line === 'ack-recovered' ? 1 : 0);
        }
        child!.stdin!.write('continue\n'); continue;
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
  let driveFailed = false, parentDeadlineReached = false;
  void drive.catch(() => { driveFailed = true; child?.kill(); });
  const deadline = setTimeout(() => { parentDeadlineReached = true; child?.kill(); }, 45000);
  try {
    const [[code, signal]] = await Promise.all([exited, drive]);
    if (observation) {
      assert(!driveFailed && !parentDeadlineReached, 'negative observation ends without parent cancellation');
      assert.equal(signal, null, 'original native failure exits without a termination signal');
      assert.equal(code, 1, 'late or absent readiness must preserve the original native failure');
      assert(!observationOverflow, 'complete negative diagnostic record stays within 8192 characters');
      // Fixed scalar/marker assertions are below the shared close/drive wait,
      // so neither the final stderr line nor unexpected stdout can be missed.
    } else {
      assert.equal(code, 0, errors);
      if (recoverFirstAck) {
        assert(!driveFailed && !parentDeadlineReached, 'ACK recovery ends without parent cancellation');
        assert.equal(signal, null, 'successful native ACK recovery exits without a termination signal');
        assert(!observationOverflow, 'complete recovery diagnostic stays within 8192 characters');
      }
    }
    assert.equal(phase, phases.length);
  } finally { clearTimeout(deadline); }
  assert(f.store.find(other.lease.sessionToken), 'another creator approval is unchanged');
  if (observation) {
    assert.equal(loginStarts, 1); assert.equal(refreshes, 0); assert.equal(signouts, 0);
    assert.equal(denials, 0); assert.equal(freshApproval, false); assert.equal(cancelled, 0);
    assert(target && f.store.find(target)?.membership?.connectionId === connectionId,
      'negative readiness observation neither renews nor revokes the exact browser approval');
    const lines = observationStderr.replace(/\r\n/gu, '\n').split('\n');
    assert.equal(lines.pop(), '', 'negative diagnostic record ends with a complete line');
    const recovery = lines.shift();
    assert.equal(recovery,
      'Companion first chat recovery: kind=preparing prior_awaiting_render=0 prior_native_ready=0 prior_frames=0 prior_deadline_matches=1',
      'held document conditions enter the preparation recovery without a prior publication');
    assert(lines.length === 6 || lines.length === 8, 'only the fixed negative diagnostic sequence is accepted');
    assert.equal(lines[0], 'Companion first chat observation: original-failure');
    assert.equal(lines.at(-1), 'Companion chat/video: subscribed empty own DOM',
      'the original exception is the final native diagnostic exactly once');
    const phasePattern = /^Companion first chat phase: active=(?<active>[01]) worker=(?<worker>[01]) awaiting_login=(?<awaiting_login>[01]) surface_ready=(?<surface_ready>[01]) frames=(?<frames>\d{1,10}) rows=(?<rows>\d{1,10}) rejected=(?<rejected>\d{1,10}) metadata=(?<metadata>[01]) pending_frame=(?<pending_frame>[01]) awaiting_render=(?<awaiting_render>[01]) native_ready=(?<native_ready>[01]) surface_discarded=(?<surface_discarded>[01]) surface_attached=(?<surface_attached>[01]) navigation_started=(?<navigation_started>[01]) nonce_handshake=(?<nonce_handshake>[01]) source_matches=(?<source_matches>[01]) pending_subscribed=(?<pending_subscribed>[01]) recovery_attempted=(?<recovery_attempted>[01]) reconnecting=(?<reconnecting>[01]) loading_remaining_ms=(?<loading_remaining_ms>\d{1,10}) render_remaining_ms=(?<render_remaining_ms>\d{1,10})$/u;
    if (lines.length === 8) assert.equal(lines[2], 'Companion first chat observation: ack-wait');
    const evidence = [lines[1], ...(lines.length === 8 ? [lines[3]] : []), lines.at(-3)].map(line => {
      const fields = phasePattern.exec(line!)?.groups;
      assert(fields, 'each phase contains only the fixed, bounded readiness scalars');
      return Object.fromEntries(Object.entries(fields).map(([key, value]) => [key, Number(value)]));
    });
    const initial = evidence[0]!, final = evidence.at(-1)!;
    assert(initial.active === 1 && initial.worker === 1 && initial.awaiting_login === 0 &&
      initial.metadata === 1 && initial.pending_frame === 1 && initial.pending_subscribed === 1 &&
      initial.surface_ready === 0 && initial.nonce_handshake === 0 && initial.native_ready === 0 &&
      initial.frames === 0 && initial.rows === 0 && initial.rejected === 0 &&
      initial.awaiting_render === 0 && initial.recovery_attempted === 1 &&
      initial.loading_remaining_ms > 0 && initial.loading_remaining_ms <= 15000,
    'original failure precedes the unchanged product deadline with one recovered document still unready');
    const checkPattern = /^Companion first chat observation check: budget_ms=(?<budget_ms>\d{1,10}) elapsed_ms=(?<elapsed_ms>\d{1,10}) held=(?<held>\d{1,10}) released=(?<released>\d{1,10}) current_held=(?<current_held>[01]) loading_deadline_reached=(?<loading_deadline_reached>[01]) late_within_loading_deadline=(?<late_within_loading_deadline>[01]) attached=(?<attached>[01]) navigation_started=(?<navigation_started>[01]) nonce_handshake=(?<nonce_handshake>[01]) source_matches=(?<source_matches>[01]) frames=(?<frames>\d{1,10}) rows=(?<rows>\d{1,10}) active=(?<active>[01])$/u;
    const checkFields = checkPattern.exec(lines.at(-2)!)?.groups;
    assert(checkFields, 'one fixed bounded observation summary precedes the original exception');
    const check = Object.fromEntries(Object.entries(checkFields).map(([key, value]) => [key, Number(value)]));
    assert(check.budget_ms > 0 && check.budget_ms <= 15000 && check.elapsed_ms < 45000 &&
      check.held === 2 && check.rows === 0 && check.frames === final.frames &&
      check.active === final.active && check.attached === final.surface_attached &&
      check.navigation_started === final.navigation_started && check.nonce_handshake === final.nonce_handshake &&
      check.source_matches === final.source_matches,
    'summary agrees with actual final surface evidence and both held document requests');
    if (mode === 'observe-late-ready') {
      assert.equal(lines.at(-4), 'Companion first chat observation: late-empty-ack');
      assert(check.released === 2 && check.current_held === 0 && check.loading_deadline_reached === 0 &&
        check.late_within_loading_deadline === 1 && check.elapsed_ms < check.budget_ms &&
        check.attached === 1 && check.navigation_started === 1 && check.nonce_handshake === 1 &&
        check.source_matches === 1 && check.frames >= 1 && check.active === 1 &&
        final.surface_ready === 1 && final.metadata === 1 && final.rejected === 0,
      'released actual document produces an empty ACK within the captured original loading deadline');
    } else {
      assert(lines.length === 6 && ['Companion first chat observation: connection-ended',
        'Companion first chat observation: existing-deadline-reached'].includes(lines.at(-4)!),
      'held document reaches a fixed terminal without entering ACK wait');
      assert(check.released === 1 && check.current_held === 1 && check.loading_deadline_reached === 1 &&
        check.late_within_loading_deadline === 0 && check.elapsed_ms >= check.budget_ms &&
        check.nonce_handshake === 0 && check.frames === 0 && final.surface_ready === 0 &&
        final.awaiting_render === 0 && final.loading_remaining_ms === 0,
      'held current document reaches its original preparation deadline without a handshake or ACK');
    }
    console.log(`companion first chat observation ${mode}: child_exit=1 original_failure_preserved=1`);
    console.log(recovery); // Exact validated recovery branch, with no document identifiers.
    for (const line of lines) console.log(line); // Only the validated fixed diagnostics above.
  } else if (recoverFirstAck) {
    assert.equal(loginStarts, 1); assert.equal(refreshes, 0); assert.equal(signouts, 0);
    assert.equal(denials, 0); assert.equal(freshApproval, false); assert.equal(cancelled, 0);
    assert(target && f.store.find(target)?.membership?.connectionId === connectionId,
      'the native render recovery and Stop retain the original approval');
    const recovery = 'Companion first chat recovery: kind=render prior_awaiting_render=1 prior_native_ready=1 prior_frames=0 prior_deadline_matches=1';
    assert.equal(observationStderr.replace(/\r\n/gu, '\n'), `${recovery}\n`,
      'only one exact render-recovery diagnostic is accepted');
    console.log(recovery);
    console.log('companion first chat render recovery: original approval, first empty ACK and Stop passed');
  } else if (earlyFirst) {
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
