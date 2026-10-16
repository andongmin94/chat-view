// SPDX-License-Identifier: GPL-2.0-or-later
// A stale management tab must not undo another browser's campaign decision.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { fixture } from './fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const selectPath = '/campaigns/' + TEST_CAMPAIGN.id + '/select';
const selected = (f: Awaited<ReturnType<typeof fixture>>, owner: string) =>
  f.store.database.prepare('SELECT s.connection_id FROM ad_sources a JOIN ad_selections s USING(source_id) WHERE a.owner = ?')
    .get(owner)?.connection_id;
const stale = async (response: Response, reason?: 'stale-page' | 'sender-missing' | 'sender-changed') => {
  assert.equal(response.status, 409);
  assert.match(response.headers.get('content-type') ?? '', /^text\/html/u);
  const html = await response.text();
  assert.match(html, /data-campaign-action="review-required"/u);
  if (reason) assert.match(html, new RegExp('data-campaign-review-reason="' + reason + '"', 'u'));
  assert.match(html, /이전 페이지의 선택·중지 요청을 실행하지 않았습니다/u);
  assert.match(html, /현재 관리 채널과 송출 역할 승인/u);
  assert.match(html, /href="\/campaigns"/u);
  assert.doesNotMatch(html, /<script|http-equiv="refresh"/u);
};

test('two management browsers cannot replay a stale select or stop after the other changes it', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const second = await f.connect('alice', 'gaming');
    const other = await f.connect('bob', 'gaming');
    const streamingId = sender.lease.membership.connectionId;
    const ws = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
      { headers: { Authorization: 'Bearer ' + sender.lease.token }, handshakeTimeout: 3000 });
    ws.on('error', () => {});
    peers.push(ws);
    await once(ws, 'message', { signal: AbortSignal.timeout(3000) });
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(selectPath, sender.b)).status, 303);
    const html = await f.page('/campaigns', sender.b);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(html)?.[1]; assert(source);
    const state = async () => (await f.request('/public/ads/' + source + '/state')).json();
    assert.equal((await f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + sender.lease.outputToken,
      'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) })).status, 200);
    assert.equal((await state() as { state: string }).state, 'visible');

    await f.page('/campaigns', second.b); // Both real browser sessions reviewed the existing selection.
    assert.equal((await f.post('/campaigns/stop', second.b)).status, 303);
    assert.equal(selected(f, 'alice'), undefined);
    assert.deepEqual(await state(), HIDDEN_AD);
    const afterStop = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
    await stale(await f.post(selectPath, sender.b));
    assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, afterStop);
    assert.equal(selected(f, 'alice'), undefined);

    await f.page('/campaigns', sender.b); // Only a freshly reviewed page may select again.
    assert.equal((await f.post(selectPath, sender.b)).status, 303);
    assert.equal(selected(f, 'alice'), streamingId);
    assert.equal((await state() as { state: string }).state, 'visible');
    const afterSelect = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
    await stale(await f.post('/campaigns/stop', second.b));
    assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, afterSelect);
    assert.equal(selected(f, 'alice'), streamingId);

    await f.page('/campaigns', second.b);
    assert.equal((await f.post('/campaigns/stop', second.b)).status, 303);
    assert.equal(selected(f, 'alice'), undefined);
    assert.deepEqual(await state(), HIDDEN_AD);
    assert.match(await f.page('/campaigns', sender.b), new RegExp(source, 'u'));
    assert.equal(f.store.find(sender.lease.sessionToken)?.membership?.connectionId, streamingId);
    assert(f.store.find(other.lease.sessionToken), 'another creator approval remains untouched');

    await f.page('/campaigns', second.b);
    const oldBrowser = { ...second.b };
    assert.equal((await f.post('/account/switch', second.b)).status, 303);
    const replay = await f.request(selectPath, { method: 'POST', headers: {
      Cookie: oldBrowser.cookie, Origin: f.origin, 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: new URLSearchParams({ csrf: oldBrowser.csrf }) });
    assert.equal(replay.status, 401, 'a switched management cookie cannot replay prior owner consent');
    assert.equal(selected(f, 'alice'), undefined);
    assert.match(await f.page('/campaigns', sender.b), /channel &lt;alice&gt;/u);
  } finally { for (const ws of peers) ws.terminate(); await f.close(); }
});

