// SPDX-License-Identifier: GPL-2.0-or-later
// Real HTTP/SQLite/ws. Provider, app launch and OBS output reports are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once, on } from 'node:events';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { LOGIN_WINDOW_MS } from '../server/chat/browser-login.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { fixture, deferred } from './fixtures/service.mts';

const conflict = /data-streaming-conflict="existing-approval"/u;
const pendingNotice = /data-streaming-request="pending"/u;
const sourceId = (html: string) => {
  const match = /\/public\/ads\/([a-f0-9]{32})/u.exec(html); assert(match); return match[1]!;
};

test('streaming conflict returns through owner connection removal and original consent without replacing game chat or reselecting ads', async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  const open = async (token: string) => {
    const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
      headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
    });
    peers.push(peer); peer.on('error', () => {});
    await once(peer, 'message', { signal: AbortSignal.timeout(3000) }); return peer;
  };
  const report = (token: string, sequence = 1) => f.request('/broadcast/output', {
    method: 'POST', headers: { Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }),
  });
  try {
    const game = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'streaming');
    const sender = await f.approve(await f.start('streaming'), 'streaming', game.b);
    const gamePeer = await open(game.lease.token), senderPeer = await open(sender.token);
    await open(bob.lease.token);
    await f.page('/campaigns', game.b);
    const select = `/campaigns/${TEST_CAMPAIGN.id}/select`;
    assert.equal((await f.post(select, game.b)).status, 303);
    assert.equal((await report(sender.outputToken!)).status, 200);
    const source = sourceId(await f.page('/campaigns', game.b));
    const state = async () => (await f.request(`/public/ads/${source}/state`)).json();
    assert.equal((await state()).state, 'visible');
    const request = await f.start('streaming');
    const login = request.verificationPath;
    const db = f.store.database;
    const snapshot = () => JSON.stringify({
      connections: db.prepare('SELECT * FROM connection_roles ORDER BY connection_id').all(),
      selections: db.prepare('SELECT * FROM ad_selections').all(),
      changes: db.prepare('SELECT total_changes() AS n').get()!.n,
      starts: f.startedWith.length, exchanges: f.exchangeCalls, refreshes: f.refreshCalls,
    });
    const before = snapshot();
    await f.page(login, game.b);
    const denied = await f.post(`${login}/approve`, game.b), html = await denied.text();
    assert.equal(denied.status, 409); assert.match(html, conflict);
    assert.match(denied.headers.get('content-type')!, /^text\/html/u);
    assert.match(denied.headers.get('content-security-policy')!, /default-src 'none'/u);
    assert.equal(denied.headers.get('set-cookie'), null);
    assert(html.includes(sender.membership.connectionId));
    assert(html.includes(request.id.slice(-6).toUpperCase()));
    assert.match(html, /href="\/account#approved-connections"/u);
    assert.match(html, /선택한 시험 캠페인이 해제/u);
    assert.match(html, /광고는 직접 재선택/u);
    assert.doesNotMatch(html, /<script|http-equiv="refresh"/u);
    for (const privateValue of [sender.token, sender.sessionToken, sender.outputToken!, game.lease.token,
      bob.lease.membership.connectionId, request.verifier]) assert(!html.includes(privateValue));
    assert.equal(f.app.login.view(request.id).role, 'streaming');
    assert.equal(snapshot(), before, '409 does not consume consent, replace approvals or restart chat');
    assert.match(await f.page('/account', game.b), pendingNotice);
    assert.doesNotMatch(await f.page('/account', bob.b), pendingNotice);
    assert.equal(snapshot(), before, 'navigation cannot mutate authority or selection');

    const revoke = `/connections/${sender.membership.connectionId}/revoke`;
    assert.equal((await f.request(revoke, { headers: { Cookie: game.b.cookie } })).status, 404);
    assert.equal((await f.post(revoke, { ...game.b, csrf: '0'.repeat(64) })).status, 403);
    assert.equal((await f.request(revoke, { method: 'POST', headers: {
      Cookie: game.b.cookie, Origin: 'https://foreign.invalid', 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: new URLSearchParams({ csrf: game.b.csrf }) })).status, 403);
    assert.equal((await f.request(`${login}?returnTo=https://foreign.invalid`, {
      headers: { Cookie: game.b.cookie },
    })).status, 403);
    assert.equal(snapshot(), before, 'GET, foreign Origin, forged CSRF and return query cannot transfer the role');
    // Even an authenticated different owner cannot revoke the conflicting sender.
    assert.equal((await f.post(revoke, bob.b)).status, 303);
    assert(f.store.find(sender.sessionToken));

    const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    const removed = await f.post(revoke, game.b);
    assert.equal(removed.status, 303); assert.equal(removed.headers.get('location'), '/account');
    await closed;
    assert.equal(f.store.find(sender.sessionToken), undefined);
    const account = await f.page('/account', game.b);
    assert.match(account, pendingNotice); assert(account.includes(`href="${login}"`));
    assert.equal(f.app.login.view(request.id).code, request.id.slice(-6).toUpperCase());
    assert.deepEqual(await state(), HIDDEN_AD);
    assert.equal(sourceId(await f.page('/campaigns', game.b)), source);
    assert.equal((await report(sender.outputToken!, 2)).status, 401);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', sender.sessionToken, 'streaming')).status, 401);

    const newSender = await f.approve(request, 'streaming', game.b);
    assert.equal(newSender.membership.role, 'streaming');
    assert.notEqual(newSender.membership.connectionId, sender.membership.connectionId);
    assert.equal(newSender.membership.broadcastSessionId, sender.membership.broadcastSessionId);
    assert.doesNotMatch(await f.page('/account', game.b), pendingNotice);
    await open(newSender.token); assert.equal((await report(newSender.outputToken!)).status, 200);
    assert.deepEqual(await state(), HIDDEN_AD, 'new consent and report cannot restore the revoked campaign');
    assert.equal(f.exchangeCalls, 2, 'role transfer reuses the authenticated channel, not a new provider login');
    assert.equal(f.startedWith.length, 2, 'no duplicate or restarted upstream');
    assert(f.store.find(game.lease.sessionToken)); assert(f.store.find(bob.lease.sessionToken));
    const controller = new AbortController();
    const messages = on(gamePeer, 'message', { signal: AbortSignal.any([controller.signal, AbortSignal.timeout(3000)]) });
    try {
      const marker = 'game chat after explicit sender replacement';
      f.chats.get('alice')!.publish(marker);
      let received = false;
      for await (const [frame] of messages) if (String(frame).includes(marker)) { received = true; break; }
      assert(received); assert.equal(gamePeer.readyState, WebSocket.OPEN);
    } finally { controller.abort(); }
    assert.equal((await f.post(select, game.b)).status, 303);
    assert.equal(sourceId(await f.page('/campaigns', game.b)), source);
    assert.equal((await state()).state, 'visible');
  } finally { for (const peer of peers) peer.terminate(); await f.close(); }
});

