// SPDX-License-Identifier: GPL-2.0-or-later
// Real existing SessionStore/Campaigns/SQLite; provider counts are synthetic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { SessionStore } from '../server/chat/session-store.mts';
import { Campaigns, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { CampaignActivity, ACTIVITY_RETENTION_MS } from '../server/ads/activity.mts';
import { activityCsv, activityPage, needsActivityReview } from '../server/ads/activity-page.mts';
import type { AudienceView } from '../server/chzzk/audience.mts';
import type { OutputAcceptance } from '../server/chat/broadcast-output.mts';
const base = 1790640000000;
function fixture(path = ':memory:') {
  let at = base, tick = 1000, sequence = 0;
  let sample: AudienceView = {state:'sampled', viewers:12, liveId:123, requestedAt:base,
    validForMs:90000, continuity:'sample-epoch'};
  const sessions = new SessionStore(path, () => at);
  const campaigns = new Campaigns({sessions, output:() => ({state:'unknown'})});
  const approval = sessions.create('alice', 'streaming');
  campaigns.select('alice', TEST_CAMPAIGN.id);
  let activity = new CampaignActivity(sessions.database, () => at, () => tick, () => sample);
  return { sessions, campaigns, approval, get activity() { return activity; },
    sample(value: AudienceView) { sample = value; },
    count(viewers: number, liveId = 123, continuity = 'sample-epoch') {
      sample = {state:'sampled', viewers, liveId, requestedAt:at, validForMs:90000, continuity};
    },
    advance(ms=5000) { at += ms; tick += ms; if(sample.state==='sampled') sample = {...sample,validForMs:sample.validForMs-ms}; },
    report(streaming = true, recording = false, overrides: Partial<OutputAcceptance> = {}) {
      activity.record('alice', approval.membership!, {leaseId:'lease', generation:0, validForMs:15000,
        report: {sequence:++sequence,streaming,recording,sampleAgeMs:0}, ...overrides});
    },
    restart() { activity.close(); activity = new CampaignActivity(sessions.database, () => at, () => tick, () => sample); },
    close() { activity.close(); sessions.close(); },
  };
}

test('last valid sample is held only across accepted consecutive streaming reports with explicit coverage', () => {
  const f=fixture();
  try {
    f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,null);
    f.advance(); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,60000);
    f.advance(); f.count(30); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,120000,'new count does not retroactively replace previous interval count');
    f.advance(); f.report(); const s=f.activity.summary('alice');
    assert.equal(s.estimatedViewerMs,270000); assert.equal(s.audienceCoverageMs,15000); assert.equal(s.audienceUnmeasuredMs,0);
    assert.equal(s.activeMs,15000); assert.equal(s.audienceMethod,'last-sample-hold');
    assert.equal(s.measuredAdViewerMs,null); assert.equal(s.hp,null); assert.equal(s.revenue,null); assert.equal(s.payable,false);
    const html=activityPage(s);
    assert.match(html,/data-evidence-stage="output"[\s\S]*?data-evidence-state="reported-intervals"/u);
    assert.match(html,/data-evidence-stage="audience"[\s\S]*?data-evidence-state="covered"/u);
    assert.match(html,/data-evidence-stage="exposure"[\s\S]*?data-evidence-state="not-measured"/u);
    assert.equal(f.sessions.database.prepare('SELECT COUNT(*) AS n FROM campaign_audience').get()!.n,1,'estimates coalesce with activity, not unbounded sample rows');
    const before=s.estimatedViewerMs; f.advance();
    for(let i=0;i<10;i++) assert.equal(f.activity.summary('alice').estimatedViewerMs,before,'reading never extrapolates or accrues');
  } finally {f.close();}
});

test('unknown is null, explicit observed zero is numeric zero, with uncovered streaming time separate', () => {
  const f=fixture();
  try {
    f.sample({state:'unavailable',reason:'not-found'}); f.report(); f.advance(); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,null);
    f.advance(); f.count(0); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,null,'one new sample cannot backfill');
    f.advance(); f.report(); const s=f.activity.summary('alice');
    assert.equal(s.estimatedViewerMs,0); assert.equal(s.audienceCoverageMs,5000); assert.equal(s.audienceUnmeasuredMs,10000);
    const html=activityPage(s);
    assert.match(html,/0\.000 시청자·분/u);
    assert.match(html,/data-evidence-state="partial"/u);
    assert.match(html,/유효한 공식 시청자 0명 표본으로 계산된 추정값 0/u);
    assert.match(html,/표본이 없던 송출 구간은 추가하지 않았습니다/u);
    assert.match(html,/data-evidence-state="not-measured"/u);
  } finally {f.close();}
});

