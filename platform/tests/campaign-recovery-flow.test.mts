// SPDX-License-Identifier: GPL-2.0-or-later
// Real service HTTP, SQLite and WebSocket; provider and OBS reports are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once, on } from 'node:events';
import { WebSocket } from 'ws';
import { BroadcastOutput } from '../server/chat/broadcast-output.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { fixture } from './fixtures/service.mts';

test('selected banner recovery separates approval, open sender and fresh report without reselecting or changing its URL', async () => {
  let now = 1000;
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot, new BroadcastOutput(() => now)) });
  const peers: WebSocket[] = [];
  const actions: string[] = [];
  const handle = f.app.handle.bind(f.app);
  f.app.handle = async (request, response) => {
    if (request.method === 'POST') actions.push(request.url!);
    await handle(request, response);
  };
  const select = `/campaigns/${TEST_CAMPAIGN.id}/select`;
  const open = async (token: string) => {
    const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
      headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
    });
    peers.push(peer); peer.on('error', () => {});
    await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
    return peer;
  };
  const report = async (token: string, sequence: number) => {
    const response = await f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
    assert.equal(response.status, 200);
  };
  try {
    const game = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'streaming');
    const gamePeer = await open(game.lease.token);
    await open(bob.lease.token); await report(bob.lease.outputToken!, 1);
    let source = '';
    const read = async (expected: string, visible = false) => {
      const writes = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
      const posts = actions.length, starts = f.startedWith.length;
      const exchanges = f.exchangeCalls, refreshes = f.refreshCalls;
      const html = await f.page('/campaigns', game.b);
      assert.equal(/data-banner-recovery-state="([^"]+)"/u.exec(html)?.[1], expected);
      assert.match(html, /href="\/account#two-pc-status"/u);
      for (const secret of [game.lease.token, game.lease.sessionToken, bob.lease.token, bob.lease.sessionToken])
        assert(!html.includes(secret), 'guidance has no native credentials');
      if (source) {
        assert(html.includes(`value="${f.origin}/public/ads/${source}"`));
        const response = await f.request(`/public/ads/${source}/state`);
        assert.equal(response.status, 200);
        const snapshot = await response.json();
        if (visible) { assert.equal(snapshot.state, 'visible'); assert.equal(snapshot.mode, 'test'); }
        else assert.deepEqual(snapshot, HIDDEN_AD);
        assert.doesNotMatch(JSON.stringify(snapshot), /recovery|alice|bob|sessionToken|connectionId/u,
          'private recovery reasons never enter the public snapshot');
      }
      assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, writes);
      assert.equal(actions.length, posts, 'navigation does not login, report, select or Stop');
      assert.equal(f.startedWith.length, starts);
      assert.equal(f.exchangeCalls, exchanges); assert.equal(f.refreshCalls, refreshes);
      return html;
    };
    await read('streaming-required'); // Bob's live sender and Alice's gaming socket do not qualify.
    const pending = await f.start('streaming');
    const sender = await f.approve(pending, 'streaming', game.b);
    await read('not-selected');
    assert.equal((await f.post(select, game.b)).status, 303);
    const setup = await f.page('/campaigns', game.b);
    const match = /\/public\/ads\/([a-f0-9]{32})/u.exec(setup); assert(match); source = match[1]!;
    const selectionRows = () => f.store.database.prepare('SELECT * FROM ad_selections').all();
    const selected = selectionRows();
    assert.match(await read('streaming-disconnected'), /현재 승인으로 자체 채팅 복귀/u);

    let senderPeer = await open(sender.token);
    assert.match(await read('waiting-report'), /방송·녹화를 시작할 필요는 없습니다/u);
    await report(sender.outputToken!, 1);
    await read('report-ready', true); // Inactive output is preview readiness, NOT exposure.
    now += 15000;
    await read('waiting-report');
    await report(bob.lease.outputToken!, 2);
    await read('waiting-report'); // Another creator cannot cure an expired report.
    await report(sender.outputToken!, 2);
    await read('report-ready', true);
    assert.deepEqual(selectionRows(), selected);

    const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    senderPeer.close(); await closed;
    const due = Date.now() + 2000;
    while (f.creators.describe('alice').presence.streamingConnections !== 0) {
      assert(Date.now() < due, 'server observes the actual sender disconnect');
      await new Promise<void>(resolve => setTimeout(resolve, 25));
    }
    await read('streaming-disconnected');
    const response = await f.native('/display/refresh', 'ChatView-Session', sender.sessionToken, 'streaming');
    assert.equal(response.status, 200);
    const renewed = await response.json() as typeof sender;
    assert.deepEqual(renewed.membership, sender.membership);
    assert.notEqual(renewed.outputToken, sender.outputToken);
    await read('streaming-disconnected'); // Renewal alone does not open a socket.
    senderPeer = await open(renewed.token);
    await read('waiting-report'); // A new socket cannot revive the previous report.
    await report(renewed.outputToken!, 1);
    await read('report-ready', true);
    assert.deepEqual(selectionRows(), selected);
    assert.equal(actions.filter(path => path === select).length, 1, 'no reselection during recovery');

    assert.equal((await f.post('/campaigns/stop', game.b)).status, 303);
    now += 500; await report(renewed.outputToken!, 2);
    await read('not-selected'); // Fresh reports do not reverse an explicit Stop.
    const controller = new AbortController();
    const messages = on(gamePeer, 'message', { signal: AbortSignal.any([
      controller.signal, AbortSignal.timeout(3000),
    ]) });
    try {
      f.chats.get('alice')!.publish('private game chat survives banner recovery');
      let delivered = false;
      for await (const [frame] of messages) {
        if (String(frame).includes('private game chat survives banner recovery')) { delivered = true; break; }
      }
      assert(delivered); assert.equal(gamePeer.readyState, WebSocket.OPEN);
    } finally { controller.abort(); }

    assert.equal((await f.post(select, game.b)).status, 303);
    await read('report-ready', true);
    assert.equal((await f.post(`/connections/${sender.membership.connectionId}/revoke`, game.b)).status, 303);
    await read('streaming-required');
    const replacement = await f.approve(await f.start('streaming'), 'streaming', game.b);
    await open(replacement.token); await report(replacement.outputToken!, 1);
    await read('not-selected'); // New approval/report cannot restore a revoked selection.
    assert.equal((await f.post(select, game.b)).status, 303);
    await read('report-ready', true);
    f.chats.get('alice')!.revoke();
    await read('provider-required');
    assert(f.store.find(bob.lease.sessionToken), 'another creator is unaffected');
  } finally {
    for (const peer of peers) peer.terminate();
    await f.close();
  }
});
