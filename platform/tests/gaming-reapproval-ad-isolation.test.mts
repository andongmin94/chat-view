// SPDX-License-Identifier: GPL-2.0-or-later
// Exact per-role approval/Socket.IO presence and nonpayable public ad state.
// Real HTTP/SQLite/WebSocket with synthetic CHZZK; no physical video claims.
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { test } from 'node:test';
import { WebSocket } from 'ws';
import { fixture } from './fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const accountRole = (html: string, role: 'gaming' | 'streaming', approved: number, online: number) => {
  const row = new RegExp('<tr data-pc-role="' + role + '">([\\s\\S]*?)<\\/tr>', 'u').exec(html)?.[1];
  assert(row, 'read-only owner account has both role rows');
  assert.match(row, new RegExp('data-approved-count="' + approved + '"', 'u'));
  assert.match(row, new RegExp('data-online-count="' + online + '"', 'u'));
};
const accountAd = (html: string, state: string, selected: boolean) => {
  assert.match(html, new RegExp('data-account-ad-state="' + state + '"', 'u'));
  assert.match(html, new RegExp('data-account-ad-selected="' + selected + '"', 'u'));
  assert.match(html, /href="\/campaigns#public-banner-status"/u);
  assert.match(html, /게임 PC의 새 채팅 승인·로그아웃은 기존 송출 PC 승인이나 송출 광고 선택을 옮기지 않습니다/u);
};
const sourceId = (html: string) => {
  const match = /\/public\/ads\/([a-f0-9]{32})/u.exec(html);
  assert(match, 'creator has the same stable public OBS source URL');
  return match[1]!;
};

test('owner revokes and reapproves only gaming chat; sender and its selected public OBS banner survive', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  const open = async (bearer: string) => {
    const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
      headers: { Authorization: 'Bearer ' + bearer }, handshakeTimeout: 3000,
    });
    peer.on('error', () => {});
    peers.push(peer);
    await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
    return peer;
  };
  const report = (token: string, sequence: number) => f.request('/broadcast/output', {
    method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }),
  });
  try {
    const sender = await f.connect('alice', 'streaming');
    const original = await f.connect('alice', 'gaming');
    const other = await f.connect('bob', 'gaming');
    const streamingSocket = await open(sender.lease.token);
    const originalSocket = await open(original.lease.token);
    await open(other.lease.token);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post('/campaigns/' + TEST_CAMPAIGN.id + '/select', sender.b)).status, 303);
    assert.equal((await report(sender.lease.outputToken!, 1)).status, 200);
    const source = sourceId(await f.page('/campaigns', sender.b));
    const publicState = async () => (await f.request('/public/ads/' + source + '/state')).json();
    assert.equal((await publicState() as { state: string }).state, 'visible');
    const selected = f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?').get(source);
    assert.deepEqual(selected, { connection_id: sender.lease.membership.connectionId });
    const beforeConnections = f.store.connections('alice');
    const beforeOutput = f.creators.describe('alice').output;
    const baselineStarts = f.startedWith.length, baselineRefresh = f.refreshCalls;
    let html = await f.page('/account', sender.b);
    accountRole(html, 'gaming', 1, 1); accountRole(html, 'streaming', 1, 1);
    accountAd(html, 'report-ready', true);
    assert.match(html, /data-output-report="reported"/u);
    const db = f.store.database;
    const beforeRead = db.prepare('SELECT total_changes() AS count').get()!.count;
    const beforeSource = sourceId(await f.page('/campaigns', sender.b));
    for (let index = 0; index < 3; index++) {
      html = await f.page('/account', sender.b);
      accountAd(html, 'report-ready', true);
      await publicState();
    }
    assert.equal(db.prepare('SELECT total_changes() AS count').get()!.count, beforeRead,
      'management/public GETs never mutate approvals or ad selection');
    assert.equal(beforeSource, source);

    const closed = once(originalSocket, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.post('/connections/' + original.lease.membership.connectionId + '/revoke', sender.b)).status, 303);
    await closed;
    assert.equal(f.store.find(original.lease.sessionToken), undefined);
    assert(f.store.find(sender.lease.sessionToken) && f.store.find(other.lease.sessionToken));
    html = await f.page('/account', sender.b);
    accountRole(html, 'gaming', 0, 0); accountRole(html, 'streaming', 1, 1);
    accountAd(html, 'report-ready', true);
    assert.equal((await publicState() as { state: string }).state, 'visible');
    assert.deepEqual(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?').get(source), selected,
      'the gaming PC role removal cannot unlink the sender campaign');

    const newRequest = await f.start('gaming');
    await f.page(newRequest.verificationPath, sender.b);
    assert.equal(f.app.login.view(newRequest.id).role, 'gaming');
    const polling = await f.native('/display/login/' + newRequest.id, 'ChatView-Login', newRequest.verifier);
    assert.equal(polling.status, 200);
    assert.deepEqual(await polling.json(), { status: 'pending' },
      'no automatic consent or session transfer while the game client awaits browser approval');
    assert.equal((await publicState() as { state: string }).state, 'visible');

    const replacement = await f.approve(newRequest, 'gaming', sender.b);
    assert.notEqual(replacement.sessionToken, original.lease.sessionToken);
    assert.notEqual(replacement.membership.connectionId, original.lease.membership.connectionId);
    assert.equal(replacement.membership.role, 'gaming');
    assert.equal(replacement.membership.broadcastSessionId, sender.lease.membership.broadcastSessionId);
    const newGameSocket = await open(replacement.token);
    html = await f.page('/account', sender.b);
    accountRole(html, 'gaming', 1, 1); accountRole(html, 'streaming', 1, 1);
    accountAd(html, 'report-ready', true);
    assert.match(html, /data-output-report="reported"/u);
    const refreshedOutput = f.creators.describe('alice').output;
    assert.equal(refreshedOutput.state, beforeOutput.state);
    if (refreshedOutput.state === 'reported' && beforeOutput.state === 'reported') {
      assert.equal(refreshedOutput.streaming, beforeOutput.streaming);
      assert.equal(refreshedOutput.recording, beforeOutput.recording);
      assert(refreshedOutput.expiresInMs > 0 && refreshedOutput.expiresInMs <= beforeOutput.expiresInMs,
        'valid report may age, but a new gaming approval cannot create another report');
    }
    assert.equal(sourceId(await f.page('/campaigns', sender.b)), source);
    assert.equal((await publicState() as { state: string }).state, 'visible');
    assert.deepEqual(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?').get(source), selected);
    assert.equal(streamingSocket.readyState, WebSocket.OPEN);
    assert.equal(f.store.find(sender.lease.sessionToken)?.membership?.connectionId, sender.lease.membership.connectionId);
    assert.equal(f.startedWith.length, baselineStarts, 'gaming reapproval never starts a second upstream');
    assert.equal(f.refreshCalls, baselineRefresh, 'gaming reapproval never renews the sender');
    assert.equal(beforeConnections.filter(c => c.role === 'streaming').length, 1);

    const streamingMessage = once(streamingSocket, 'message', { signal: AbortSignal.timeout(3000) });
    const gamingMessage = once(newGameSocket, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('game restored with existing OBS sender');
    assert.match(String((await streamingMessage)[0]), /game restored with existing OBS sender/u);
    assert.match(String((await gamingMessage)[0]), /game restored with existing OBS sender/u);

    const gameClosed = once(newGameSocket, 'close', { signal: AbortSignal.timeout(3000) });
    const logout = await f.native('/display/signout', 'ChatView-Session', replacement.sessionToken, 'gaming');
    assert.equal(logout.status, 200); assert.deepEqual(await logout.json(), { signedOut: true });
    await gameClosed;
    html = await f.page('/account', sender.b);
    accountRole(html, 'gaming', 0, 0); accountRole(html, 'streaming', 1, 1);
    accountAd(html, 'report-ready', true);
    assert.equal((await publicState() as { state: string }).state, 'visible');
    assert(f.store.find(sender.lease.sessionToken) && f.store.find(other.lease.sessionToken));
    assert.equal(streamingSocket.readyState, WebSocket.OPEN);
    const bob = await f.page('/account', other.b);
    accountRole(bob, 'gaming', 1, 1); accountRole(bob, 'streaming', 0, 0);
    accountAd(bob, 'not-selected', false);
    assert(!bob.includes(source) && !bob.includes(sender.lease.membership.connectionId),
      'another owner cannot see Alice selected OBS source or sender identity');
  } finally { for (const peer of peers) peer.terminate(); await f.close(); }
});

