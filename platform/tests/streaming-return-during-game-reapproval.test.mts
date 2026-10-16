// SPDX-License-Identifier: GPL-2.0-or-later
// A gaming PC is reauthorized while the already-approved streaming PC goes offline.
// Real HTTP/SQLite/ws with a synthetic CHZZK provider, not broadcast attestation.
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { test } from 'node:test';
import { WebSocket } from 'ws';
import { fixture } from './fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const sourceId = (html: string) => {
  const match = /\/public\/ads\/([a-f0-9]{32})/u.exec(html);
  assert(match, 'the existing public source URL is available');
  return match[1]!;
};
function role(html: string, which: 'gaming' | 'streaming', approved: number, connected: number) {
  const row = new RegExp('<tr data-pc-role="' + which + '">([\\s\\S]*?)<\\/tr>', 'u').exec(html)?.[1];
  assert(row);
  assert.match(row, new RegExp('data-approved-count="' + approved + '"', 'u'));
  assert.match(row, new RegExp('data-online-count="' + connected + '"', 'u'));
}
function ad(html: string, expected: string, selected: boolean) {
  assert.match(html, new RegExp('data-account-ad-state="' + expected + '"', 'u'));
  assert.match(html, new RegExp('data-account-ad-selected="' + selected + '"', 'u'));
  assert.match(html, /href="\/campaigns#public-banner-status"/u);
}
const connectPeer = async (origin: string, token: string, peers: WebSocket[]) => {
  const ws = new WebSocket(origin.replace('http:', 'ws:') + '/display/events',
    { headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
  peers.push(ws);
  ws.on('error', () => {});
  await once(ws, 'message', { signal: AbortSignal.timeout(3000) });
  return ws;
};
const report = (f: Awaited<ReturnType<typeof fixture>>, token: string, sequence: number) =>
  f.request('/broadcast/output', { method: 'POST', headers: {
    Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
  }, body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });

test('sender disconnects during gaming reapproval: banner hides, gaming chat works, sender returns on same grant and URL after fresh report', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const oldGaming = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const other = await f.connect('bob', 'gaming');
    const originalSenderPeer = await connectPeer(f.origin, sender.lease.token, peers);
    const originalGamingPeer = await connectPeer(f.origin, oldGaming.token, peers);
    await connectPeer(f.origin, other.lease.token, peers);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post('/campaigns/' + TEST_CAMPAIGN.id + '/select', sender.b)).status, 303);
    assert.equal((await report(f, sender.lease.outputToken!, 1)).status, 200);
    const source = sourceId(await f.page('/campaigns', sender.b));
    const state = async () => (await f.request('/public/ads/' + source + '/state')).json() as Promise<{ state: string }>;
    assert.equal((await state()).state, 'visible');
    const linked = f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?').get(source);
    assert.equal(linked?.connection_id, sender.lease.membership.connectionId);
    const upstreams = f.startedWith.length, providerRefresh = f.refreshCalls;
    const output = f.creators.describe('alice').output;
    assert.equal(output.state, 'reported');

    // Revoke the gaming approval alone, then start a fresh gaming-role request.
    const gameClosed = once(originalGamingPeer, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.post('/connections/' + oldGaming.membership.connectionId + '/revoke', sender.b)).status, 303);
    await gameClosed;
    const replacementRequest = await f.start('gaming');
    assert.equal(f.app.login.view(replacementRequest.id).role, 'gaming');
    let html = await f.page('/account', sender.b);
    role(html, 'gaming', 0, 0); role(html, 'streaming', 1, 1);
    ad(html, 'report-ready', true);

    // The sender loses only its open WebSocket, not its stored role, campaign,
    // browser-source address, or the game's pending approval request.
    const senderClosed = once(originalSenderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    originalSenderPeer.close(); await senderClosed;
    html = await f.page('/account', sender.b);
    role(html, 'gaming', 0, 0); role(html, 'streaming', 1, 0);
    ad(html, 'streaming-disconnected', true);
    assert.match(html, /현재 승인으로 자체 채팅 복귀/u);
    assert.match(html, /같은 승인으로 복귀할 때 광고 재선택·URL 교체는 필요하지 않습니다/u);
    assert.match(html, /data-output-report="unknown"/u);
    assert.deepEqual(await state(), HIDDEN_AD);
    assert.equal((await report(f, sender.lease.outputToken!, 2)).status, 409,
      'offline output reports cannot keep the public banner visible');
    assert.equal(f.store.find(sender.lease.sessionToken)?.membership?.connectionId,
      sender.lease.membership.connectionId);
    assert.equal(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source)?.connection_id, linked!.connection_id);

    // A browser explicitly approves the new game role while the sender remains
    // offline. The resulting new gaming WebSocket must keep reading chat.
    const newGaming = await f.approve(replacementRequest, 'gaming', sender.b);
    assert.notEqual(newGaming.membership.connectionId, oldGaming.membership.connectionId);
    assert.equal(newGaming.membership.role, 'gaming');
    assert.equal(newGaming.membership.broadcastSessionId, sender.lease.membership.broadcastSessionId);
    const newGamingPeer = await connectPeer(f.origin, newGaming.token, peers);
    const received = once(newGamingPeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('game chat remains live while sender offline');
    assert.match(String((await received)[0]), /game chat remains live while sender offline/u);
    html = await f.page('/account', sender.b);
    role(html, 'gaming', 1, 1); role(html, 'streaming', 1, 0);
    ad(html, 'streaming-disconnected', true);
    assert.deepEqual(await state(), HIDDEN_AD);
    assert.equal(sourceId(await f.page('/campaigns', sender.b)), source);

    // Only the sender explicitly chooses the current approval return. The
    // renewed sender's socket alone does NOT restore an expired/cleared report.
    const response = await f.native('/display/refresh', 'ChatView-Session', sender.lease.sessionToken, 'streaming');
    assert.equal(response.status, 200);
    const returned = await response.json() as typeof sender.lease;
    assert.deepEqual(returned.membership, sender.lease.membership);
    assert.equal(f.store.find(sender.lease.sessionToken)?.membership?.connectionId,
      sender.lease.membership.connectionId,
      'return rotates the short bearer without replacing the existing streaming approval');
    assert(returned.outputToken, 'new sender lease has its own output reporting proof');
    const senderPeer = await connectPeer(f.origin, returned.token, peers);
    html = await f.page('/account', sender.b);
    role(html, 'gaming', 1, 1); role(html, 'streaming', 1, 1);
    ad(html, 'waiting-report', true);
    assert.match(html, /새 보고를 기다리는 동안 공개 소스는 투명합니다/u);
    assert.deepEqual(await state(), HIDDEN_AD,
      'reconnecting without a new report must never reuse stale exposure conditions');

    assert.equal((await report(f, returned.outputToken!, 1)).status, 200);
    assert.equal((await state()).state, 'visible');
    html = await f.page('/account', sender.b);
    role(html, 'gaming', 1, 1); role(html, 'streaming', 1, 1);
    ad(html, 'report-ready', true);
    assert.equal(sourceId(await f.page('/campaigns', sender.b)), source,
      'same approved sender returns with exactly the prior public URL');
    assert.equal(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source)?.connection_id, linked!.connection_id,
      'offline/return never reselects, replaces or unlinks the campaign');
    assert.equal(f.store.find(newGaming.sessionToken)?.membership?.connectionId, newGaming.membership.connectionId);
    assert.equal(f.store.find(sender.lease.sessionToken)?.membership?.connectionId,
      sender.lease.membership.connectionId);
    assert.equal(f.startedWith.length, upstreams, 'two role returns cannot restart the shared provider upstream');
    assert.equal(f.refreshCalls, providerRefresh, 'sender display return never refreshes provider OAuth');
    assert(f.store.find(other.lease.sessionToken), 'another owner has unchanged gaming authority');
    const gameReceived = once(newGamingPeer, 'message', { signal: AbortSignal.timeout(3000) });
    const senderReceived = once(senderPeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('same creator both roles after sender returns');
    assert.match(String((await gameReceived)[0]), /same creator both roles after sender returns/u);
    assert.match(String((await senderReceived)[0]), /same creator both roles after sender returns/u);
  } finally {
    for (const peer of peers) peer.terminate();
    await f.close();
  }
});

