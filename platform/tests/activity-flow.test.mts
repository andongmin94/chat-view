// SPDX-License-Identifier: GPL-2.0-or-later
// Actual account HTTP, SQLite and WebSocket; provider and OBS input are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { fixture } from './fixtures/service.mts';
type Fixture = Awaited<ReturnType<typeof fixture>>;
type Browser = { cookie: string; csrf: string };
const options = { createDisplay: (access: ConstructorParameters<typeof DisplayGateway>[0], origin: () => string,
  snapshot: () => unknown) => new DisplayGateway(access, origin, snapshot) };
async function open(origin: string, token: string) {
  const peer = new WebSocket(origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
  });
  peer.on('error', () => {}); await once(peer, 'open'); return peer;
}
const report = (f: Fixture, token: string, sequence: number, streaming = true, recording = false) =>
  f.request('/broadcast/output', { method: 'POST', headers: {
    Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
  }, body: JSON.stringify({ sequence, streaming, recording, sampleAgeMs: 0 }) });
async function select(f: Fixture, b: Browser) {
  await f.page('/campaigns', b);
  assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, b)).status, 303);
  const html = await f.page('/campaigns', b);
  assert.match(html, /\/campaigns\/activity/u);
  return /\/public\/ads\/([a-f0-9]{32})/u.exec(html)![1]!;
}
async function activeOnPage(f: Fixture, b: Browser) {
  const response = await f.request('/campaigns/activity', { headers: { Cookie: b.cookie } });
  assert.equal(response.status, 200); assert.equal(response.headers.get('cache-control'), 'no-store');
  const html = await response.text();
  assert.match(html, /비지급 시험 기록/u); assert.match(html, /미측정/u);
  assert.match(html, /data-evidence-stage="output"/u);
  assert.match(html, /data-evidence-stage="audience"/u);
  assert.match(html, /data-evidence-stage="exposure"/u);
  return Number(/data-metric="active-ms" data-value="(\d+)"/u.exec(html)![1]!);
}

test('selected accepted output becomes private non-payable history; public/display credentials cannot read or inflate it',
  { timeout: 20000 }, async () => {
  const f = await fixture(options);
  try {
    assert.equal((await f.request('/campaigns/activity')).status, 401);
    const alice = await f.connect('alice', 'streaming'), bob = await f.connect('bob', 'streaming');
    const gameRequest = await f.start('gaming'), game = await f.approve(gameRequest, 'gaming', alice.b);
    const id = await select(f, alice.b);
    const peer = await open(f.origin, alice.lease.token);
    const first = await report(f, alice.lease.outputToken!, 1);
    assert.equal(first.status, 200); assert.deepEqual(await first.json(), { accepted: true, sequence: 1 });
    assert.equal(await activeOnPage(f, alice.b), 0);
    await delay(600);
    assert.equal((await report(f, alice.lease.outputToken!, 2)).status, 200);
    const summary = f.creators.activity.summary('alice');
    assert(summary.activeMs >= 500 && summary.activeMs < 15000);
    assert.equal(await activeOnPage(f, alice.b), summary.activeMs);
    assert.equal(await activeOnPage(f, bob.b), 0);
    assert.equal((await report(f, alice.lease.outputToken!, 2)).status, 409);
    for (const token of [alice.lease.token, alice.lease.sessionToken, game.token, id]) {
      assert.notEqual((await report(f, token, 3)).status, 200);
      assert.equal((await f.request('/campaigns/activity', { headers: { Authorization: `Bearer ${token}` } })).status, 401);
    }
    assert.equal((await f.request('/campaigns/activity?owner=alice', { headers: { Cookie: bob.b.cookie } })).status, 403);
    for (let i = 0; i < 5; i++) {
      const body = await (await f.request(`/public/ads/${id}/state`)).text();
      assert.doesNotMatch(body, /activeMs|campaign_activity|estimatedViewer|alice|sessionToken/u);
      assert.equal(await activeOnPage(f, alice.b), summary.activeMs);
    }
    assert.deepEqual(f.creators.activity.summary('alice'), summary, 'reads and rejected reports do not write history');
    const html = await f.page('/campaigns/activity', alice.b);
    for (const value of [alice.lease.token, alice.lease.outputToken!, alice.lease.sessionToken,
      game.token, alice.lease.membership.connectionId]) assert(!html.includes(value));
    assert.match(await f.page('/account', alice.b), /\/campaigns\/activity/u);
    const next = once(peer, 'message'); f.chats.get('alice')!.publish('private chat still live');
    const [frame] = await next; assert.doesNotMatch(String(frame), /activeMs|campaign_activity|estimatedViewer/u);
    assert.equal(peer.readyState, WebSocket.OPEN);
    await f.page('/account', alice.b); await f.post('/logout', alice.b);
    assert.equal((await f.request('/campaigns/activity', { headers: { Cookie: alice.b.cookie } })).status, 401);
  } finally { await f.close(); }
});

