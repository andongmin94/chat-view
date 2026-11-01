// SPDX-License-Identifier: GPL-2.0-or-later
// Existing accepted-output/SQLite path; audience counts are synthetic, never payable.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { SessionStore } from '../server/chat/session-store.mts';
import { Campaigns, TEST_CAMPAIGN, TEST_CAMPAIGN_ALT } from '../server/ads/campaigns.mts';
import { CampaignActivity, ACTIVITY_RETENTION_MS } from '../server/ads/activity.mts';
import { activityPage } from '../server/ads/activity-page.mts';
import { campaignsPage } from '../server/ads/pages.mts';
import type { AudienceView } from '../server/chzzk/audience.mts';

const base = 1790640000000;
function fixture(path = ':memory:') {
  let at = base, tick = 1000, sequence = 0;
  let sample: AudienceView = { state: 'sampled', viewers: 6, liveId: 123,
    requestedAt: at, validForMs: 90000, continuity: 'trial-sample' };
  const sessions = new SessionStore(path, () => at);
  const campaigns = new Campaigns({ sessions, output: () => ({ state: 'unknown' }) });
  const alice = sessions.create('alice', 'streaming'), bob = sessions.create('bob', 'streaming');
  let activity = new CampaignActivity(sessions.database, () => at, () => tick, () => sample);
  campaigns.select('alice', TEST_CAMPAIGN.id);
  return { sessions, campaigns, alice, bob, get activity() { return activity; },
    trial(campaign: string = TEST_CAMPAIGN.id) { return activity.trials('alice').find(row => row.campaignId === campaign)!; },
    advance(ms = 5000) {
      at += ms; tick += ms;
      if (sample.state === 'sampled') sample = { ...sample, validForMs: sample.validForMs - ms };
    },
    sample(value: AudienceView) { sample = value; },
    count(viewers: number) {
      sample = { state: 'sampled', viewers, liveId: 123, requestedAt: at,
        validForMs: 90000, continuity: 'trial-sample' };
    },
    report(options: { sequence?: number; streaming?: boolean; recording?: boolean; leaseId?: string } = {}) {
      sequence = options.sequence ?? sequence + 1;
      activity.record('alice', alice.membership!, { leaseId: options.leaseId ?? 'trial-lease', generation: 0,
        validForMs: 15000, report: { sequence, streaming: options.streaming ?? true,
          recording: options.recording ?? false, sampleAgeMs: 0 } });
    },
    select(campaign: string) { campaigns.select('alice', campaign); activity.clear('alice'); },
    restart() { activity.close(); activity = new CampaignActivity(sessions.database, () => at, () => tick, () => sample); },
    close() { activity.close(); sessions.close(); },
  };
}

test('trial keeps fractional progress and credits each whole HP exactly once without page-read accrual', () => {
  const f = fixture();
  try {
    assert.equal(f.trial().remainingHp, 100);
    assert.equal(f.trial().startedAt, null);
    assert.equal(f.sessions.database.prepare('SELECT COUNT(*) AS n FROM campaign_trial_balances').get()!.n, 0);
    f.report(); f.advance(); f.report();
    assert.equal(f.trial().creditedViewerMs, 30000);
    assert.equal(f.trial().remainingHp, 100);
    assert.equal(f.trial().points, 0);
    assert.equal(f.trial().progressViewerMs, 30000);
    f.report({ sequence: 2 });
    assert.equal(f.trial().creditedViewerMs, 30000, 'duplicate accepted sequence cannot charge twice');
    f.advance(); f.report();
    const before = f.activity.summary('alice');
    assert.equal(f.trial().remainingHp, 99); assert.equal(f.trial().points, 1);
    assert.equal(f.trial().progressViewerMs, 0);
    assert.equal(before.trial.rewards.length, 1);
    assert.deepEqual(before.trial.rewards.map(row => [row.consumedHp, row.points]), [[1, 1]]);
    for (let i = 0; i < 5; i++) {
      const html = activityPage(f.activity.summary('alice'), f.activity.evidence('alice'));
      assert.match(html, /data-trial-hp="99"/u); assert.match(html, /data-trial-points="1"/u);
      assert.match(html, /현금 가치 없음/u); assert.match(html, /과거 활동은 소급하지 않습니다/u);
      assert.match(html, /data-evidence-state="not-measured"/u);
    }
    assert.deepEqual(f.activity.summary('alice'), before);
    assert.equal(before.hp, null); assert.equal(before.revenue, null); assert.equal(before.measuredAdViewerMs, null);
    assert.equal(before.payable, false);
    assert(f.activity.trials('bob').every(row => row.points === 0 && row.remainingHp === 100));
    assert.deepEqual(f.activity.trialRewards('bob'), []);
  } finally { f.close(); }
});