test('conflict navigation is browser-local and account switching discards it without revoking either owner', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'streaming'), other = await f.connect('alice', 'gaming');
    const request = await f.start('streaming');
    await f.page(request.verificationPath, alice.b);
    assert.equal((await f.post(`${request.verificationPath}/approve`, alice.b)).status, 409);
    assert.match(await f.page('/account', alice.b), pendingNotice);
    assert.doesNotMatch(await f.page('/account', other.b), pendingNotice, 'same owner, different browser');
    assert.equal((await f.post('/account/switch', alice.b)).status, 303);
    const fresh = { cookie: '', csrf: '' }, bobRequest = await f.start('gaming');
    await f.authenticate('bob', bobRequest, fresh);
    assert.doesNotMatch(await f.page('/account', fresh), pendingNotice);
    assert(f.store.find(alice.lease.sessionToken)); assert(f.store.find(other.lease.sessionToken));
    assert.equal(f.app.login.view(request.id).role, 'streaming', 'switching identity does not approve or deny the old request');
    assert.equal((await f.post(`${request.verificationPath}/approve`, alice.b)).status, 403, 'old cookie cannot approve');
  } finally { await f.close(); }
});

for (const ending of ['deny', 'expire', 'clear'] as const) {
  test(`a ${ending}d conflict cannot leave a live return link or delete the old sender`, async t => {
    let now = 1000;
    t.mock.method(performance, 'now', () => now);
    const f = await fixture();
    try {
      const alice = await f.connect('alice', 'streaming'), request = await f.start('streaming');
      await f.page(request.verificationPath, alice.b);
      assert.equal((await f.post(`${request.verificationPath}/approve`, alice.b)).status, 409);
      if (ending === 'deny') f.app.login.deny(request.id); // Another tab acts while account page is open.
      else if (ending === 'expire') now += LOGIN_WINDOW_MS;
      else f.app.login.clear();
      const account = await f.page('/account', alice.b);
      assert.match(account, /data-streaming-request="unavailable"/u);
      assert(!account.includes(`href="${request.verificationPath}"`));
      assert.doesNotMatch(await f.page('/account', alice.b), /data-streaming-request/u, 'discard the consumed notice');
      const closed = await f.request(request.verificationPath, { headers: { Cookie: alice.b.cookie } });
      assert.equal(closed.status, ending === 'deny' ? 409 : 410);
      const html = await closed.text();
      assert.match(html, /채팅 연결 요청 종료/u); assert.doesNotMatch(html, /\/approve|\/revoke|<form/u);
      assert(f.store.find(alice.lease.sessionToken));
      assert.equal((await f.post(`${request.verificationPath}/approve`, alice.b)).status, ending === 'deny' ? 409 : 410);
    } finally { await f.close(); }
  });
}

test('a competing sender created during chat readiness gets the same explicit conflict, not a false approval', async t => {
  const f = await fixture();
  const entered = deferred<void>(), gate = deferred<void>();
  try {
    const alice = await f.connect('alice', 'gaming'), request = await f.start('streaming');
    await f.page(request.verificationPath, alice.b);
    const ensure = f.creators.ensureChat.bind(f.creators);
    t.mock.method(f.creators, 'ensureChat', async (owner: string) => {
      entered.resolve(); await gate.promise; await ensure(owner);
    });
    const response = f.post(`${request.verificationPath}/approve`, alice.b);
    await entered.promise;
    const competing = f.store.create('alice', 'streaming');
    gate.resolve();
    const result = await response;
    assert.equal(result.status, 409); assert.match(await result.text(), conflict);
    assert.equal(f.app.login.view(request.id).role, 'streaming');
    assert(f.store.find(competing.token)); assert(f.store.find(alice.lease.sessionToken));
    assert.match(await f.page('/account', alice.b), pendingNotice);
  } finally { gate.resolve(); await f.close(); }
});
