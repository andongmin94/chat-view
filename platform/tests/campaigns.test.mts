// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { SessionStore, SESSION_MS } from '../server/chat/session-store.mts';
import { Campaigns, CampaignError, TEST_CAMPAIGN, HIDDEN_AD } from '../server/ads/campaigns.mts';
import type { OutputView } from '../server/chat/broadcast-output.mts';

const denied = (status: number) => (e: unknown) => e instanceof CampaignError && e.status === status;
const reported = (): OutputView => ({ state: 'reported', streaming: false, recording: false, expiresInMs: 15000 });

test('only a registered test campaign and an approved streaming connection can be selected', () => {
  const sessions = new SessionStore(), ads = new Campaigns({ sessions, output: reported });
  try {
    assert.throws(() => ads.select('alice', 'unknown'), denied(404));
    assert.throws(() => ads.select('alice', TEST_CAMPAIGN.id), denied(409));
    sessions.create('alice', 'gaming');
    assert.throws(() => ads.select('alice', TEST_CAMPAIGN.id), denied(409));
    sessions.create('bob', 'streaming');
    assert.throws(() => ads.select('alice', TEST_CAMPAIGN.id), denied(409));
    sessions.create('alice', 'streaming'); ads.select('alice', TEST_CAMPAIGN.id);
    assert.equal(ads.status('alice').selected, true);
    assert.match(ads.status('alice').sourceId!, /^[a-f0-9]{32}$/u);
    assert.deepEqual(ads.status('bob'), { selected: false });
  } finally { sessions.close(); }
});

test('public snapshots contain only artwork; absence, stop and invalid ID disclose no creator state', () => {
  const sessions = new SessionStore();
  const seen: string[] = [];
  const ads = new Campaigns({ sessions, output: (owner, membership) => {
    seen.push(owner); assert.equal(membership.role, 'streaming'); return reported();
  } });
  try {
    const approval = sessions.create('private-alice-channel', 'streaming');
    ads.select('private-alice-channel', TEST_CAMPAIGN.id);
    const id = ads.status('private-alice-channel').sourceId!;
    const frame = ads.snapshot(id);
    assert.deepEqual(frame, { type: 'ad-snapshot', version: 1, mode: 'test', state: 'visible',
      expiresInMs: 15000, campaign: TEST_CAMPAIGN });
    for (const privateValue of [approval.token, approval.id, approval.membership!.broadcastSessionId, 'private-alice-channel'])
      assert.equal(JSON.stringify(frame).includes(privateValue), false);
    assert.deepEqual(ads.snapshot('invalid'), HIDDEN_AD);
    assert.deepEqual(ads.snapshot('0'.repeat(32)), HIDDEN_AD);
    assert.deepEqual(seen, ['private-alice-channel'], 'unknown public address never loads an account');
    ads.stop('private-alice-channel'); ads.stop('private-alice-channel');
    assert.deepEqual(ads.snapshot(id), HIDDEN_AD);
    assert.ok(sessions.find(approval.token), 'stopping an ad does not revoke private chat');
    ads.select('private-alice-channel', TEST_CAMPAIGN.id);
    assert.equal(ads.status('private-alice-channel').sourceId, id, 'OBS source address survives stop/reselect');
  } finally { sessions.close(); }
});

test('selection expires with reports and approval, never fabricating freshness or exposure', () => {
  let now = 1000;
  let output: OutputView = { state: 'unknown' };
  const sessions = new SessionStore(':memory:', () => now);
  const ads = new Campaigns({ sessions, output: () => output });
  try {
    sessions.create('alice', 'streaming'); ads.select('alice', TEST_CAMPAIGN.id);
    const id = ads.status('alice').sourceId!;
    assert.deepEqual(ads.snapshot(id), HIDDEN_AD);
    output = { state: 'reported', streaming: false, recording: true, expiresInMs: 4567 };
    assert.equal(ads.snapshot(id).expiresInMs, 4567, 'preview does not require a broadcast or become an exposure count');
    now += SESSION_MS - 300;
    assert.equal(ads.snapshot(id).expiresInMs, 300, 'native approval expiry further bounds the renderer');
    now += 300;
    assert.deepEqual(ads.snapshot(id), HIDDEN_AD);
    assert.equal(ads.status('alice').selected, false);
  } finally { sessions.close(); }
});

