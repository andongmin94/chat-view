// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { SessionStore } from '../server/chat/session-store.mts';
import { Campaigns, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { CampaignActivity, ACTIVITY_RETENTION_MS, ACTIVITY_ROW_LIMIT } from '../server/ads/activity.mts';
import { activityPage } from '../server/ads/activity-page.mts';
import type { OutputAcceptance } from '../server/chat/broadcast-output.mts';
const base = 1790640000000;
function setup(path = ':memory:') {
  let now = base, tick = 1000;
  const sessions = new SessionStore(path, () => now);
  const campaigns = new Campaigns({ sessions, output: () => ({ state: 'unknown' }) });
  let activity = new CampaignActivity(sessions.database, () => now, () => tick);
  const alice = sessions.create('alice', 'streaming'), bob = sessions.create('bob', 'streaming');
  const receipt = (sequence: number, options: Partial<OutputAcceptance> = {}): OutputAcceptance => ({
    leaseId: 'lease-a', generation: 0, validForMs: 15000,
    report: { sequence, streaming: true, recording: false, sampleAgeMs: 0 }, ...options,
  });
  return { sessions, campaigns, alice, bob, receipt, get activity() { return activity; },
    select(owner = 'alice') { campaigns.select(owner, TEST_CAMPAIGN.id); activity.clear(owner); },
    advance(ms: number) { now += ms; tick += ms; },
    jump(ms: number) { now += ms; },
    restart() { activity.close(); tick = 1000; activity = new CampaignActivity(sessions.database, () => now, () => tick); },
    record(sequence: number, options: Partial<OutputAcceptance> = {}) { activity.record('alice', alice.membership!, receipt(sequence, options)); },
    close() { activity.close(); sessions.close(); },
  };
}

test('two accepted selected reports bracket time; first receipt and page reads never accrue', () => {
  const f = setup();
  try {
    f.record(1); assert.equal(f.activity.summary('alice').reports, 0);
    f.select(); f.record(2); assert.equal(f.activity.summary('alice').activeMs, 0);
    f.advance(5000); f.record(3);
    assert.equal(f.activity.summary('alice').streamingMs, 5000);
    f.advance(5000); f.record(4);
    const summary = f.activity.summary('alice');
    assert.equal(summary.activeMs, 10000); assert.equal(summary.rows.length, 2);
    assert.equal(summary.reports, 3); assert.equal(summary.rows[0]!.reports, 2);
    f.advance(5000);
    for (let i = 0; i < 10; i++) {
      f.campaigns.snapshot(f.campaigns.status('alice').sourceId!);
      assert.deepEqual(f.activity.summary('alice'), summary);
    }
    assert.equal(summary.estimatedViewerMs, null); assert.equal(summary.measuredAdViewerMs, null);
    assert.equal(summary.hp, null); assert.equal(summary.revenue, null); assert.equal(summary.payable, false);
  } finally { f.close(); }
});

test('preview, recording, streaming and simultaneous reports are distinct without double counting', () => {
  const f = setup();
  try {
    f.select();
    let seq = 0;
    const send = (streaming: boolean, recording: boolean) => {
      f.advance(1000); f.record(++seq, { report: { sequence: seq, streaming, recording, sampleAgeMs: 0 } });
    };
    send(false, false); send(false, false);
    assert.equal(f.activity.summary('alice').activeMs, 0);
    send(false, true); send(false, true);
    send(true, true); send(true, true);
    send(true, false); send(true, false);
    send(false, false); send(false, false);
    const s = f.activity.summary('alice');
    assert.equal(s.activeMs, 3000); assert.equal(s.streamingMs, 2000); assert.equal(s.recordingMs, 2000);
    assert.equal(s.idleMs, 2000); assert.equal(s.unknownIntervals, 5);
    assert(s.rows.some(row => row.reason === 'state-change' && row.durationMs === 0));
  } finally { f.close(); }
});

test('duplicate and out-of-order reports neither extend the tail nor change the cursor', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    const s = f.activity.summary('alice');
    f.advance(500); f.record(2); f.record(1);
    assert.deepEqual(f.activity.summary('alice'), s);
    f.advance(500); f.record(3);
    assert.equal(f.activity.summary('alice').activeMs, 2000);
  } finally { f.close(); }
});