test('trial clamps at 100 HP and points while the selected public source is left intact', () => {
  const f = fixture();
  try {
    f.count(1_000_000); const selected = f.campaigns.status('alice');
    f.report(); f.advance(); f.report();
    assert.equal(f.trial().creditedViewerMs, 6_000_000);
    assert.equal(f.trial().remainingHp, 0); assert.equal(f.trial().points, 100);
    assert.equal(f.trial().progressViewerMs, 0); assert.equal(f.activity.trialRewards('alice').length, 100);
    f.advance(); f.report();
    assert.equal(f.trial().points, 100); assert.equal(f.activity.trialRewards('alice').length, 100);
    assert.deepEqual(f.campaigns.status('alice'), selected, 'HP exhaustion never stops or changes the banner');
    const html = campaignsPage(selected, 'a'.repeat(64), 'https://service.invalid', {
      selectionState: 'ready', streamingConnected: true, previewReady: false,
      output: { state: 'unknown' }, viewToken: 'b'.repeat(64), trial: f.activity.trials('alice') });
    assert.match(html, /data-campaign-trial="chatview-test"[\s\S]*?0 \/ 100 HP/u);
    assert.match(html, /시험 적립만 중단됩니다\. 공개 배너와 선택은 자동 변경되지 않습니다/u);
    assert.match(activityPage(f.activity.summary('alice'), f.activity.evidence('alice')), /data-trial-state="exhausted"/u);
  } finally { f.close(); }
});

test('missing samples, real zero, recording-only and preview are distinct and yield no trial charge', () => {
  const f = fixture();
  try {
    f.sample({ state: 'unavailable', reason: 'disabled' });
    f.report(); f.advance(); f.report();
    assert.equal(f.trial().startedAt, null); assert.equal(f.activity.summary('alice').estimatedViewerMs, null);
    f.count(0); f.advance(); f.report(); f.advance(); f.report();
    assert.notEqual(f.trial().startedAt, null, 'a sampled zero is measured evidence, unlike missing samples');
    assert.equal(f.activity.summary('alice').estimatedViewerMs, 0); assert.equal(f.trial().points, 0);
    f.count(6);
    for (const recording of [false, true]) {
      f.advance(); f.report({ streaming: false, recording }); f.advance(); f.report({ streaming: false, recording });
    }
    assert.equal(f.trial().creditedViewerMs, 0);
    f.advance(); f.report({ recording: true }); f.advance(); f.report({ recording: true });
    assert.equal(f.trial().creditedViewerMs, 30000, 'simultaneous stream/record is counted once');
  } finally { f.close(); }
});