test('non-payable CSV distinguishes missing audience evidence from explicitly sampled zero', () => {
  const f = fixture();
  try {
    f.sample({ state: 'unavailable', reason: 'not-found' });
    f.report(); f.advance(); f.report();
    f.advance(); f.count(0); f.report();
    f.advance(); f.report();
    const evidence = f.activity.evidence('alice');
    assert.equal(evidence.length, 2);
    const streaming = evidence.find(row => row.reason === 'continuous')!;
    assert.equal(streaming.durationMs, 15000);
    assert.equal(streaming.estimatedViewerMs, 0, 'sampled zero is persisted as numeric 0');
    assert.equal(streaming.audienceCoverageMs, 5000, 'not all output time had a valid sample');
    const first = evidence.find(row => row.reason === 'first-report')!;
    assert.equal(first.estimatedViewerMs, null);
    assert.equal(first.audienceCoverageMs, null);
    assert.deepEqual(f.activity.evidence('other-creator'), [], 'no cross-account evidence');
    const csv = activityCsv(evidence);
    const [header, ...rows] = csv.trimEnd().split('\r\n').map(line => line.split(','));
    const field = (row: string[], name: string) =>
      row[header!.indexOf(name)]!.replace(/^"|"$/gu, '');
    const sampled = rows.find(row => field(row, 'report_reason') === 'continuous')!;
    assert.equal(field(sampled, 'channel_viewer_estimate_ms'), '0');
    assert.equal(field(sampled, 'channel_sample_covered_ms'), '5000');
    assert.equal(field(sampled, 'channel_sample_coverage'), 'partial');
    for (const name of ['measured_ad_view_ms', 'hp', 'revenue'])
      assert.equal(field(sampled, name), '', 'no invented payable evidence');
    const unmeasured = rows.find(row => field(row, 'report_reason') === 'first-report')!;
    assert.equal(field(unmeasured, 'channel_viewer_estimate_ms'), '');
    assert.equal(field(unmeasured, 'channel_sample_coverage'), 'unmeasured');
    const formula = activityCsv([{ ...streaming, campaignId: '=1+1' }]);
    assert.match(formula, /"'=1\+1"/u, 'CSV text cannot execute spreadsheet formulas');
    assert.equal(activityCsv([]).trimEnd().split('\r\n').length, 1,
      'no samples produces only a header and never a fake zero-viewer row');
  } finally { f.close(); }
});

test('recording-only and preview do not yield viewer time, and simultaneous streaming/recording is counted once', () => {
  const f=fixture();
  try {
    for(const [stream,record] of [[false,false],[false,true],[true,true],[true,false]]) {
      f.advance(1000); f.report(stream,record); f.advance(1000); f.report(stream,record);
    }
    const s=f.activity.summary('alice');
    assert.equal(s.activeMs,3000); assert.equal(s.estimatedViewerMs,24000); assert.equal(s.audienceCoverageMs,2000);
  } finally {f.close();}
});

test('failed, stale and replaced-live samples exclude their boundaries without bridging prior counts', () => {
  const f=fixture();
  try {
    f.report(); f.advance(); f.report();
    for (const reason of ['stale','provider-error','not-found','page-limit','not-authorized'] as const) {
      const before=f.activity.summary('alice').estimatedViewerMs;
      f.advance(); f.sample({state:'unavailable',reason}); f.report();
      f.advance(); f.count(20,123,`new-${reason}`); f.report();
      assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
    }
    f.advance(); f.report(); const before=f.activity.summary('alice').estimatedViewerMs;
    f.advance(); f.count(100,999,'new-live'); f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
    f.advance(); f.count(100,999,'after-missed-poll'); f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
  } finally {f.close();}
});

test('sequence, lease and selection discontinuities exclude viewer estimates, not only activity duration', () => {
  const f=fixture();
  try {
    f.report(); f.advance(); f.report(); const before=f.activity.summary('alice').estimatedViewerMs;
    f.advance(); f.report(true,false,{report:{sequence:99,streaming:true,recording:false,sampleAgeMs:0}});
    assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
    f.advance(); f.report(true,false,{leaseId:'new-lease'}); assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
    f.campaigns.stop('alice'); f.activity.clear('alice'); f.advance(); f.report();
    f.campaigns.select('alice',TEST_CAMPAIGN.id); f.activity.clear('alice'); f.advance(); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
    f.sessions.remove(f.approval.token); f.advance(); f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,before);
  } finally {f.close();}
});

test('audience storage and activity writes are atomic; failure cannot bridge the missing interval', () => {
  const f=fixture();
  try {
    f.report(); f.advance(); f.report(); const before=f.activity.summary('alice');
    f.sessions.database.exec("CREATE TRIGGER reject_audience BEFORE UPDATE ON campaign_audience BEGIN SELECT RAISE(ABORT, 'private database failure'); END");
    f.advance(); assert.throws(()=>f.report());
    assert.equal(f.activity.summary('alice').estimatedViewerMs,before.estimatedViewerMs);
    assert.equal(f.activity.summary('alice').activeMs,before.activeMs);
    f.sessions.database.exec('DROP TRIGGER reject_audience'); f.advance(); f.report();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,before.estimatedViewerMs);
    f.advance(); f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,120000);
  } finally {f.close();}
});