test('sequence gaps, report lifetime and lease lifetime are excluded, not filled', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(3);
    let s = f.activity.summary('alice');
    assert.equal(s.rows[0]!.reason, 'sequence-gap'); assert.equal(s.activeMs, 0);
    f.advance(1000); f.record(4); assert.equal(f.activity.summary('alice').activeMs, 1000);
    f.advance(15000); f.record(5);
    assert.equal(f.activity.summary('alice').rows[0]!.reason, 'stale-gap');
    f.advance(1000); f.record(6, { validForMs: 600 });
    f.advance(600); f.record(7);
    s = f.activity.summary('alice');
    assert.equal(s.rows[0]!.reason, 'stale-gap'); assert.equal(s.activeMs, 2000);
  } finally { f.close(); }
});

test('same-lease reconnect generation and renewed leases require new brackets', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    f.advance(1000); f.record(3, { generation: 1 });
    assert.equal(f.activity.summary('alice').rows[0]!.reason, 'connection-change');
    assert.equal(f.activity.summary('alice').activeMs, 1000);
    f.advance(1000); f.record(4, { generation: 1 });
    f.advance(1000); f.record(1, { leaseId: 'renewed', generation: 1 });
    assert.equal(f.activity.summary('alice').activeMs, 2000);
    f.advance(1000); f.record(2, { leaseId: 'renewed', generation: 1 });
    assert.equal(f.activity.summary('alice').activeMs, 3000);
  } finally { f.close(); }
});

test('stop and immediate reselect preserve history but cannot interpolate the stopped gap', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    f.campaigns.stop('alice'); f.activity.clear('alice');
    f.advance(1000); f.record(3);
    assert.equal(f.activity.summary('alice').reports, 2);
    f.select(); f.advance(1000); f.record(4);
    assert.equal(f.activity.summary('alice').activeMs, 1000);
    f.advance(1000); f.record(5); assert.equal(f.activity.summary('alice').activeMs, 2000);
  } finally { f.close(); }
});

test('ownership, exact streaming approval and current selection gate all records', () => {
  const f = setup();
  try {
    f.select(); f.select('bob');
    f.activity.record('alice', f.bob.membership!, f.receipt(1));
    f.activity.record('bob', f.alice.membership!, f.receipt(1));
    const game = f.sessions.create('alice', 'gaming');
    f.activity.record('alice', game.membership!, f.receipt(1));
    assert.equal(f.activity.summary('alice').reports, 0); assert.equal(f.activity.summary('bob').reports, 0);
    f.record(1); f.advance(1000); f.record(2);
    f.sessions.remove(f.alice.token); f.advance(1000); f.record(3);
    assert.equal(f.activity.summary('alice').activeMs, 1000);
    const replacement = f.sessions.create('alice', 'streaming');
    f.activity.record('alice', replacement.membership!, f.receipt(1, { leaseId: 'replacement' }));
    assert.equal(f.activity.summary('alice').reports, 2, 'new consent needs explicit campaign selection');
    assert.equal(f.activity.summary('bob').reports, 0);
  } finally { f.close(); }
});

test('database errors fail the receipt and break continuity without inventing successful history', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    f.sessions.database.exec(`CREATE TRIGGER deny_activity BEFORE UPDATE ON campaign_activity
      BEGIN SELECT RAISE(ABORT, 'synthetic storage failure'); END;`);
    f.advance(1000); assert.throws(() => f.record(3));
    assert.equal(f.activity.summary('alice').activeMs, 1000);
    f.sessions.database.exec('DROP TRIGGER deny_activity');
    f.advance(1000); f.record(4);
    assert.equal(f.activity.summary('alice').rows[0]!.reason, 'first-report');
    assert.equal(f.activity.summary('alice').activeMs, 1000);
    f.advance(1000); f.record(5); assert.equal(f.activity.summary('alice').activeMs, 2000);
  } finally { f.close(); }
});