test('actual sender approval revocation deletes campaign selection, so a newly approved sender cannot restore it until owner reselects', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const gaming = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const senderPeer = await connectPeer(f.origin, sender.lease.token, peers);
    const gamePeer = await connectPeer(f.origin, gaming.token, peers);
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post('/campaigns/' + TEST_CAMPAIGN.id + '/select', sender.b)).status, 303);
    assert.equal((await report(f, sender.lease.outputToken!, 1)).status, 200);
    const source = sourceId(await f.page('/campaigns', sender.b));
    const state = async () => (await f.request('/public/ads/' + source + '/state')).json() as Promise<{ state: string }>;
    assert.equal((await state()).state, 'visible');

    const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.post('/connections/' + sender.lease.membership.connectionId + '/revoke', sender.b)).status, 303);
    await closed;
    assert.equal(f.store.find(sender.lease.sessionToken), undefined);
    let html = await f.page('/account', sender.b);
    role(html, 'gaming', 1, 1); role(html, 'streaming', 0, 0);
    ad(html, 'not-selected', false);
    assert.match(html, /송출 PC 역할 승인이 없습니다/u);
    assert.match(html, /직접 다시 선택하세요/u);
    assert.deepEqual(await state(), HIDDEN_AD);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming')).status, 401,
      'revoked sender must not refresh as a disconnected-but-approved session');

    const next = await f.approve(await f.start('streaming'), 'streaming', sender.b);
    assert.notEqual(next.membership.connectionId, sender.lease.membership.connectionId);
    const replacementPeer = await connectPeer(f.origin, next.token, peers);
    assert.equal((await report(f, next.outputToken!, 1)).status, 200);
    html = await f.page('/account', sender.b);
    role(html, 'gaming', 1, 1); role(html, 'streaming', 1, 1);
    ad(html, 'not-selected', false);
    assert.deepEqual(await state(), HIDDEN_AD,
      'new streaming role and fresh output report cannot restore revoked campaign selection');
    assert.equal(sourceId(await f.page('/campaigns', sender.b)), source,
      'stable OBS Browser Source URL does not imply automatic campaign reselection');
    assert.equal(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source), undefined);
    assert.equal(gamePeer.readyState, WebSocket.OPEN,
      'creator gaming chat remains authorized while sender role is replaced');

    assert.equal((await f.post('/campaigns/' + TEST_CAMPAIGN.id + '/select', sender.b)).status, 303);
    assert.equal(f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source)?.connection_id, next.membership.connectionId);
    assert.equal((await state()).state, 'visible');
    assert.equal(replacementPeer.readyState, WebSocket.OPEN);
    const received = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('gaming feed remains independent of sender replacement');
    assert.match(String((await received)[0]), /gaming feed remains independent of sender replacement/u);
  } finally {
    for (const peer of peers) peer.terminate();
    await f.close();
  }
});
