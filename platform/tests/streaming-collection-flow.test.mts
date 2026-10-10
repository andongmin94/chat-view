// SPDX-License-Identifier: GPL-2.0-or-later
// Actual HTTP races, SQLite and WebSocket. Provider/OBS reports are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once, on } from 'node:events';
import { WebSocket } from 'ws';
import { LOGIN_POLL_MS, LOGIN_WINDOW_MS } from '../server/chat/browser-login.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { fixture } from './fixtures/service.mts';

for (const reverse of [false, true]) {
  test(`simultaneous consent and app collection (reverse=${reverse}) keep one sender and require explicit same-request recovery`, async t => {
    let now = 1000;
    t.mock.method(performance, 'now', () => now);
    const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
    const peers: WebSocket[] = [];
    const open = async (token: string) => {
      const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
        headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
      });
      peers.push(peer); peer.on('error', () => {});
      await once(peer, 'message', { signal: AbortSignal.timeout(3000) }); return peer;
    };
    try {
      const game = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'streaming');
      const gamePeer = await open(game.lease.token);
      await open(bob.lease.token);
      const requests = [await f.start('streaming'), await f.start('streaming')];
      if (reverse) requests.reverse();
      const first = requests[0]!, second = requests[1]!;
      await f.page(first.verificationPath, game.b); await f.page(second.verificationPath, game.b);
      const consents = await Promise.all(requests.map(r => f.post(`${r.verificationPath}/approve`, game.b)));
      for (let i = 0; i < consents.length; i++) {
        const response = consents[i]!; assert.equal(response.status, 200);
        const html = await response.text();
        assert.match(html, /data-login-collection="awaiting-app"/u);
        assert.match(html, /아직 PC 연결 완료가 아닙니다/u);
        assert(html.includes(`href="${requests[i]!.verificationPath}"`));
        assert.doesNotMatch(html, /<script|http-equiv="refresh"/u);
      }
      assert.equal(f.store.connections('alice').filter(c => c.role === 'streaming').length, 0);
      for (const r of requests) {
        assert.match(await f.page(r.verificationPath, game.b), /data-login-collection="awaiting-app"/u);
      }
      const collect = (r: typeof first) => f.native(`/display/login/${r.id}`, 'ChatView-Login', r.verifier);
      const replies = await Promise.all(requests.map(collect));
      type Result = { status: 'pending' } | { status: 'approved'; lease: typeof game.lease };
      const results: Result[] = [];
      for (const response of replies) { assert.equal(response.status, 200); results.push(await response.json() as Result); }
      assert.equal(results.filter(r => r.status === 'approved').length, 1);
      assert.equal(results.filter(r => r.status === 'pending').length, 1);
      const winnerIndex = results.findIndex(r => r.status === 'approved');
      const winner = results[winnerIndex]!; assert(winner.status === 'approved');
      const loser = requests[1 - winnerIndex]!;
      assert.deepEqual(results[1 - winnerIndex], { status: 'pending' });
      assert.equal((await collect(requests[winnerIndex]!)).status, 410);
      assert.equal((await collect(loser)).status, 429, 'collection conflict preserves the original poll budget');
      assert.equal(f.store.connections('alice').length, 2, 'the loser has no orphan renewable approval');
      const senderPeer = await open(winner.lease.token);
      const report = (token: string) => f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
      const select = `/campaigns/${TEST_CAMPAIGN.id}/select`;
      await f.page('/campaigns', game.b); assert.equal((await f.post(select, game.b)).status, 303);
      assert.equal((await report(winner.lease.outputToken!)).status, 200);
      const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(await f.page('/campaigns', game.b))?.[1]; assert(source);
      const state = async () => (await f.request(`/public/ads/${source}/state`)).json();
      assert.equal((await state()).state, 'visible');
      const rows = () => JSON.stringify({
        connections: f.store.database.prepare('SELECT * FROM connection_roles ORDER BY connection_id').all(),
        selected: f.store.database.prepare('SELECT * FROM ad_selections').all(),
      });
      const before = rows(), writes = f.store.database.prepare('SELECT total_changes() AS n').get()!.n;
      const conflict = await f.request(loser.verificationPath, { headers: { Cookie: game.b.cookie } });
      assert.equal(conflict.status, 409);
      const html = await conflict.text();
      assert.match(html, /data-login-collection="retry-required"/u);
      assert.match(html, /data-streaming-conflict="existing-approval"/u);
      assert(html.includes(winner.lease.membership.connectionId));
      for (const secret of [winner.lease.token, winner.lease.sessionToken, winner.lease.outputToken!,
        game.lease.token, loser.verifier, bob.lease.membership.connectionId]) assert(!html.includes(secret));
      assert.match(await f.page('/account', game.b), /data-streaming-request="pending"/u);
      assert.equal(rows(), before); assert.equal(f.store.database.prepare('SELECT total_changes() AS n').get()!.n, writes);
      const foreign = await f.request(loser.verificationPath, { headers: { Cookie: bob.b.cookie } });
      assert.doesNotMatch(await foreign.text(), /data-login-collection|data-streaming-conflict/u);
      assert.equal((await f.post(`${loser.verificationPath}/approve`, { ...game.b, csrf: '0'.repeat(64) })).status, 403);
      assert.equal(rows(), before);
      const revoke = `/connections/${winner.lease.membership.connectionId}/revoke`;
      assert.equal((await f.post(revoke, bob.b)).status, 303); assert.equal(rows(), before);
      const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
      assert.equal((await f.post(revoke, game.b)).status, 303); await closed;
      now += LOGIN_POLL_MS;
      assert.deepEqual(await (await collect(loser)).json(), { status: 'pending' }, 'a freed slot cannot replay old consent');
      assert.deepEqual(await state(), HIDDEN_AD);
      assert.equal(f.store.connections('alice').length, 1);
      const retryPage = await f.page(loser.verificationPath, game.b);
      assert.match(retryPage, /data-login-collection="retry-required"/u);
      assert.match(retryPage, /자리가 비어도 자동 연결하지 않습니다/u);
      assert.equal((await f.post(`${loser.verificationPath}/approve`, game.b)).status, 200);
      now += LOGIN_POLL_MS;
      const response = await collect(loser); assert.equal(response.status, 200);
      const replacement = await response.json() as Result; assert(replacement.status === 'approved');
      assert.notEqual(replacement.lease.membership.connectionId, winner.lease.membership.connectionId);
      assert.equal(replacement.lease.membership.broadcastSessionId, game.lease.membership.broadcastSessionId);
      assert.equal((await collect(loser)).status, 410, 'the recovered success is still one-use');
      await open(replacement.lease.token); assert.equal((await report(replacement.lease.outputToken!)).status, 200);
      assert.deepEqual(await state(), HIDDEN_AD, 'new consent, collection and report cannot reselect an ad');
      const controller = new AbortController();
      const messages = on(gamePeer, 'message', { signal: AbortSignal.any([controller.signal, AbortSignal.timeout(3000)]) });
      try {
        const marker = 'game chat survives two senders racing'; f.chats.get('alice')!.publish(marker);
        let delivered = false;
        for await (const [frame] of messages) if (String(frame).includes(marker)) { delivered = true; break; }
        assert(delivered); assert.equal(gamePeer.readyState, WebSocket.OPEN);
      } finally { controller.abort(); }
      assert(f.store.find(bob.lease.sessionToken)); assert(f.store.find(game.lease.sessionToken));
      assert.equal(f.startedWith.length, 2); assert.equal(f.exchangeCalls, 2); assert.equal(f.refreshCalls, 0);
      await f.page('/campaigns', game.b); assert.equal((await f.post(select, game.b)).status, 303);
      assert((await f.page('/campaigns', game.b)).includes(`value="${f.origin}/public/ads/${source}"`));
      assert.equal((await state()).state, 'visible');
    } finally { for (const peer of peers) peer.terminate(); await f.close(); }
  });
}