test('streaming logout atomically deletes its selection but preserves the stable public address and other accounts', () => {
  const sessions = new SessionStore(), ads = new Campaigns({ sessions, output: reported });
  try {
    const game = sessions.create('alice', 'gaming'), stream = sessions.create('alice', 'streaming');
    const bob = sessions.create('bob', 'streaming');
    ads.select('alice', TEST_CAMPAIGN.id); ads.select('bob', TEST_CAMPAIGN.id);
    const aliceId = ads.status('alice').sourceId!, bobId = ads.status('bob').sourceId!;
    sessions.remove(stream.token);
    assert.deepEqual(ads.snapshot(aliceId), HIDDEN_AD);
    assert.equal(sessions.database.prepare('SELECT count(*) AS n FROM ad_selections').get()?.n, 1);
    assert.ok(sessions.find(game.token)); assert.ok(sessions.find(bob.token));
    sessions.create('alice', 'streaming');
    assert.deepEqual(ads.snapshot(aliceId), HIDDEN_AD, 'new login never opts back into an old ad');
    ads.select('alice', TEST_CAMPAIGN.id); assert.equal(ads.status('alice').sourceId, aliceId);
    assert.equal(ads.snapshot(bobId).state, 'visible');
    sessions.removeOwner('alice');
    assert.deepEqual(ads.snapshot(aliceId), HIDDEN_AD);
    assert.equal(ads.snapshot(bobId).state, 'visible');
  } finally { sessions.close(); }
});

test('only the selected owner is stopped and an atomic selection failure leaves no orphan public source', () => {
  const sessions = new SessionStore(), ads = new Campaigns({ sessions, output: reported });
  try {
    sessions.create('alice', 'streaming'); sessions.create('bob', 'streaming');
    ads.select('alice', TEST_CAMPAIGN.id);
    const id = ads.status('alice').sourceId!;
    ads.stop('bob'); assert.equal(ads.snapshot(id).state, 'visible');
    sessions.database.exec(`CREATE TRIGGER deny_test_ad BEFORE INSERT ON ad_selections
      BEGIN SELECT RAISE(ABORT, 'synthetic storage failure'); END;`);
    assert.throws(() => ads.select('bob', TEST_CAMPAIGN.id));
    assert.deepEqual(ads.status('bob'), { selected: false });
    assert.equal(ads.snapshot(id).state, 'visible');
  } finally { sessions.close(); }
});

test('normal reopen retains selection, but only a fresh live sender can make the renderer visible again', () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-ads-')), path = join(dir, 'sessions.sqlite');
  let sessions = new SessionStore(path);
  try {
    const stream = sessions.create('alice', 'streaming');
    let ads = new Campaigns({ sessions, output: reported });
    ads.select('alice', TEST_CAMPAIGN.id); const id = ads.status('alice').sourceId!;
    sessions.close(); sessions = new SessionStore(path);
    let ready = false;
    ads = new Campaigns({ sessions, output: () => ready ? reported() : { state: 'unknown' } });
    assert.equal(ads.status('alice').sourceId, id); assert.equal(ads.status('alice').selected, true);
    assert.deepEqual(ads.snapshot(id), HIDDEN_AD);
    ready = true; assert.equal(ads.snapshot(id).state, 'visible');
    sessions.remove(stream.token);
    sessions.close(); sessions = new SessionStore(path);
    ads = new Campaigns({ sessions, output: reported });
    assert.deepEqual(ads.snapshot(id), HIDDEN_AD, 'logout cannot return through process restart');
    assert.equal(ads.status('alice').sourceId, id);
  } finally { sessions.close(); rmSync(dir, { recursive: true, force: true }); }
});