test('replacing a sender approval invalidates an older campaign form even with the same owner CSRF', async () => {
  const f = await fixture();
  try {
    const sender = await f.connect('alice', 'streaming');
    const owner = sender.b;
    await f.page('/campaigns', owner);
    assert.equal((await f.post(selectPath, owner)).status, 303);
    const former = await f.page('/campaigns', owner);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(former)?.[1]; assert(source);
    assert.equal((await f.post('/connections/' + sender.lease.membership.connectionId + '/revoke', owner)).status, 303);
    assert.equal(selected(f, 'alice'), undefined);
    const replacement = await f.approve(await f.start('streaming'), 'streaming', owner);
    assert.notEqual(replacement.membership.connectionId, sender.lease.membership.connectionId);

    const before = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
    await stale(await f.post(selectPath, owner), 'sender-changed');
    assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, before);
    assert.equal(selected(f, 'alice'), undefined);
    const fresh = await f.page('/campaigns', owner);
    assert.match(fresh, /data-campaign-selection-state="ready"/u);
    assert.match(fresh, /data-banner-manual-reselect="ready"/u);
    assert.equal((await f.post(selectPath, owner)).status, 303);
    assert.equal(selected(f, 'alice'), replacement.membership.connectionId);
    assert.match(await f.page('/campaigns', owner), new RegExp(source, 'u'));
  } finally { await f.close(); }
});

test('one browser back/older tab cannot use a prior campaign form after a newer read', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const game = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const bob = await f.connect('bob', 'gaming');
    for (const token of [sender.lease.token, game.token, bob.lease.token]) {
      const ws = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
        { headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      ws.on('error', () => {}); peers.push(ws);
      await once(ws, 'message', { signal: AbortSignal.timeout(3000) });
    }
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(selectPath, sender.b)).status, 303);
    const oldPage = await f.page('/campaigns', sender.b);
    const oldView = /name="view" value="([a-f0-9]{64})"/u.exec(oldPage)?.[1]; assert(oldView);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(oldPage)?.[1]; assert(source);
    const senderId = sender.lease.membership.connectionId;
    const act = async (path: string, view: string, csrf = sender.b.csrf, origin = f.origin) =>
      f.request(path, { method: 'POST', headers: { Cookie: sender.b.cookie,
        Origin: origin, 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf, view }) });
    // Returning via browser history is read-only; another tab viewing the same
    // account must invalidate the old page-specific consent even if nothing
    // about the streamer grant or selected campaign changed.
    await f.page('/account', sender.b);
    const refreshed = await f.page('/campaigns', sender.b);
    const newView = /name="view" value="([a-f0-9]{64})"/u.exec(refreshed)?.[1];
    assert(newView); assert.notEqual(oldView, newView);
    const before = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
    await stale(await act('/campaigns/stop', oldView), 'stale-page');
    assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, before);
    assert.equal(selected(f, 'alice'), senderId);
    assert.equal(f.store.find(game.sessionToken)?.membership?.role, 'gaming');
    assert(f.store.find(bob.lease.sessionToken));
    assert.equal((await f.request('/public/ads/' + source + '/state')).status, 200);
    // A wrong/missing CSRF or wrong Origin cannot turn the extra page marker
    // into a bypass of the existing browser protections.
    assert.equal((await act('/campaigns/stop', newView, 'a'.repeat(64))).status, 403);
    assert.equal((await act('/campaigns/stop', newView, sender.b.csrf, 'https://evil.invalid')).status, 403);
    assert.equal((await act('/campaigns/stop', 'not-a-nonce')).status, 403);
    assert.equal(selected(f, 'alice'), senderId);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post('/campaigns/stop', sender.b)).status, 303,
      'the user may explicitly stop after a fresh review');
    assert.equal(selected(f, 'alice'), undefined);
    assert.match(await f.page('/campaigns', sender.b), new RegExp(source, 'u'),
      'old public URL remains available after an explicit Stop');
  } finally { for (const ws of peers) ws.terminate(); await f.close(); }
});