test('real disconnect, lease renewal, campaign stop/reselect and revoke break the activity interval',
  { timeout: 20000 }, async () => {
  const f = await fixture(options);
  try {
    const alice = await f.connect('alice', 'streaming'); await select(f, alice.b);
    let peer = await open(f.origin, alice.lease.token);
    assert.equal((await report(f, alice.lease.outputToken!, 1)).status, 200);
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 2)).status, 200);
    let total = await activeOnPage(f, alice.b);
    const closed = once(peer, 'close'); peer.close(); await closed;
    await new Promise<void>(resolve => setImmediate(resolve));
    assert.equal((await report(f, alice.lease.outputToken!, 3)).status, 409);
    peer = await open(f.origin, alice.lease.token); await delay(600);
    assert.equal((await report(f, alice.lease.outputToken!, 3)).status, 200);
    assert.equal(await activeOnPage(f, alice.b), total, 'same lease reconnect is a new bracket');
    assert.equal(f.creators.activity.summary('alice').rows[0]!.reason, 'connection-change');
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 4)).status, 200);
    total = await activeOnPage(f, alice.b);
    const replacement = await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken, 'streaming');
    assert.equal(replacement.status, 200);
    const lease = await replacement.json() as { token: string; outputToken: string };
    await open(f.origin, lease.token);
    assert.equal((await report(f, lease.outputToken, 1)).status, 200);
    assert.equal(await activeOnPage(f, alice.b), total, 'renewal never bridges leases');
    await f.page('/campaigns', alice.b);
    assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
    await select(f, alice.b); await delay(600);
    assert.equal((await report(f, lease.outputToken, 2)).status, 200);
    assert.equal(await activeOnPage(f, alice.b), total, 'stop and immediate reselect need two new reports');
    await delay(600); assert.equal((await report(f, lease.outputToken, 3)).status, 200);
    total = await activeOnPage(f, alice.b);
    assert.equal((await f.native('/display/signout', 'ChatView-Session', alice.lease.sessionToken)).status, 200);
    assert.equal((await report(f, lease.outputToken, 4)).status, 401);
    assert.equal(await activeOnPage(f, alice.b), total, 'past non-payable history survives role revocation');
    const again = await f.connect('alice', 'streaming'); await open(f.origin, again.lease.token);
    const before = f.creators.activity.summary('alice');
    assert.equal((await report(f, again.lease.outputToken!, 1)).status, 200);
    assert.deepEqual(f.creators.activity.summary('alice'), before, 'new consent alone does not reselect campaign');
  } finally { await f.close(); }
});

test('storage error is a sanitized HTTP failure; chat stays live and later records cannot bridge missing writes',
  { timeout: 15000 }, async () => {
  const f = await fixture(options);
  try {
    const alice = await f.connect('alice', 'streaming'); await select(f, alice.b);
    const peer = await open(f.origin, alice.lease.token);
    assert.equal((await report(f, alice.lease.outputToken!, 1)).status, 200);
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 2)).status, 200);
    const before = f.creators.activity.summary('alice');
    f.store.database.exec(`CREATE TRIGGER deny_activity BEFORE UPDATE ON campaign_activity
      BEGIN SELECT RAISE(ABORT, 'PRIVATE_STORAGE_ERROR'); END;`);
    await delay(600);
    const failed = await report(f, alice.lease.outputToken!, 3);
    assert.equal(failed.status, 503); assert.doesNotMatch(await failed.text(), /PRIVATE_STORAGE_ERROR/u);
    assert.deepEqual(f.creators.activity.summary('alice'), before);
    assert.equal(peer.readyState, WebSocket.OPEN);
    f.store.database.exec('DROP TRIGGER deny_activity');
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 4)).status, 200);
    assert.equal(await activeOnPage(f, alice.b), before.activeMs);
    assert.equal(f.creators.activity.summary('alice').rows[0]!.reason, 'first-report');
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 5)).status, 200);
    assert((await activeOnPage(f, alice.b)) > before.activeMs);
  } finally { await f.close(); }
});

test('service restart retains private history and selection but never invents activity during downtime',
  { timeout: 15000 }, async () => {
  const directory = mkdtempSync(join(tmpdir(), 'chatview-activity-flow-'));
  const persistent = { ...options, path: join(directory, 'sessions.sqlite'), key: randomBytes(32) };
  let f = await fixture(persistent);
  try {
    const alice = await f.connect('alice', 'streaming'), source = await select(f, alice.b);
    await open(f.origin, alice.lease.token);
    assert.equal((await report(f, alice.lease.outputToken!, 1)).status, 200);
    await delay(600); assert.equal((await report(f, alice.lease.outputToken!, 2)).status, 200);
    const total = await activeOnPage(f, alice.b);
    await f.close(); await delay(600); f = await fixture(persistent);
    assert.equal(f.creators.activity.summary('alice').activeMs, total);
    assert.equal((await f.request('/campaigns/activity', { headers: { Cookie: alice.b.cookie } })).status, 401);
    assert.equal((await (await f.request(`/public/ads/${source}/state`)).json()).state, 'hidden');
    assert.equal(f.exchangeCalls, 0); assert.equal(f.userCalls, 0);
    const resumed = await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken, 'streaming');
    assert.equal(resumed.status, 200);
    const lease = await resumed.json() as { token: string; outputToken: string };
    await open(f.origin, lease.token);
    assert.equal((await report(f, lease.outputToken, 1)).status, 200);
    assert.equal(f.creators.activity.summary('alice').activeMs, total);
    const pending = await f.start('gaming');
    await f.authenticate('alice', pending);
    assert.equal(await activeOnPage(f, f.browser), total);
    assert.match(await f.page('/campaigns/activity'), /새 관측의 첫 보고/u);
  } finally { await f.close(); rmSync(directory, { recursive: true, force: true }); }
});
