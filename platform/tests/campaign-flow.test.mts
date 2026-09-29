// SPDX-License-Identifier: GPL-2.0-or-later
// Real account HTTP, SQLite and ws boundaries; provider/OBS observations are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { BroadcastOutput } from '../server/chat/broadcast-output.mts';
import { HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { fixture, nonce } from './fixtures/service.mts';
type Fixture = Awaited<ReturnType<typeof fixture>>;
type Browser = { cookie: string; csrf: string };
const selectPath = `/campaigns/${TEST_CAMPAIGN.id}/select`;
const sourceId = (html: string) => {
  const match = /\/public\/ads\/([a-f0-9]{32})/u.exec(html);
  assert(match); return match[1]!;
};
async function select(f: Fixture, b: Browser) {
  await f.page('/campaigns', b);
  assert.equal((await f.post(selectPath, b)).status, 303);
  return sourceId(await f.page('/campaigns', b));
}
async function open(origin: string, token: string) {
  const peer = new WebSocket(origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000,
  });
  peer.on('error', () => {}); await once(peer, 'open'); return peer;
}
async function report(f: Fixture, token: string, sequence = 1) {
  const response = await f.request('/broadcast/output', { method: 'POST', headers: {
    Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
  }, body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
  assert.equal(response.status, 200);
}
const snapshot = async (f: Fixture, id: string) => {
  const response = await f.request(`/public/ads/${id}/state`);
  assert.equal(response.status, 200); assert.equal(response.headers.get('set-cookie'), null);
  assert.equal(response.headers.get('cache-control'), 'no-store');
  return await response.json();
};

test('existing account chooses a test campaign; separate public page renders only with the selected live sender', async () => {
  let now = 1000;
  const f = await fixture({ createDisplay: (access, origin, get) =>
    new DisplayGateway(access, origin, get, new BroadcastOutput(() => now)) });
  try {
    const alice = await f.connect('alice-private-channel', 'streaming');
    const bob = await f.connect('bob', 'streaming');
    const aliceId = await select(f, alice.b), bobId = await select(f, bob.b);
    const gameRequest = await f.start('gaming');
    const game = await f.approve(gameRequest, 'gaming', alice.b);
    assert.notEqual(aliceId, bobId);
    assert.deepEqual(await snapshot(f, aliceId), HIDDEN_AD);
    const alicePeer = await open(f.origin, alice.lease.token);
    const bobPeer = await open(f.origin, bob.lease.token);
    await report(f, bob.lease.outputToken!);
    assert.deepEqual(await snapshot(f, aliceId), HIDDEN_AD, 'another creator report does not enable this banner');
    await report(f, alice.lease.outputToken!);
    const visible = await snapshot(f, aliceId);
    assert.equal(visible.state, 'visible'); assert.equal(visible.mode, 'test');
    assert.deepEqual(visible.campaign, TEST_CAMPAIGN);
    for (const secret of [alice.lease.token, alice.lease.outputToken!, alice.lease.sessionToken,
      alice.lease.membership.broadcastSessionId, 'alice-private-channel']) assert(!JSON.stringify(visible).includes(secret));
    const page = await f.request(`/public/ads/${aliceId}`);
    assert.match(page.headers.get('content-security-policy')!, /script-src 'self';/u);
    const html = await page.text();
    assert.match(html, /id="banner" hidden/u); assert.doesNotMatch(html, /csrf|alice-private-channel|sessionToken/u);
    for (const asset of ['ad-source.js', 'ad-renderer.js', 'ad-source.css'])
      assert.equal((await f.request(`/public/ads/${asset}`)).status, 200);
    await f.page('/campaigns', alice.b);
    assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
    assert.deepEqual(await snapshot(f, aliceId), HIDDEN_AD);
    assert.equal((await snapshot(f, bobId)).state, 'visible');
    assert.ok(f.store.find(game.sessionToken));
    const nextMessage = once(alicePeer, 'message');
    f.chats.get('alice-private-channel')!.publish('private chat survives public ad stop');
    const [frame] = await nextMessage;
    assert.doesNotMatch(String(frame), /ad-snapshot|chatview-test/u);
    assert.equal(alicePeer.readyState, WebSocket.OPEN);
    assert.equal(await select(f, alice.b), aliceId);
    now += 15000;
    assert.deepEqual(await snapshot(f, aliceId), HIDDEN_AD, 'report expiry blanks the public output');
    now += 500; await report(f, alice.lease.outputToken!, 2);
    assert.equal((await snapshot(f, aliceId)).state, 'visible');
    const closed = once(alicePeer, 'close'); alicePeer.close(); await closed;
    // Retiring the remote socket is observed on the server by the next event turn.
    await new Promise<void>(resolve => setImmediate(resolve));
    assert.deepEqual(await snapshot(f, aliceId), HIDDEN_AD);
    bobPeer.terminate();
  } finally { await f.close(); }
});

test('public addresses and native capabilities cannot select campaigns or read private account state', async () => {
  const f = await fixture();
  try {
    assert.equal((await f.request('/campaigns')).status, 401);
    const alice = await f.connect('alice', 'gaming');
    await f.page('/campaigns', alice.b);
    const missing = await f.post(selectPath, alice.b);
    assert.equal(missing.status, 409); assert.match(await missing.text(), /송출 역할/u);
    assert.equal((await f.post('/campaigns/unknown/select', alice.b)).status, 404);
    const stream = await f.start('streaming'); await f.approve(stream, 'streaming', alice.b);
    const id = await select(f, alice.b);
    assert.equal((await f.request(selectPath, { method: 'POST', headers: {
      Authorization: `ChatView-Session ${alice.lease.sessionToken}`, 'Content-Type': 'application/x-www-form-urlencoded',
      Origin: f.origin }, body: new URLSearchParams({ csrf: alice.b.csrf }) })).status, 403);
    assert.equal((await f.request(selectPath, { method: 'POST', headers: {
      Cookie: alice.b.cookie, Origin: f.origin, 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: `csrf=${nonce()}` })).status, 403);
    assert.equal((await f.request(selectPath, { method: 'POST', headers: {
      Cookie: alice.b.cookie, Origin: 'https://foreign.invalid', 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: `csrf=${alice.b.csrf}` })).status, 403);
    assert.equal((await f.request(`/public/ads/${id}`, { method: 'POST' })).status, 405);
    const query = await f.request(`/public/ads/${id}?owner=alice`);
    assert.equal(query.status, 400); assert.equal(await query.text(), '', 'public error does not render private error text');
    assert.deepEqual(await snapshot(f, '0'.repeat(32)), HIDDEN_AD);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', id)).status, 401);
    assert.equal(f.exchangeCalls, 1, 'public reads never authenticate a provider');
  } finally { await f.close(); }
});

test('logout, reconsent and service restart do not silently restore revoked campaign selection', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-ad-flow-')), path = join(dir, 'sessions.sqlite'), key = randomBytes(32);
  const options = { path, key, createDisplay: (access: ConstructorParameters<typeof DisplayGateway>[0], origin: () => string, get: () => unknown) => new DisplayGateway(access, origin, get) };
  let f = await fixture(options);
  try {
    const alice = await f.connect('alice', 'streaming');
    const id = await select(f, alice.b);
    await f.close(); f = await fixture(options);
    assert.deepEqual(await snapshot(f, id), HIDDEN_AD);
    assert.equal(f.userCalls, 0); assert.equal(f.refreshCalls, 0); assert.equal(f.active.size, 0);
    const response = await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken, 'streaming');
    assert.equal(response.status, 200);
    const lease = await response.json() as { token: string; outputToken: string };
    await open(f.origin, lease.token); await report(f, lease.outputToken);
    assert.equal((await snapshot(f, id)).state, 'visible', 'normal restart retains a still-approved selection');
    assert.equal((await f.native('/display/signout', 'ChatView-Session', alice.lease.sessionToken)).status, 200);
    assert.deepEqual(await snapshot(f, id), HIDDEN_AD);
    const again = await f.connect('alice', 'streaming');
    await open(f.origin, again.lease.token); await report(f, again.lease.outputToken!);
    assert.deepEqual(await snapshot(f, id), HIDDEN_AD);
    assert.equal(await select(f, again.b), id, 'same public OBS URL, but explicit selection is required');
    f.failRevoke(); await f.creators.revoke('alice');
    assert.deepEqual(await snapshot(f, id), HIDDEN_AD, 'local revoke wins even when the provider is unavailable');
  } finally { await f.close(); rmSync(dir, { recursive: true, force: true }); }
});