test('wall-clock jumps never generate large or negative measured durations', () => {
  const f = setup();
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    f.jump(3600000); f.advance(1000); f.record(3);
    assert.equal(f.activity.summary('alice').rows[0]!.reason, 'clock-change');
    f.jump(-7200000); f.advance(1000); f.record(4);
    const s = f.activity.summary('alice');
    assert.equal(s.activeMs, 1000); assert.equal(s.rows[0]!.from, s.rows[0]!.to);
    assert(s.rows.every(r => r.durationMs >= 0 && r.to >= r.from));
  } finally { f.close(); }
});

test('durable records survive reopening; volatile heads never bridge server downtime', () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-activity-')), path = join(dir, 'sessions.sqlite');
  const f = setup(path);
  let reopened: SessionStore | undefined;
  try {
    f.select(); f.record(1); f.advance(1000); f.record(2);
    const before = f.activity.summary('alice');
    f.restart(); f.advance(5000); f.record(3);
    assert.equal(f.activity.summary('alice').activeMs, before.activeMs);
    const membership = f.alice.membership!, receipt = f.receipt(1, { leaseId: 'after-restart' });
    const forbidden = [f.alice.token, 'lease-a', membership.connectionId, membership.broadcastSessionId];
    f.close();
    reopened = new SessionStore(path, () => base + 10000);
    const next = new CampaignActivity(reopened.database, () => base + 10000, () => 10);
    assert.equal(next.summary('alice').activeMs, 1000);
    next.record('alice', membership, receipt);
    assert.equal(next.summary('alice').activeMs, 1000);
    // Only inspect the history table: the approval DB legitimately owns IDs.
    const records = JSON.stringify(reopened.database.prepare('SELECT * FROM campaign_activity').all());
    for (const value of forbidden) assert(!records.includes(value));
    assert(!readFileSync(path).includes(Buffer.from(f.alice.token)));
    next.close();
  } finally { f.close(); reopened?.close(); rmSync(dir, { recursive: true, force: true }); }
});

test('retention, result size and per-owner row cap are explicit and bounded', () => {
  const f = setup();
  try {
    f.select(); f.select('bob');
    f.activity.record('bob', f.bob.membership!, f.receipt(1));
    for (let i = 1; i <= ACTIVITY_ROW_LIMIT + 10; i++) {
      f.advance(1000); f.record(i, { report: { sequence: i, streaming: i % 2 === 0, recording: false, sampleAgeMs: 0 } });
    }
    const s = f.activity.summary('alice');
    assert.equal(s.intervals, ACTIVITY_ROW_LIMIT); assert.equal(s.rows.length, 50);
    assert.equal(f.activity.summary('bob').reports, 1);
    f.advance(ACTIVITY_RETENTION_MS + 1000);
    assert.equal(f.activity.summary('alice').reports, 0); assert.equal(f.activity.summary('bob').reports, 0);
  } finally { f.close(); }
});

test('history page distinguishes unmeasured audience metrics, omitted gaps and non-payable totals', () => {
  const f = setup();
  try {
    const empty = activityPage(f.activity.summary('alice'));
    assert.match(empty, /아직 저장된 보고가 없습니다/u);
    f.select(); f.record(1); f.advance(1000); f.record(3);
    const html = activityPage(f.activity.summary('alice'));
    for (const label of ['비지급 시험 기록', '광고 노출·시청시간이 아닙니다', '미측정', '보고 순번 누락', '미산정', '최근 50건', '1,000건'])
      assert(html.includes(label));
    assert.doesNotMatch(html, /<script|<form|sessionToken|lease-a|onclick=/u);
    assert(!html.includes(f.alice.token)); assert(!html.includes(f.alice.membership!.connectionId));
    assert.match(html, /data-metric="active-ms" data-value="0"/u);
  } finally { f.close(); }
});
