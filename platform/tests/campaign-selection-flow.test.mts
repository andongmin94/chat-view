// SPDX-License-Identifier: GPL-2.0-or-later
// Real account HTTP, SQLite and WebSocket paths; provider and OBS reports are
// synthetic. This does not certify native launch, physical video or ad exposure.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once, on } from 'node:events';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import type { CampaignSelectionState } from '../server/ads/pages.mts';
import { fixture } from './fixtures/service.mts';

type Fixture = Awaited<ReturnType<typeof fixture>>;
type Browser = { cookie: string; csrf: string };
const selectPath = `/campaigns/${TEST_CAMPAIGN.id}/select`;
const options = { createDisplay: (access: ConstructorParameters<typeof DisplayGateway>[0],
  origin: () => string, snapshot: () => unknown) => new DisplayGateway(access, origin, snapshot) };
async function screen(f: Fixture, b: Browser, selection: CampaignSelectionState,
  preview: 'not-selected' | 'waiting-report' | 'report-ready') {
  const html = await f.page('/campaigns', b);
  assert(html.includes(`data-campaign-selection-state="${selection}"`), 'current account prerequisite is explicit');
  assert.equal(html.includes(`action="${selectPath}"`), selection === 'ready', 'GET offers only an eligible selection');
  assert(html.includes(`data-public-banner-state="${preview}"`), 'selection and public preparation are separate');
  assert.match(html, /시험 광고 · 지급 없음/u);
  return html;
}
const source = (html: string) => {
  const match = /id="source-url"[^>]*value="([^"]+)"/u.exec(html);
  assert(match, 'existing public source remains available');
  return new URL(match[1]!);
};
async function snapshot(f: Fixture, url: URL) {
  const response = await f.request(`${url.pathname}/state`);
  assert.equal(response.status, 200);
  assert.equal(response.headers.get('cache-control'), 'no-store');
  assert.equal(response.headers.get('set-cookie'), null);
  return await response.json();
}
async function open(f: Fixture, token: string) {
  const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
  });
  peer.on('error', () => {});
  const signal = AbortSignal.timeout(3000);
  // Register both listeners before either event; an open socket alone is not
  // proof that the server accepted its first recipient-specific snapshot.
  try { await Promise.all([once(peer, 'open', { signal }), once(peer, 'message', { signal })]); }
  catch (error) { peer.terminate(); throw error; }
  return peer;
}
async function report(f: Fixture, token: string) {
  const response = await f.request('/broadcast/output', { method: 'POST', headers: {
    Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
  }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
  assert.equal(response.status, 200);
}

test('gaming-only account prepares a streaming role, selects explicitly and reuses its source after revocation',
  { timeout: 15000 }, async () => {
  const f = await fixture(options);
  const peers: WebSocket[] = [];
  try {
    const game = await f.connect('alice', 'gaming'), other = await f.connect('bob', 'streaming');
    const gamePeer = await open(f, game.lease.token); peers.push(gamePeer);
    const otherPeer = await open(f, other.lease.token); peers.push(otherPeer);
    await screen(f, other.b, 'ready', 'not-selected');
    assert.equal((await f.post(selectPath, other.b)).status, 303);
    await report(f, other.lease.outputToken!);
    const otherSource = source(await screen(f, other.b, 'ready', 'report-ready'));
    const originalConnections = f.store.connections('alice'), exchanges = f.exchangeCalls;

    const missing = await screen(f, game.b, 'streaming-required', 'not-selected');
    assert.doesNotMatch(missing, /id="source-url"|id="copy-public-source"|<script/u);
    assert.match(missing, /게임 PC에 OBS를 설치하거나 실행하지 않습니다/u);
    assert.match(missing, /이 관리 화면과 같은 치지직 채널/u);
    await screen(f, game.b, 'streaming-required', 'not-selected');
    assert.deepEqual(f.store.connections('alice'), originalConnections, 'refresh never creates an approval');
    assert.equal(f.exchangeCalls, exchanges, 'guidance does not start provider authentication');
    assert.equal((await f.post(selectPath, game.b)).status, 409, 'POST still enforces the missing role');
    assert.equal(Number(f.store.database.prepare('SELECT COUNT(*) AS n FROM ad_sources WHERE owner = ?')
      .get('alice')?.n), 0, 'rejected selection never creates a public source');

    const pending = await f.start('streaming');
    await screen(f, game.b, 'streaming-required', 'not-selected');
    assert.deepEqual(f.store.connections('alice'), originalConnections, 'a request is not role consent');
    const stream = await f.approve(pending, 'streaming', game.b);
    assert.equal(stream.membership.broadcastSessionId, game.lease.membership.broadcastSessionId);
    const prepared = await screen(f, game.b, 'ready', 'not-selected');
    assert.doesNotMatch(prepared, /id="source-url"/u, 'role consent does not select or create an ad');
    assert.equal((await f.post(selectPath, game.b)).status, 303, 'offline but approved sender may select explicitly');
    const url = source(await screen(f, game.b, 'ready', 'waiting-report'));
    assert.equal(url.origin, f.origin);
    assert.notEqual(url.pathname, otherSource.pathname);
    assert.deepEqual(await snapshot(f, url), HIDDEN_AD, 'selection alone cannot show public artwork');
    const streamPeer = await open(f, stream.token); peers.push(streamPeer);
    await report(f, stream.outputToken!);
    const selected = await screen(f, game.b, 'ready', 'report-ready');
    assert.equal((await snapshot(f, url)).state, 'visible');
    for (const secret of [game.lease.token, game.lease.sessionToken, stream.token, stream.sessionToken,
      stream.outputToken!, other.lease.sessionToken]) assert(!selected.includes(secret), 'no native secret in management HTML');

    // Keep the real gaming socket connected while the authenticated owner
    // removes ONLY the selected streaming approval through its existing form.
    await f.page('/account', game.b);
    const closed = once(streamPeer, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.post(`/connections/${stream.membership.connectionId}/revoke`, game.b)).status, 303);
    await closed;
    assert(!f.store.find(stream.sessionToken), 'old sender is revoked');
    assert(f.store.find(game.lease.sessionToken), 'game approval survives');
    const revoked = await screen(f, game.b, 'streaming-required', 'not-selected');
    assert.equal(source(revoked).href, url.href, 'revocation retains the public address');
    assert.match(revoked, /광고는 자동 선택되지 않습니다/u);
    assert.deepEqual(await snapshot(f, url), HIDDEN_AD);
    assert.equal((await f.post(selectPath, game.b)).status, 409, 'stale select form cannot bypass revocation');
    assert.equal((await snapshot(f, otherSource)).state, 'visible', 'other creator output is unchanged');

    const replacement = await f.approve(await f.start('streaming'), 'streaming', game.b);
    assert.notEqual(replacement.membership.connectionId, stream.membership.connectionId);
    assert.equal(replacement.membership.broadcastSessionId, game.lease.membership.broadcastSessionId);
    const replacementPeer = await open(f, replacement.token); peers.push(replacementPeer);
    await report(f, replacement.outputToken!);
    const reapproved = await screen(f, game.b, 'ready', 'not-selected');
    assert.equal(source(reapproved).href, url.href);
    assert.deepEqual(await snapshot(f, url), HIDDEN_AD, 'fresh approval and report do not reselect an ad');
    assert.equal((await f.post(selectPath, game.b)).status, 303);
    assert.equal(source(await screen(f, game.b, 'ready', 'report-ready')).href, url.href);
    assert.equal((await snapshot(f, url)).state, 'visible', 'explicit reselection reuses the existing OBS source');
    assert.equal((await f.post('/campaigns/stop', game.b)).status, 303);
    assert.equal(source(await screen(f, game.b, 'ready', 'not-selected')).href, url.href);
    assert.deepEqual(await snapshot(f, url), HIDDEN_AD);

    const delivery = new AbortController();
    const frames = on(gamePeer, 'message', { signal: AbortSignal.any([delivery.signal, AbortSignal.timeout(3000)]) });
    try {
      f.chats.get('alice')!.publish('game-chat-after-campaign-stop');
      for await (const [data] of frames) {
        assert.doesNotMatch(String(data), /ad-snapshot|chatview-test/u);
        if (String(data).includes('game-chat-after-campaign-stop')) break;
      }
    } finally { delivery.abort(); }
    assert.equal(gamePeer.readyState, WebSocket.OPEN, 'game chat remains connected across the full ad flow');
    assert.equal(f.exchangeCalls, exchanges, 'role selection never silently reauthenticates the provider');
    assert(f.store.find(other.lease.sessionToken));
    assert.equal((await snapshot(f, otherSource)).state, 'visible');
  } finally { for (const peer of peers) peer.terminate(); await f.close(); }
});

test('provider denial requires reauthorization rather than merely offering another streaming role', async () => {
  const f = await fixture(options);
  try {
    const alice = await f.connect('alice', 'streaming'), other = await f.connect('bob', 'streaming');
    await screen(f, alice.b, 'ready', 'not-selected');
    assert.equal((await f.post(selectPath, alice.b)).status, 303);
    const url = source(await screen(f, alice.b, 'ready', 'waiting-report'));
    f.chats.get('alice')!.revoke(); // Synthetic provider signal, real creator/gateway/store cleanup.
    const exchanges = f.exchangeCalls;
    const denied = await screen(f, alice.b, 'provider-required', 'not-selected');
    assert.match(denied, /치지직 재승인 필요/u);
    assert.equal(source(denied).href, url.href);
    assert.equal((await f.post(selectPath, alice.b)).status, 401, 'UI is not the authorization boundary');
    assert.deepEqual(await snapshot(f, url), HIDDEN_AD);
    assert.equal(f.store.connections('alice').length, 0);
    assert.equal(f.exchangeCalls, exchanges, 'GET does not restore revoked provider authority');
    assert(f.store.find(other.lease.sessionToken));
    await screen(f, other.b, 'ready', 'not-selected');
  } finally { await f.close(); }
});