test('persisted estimates survive restart but expire with parent history; cache and interpolation do not survive', () => {
  const dir=mkdtempSync(join(tmpdir(),'chatview-audience-')),path=join(dir,'sessions.sqlite');
  const f=fixture(path);
  try {
    f.report(); f.advance(); f.report(); f.advance(60000); f.sample({state:'unavailable',reason:'pending'}); f.restart();
    assert.equal(f.activity.summary('alice').estimatedViewerMs,60000);
    f.report(); assert.equal(f.activity.summary('alice').estimatedViewerMs,60000);
    assert.equal(f.activity.summary('bob').estimatedViewerMs,null);
    f.advance(ACTIVITY_RETENTION_MS); assert.equal(f.activity.summary('alice').estimatedViewerMs,null);
    assert.equal(f.sessions.database.prepare('SELECT COUNT(*) AS n FROM campaign_audience').get()!.n,0);
  } finally {f.close();rmSync(dir,{recursive:true,force:true});}
});

test('owner page distinguishes channel estimate, coverage, unmeasured attention and disabled collection', () => {
  const f=fixture();
  try {
    f.report(); f.advance(); f.report(); const html=activityPage(f.activity.summary('alice'));
    for(const text of ['12명','1.000 시청자·분','0명으로 대체하지 않음','제공자 표본 시각은 미제공',
      '배너를 본 사람 수가 아니며','미측정 — 노출·시청 계측 없음','계산하지 않음 — 지급 가능한 실적 없음']) assert(html.includes(text));
    for(const secret of ['sample-epoch',f.approval.token,f.approval.membership!.connectionId]) assert(!html.includes(secret));
    f.sample({state:'unavailable',reason:'disabled'});
    const disabled=activityPage(f.activity.summary('alice'));
    assert.match(disabled,/이용 조건·권한·할당량/u);
    assert.match(disabled,/data-evidence-state="covered"/u);
    assert.match(disabled,/현재 시청자 표본과 아래 7일 이내 보존된 추정 합계는 시점이 다를 수 있습니다/u);
  } finally {f.close();}
});


test('owner review queue distinguishes continuity gaps from missing channel samples and true sampled zero', () => {
  const f=fixture();
  try {
    f.report(); f.advance(); f.report();
    let evidence=f.activity.evidence('alice');
    const first=evidence.find(row=>row.reason==='first-report')!;
    let continuous=evidence.find(row=>row.reason==='continuous')!;
    assert(needsActivityReview(first),'a first receipt is a normal unbracketed boundary worth checking');
    assert(!needsActivityReview(continuous),'a fully covered streaming report is not a missing sample');
    f.advance(); f.sample({state:'unavailable',reason:'provider-error'}); f.report();
    evidence=f.activity.evidence('alice');
    continuous=evidence.find(row=>row.reason==='continuous')!;
    assert.equal(continuous.durationMs,10000);
    assert.equal(continuous.audienceCoverageMs,5000);
    assert(needsActivityReview(continuous),'coalesced streaming history becomes reviewable when only partly sampled');
    const review=evidence.filter(needsActivityReview);
    assert.equal(review.length,2);
    const html=activityPage(f.activity.summary('alice'),review);
    assert.match(html,/data-activity-view="review"/u);
    assert.match(html,/data-review-count="2"/u);
    assert.match(html,/일부 표본만 포함 · 5\.000초 \/ 10\.000초/u);
    assert.match(html,/연속 출력 확인 전 · 합산 제외/u);
    assert.match(html,/위쪽 합계는 여전히 전체 보관 근거/u);
    assert.match(html,/\/campaigns\/activity\/review\.csv/u);
    assert.match(html,/광고 실제 시청 · HP · 보상/u);
    assert.doesNotMatch(html,/<script|onerror=|clientSecret|sample-epoch/u);
    const csv=activityCsv(review);
    assert.equal(csv.trimEnd().split('\r\n').length,review.length+1);
    assert.doesNotMatch(csv,/sample-epoch|lease|viewer_id/u);
  } finally {f.close();}

  const zero=fixture();
  try {
    zero.count(0); zero.report(); zero.advance(); zero.report();
    const rows=zero.activity.evidence('alice');
    const covered=rows.find(row=>row.reason==='continuous')!;
    assert.equal(covered.estimatedViewerMs,0);
    assert.equal(covered.audienceCoverageMs,5000);
    assert(!needsActivityReview(covered),'real zero viewers is covered evidence, not missing');
    zero.report(false,true); zero.advance(); zero.report(false,true);
    const recording=zero.activity.evidence('alice').find(row=>
      row.reason==='continuous' && row.state==='recording')!;
    assert(!needsActivityReview(recording),'recording-only time never requires a live audience sample');
  } finally {zero.close();}
});
