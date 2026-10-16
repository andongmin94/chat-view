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