test('account ad summary distinguishes selected streaming slot, disconnected sender, stale report and manual stop', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  let senderSocket: WebSocket | undefined, gameSocket: WebSocket | undefined;
  try {
    const sender = await f.connect('alice', 'streaming');
    const game = await f.connect('alice', 'gaming');
    const open = async (bearer: string) => {
      const socket = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
        { headers: { Authorization: 'Bearer ' + bearer }, handshakeTimeout: 3000 });
      socket.on('error', () => {});
      await once(socket, 'message', { signal: AbortSignal.timeout(3000) }); return socket;
    };
    senderSocket = await open(sender.lease.token);
    gameSocket = await open(game.lease.token);
    let html = await f.page('/account', sender.b);
    accountAd(html, 'not-selected', false);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post('/campaigns/' + TEST_CAMPAIGN.id + '/select', sender.b)).status, 303);
    html = await f.page('/account', sender.b);
    accountAd(html, 'waiting-report', true);
    accountRole(html, 'gaming', 1, 1); accountRole(html, 'streaming', 1, 1);
    const close = once(senderSocket, 'close', { signal: AbortSignal.timeout(3000) });
    senderSocket.close(); await close;
    // Closing a sender socket does not revoke the role or the selected campaign.
    html = await f.page('/account', sender.b);
    accountAd(html, 'streaming-disconnected', true);
    accountRole(html, 'gaming', 1, 1); accountRole(html, 'streaming', 1, 0);
    assert.equal(gameSocket.readyState, WebSocket.OPEN);
    const source = sourceId(await f.page('/campaigns', sender.b));
    assert.deepEqual(await (await f.request('/public/ads/' + source + '/state')).json(), HIDDEN_AD);
    assert.equal((await f.post('/campaigns/stop', sender.b)).status, 303);
    html = await f.page('/account', sender.b);
    accountAd(html, 'not-selected', false);
    assert.equal(sourceId(await f.page('/campaigns', sender.b)), source,
      'manual stop retains original public URL without automatically selecting an ad');
  } finally {
    senderSocket?.terminate(); gameSocket?.terminate(); await f.close();
  }
});