for (const ending of ['deny', 'expire', 'clear'] as const) {
  test(`HTTP collection conflict then ${ending} preserves the successful sender`, async t => {
    let now = 1000; t.mock.method(performance, 'now', () => now);
    const f = await fixture();
    try {
      const alice = await f.connect('alice', 'gaming');
      const a = await f.start('streaming'), b = await f.start('streaming');
      await f.page(a.verificationPath, alice.b);
      for (const r of [a, b]) assert.equal((await f.post(`${r.verificationPath}/approve`, alice.b)).status, 200);
      const result = await f.native(`/display/login/${a.id}`, 'ChatView-Login', a.verifier);
      const winner = await result.json() as { status: string; lease: typeof alice.lease }; assert.equal(winner.status, 'approved');
      const loser = await f.native(`/display/login/${b.id}`, 'ChatView-Login', b.verifier);
      assert.equal(loser.status, 200); assert.deepEqual(await loser.json(), { status: 'pending' });
      const view = await f.request(b.verificationPath, { headers: { Cookie: alice.b.cookie } });
      assert.equal(view.status, 409); await view.text();
      if (ending === 'deny') assert.equal((await f.post(`${b.verificationPath}/deny`, alice.b)).status, 200);
      else if (ending === 'expire') now += LOGIN_WINDOW_MS;
      else f.app.login.clear();
      now += LOGIN_POLL_MS;
      assert.equal((await f.native(`/display/login/${b.id}`, 'ChatView-Login', b.verifier)).status,
        ending === 'deny' ? 403 : 410);
      const closed = await f.request(b.verificationPath, { headers: { Cookie: alice.b.cookie } });
      assert.equal(closed.status, 410); assert.doesNotMatch(await closed.text(), /<form|\/approve/u);
      assert.doesNotMatch(await f.page('/account', alice.b), /data-streaming-request="pending"/u);
      assert(f.store.find(winner.lease.sessionToken)); assert(f.store.find(alice.lease.sessionToken));
      assert.equal(f.store.connections('alice').length, 2);
    } finally { await f.close(); }
  });
}
