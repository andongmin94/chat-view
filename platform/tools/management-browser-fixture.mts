// SPDX-License-Identifier: GPL-2.0-or-later
// Parent-controlled fixture only. Real service HTTP/SQLite/ws, synthetic CHZZK.
// No debug HTTP routes; commands and redacted results use inherited stdio.
import assert from 'node:assert/strict';
import { once, on } from 'node:events';
import { createInterface } from 'node:readline';
import { WebSocket } from 'ws';
import { fixture } from '../tests/fixtures/service.mts';
import { LOGIN_POLL_MS } from '../server/chat/browser-login.mts';
import { BroadcastOutput } from '../server/chat/broadcast-output.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const realNow = Date.now.bind(Date);
let offset = 0, outputNow = 1000;
Date.now = () => realNow() + offset; // Expire browser state, not native/provider grants.
const f = await fixture({ now: realNow,
  createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot, new BroadcastOutput(() => outputNow)) });
let peer: WebSocket | undefined;
const collectionPeers: WebSocket[] = [];
let pending: Awaited<ReturnType<typeof f.start>> | undefined;
const metadata: { path: string; method: string; site: string; mode: string; dest: string;
  cookie: boolean; authorization: boolean; origin: string; status: number }[] = [];
const handle = f.app.handle.bind(f.app);
f.app.handle = async (request, response) => {
  // Paths and presence bits only; never retain cookie/state/code/CSRF or bodies.
  const path = new URL(request.url!, f.origin).pathname;
  const sample = { path: /^\/(?:display\/)?login\/[a-f0-9]{32}/u.test(path) ? '/login/:id' : path,
    method: request.method ?? '', site: String(request.headers['sec-fetch-site'] ?? ''),
    mode: String(request.headers['sec-fetch-mode'] ?? ''), dest: String(request.headers['sec-fetch-dest'] ?? ''),
    cookie: !!request.headers.cookie, authorization: !!request.headers.authorization,
    origin: request.headers.origin === undefined ? 'absent' : request.headers.origin === f.origin ? 'same-origin'
      : request.headers.origin === 'null' ? 'null' : 'other', status: 0 };
  response.once('finish', () => {
    sample.status = response.statusCode;
    metadata.push(sample); if (metadata.length > 500) metadata.shift();
  });
  await handle(request, response);
};
const reply = (data: unknown) => process.stdout.write(JSON.stringify(data) + '\n');
const deadline = setTimeout(() => process.exit(2), 180_000);
try {
  const alice = await f.connect('alice', 'streaming'), bob = await f.connect('bob', 'gaming');
  await f.page('/campaigns', alice.b);
  assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, alice.b)).status, 303);
  const snapshot = () => JSON.stringify({
    alice: f.store.connections('alice'), bob: f.store.connections('bob'),
    selections: f.store.database.prepare('SELECT * FROM ad_selections').all(),
    aliceGrant: f.grants.load('alice'), bobGrant: f.grants.load('bob'),
  });
  const before = snapshot(), starts = f.startedWith.length;
  const openSender = async (token: string) => {
    peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
      { headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000 });
    peer.on('error', () => {}); await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
  };
  await openSender(alice.lease.token);
  let outputToken = alice.lease.outputToken!, reportSequence = 0;
  let conflicted: Awaited<ReturnType<typeof f.start>> | undefined;
  let replacement: typeof alice.lease | undefined;
  const sourceId = String(f.store.database.prepare('SELECT source_id FROM ad_sources WHERE owner = ?').get('alice')!.source_id);
  const selection = () => f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?').get(sourceId);
  type Started = Awaited<ReturnType<typeof f.start>>;
  let stale: { old: Started; current?: Started; serverCancelled: boolean } | undefined;
  let gamingReturn: { old: typeof bob.lease; oldPeer: WebSocket; request?: Started;
    lease?: typeof bob.lease; gamePeer?: WebSocket } | undefined;
  let collection: { requests: Started[]; gamer: WebSocket; loser?: Started;
    lease?: typeof alice.lease; sender?: WebSocket; lastPollAt: number; sequence: number; before: string } | undefined;
  let senderReapproval: { original: typeof alice.lease; gamingRequest: Started;
    game?: typeof bob.lease; gamePeer?: WebSocket; newRequest?: Started;
    sender?: typeof alice.lease; senderPeer?: WebSocket } | undefined;
  const unaffected = () => JSON.stringify({ alice: f.store.connections('alice'), selection: selection(),
    aliceGrant: f.grants.load('alice'), bobGrant: f.grants.load('bob'), starts: f.startedWith.length });
  const openCollectionPeer = async (token: string) => {
    const socket = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
      { headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000 });
    collectionPeers.push(socket); socket.on('error', () => {});
    await once(socket, 'message', { signal: AbortSignal.timeout(3000) }); return socket;
  };
  const collect = (request: Started) => f.native(`/display/login/${request.id}`, 'ChatView-Login', request.verifier);
  const waitCollectionCadence = async () => {
    assert(collection);
    await new Promise<void>(resolve => setTimeout(resolve,
      Math.max(0, LOGIN_POLL_MS - (performance.now() - collection!.lastPollAt))));
  };
  reply({ ready: true, origin: f.origin });
  const input = createInterface({ input: process.stdin });
  let sequence = 0;
  for await (const line of input) {
    assert(line.length <= 512, 'bounded parent command');
    const command = JSON.parse(line) as { op: string };
    if (command.op === 'close') { reply({ ok: true }); break; }
    if (command.op === 'expire-browser') { offset += 3_600_001; reply({ ok: true }); continue; }
    if (command.op === 'expire-confirmation') { offset += 300_001; reply({ ok: true }); continue; }
    if (command.op === 'metadata') { reply({ metadata }); continue; }
    // A preexisting Alice sender and selected test banner must outlive a
    // revoked/reapproved gaming peer. Browser forms, not fixture commands,
    // perform every role revocation and user consent action.
    if (command.op === 'game-role-start') {
      assert(!gamingReturn);
      const game = await f.approve(await f.start('gaming'), 'gaming', alice.b);
      // Preexisting game approval is issued using Alice's confirmed browser,
      // not a new provider reauthorization that could rotate durable grants.
      gamingReturn = { old: game, oldPeer: await openCollectionPeer(game.token) };
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
      reply({ connectionId: game.membership.connectionId }); continue;
    }
    if (command.op === 'game-role-revoked') {
      assert(gamingReturn && !gamingReturn.request);
      if (gamingReturn.oldPeer.readyState !== WebSocket.CLOSED)
        await once(gamingReturn.oldPeer, 'close', { signal: AbortSignal.timeout(3000) });
      assert.equal(f.store.find(gamingReturn.old.sessionToken), undefined);
      assert(f.store.find(alice.lease.sessionToken) && peer?.readyState === WebSocket.OPEN);
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
      reply({ ok: true }); continue;
    }
    if (command.op === 'game-role-new') {
      assert(gamingReturn && !gamingReturn.request);
      gamingReturn.request = await f.start('gaming');
      assert.equal(f.app.login.view(gamingReturn.request.id).role, 'gaming');
      reply({ path: gamingReturn.request.verificationPath,
        code: gamingReturn.request.id.slice(-6).toUpperCase() }); continue;
    }
    if (command.op === 'game-role-collect') {
      assert(gamingReturn?.request && !gamingReturn.lease);
      const response = await f.native(`/display/login/${gamingReturn.request.id}`,
        'ChatView-Login', gamingReturn.request.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as { status: 'approved'; lease: typeof bob.lease };
      assert.equal(result.status, 'approved');
      gamingReturn.lease = result.lease;
      assert.equal(gamingReturn.lease.membership.role, 'gaming');
      assert.notEqual(gamingReturn.lease.membership.connectionId, gamingReturn.old.membership.connectionId);
      assert.equal(gamingReturn.lease.membership.broadcastSessionId, alice.lease.membership.broadcastSessionId);
      gamingReturn.gamePeer = await openCollectionPeer(gamingReturn.lease.token);
      reply({ connectionId: gamingReturn.lease.membership.connectionId }); continue;
    }
    if (command.op === 'game-role-offline-check') {
      assert(gamingReturn?.lease && gamingReturn.gamePeer?.readyState === WebSocket.OPEN);
      assert(peer?.readyState === WebSocket.CLOSED && f.store.find(alice.lease.sessionToken));
      assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 0);
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId,
        'sender disconnect never changes the selected campaign FK');
      const state = await f.request(`/public/ads/${sourceId}/state`);
      assert.equal(state.status, 200);
      assert.deepEqual(await state.json(), { type: 'ad-snapshot', version: 1,
        mode: 'test', state: 'hidden', expiresInMs: 0 });
      const controller = new AbortController();
      const messages = on(gamingReturn.gamePeer, 'message',
        { signal: AbortSignal.any([controller.signal, AbortSignal.timeout(3000)]) });
      try {
        const marker = 'gaming-chat-while-sender-offline';
        f.chats.get('alice')!.publish(marker);
        let received = false;
        for await (const [frame] of messages) if (String(frame).includes(marker)) { received = true; break; }
        assert(received, 'gaming HUD still gets creator chat while the sender is offline');
      } finally { controller.abort(); }
      reply({ ok: true }); continue;
    }
    if (command.op === 'game-role-sender-returned') {
      assert(gamingReturn?.lease && gamingReturn.gamePeer?.readyState === WebSocket.OPEN);
      assert(peer?.readyState === WebSocket.OPEN);
      const account = f.creators.describe('alice');
      assert.equal(account.presence.gamingConnections, 1);
      assert.equal(account.presence.streamingConnections, 1);
      assert.equal(account.output.state, 'unknown',
        'a same-approval streaming return is not a new valid output report');
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId);
      const state = await f.request(`/public/ads/${sourceId}/state`);
      assert.deepEqual(await state.json(), { type: 'ad-snapshot', version: 1,
        mode: 'test', state: 'hidden', expiresInMs: 0 });
      reply({ ok: true }); continue;
    }
    if (command.op === 'game-role-sender-restored') {
      assert(gamingReturn?.lease && gamingReturn.gamePeer?.readyState === WebSocket.OPEN);
      assert(peer?.readyState === WebSocket.OPEN && f.store.find(alice.lease.sessionToken));
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId);
      assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
      assert.equal(f.creators.describe('alice').output.state, 'reported');
      const state = await f.request(`/public/ads/${sourceId}/state`);
      assert.equal((await state.json() as { state: string }).state, 'visible',
        'only the returned sender new output report restores the prior selected banner');
      reply({ ok: true }); continue;
    }
    if (command.op === 'game-role-check') {
      assert(gamingReturn?.lease && gamingReturn.gamePeer?.readyState === WebSocket.OPEN);
      assert(peer?.readyState === WebSocket.OPEN && f.store.find(alice.lease.sessionToken));
      assert.equal(f.store.find(gamingReturn.lease.sessionToken)?.owner, 'alice');
      assert.equal(selection()?.connection_id, alice.lease.membership.connectionId);
      assert(f.store.find(bob.lease.sessionToken));
      assert.equal(f.startedWith.length, starts, 'game-only reapproval does not restart either creator upstream');
      assert.equal(f.refreshCalls, 0, 'game-only reapproval never renews the sender');
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
      assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
      const senderReceived = once(peer, 'message', { signal: AbortSignal.timeout(3000) });
      const gameReceived = once(gamingReturn.gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
      const marker = 'game-role-reapproved-and-sender-kept';
      f.chats.get('alice')!.publish(marker);
      assert(String((await senderReceived)[0]).includes(marker) &&
        String((await gameReceived)[0]).includes(marker));
      reply({ ok: true }); continue;
    }
    if (command.op === 'game-role-released') {
      assert(gamingReturn?.lease && gamingReturn.gamePeer);
      if (gamingReturn.gamePeer.readyState !== WebSocket.CLOSED)
        await once(gamingReturn.gamePeer, 'close', { signal: AbortSignal.timeout(3000) });
      assert.equal(f.store.find(gamingReturn.lease.sessionToken), undefined);
      assert.equal(snapshot(), before, 'game-only reapproval and revocation leave original sender/banner untouched');
      assert(peer?.readyState === WebSocket.OPEN);
      gamingReturn = undefined;
      reply({ ok: true }); continue;
    }
    // Explicit parent actions stand in for the sender, never for browser JS.
    // The production management GET cannot invoke any of these operations.
    if (command.op === 'sender-disconnect') {
      assert(peer?.readyState === WebSocket.OPEN);
      const closed = once(peer, 'close', { signal: AbortSignal.timeout(3000) });
      peer.close(); await closed;
      const due = realNow() + 2000;
      while (f.creators.describe('alice').presence.streamingConnections !== 0) {
        assert(realNow() < due, 'server observes sender socket close');
        await new Promise<void>(resolve => setTimeout(resolve, 25));
      }
      reply({ ok: true }); continue;
    }
    if (command.op === 'sender-return') {
      assert(peer?.readyState === WebSocket.CLOSED);
      const response = await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken, 'streaming');
      assert.equal(response.status, 200);
      const lease = await response.json() as typeof alice.lease;
      assert.deepEqual(lease.membership, alice.lease.membership);
      assert(lease.outputToken); outputToken = lease.outputToken; reportSequence = 0;
      await openSender(lease.token);
      reply({ ok: true }); continue;
    }
    if (command.op === 'sender-report') {
      assert(peer?.readyState === WebSocket.OPEN);
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${outputToken}`, 'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence: ++reportSequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200);
      reply({ ok: true }); continue;
    }
    if (command.op === 'expire-output-report') { outputNow += 15000; reply({ ok: true }); continue; }
    if (command.op === 'native-start') {
      assert(!pending); pending = await f.start('streaming');
      reply({ path: pending.verificationPath }); continue;
    }
    if (command.op === 'native-collect') {
      assert(pending);
      const response = await f.native(`/display/login/${pending.id}`, 'ChatView-Login', pending.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as { status: string; lease: { sessionToken: string } };
      assert.equal(result.status, 'approved');
      const session = f.store.find(result.lease.sessionToken);
      assert(session?.owner === 'charlie' && session.membership?.role === 'streaming');
      reply({ ok: true }); continue;
    }
    // The native client has already stopped: only this bounded parent fixture
    // can simulate the lost cancellation response. The Chromium page is real.
    if (command.op === 'stale-start') {
      assert(!stale);
      stale = { old: await f.start('gaming'), serverCancelled: false };
      reply({ path: stale.old.verificationPath, code: stale.old.id.slice(-6).toUpperCase() }); continue;
    }
    if (command.op === 'stale-cancel-response-lost') {
      assert(stale && !stale.current && !stale.serverCancelled);
      const result = await f.native(`/display/login/${stale.old.id}/cancel`,
        'ChatView-Login', stale.old.verifier);
      assert.equal(result.status, 200);
      assert.deepEqual(await result.json(), { cancelled: true });
      stale.serverCancelled = true; // Discard response in the simulated native caller.
      reply({ ok: true }); continue;
    }
    if (command.op === 'stale-new-start') {
      assert(stale && !stale.current);
      stale.current = await f.start('gaming');
      assert.notEqual(stale.current.id, stale.old.id);
      reply({ path: stale.current.verificationPath, code: stale.current.id.slice(-6).toUpperCase() }); continue;
    }
    if (command.op === 'stale-check') {
      assert(stale?.current);
      assert.throws(() => f.app.login.view(stale!.old.id), (error: unknown) =>
        error instanceof Error && 'status' in error &&
        (error.status === (stale!.serverCancelled ? 410 : 409)),
        'old request is terminal only by authenticated cancel or explicit browser denial');
      assert.equal(f.app.login.view(stale.current.id).role, 'gaming');
      assert.equal(snapshot(), before, 'no creator, original PC or ad selection changed');
      reply({ ok: true }); continue;
    }
    if (command.op === 'stale-collect') {
      assert(stale?.current);
      const response = await f.native(`/display/login/${stale.current.id}`,
        'ChatView-Login', stale.current.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as { status: string; lease: { sessionToken: string; membership?: { role: string } } };
      assert.equal(result.status, 'approved');
      const session = f.store.find(result.lease.sessionToken);
      assert(session?.owner === 'charlie' && session.membership?.role === 'gaming');
      assert.equal(snapshot(), before, 'fresh consent never changes another sender or ad selection');
      stale = undefined;
      reply({ ok: true }); continue;
    }
    // This final flow deliberately replaces only Alice's sender via browser
    // CSRF forms. Parent commands never revoke a connection or select an ad.
    if (command.op === 'conflict-start') {
      assert(!conflicted && !replacement); conflicted = await f.start('streaming');
      reply({ path: conflicted.verificationPath, connectionId: alice.lease.membership.connectionId }); continue;
    }
    if (command.op === 'conflict-cancelled') {
      assert(conflicted);
      const result = await f.native(`/display/login/${conflicted.id}`, 'ChatView-Login', conflicted.verifier);
      assert.equal(result.status, 403); conflicted = undefined;
      assert.equal(snapshot(), before, 'cancel preserves the existing sender and campaign');
      reply({ ok: true }); continue;
    }
    if (command.op === 'conflict-released') {
      assert(conflicted); assert.equal(f.app.login.view(conflicted.id).role, 'streaming');
      assert.equal(f.store.find(alice.lease.sessionToken), undefined);
      assert.equal(selection(), undefined); assert(f.store.find(bob.lease.sessionToken));
      assert(peer);
      if (peer.readyState !== WebSocket.CLOSED) await once(peer, 'close', { signal: AbortSignal.timeout(3000) });
      reply({ ok: true }); continue;
    }
    if (command.op === 'conflict-collect') {
      assert(conflicted && !replacement);
      const response = await f.native(`/display/login/${conflicted.id}`, 'ChatView-Login', conflicted.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as { status: string; lease: typeof alice.lease };
      assert.equal(result.status, 'approved'); replacement = result.lease; conflicted = undefined;
      const session = f.store.find(replacement.sessionToken);
      assert(session?.owner === 'alice' && session.membership?.role === 'streaming');
      assert.notEqual(session.membership.connectionId, alice.lease.membership.connectionId);
      assert.equal(session.membership.broadcastSessionId, alice.lease.membership.broadcastSessionId);
      assert.equal(selection(), undefined, 'explicit role approval does not reselect the campaign');
      assert(replacement.outputToken); outputToken = replacement.outputToken; reportSequence = 0;
      await openSender(replacement.token);
      reply({ ok: true }); continue;
    }
    if (command.op === 'conflict-selected') {
      assert(replacement && peer?.readyState === WebSocket.OPEN);
      assert.equal(selection()?.connection_id, replacement.membership.connectionId);
      assert.equal(f.store.connections('alice').length, 1);
      assert(f.store.find(bob.lease.sessionToken));
      assert.equal(f.startedWith.filter(token => !token.includes(':charlie:')).length, starts);
      assert.equal(f.refreshCalls, 0);
      reply({ ok: true }); continue;
    }
    // Bob's previously empty sender slot is raced by two real app polls.
    // Only browser forms consent/revoke/select; stdio never bypasses those actions.
    if (command.op === 'collection-start') {
      assert(!collection && replacement);
      collection = { requests: [await f.start('streaming'), await f.start('streaming')],
        gamer: await openCollectionPeer(bob.lease.token), lastPollAt: -Infinity, sequence: 0, before: unaffected() };
      reply({ paths: collection.requests.map(r => r.verificationPath) }); continue;
    }
    if (command.op === 'collection-race') {
      assert(collection && !collection.lease);
      assert.equal(f.store.connections('bob').filter(c => c.role === 'streaming').length, 0);
      const responses = await Promise.all(collection.requests.map(collect));
      type Result = { status: 'pending' } | { status: 'approved'; lease: typeof alice.lease };
      const results: Result[] = [];
      for (const response of responses) { assert.equal(response.status, 200); results.push(await response.json() as Result); }
      assert.equal(results.filter(r => r.status === 'approved').length, 1);
      assert.equal(results.filter(r => r.status === 'pending').length, 1);
      const index = results.findIndex(r => r.status === 'pending');
      const winner = results[1 - index]!; assert(winner.status === 'approved');
      collection.loser = collection.requests[index]!; collection.lease = winner.lease;
      collection.lastPollAt = performance.now();
      assert.equal(f.app.login.collectionState(collection.loser.id, 'bob'), 'retry-required');
      assert.equal(f.store.connections('bob').length, 2);
      collection.sender = await openCollectionPeer(winner.lease.token);
      reply({ loserIndex: index, connectionId: winner.lease.membership.connectionId }); continue;
    }
    if (command.op === 'collection-empty') {
      assert(collection?.loser && collection.lease && collection.sender);
      assert.equal(f.store.find(collection.lease.sessionToken), undefined);
      if (collection.sender.readyState !== WebSocket.CLOSED)
        await once(collection.sender, 'close', { signal: AbortSignal.timeout(3000) });
      await waitCollectionCadence();
      const response = await collect(collection.loser); assert.equal(response.status, 200);
      assert.deepEqual(await response.json(), { status: 'pending' }); collection.lastPollAt = performance.now();
      assert.equal(f.store.connections('bob').length, 1, 'a free role never replays the failed consent');
      assert.equal(unaffected(), collection.before);
      reply({ ok: true }); continue;
    }
    if (command.op === 'collection-recollect') {
      assert(collection?.loser && collection.lease);
      const previous = collection.lease;
      await waitCollectionCadence();
      const response = await collect(collection.loser); assert.equal(response.status, 200);
      const result = await response.json() as { status: string; lease: typeof alice.lease };
      assert.equal(result.status, 'approved'); collection.lease = result.lease; collection.sequence = 0;
      assert.notEqual(collection.lease.membership.connectionId, previous.membership.connectionId);
      assert.equal(f.store.find(collection.lease.sessionToken)?.owner, 'bob');
      assert.equal(f.store.database.prepare(`SELECT 1 FROM ad_selections s JOIN ad_sources a USING(source_id)
        WHERE a.owner = ?`).get('bob'), undefined, 'new approval cannot restore the removed selection');
      collection.sender = await openCollectionPeer(collection.lease.token);
      assert.equal((await collect(collection.loser)).status, 410);
      reply({ ok: true }); continue;
    }
    if (command.op === 'collection-report') {
      assert(collection?.lease?.outputToken && collection.sender?.readyState === WebSocket.OPEN);
      outputNow += 500;
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${collection.lease.outputToken}`, 'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence: ++collection.sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200); reply({ ok: true }); continue;
    }
    if (command.op === 'collection-check') {
      assert(collection?.lease && collection.gamer.readyState === WebSocket.OPEN);
      assert.equal(unaffected(), collection.before); assert(f.store.find(bob.lease.sessionToken));
      const row = f.store.database.prepare(`SELECT connection_id FROM ad_selections s JOIN ad_sources a USING(source_id)
        WHERE a.owner = ?`).get('bob');
      assert.equal(row?.connection_id, collection.lease.membership.connectionId);
      const controller = new AbortController();
      const messages = on(collection.gamer, 'message', { signal: AbortSignal.any([controller.signal, AbortSignal.timeout(3000)]) });
      try {
        const marker = 'collection-race-game-chat'; f.chats.get('bob')!.publish(marker);
        let received = false;
        for await (const [frame] of messages) if (String(frame).includes(marker)) { received = true; break; }
        assert(received);
      } finally { controller.abort(); }
      reply({ ok: true }); continue;
    }
    // Final browser flow: explicitly revoke Alice's streaming approval, keep
    // her gaming chat connected, reapprove streaming using the browser form
    // and manually reselect the same public OBS URL. Parent commands represent
    // only the two native request/collect operations and a fresh report.
    if (command.op === 'sender-reapprove-prepare') {
      assert(replacement && peer?.readyState === WebSocket.OPEN && !senderReapproval);
      assert.equal(selection()?.connection_id, replacement.membership.connectionId);
      senderReapproval = { original: replacement, gamingRequest: await f.start('gaming') };
      outputNow += 500;
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${outputToken}`, 'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence: ++reportSequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200);
      assert.equal((await (await f.request(`/public/ads/${sourceId}/state`)).json() as {state:string}).state, 'visible');
      reply({ path: senderReapproval.gamingRequest.verificationPath,
        code: senderReapproval.gamingRequest.id.slice(-6).toUpperCase(),
        senderConnectionId: replacement.membership.connectionId }); continue;
    }
    if (command.op === 'sender-reapprove-game-collected') {
      assert(senderReapproval && !senderReapproval.game);
      const request = senderReapproval.gamingRequest;
      const response = await f.native(`/display/login/${request.id}`, 'ChatView-Login', request.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as {status:string;lease:typeof bob.lease};
      assert.equal(result.status, 'approved'); senderReapproval.game = result.lease;
      assert.equal(result.lease.membership.role, 'gaming');
      senderReapproval.gamePeer = await openCollectionPeer(result.lease.token);
      reply({ gameConnectionId: result.lease.membership.connectionId }); continue;
    }
    if (command.op === 'sender-reapprove-revoked') {
      assert(senderReapproval?.game && senderReapproval.gamePeer?.readyState === WebSocket.OPEN);
      assert(f.store.find(senderReapproval.original.sessionToken) === undefined);
      assert(peer);
      if (peer.readyState !== WebSocket.CLOSED)
        await once(peer, 'close', { signal: AbortSignal.timeout(3000) });
      assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 0);
      assert.equal(selection(), undefined);
      assert.equal((await (await f.request(`/public/ads/${sourceId}/state`)).json() as {state:string}).state, 'hidden');
      assert(f.store.find(bob.lease.sessionToken), 'other creator gaming authority remains intact');
      const marker = 'alice-game-chat-after-sender-revoked';
      const message = once(senderReapproval.gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
      f.chats.get('alice')!.publish(marker);
      assert(String((await message)[0]).includes(marker));
      reply({ok:true}); continue;
    }
    if (command.op === 'sender-reapprove-request') {
      assert(senderReapproval?.game && !senderReapproval.newRequest);
      senderReapproval.newRequest = await f.start('streaming');
      assert.equal(f.app.login.view(senderReapproval.newRequest.id).role, 'streaming');
      reply({ path: senderReapproval.newRequest.verificationPath,
        code: senderReapproval.newRequest.id.slice(-6).toUpperCase() }); continue;
    }
    if (command.op === 'sender-reapprove-collected') {
      assert(senderReapproval?.newRequest && !senderReapproval.sender);
      const req = senderReapproval.newRequest;
      const response = await f.native(`/display/login/${req.id}`, 'ChatView-Login', req.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as {status:string;lease:typeof alice.lease};
      assert.equal(result.status, 'approved'); senderReapproval.sender = result.lease;
      assert.equal(result.lease.membership.role, 'streaming');
      assert.notEqual(result.lease.membership.connectionId, senderReapproval.original.membership.connectionId);
      assert.equal(result.lease.membership.broadcastSessionId, senderReapproval.game.membership.broadcastSessionId);
      assert.equal(selection(), undefined, 'fresh role approval cannot reselect an expired FK');
      senderReapproval.senderPeer = await openCollectionPeer(result.lease.token);
      reply({ senderConnectionId: result.lease.membership.connectionId }); continue;
    }
    if (command.op === 'sender-reapprove-report') {
      assert(senderReapproval?.sender?.outputToken && senderReapproval.senderPeer?.readyState === WebSocket.OPEN);
      outputNow += 500;
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${senderReapproval.sender.outputToken}`,
        'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200);
      assert.equal(selection(), undefined, 'new output report is not consent to the old campaign');
      assert.equal((await (await f.request(`/public/ads/${sourceId}/state`)).json() as {state:string}).state, 'hidden');
      reply({ok:true}); continue;
    }
    if (command.op === 'sender-reapprove-confirmed') {
      assert(senderReapproval?.game && senderReapproval.gamePeer?.readyState === WebSocket.OPEN);
      assert(senderReapproval.sender && senderReapproval.senderPeer?.readyState === WebSocket.OPEN);
      assert.equal(selection()?.connection_id, senderReapproval.sender.membership.connectionId);
      assert.equal(f.store.find(senderReapproval.game.sessionToken)?.owner, 'alice');
      assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
      assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
      assert.equal((await (await f.request(`/public/ads/${sourceId}/state`)).json() as {state:string}).state, 'visible');
      assert.equal(f.startedWith.filter(token => !token.includes(':charlie:')).length, starts,
        'role-only replacement cannot rotate creator/provider upstream');
      assert.equal(f.refreshCalls, 0);
      assert(f.store.find(bob.lease.sessionToken));
      const gameReceived = once(senderReapproval.gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
      const senderReceived = once(senderReapproval.senderPeer, 'message', { signal: AbortSignal.timeout(3000) });
      const marker = 'alice-gaming-preserved-on-sender-reapproval';
      f.chats.get('alice')!.publish(marker);
      assert(String((await gameReceived)[0]).includes(marker) && String((await senderReceived)[0]).includes(marker));
      reply({ok:true}); continue;
    }
    assert.equal(command.op, 'check');
    assert(snapshot() === before, 'browser actions must preserve native approvals, grants and selection');
    assert.equal(f.startedWith.filter(token => !token.includes(':charlie:')).length, starts);
    assert.equal(f.refreshCalls, 0);
    assert(peer && peer.readyState === WebSocket.OPEN);
    const marker = `browser-fixture-${++sequence}`;
    f.chats.get('alice')!.publish(marker);
    for (;;) {
      const [frame] = await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
      if (String(frame).includes(marker)) break;
    }
    reply({ ok: true });
  }
} catch {
  // Assertion objects can contain fixture credentials. Do not export them.
  reply({ error: 'Management browser fixture invariant failed' });
  process.exitCode = 1;
} finally {
  clearTimeout(deadline); for (const socket of collectionPeers) socket.terminate(); peer?.terminate(); await f.close(); Date.now = realNow;
}