test('stop, creative switch, reselect, approval revoke and restart preserve separate trial balances', () => {
  const f = fixture();
  try {
    f.report(); f.advance(); f.report();
    f.campaigns.stop('alice'); f.activity.clear('alice');
    f.advance(); f.report(); assert.equal(f.trial().creditedViewerMs, 30000);
    f.select(TEST_CAMPAIGN_ALT.id); f.advance(); f.report(); f.advance(); f.report();
    assert.equal(f.trial(TEST_CAMPAIGN_ALT.id).creditedViewerMs, 30000);
    assert.equal(f.trial().creditedViewerMs, 30000);
    f.select(TEST_CAMPAIGN.id); f.advance(); f.report();
    assert.equal(f.trial().creditedViewerMs, 30000, 'reselect has a fresh bracket and no reset');
    f.restart(); f.advance(); f.report(); assert.equal(f.trial().creditedViewerMs, 30000);
    f.advance(); f.report(); assert.equal(f.trial().points, 1);
    f.sessions.removeOwner('alice'); f.advance(); f.report();
    assert.equal(f.trial().points, 1, 'revoking approvals cannot reset the creator trial');
    assert.equal(f.trial(TEST_CAMPAIGN_ALT.id).creditedViewerMs, 30000);
  } finally { f.close(); }
});

test('trial reward failure rolls back activity, estimates and balance and breaks the next bracket', () => {
  const f = fixture();
  try {
    f.report(); f.advance(); f.report();
    f.sessions.database.exec("CREATE TRIGGER reject_trial BEFORE INSERT ON campaign_trial_rewards BEGIN SELECT RAISE(ABORT, 'private trial failure'); END");
    f.advance(); const before = f.activity.summary('alice'); assert.throws(() => f.report());
    assert.deepEqual(f.activity.summary('alice'), before);
    f.sessions.database.exec('DROP TRIGGER reject_trial');
    f.advance(); f.report(); assert.equal(f.trial().creditedViewerMs, 30000);
    f.advance(); f.report(); assert.equal(f.trial().creditedViewerMs, 60000);
    assert.equal(f.activity.trialRewards('alice').length, 1);
  } finally { f.close(); }
});

test('preexisting activity and estimates are never converted into trial HP on reads or restart', () => {
  const f = fixture();
  try {
    const inserted = f.sessions.database.prepare(`INSERT INTO campaign_activity
      (owner,campaign_id,from_at,to_at,duration_ms,state,reason,reports)
      VALUES ('alice',?, ?,?,5000,'streaming','continuous',2)`)
      .run(TEST_CAMPAIGN.id, base - 5000, base);
    f.sessions.database.prepare('INSERT INTO campaign_audience VALUES (?,6000000,5000)').run(inserted.lastInsertRowid);
    f.restart();
    assert.equal(f.activity.summary('alice').estimatedViewerMs, 6_000_000);
    assert.equal(f.trial().points, 0); assert.equal(f.trial().remainingHp, 100);
    f.report(); f.advance(); f.report();
    assert.equal(f.trial().creditedViewerMs, 30000, 'only the newly accepted delta is credited');
  } finally { f.close(); }
});

test('retention removes individual rewards without replenishing the durable trial balance', () => {
  const directory = mkdtempSync(join(tmpdir(), 'chatview-trial-')), path = join(directory, 'trial.sqlite');
  const f = fixture(path);
  try {
    f.report(); f.advance(); f.report(); f.advance(); f.report();
    assert.equal(f.trial().points, 1);
    f.advance(ACTIVITY_RETENTION_MS + 1); const summary = f.activity.summary('alice');
    assert.equal(summary.intervals, 0); assert.deepEqual(summary.trial.rewards, []);
    assert.equal(f.trial().remainingHp, 99); assert.equal(f.trial().points, 1);
    f.close();
    const sessions = new SessionStore(path, () => base + ACTIVITY_RETENTION_MS + 20000);
    const restored = new CampaignActivity(sessions.database, () => base + ACTIVITY_RETENTION_MS + 20000);
    try {
      assert.equal(restored.trials('alice').find(row => row.campaignId === TEST_CAMPAIGN.id)!.points, 1);
      assert.deepEqual(restored.trialRewards('alice'), []);
    } finally { restored.close(); sessions.close(); }
  } finally { f.close(); rmSync(directory, { recursive: true, force: true }); }
});