test('revoked sender has its own review-required explanation, distinct from old browser history', async () => {
  const f = await fixture();
  try {
    const sender = await f.connect('alice', 'streaming');
    const gaming = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(selectPath, sender.b)).status, 303);
    const before = await f.page('/campaigns', sender.b);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(before)?.[1]; assert(source);
    const revoked = '/connections/' + sender.lease.membership.connectionId + '/revoke';
    assert.equal((await f.post(revoked, sender.b)).status, 303);
    assert.equal(selected(f, 'alice'), undefined);
    const snapshot = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
    await stale(await f.post(selectPath, sender.b), 'sender-missing');
    assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, snapshot);
    assert.equal(f.store.find(gaming.sessionToken)?.membership?.role, 'gaming');
    const current = await f.page('/campaigns', sender.b);
    assert.match(current, /data-campaign-selection-state="streaming-required"/u);
    assert.match(current, /data-banner-manual-reselect="sender-required"/u);
    assert.match(current, new RegExp(source, 'u'));
    assert.equal((await f.request('/public/ads/' + source + '/state')).status, 200);
    assert.equal(selected(f, 'alice'), undefined, 'the read and rejected action never reselect an ad');
  } finally { await f.close(); }
});

test('same-browser tabs across sender outage keep gaming chat, selection and URL; only the latest reviewed form may act', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const gamer = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const other = await f.connect('bob', 'gaming');
    const open = async (token: string) => {
      const ws = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
        { headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      ws.on('error', () => {}); peers.push(ws);
      await once(ws, 'message', { signal: AbortSignal.timeout(3000) });
      return ws;
    };
    const senderPeer = await open(sender.lease.token);
    const gamePeer = await open(gamer.token);
    await open(other.lease.token);
    const senderId = sender.lease.membership.connectionId;
    const selectPath = '/campaigns/' + TEST_CAMPAIGN.id + '/select';
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(selectPath, sender.b)).status, 303);
    const initial = await f.page('/campaigns', sender.b);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(initial)?.[1]; assert(source);
    const url = f.origin + '/public/ads/' + source;
    const state = async () => (await f.request('/public/ads/' + source + '/state')).json();
    const report = (token: string) => f.request('/broadcast/output', {
      method: 'POST', headers: { Authorization: 'ChatView-Output ' + token,
        'Content-Type': 'application/json' },
      body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }),
    });
    assert.equal((await report(sender.lease.outputToken!)).status, 200);
    assert.equal((await state() as { state: string }).state, 'visible');
    const reviewed = async () => {
      const html = await f.page('/campaigns', sender.b);
      assert(html.includes(url), 'original public URL is retained on every read');
      assert(sender.b.view);
      return { html, view: sender.b.view };
    };
    const tabPost = (path: string, view: string) => f.request(path, {
      method: 'POST', headers: { Cookie: sender.b.cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf: sender.b.csrf, view }),
    });
    const selectionUnchanged = () =>
      assert.equal(selected(f, 'alice'), senderId, 'the selected campaign remains bound to the same streaming approval');
    const changes = () => f.store.database.prepare('SELECT total_changes() AS n').get()!.n;

    // Two real HTTP page reads share the SAME authenticated browser cookie.
    // The second one is current; the first holds an earlier form in another tab.
    const first = await reviewed(), second = await reviewed();
    assert.notEqual(first.view, second.view);
    selectionUnchanged();
    const disconnect = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    senderPeer.close(); await disconnect;
    const offline = await reviewed();
    assert.match(offline.html, /data-banner-recovery-state="streaming-disconnected"/u);
    assert.match(offline.html, /캠페인 재선택이나 URL 교체는 필요하지 않습니다/u);
    assert.equal(f.creators.describe('alice').presence.streamingConnections, 0);
    assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
    assert.deepEqual(await state(), HIDDEN_AD, 'the prior report cannot remain valid after socket loss');
    assert.equal(f.creators.describe('alice').output.state, 'unknown');
    const beforeOffline = changes();
    await stale(await tabPost(selectPath, first.view), 'stale-page');
    assert.equal(changes(), beforeOffline, 'old selected form never writes while streaming is offline');
    selectionUnchanged();
    const gameChat = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('game chat during manager-tab sender outage');
    assert.match(String((await gameChat)[0]), /game chat during manager-tab sender outage/u);
    assert.equal(f.store.find(gamer.sessionToken)?.membership?.role, 'gaming');
    assert(f.store.find(other.lease.sessionToken), 'another creator stays authorized');

    // Explicit return preserves the original streaming connection ID but
    // rotates short display/output tokens. An old report is never replayed.
    const renewed = await f.native('/display/refresh', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming');
    assert.equal(renewed.status, 200);
    const returned = await renewed.json() as typeof sender.lease;
    assert.deepEqual(returned.membership, sender.lease.membership);
    const returnedPeer = await open(returned.token);
    const waiting = await reviewed();
    assert.match(waiting.html, /data-banner-recovery-state="waiting-report"/u);
    assert.equal(f.creators.describe('alice').presence.streamingConnections, 1);
    assert.deepEqual(await state(), HIDDEN_AD);
    const beforeWaiting = changes();
    await stale(await tabPost('/campaigns/stop', offline.view), 'stale-page');
    assert.equal(changes(), beforeWaiting);
    selectionUnchanged();

    assert.equal((await report(returned.outputToken!)).status, 200);
    assert.equal((await state() as { state: string }).state, 'visible');
    const ready = await reviewed();
    assert.match(ready.html, /data-banner-recovery-state="report-ready"/u);
    const beforeReady = changes();
    await stale(await tabPost('/campaigns/stop', waiting.view), 'stale-page');
    assert.equal(changes(), beforeReady, 'the obsolete form cannot stop a restored public banner');
    selectionUnchanged();

    // An old tab's 409 must NOT invalidate the more recently reviewed tab.
    // These two writes are explicit user actions, not report/GET side effects.
    assert.equal((await f.post('/campaigns/stop', sender.b)).status, 303,
      'the newest management tab retains its valid manual Stop after an old tab was rejected');
    assert.equal(selected(f, 'alice'), undefined);
    assert.deepEqual(await state(), HIDDEN_AD);
    await stale(await tabPost(selectPath, ready.view), 'stale-page');
    assert.equal(selected(f, 'alice'), undefined, 'old Select cannot undo a manually stopped campaign');
    const stopped = await reviewed();
    assert.match(stopped.html, /data-banner-manual-reselect="ready"/u);
    assert.equal((await f.post(selectPath, sender.b)).status, 303,
      'only a fresh explicit selection restores the prior public URL');
    selectionUnchanged();
    assert.equal((await state() as { state: string }).state, 'visible');
    assert((await f.page('/campaigns', sender.b)).includes(url), 'source address is unchanged');
    const response = once(returnedPeer, 'message', { signal: AbortSignal.timeout(3000) });
    const gameResponse = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('game and sender chat survive manual campaign controls');
    assert.match(String((await response)[0]), /game and sender chat survive manual campaign controls/u);
    assert.match(String((await gameResponse)[0]), /game and sender chat survive manual campaign controls/u);
    assert(f.store.find(other.lease.sessionToken), 'other creator was not modified');
  } finally { for (const ws of peers) ws.terminate(); await f.close(); }
});
