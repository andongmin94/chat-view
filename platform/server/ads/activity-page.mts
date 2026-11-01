// SPDX-License-Identifier: GPL-2.0-or-later
import type { ActivitySummary, ActivityEvidenceRow, ActivityReason, ActivityState } from './activity.mts';
import { TRIAL_VIEWER_MS_PER_HP } from './activity.mts';
import { testCampaign } from './campaigns.mts';
import type { AudienceReason } from '../chzzk/audience.mts';
const states: Record<ActivityState, string> = {
  streaming: '송출 활성 보고', recording: '녹화 활성 보고', both: '송출·녹화 동시 보고',
  idle: '출력 비활성 보고', unknown: '계산 제외',
};
const reasons: Record<ActivityReason, string> = {
  'first-report': '새 관측의 첫 보고', continuous: '동일 상태의 연속 보고',
  'connection-change': '연결 교체·재연결', 'sequence-gap': '보고 순번 누락',
  'stale-gap': '보고 유효시간 초과', 'state-change': '출력 상태 전환',
  'clock-change': '서버 시계 불연속',
};
const audienceReasons: Record<AudienceReason, string> = {
  disabled: '수집 비활성 — 운영자의 이용 조건·권한·할당량 확인 필요',
  pending: '아직 수집한 표본 없음', stale: '표본 유효시간 초과 또는 시계 불연속',
  'not-found': '조회 목록에서 채널을 찾지 못함 (시청자 0명이라는 뜻이 아님)',
  'page-limit': '공유 조회 범위 밖 — 미측정', 'provider-error': '제공자 조회 실패 — 미측정',
  'rate-limited': '제공자 호출 제한 — 조회 간격 연장', 'not-authorized': 'Client 인증 또는 이용 권한 확인 필요',
};
const duration = (ms: number) => `${(ms / 1000).toFixed(3)}초`;
const date = (at: number) => new Date(at).toISOString().replace('T', ' ').replace('Z', ' UTC');

// A creator-facing review queue, never an ad-exposure/payment ledger.
// First reports and state/sequence/lease gaps need a fresh bracket; even a
// continuous streaming interval remains reviewable when official *channel*
// audience sampling covers only part (or none) of that interval.
export function needsActivityReview(row: ActivityEvidenceRow): boolean {
  return row.reason !== 'continuous' ||
    ((row.state === 'streaming' || row.state === 'both') &&
     (row.audienceCoverageMs === null || row.audienceCoverageMs < row.durationMs));
}
// Saved channel sample coverage, not the transient "current viewers" widget.
// A non-streaming/boundary row is inapplicable, not a zero-viewer measurement.
function channelEvidence(row: ActivityEvidenceRow): {
  state: 'not-applicable' | 'unmeasured' | 'partial' | 'covered'; coverage: string; estimate: string;
} {
  if (row.reason !== 'continuous')
    return { state: 'not-applicable', coverage: '보고 경계 · 산정 제외',
      estimate: '연속 송출 확인 전 — 추정 대상 아님' };
  if (row.state !== 'streaming' && row.state !== 'both')
    return { state: 'not-applicable', coverage: '송출 외 구간 · 산정 제외',
      estimate: '채널 시청자 시간 추정 대상 아님' };
  if (row.audienceCoverageMs === null)
    return { state: 'unmeasured', coverage: '공식 채널 표본 없음 · 미측정',
      estimate: '시청자 0명이 아닌 미측정' };
  const state = row.audienceCoverageMs < row.durationMs ? 'partial' : 'covered';
  return {
    state,
    coverage: `${state === 'partial' ? '일부 표본만 포함' : '전체 표본 포함'} · ${duration(row.audienceCoverageMs)} / ${duration(row.durationMs)}`,
    estimate: row.estimatedViewerMs === null ? '추정값 산정 불가'
      : `${(row.estimatedViewerMs / 60000).toFixed(3)} 시청자·분 (추정)${row.estimatedViewerMs === 0 ? ', 표본 0명' : ''}`,
  };
}

function reviewGuidance(row: ActivityEvidenceRow): string {
  switch (row.reason) {
    case 'first-report': return '정상 시작 경계일 수 있습니다. 이후 연속 보고가 들어왔는지 확인하세요.';
    case 'state-change': return '출력 상태가 바뀌었습니다. 새 상태의 연속 보고부터 계산합니다.';
    case 'connection-change': return '재연결 뒤 새 보고 두 개가 필요합니다. 송출 PC 연결을 확인하세요.';
    case 'sequence-gap': return '보고 순번이 끊겼습니다. 송출 PC의 보고 연결을 확인하세요.';
    case 'stale-gap': return '보고 사이가 유효시간을 넘었습니다. 송출 PC 출력 보고를 확인하세요.';
    case 'clock-change': return '서버 시계가 불연속입니다. 해당 구간을 합산하지 않습니다.';
    case 'continuous': return row.audienceCoverageMs === null
      ? '위 공식 채널 표본 상태에서 수집 비활성·조회 실패·목록 범위 등을 확인하세요.'
      : '표본이 없던 송출 시간은 시청자 0명으로 계산하지 않았습니다.';
  }
}

// CSV is downloaded only by an authenticated creator browser. No client
// spreadsheet formulas, chat, credentials, payout or verified ad exposure are
// included. Every dynamic string is escaped and formula-prefixed defensively.
function cell(value: string | number | null): string {
  if (value === null) return '';
  const text = String(value);
  const safe = /^[=+\-@\t\r\n]/u.test(text) ? "'" + text : text;
  return '"' + safe.replaceAll('"', '""') + '"';
}
export function activityCsv(rows: readonly ActivityEvidenceRow[]): string {
  const header = [
    'evidence_type', 'campaign_id', 'from_utc', 'to_utc', 'output_state',
    'report_reason', 'accepted_reports', 'output_interval_ms',
    'channel_viewer_estimate_ms', 'channel_sample_covered_ms',
    'channel_sample_coverage', 'measured_ad_view_ms', 'hp', 'revenue',
  ];
  const entries = rows.map(row => [
    'non-payable-test-output-report', row.campaignId,
    new Date(row.from).toISOString(), new Date(row.to).toISOString(),
    row.state, row.reason, row.reports, row.durationMs,
    row.estimatedViewerMs, row.audienceCoverageMs,
    row.audienceCoverageMs === null ? 'unmeasured'
      : row.audienceCoverageMs < row.durationMs ? 'partial' : 'covered',
    null, null, null, // No measured ad exposure, HP or payable earnings.
  ].map(value => cell(value)).join(','));
  return [header.join(','), ...entries].join('\r\n') + '\r\n';
}

// Only server-derived numbers and fixed labels enter this document. No query
// selector, tokens, private chat, client HTML, or public access to the history.
export function activityPage(summary: ActivitySummary, rows: readonly ActivityEvidenceRow[], review = false): string {
  const audience = summary.audience;
  const listedRows = rows.slice(0, 50);
  const download = review ? '/campaigns/activity/review.csv' : '/campaigns/activity.csv';
  // Reported activity, estimated channel-wide viewers and unmeasured
  // advertisement exposure are different evidence; never coerce missing to 0.
  const outputState = summary.reports === 0 ? 'no-reports'
    : summary.activeMs === 0 ? 'no-active-interval' : 'reported-intervals';
  const outputExplanation = summary.reports === 0
    ? '아직 이 캠페인의 승인된 송출 PC 출력 보고가 없습니다. 기록이 없다는 뜻이지 광고 시청자가 0명이라는 뜻은 아닙니다.'
    : summary.activeMs === 0
      ? '보고는 도착했지만 유효한 연속 활성 출력 구간은 0초입니다. 처음 보고·상태 전환·누락 구간은 합산하지 않습니다.'
      : '연속된 유효 출력 보고 사이의 활성 구간만 합산했습니다. 실제 OBS 영상 픽셀이나 시청자 화면은 확인하지 않았습니다.';
  const estimateState = summary.audienceCoverageMs === 0 ? 'unmeasured'
    : summary.estimatedViewerMs === null ? 'unrepresentable'
      : summary.audienceUnmeasuredMs > 0 ? 'partial' : 'covered';
  const estimateExplanation = estimateState === 'unmeasured'
    ? '시청자 시간 추정에 포함할 수 있는 연속 송출 보고 구간이 없습니다. 미측정은 시청자 0명과 다릅니다.'
    : estimateState === 'unrepresentable'
      ? '추정에 포함한 구간은 있지만 수치 범위 초과로 합계를 표시할 수 없습니다. 0으로 대체하지 않습니다.'
      : summary.estimatedViewerMs === 0
        ? '유효한 공식 시청자 0명 표본으로 계산된 추정값 0입니다. 미측정 광고 노출이 0회였다는 뜻은 아닙니다.' +
          (summary.audienceUnmeasuredMs > 0 ? ' 표본이 없던 송출 구간은 추가하지 않았습니다.' : '')
        : estimateState === 'partial'
          ? '시청자 표본이 유효했던 송출 구간만 추정했습니다. 표본이 없던 송출 구간은 추가하지 않았습니다.'
          : '유효 표본이 있는 연속 송출 보고 구간만 추정했습니다. 시청자 수는 채널 전체 기준입니다.';
  return `<p><strong>비지급 시험 기록 · 출력 활동은 광고 노출·시청시간이 아닙니다.</strong></p>
<nav aria-label="측정 근거 단계"><a href="#activity-output">1. 송출 PC 출력 보고</a> ·
<a href="#activity-audience">2. 채널 시청자 추정</a> ·
<a href="#activity-exposure">3. 광고 실제 시청 미측정</a> ·
<a href="#activity-trial">4. 시험 HP·포인트</a></nav>
<p>이 계정이 최근 선택했던 시험 캠페인들의 승인된 송출 연결이 보낸 보고만 기록합니다.
시험 소재를 전환하면 다음 보고에서 새 구간이 시작되며 이전 구간을 이어 합산하지 않습니다.
같은 상태의 연속 보고 두 개 사이를 보수적으로 근사하며, 실제 지속 송출을 보증하지 않습니다.
첫 보고·상태 전환·누락 구간은 계산하지 않고 마지막 보고 이후 시간도 더하지 않습니다.</p>
<section id="activity-output" aria-labelledby="activity-output-title" data-evidence-stage="output">
<h2 id="activity-output-title">1. 송출 PC 출력 보고 · 관측된 활동</h2>
<p data-evidence-state="${outputState}">${outputExplanation}</p>
<dl>
<dt>출력 활성 구간 합집합</dt><dd data-metric="active-ms" data-value="${summary.activeMs}">${duration(summary.activeMs)}</dd>
<dt>송출 활성 보고 구간</dt><dd data-metric="streaming-ms" data-value="${summary.streamingMs}">${duration(summary.streamingMs)}</dd>
<dt>녹화 활성 보고 구간</dt><dd data-metric="recording-ms" data-value="${summary.recordingMs}">${duration(summary.recordingMs)}</dd>
<dt>출력 비활성 보고 구간</dt><dd>${duration(summary.idleMs)} (활동 합계 제외)</dd>
</dl></section>
<section id="activity-audience" aria-labelledby="activity-audience-title" data-evidence-stage="audience">
<h2 id="activity-audience-title">2. 공식 채널 시청자 표본 · 조건부 추정</h2>
<p data-evidence-state="${estimateState}">${estimateExplanation}</p>
<p>현재 시청자 표본과 아래 7일 이내 보존된 추정 합계는 시점이 다를 수 있습니다.
표본 수집은 기본적으로 꺼져 있으며, 송출 보고 구간과 같은 라이브의 유효 표본이 연결된 경우에만 추정합니다.</p>
<dl>
<dt>공식 현재 시청자 표본</dt><dd data-metric="audience-state">${audience.state === 'sampled'
  ? `${audience.viewers}명 · 서버 조회 시작 ${date(audience.requestedAt)} (제공자 표본 시각은 미제공)`
  : audienceReasons[audience.reason]}</dd>
<dt>비지급 추정 시청자 시간</dt><dd data-metric="viewer-ms" data-value="${summary.estimatedViewerMs ?? ''}">${summary.estimatedViewerMs === null
  ? summary.audienceCoverageMs > 0 ? '수치 범위 초과 — 합계 표시 불가' : '미측정 — 시청자 데이터 없음'
  : `${(summary.estimatedViewerMs / 60000).toFixed(3)} 시청자·분 (추정)`}</dd>
<dt>추정에 포함한 송출 보고 구간</dt><dd data-metric="audience-coverage-ms" data-value="${summary.audienceCoverageMs}">${duration(summary.audienceCoverageMs)}</dd>
<dt>시청자 표본 없이 남은 송출 보고 구간</dt><dd>${duration(summary.audienceUnmeasuredMs)} (0명으로 대체하지 않음)</dd>
</dl>
<p>추정 방식: 각 연속 송출 보고 구간 시작에 알려진 마지막 공식 시청자 수를 해당 구간 동안 유지하는 근사입니다.
같은 라이브의 유효 표본이 양 끝에 있는 연속 송출 보고 구간만 계산하며, 표본 유효시간은 조회 시작 후 최대 90초입니다.
녹화만 켠 구간·조회 실패·라이브 교체·재시작 공백은 시청자 시간으로 채우지 않습니다.
치지직 채널 전체의 시청자 수이지 배너를 본 사람 수가 아니며, 광고 가시성과 주목 여부는 미검증입니다.</p>
</section>
<section id="activity-exposure" aria-labelledby="activity-exposure-title" data-evidence-stage="exposure">
<h2 id="activity-exposure-title">3. 광고 실제 시청 · HP · 보상</h2>
<p data-evidence-state="not-measured">실제 광고 노출·시청시간은 측정하지 않습니다.
출력 보고가 있거나 시청자 시간이 추정돼도 시청자 화면의 광고 표시·주목 여부를 확인할 수 없습니다.</p>
<dl>
<dt>실측 광고 시청시간</dt><dd>미측정 — 노출·시청 계측 없음</dd>
<dt>광고 실측 기반 HP·현금 수익·지급</dt><dd>계산하지 않음 — 지급 가능한 실적 없음</dd>
</dl></section>
<section id="activity-trial" aria-labelledby="activity-trial-title" data-evidence-stage="trial">
<h2 id="activity-trial-title">4. 시험 HP·포인트 · 현금 가치 없음</h2>
<p>계정과 시험 소재마다 100 HP로 시작합니다. 새로 확인한 추정 채널 시청자시간 1분마다
1 HP를 소모하고 시험 1포인트를 기록합니다.
1 HP 미만의 진행은 다음 구간에 이어지며 소재별 최대 100포인트입니다. 과거 활동은 소급하지 않습니다.</p>
<p>광고 실제 시청 실적·현금·출금·지급 확정이 아닙니다. 시청자 표본 누락·녹화 전용·미리보기에는
차감과 적립이 없고, 유효한 0명 표본은 미측정과 구분합니다. 송출·녹화 동시 보고는 한 번만 계산합니다.</p>
${summary.trial.campaigns.map(trial => `<section data-trial-campaign="${trial.campaignId}" data-trial-state="${trial.remainingHp === 0 ? 'exhausted' : trial.startedAt === null ? 'not-started' : 'active'}">
<h3>${testCampaign(trial.campaignId)!.title}</h3>
<dl><dt>남은 시험 HP</dt><dd data-trial-hp="${trial.remainingHp}">${trial.remainingHp} / ${trial.initialHp} HP</dd>
<dt>누적 시험 포인트</dt><dd data-trial-points="${trial.points}">${trial.points}포인트 · 현금 가치 없음</dd>
<dt>다음 1 HP 진행</dt><dd>${(trial.progressViewerMs / TRIAL_VIEWER_MS_PER_HP).toFixed(3)} / 1분 (추정 채널 시청자시간)</dd></dl>
${trial.remainingHp === 0 ? '<p>시험 HP가 소진돼 이 소재의 추가 차감·포인트 적립만 중단됩니다. 선택과 공개 배너는 자동으로 바뀌거나 중지되지 않습니다.</p>'
  : trial.startedAt === null ? '<p>아직 이 소재에 반영한 새 시청자 추정 구간이 없습니다. 현재 표본 수집 상태는 위에서 확인하세요.</p>' : ''}
</section>`).join('')}
<p>중지·다른 소재 선택·재선택·승인 철회·서버 재시작으로 시험 잔액과 누적 포인트를 초기화하지 않습니다.
소재별 잔액은 유지하며 아래 개별 보상 기록만 기존 활동과 같은 최근 7일 보관 기준을 적용합니다.</p>
<h3>최근 시험 포인트 기록</h3>
${summary.trial.rewards.length ? `<div style="overflow-x:auto"><table style="width:100%;text-align:left;font-size:14px;border-collapse:collapse">
<caption>최근 7일 시험 보상 중 최대 50건 · 현금 지급 기록 아님</caption>
<thead><tr><th scope="col">서버 확인 시각 (UTC)</th><th scope="col">시험 소재</th><th scope="col">소모 HP 누계</th><th scope="col">시험 적립</th></tr></thead>
<tbody>${summary.trial.rewards.slice(0, 50).map(reward => `<tr data-trial-reward="${reward.campaignId}:${reward.consumedHp}"><td>${date(reward.at)}</td>
<td>${testCampaign(reward.campaignId)!.title}</td><td style="white-space:nowrap">${reward.consumedHp} HP</td><td style="white-space:nowrap">+${reward.points}포인트</td></tr>`).join('')}</tbody></table></div>`
  : '<p>최근 7일에 저장된 시험 포인트 기록이 없습니다. 잔액과 누적 포인트는 위에 따로 표시합니다.</p>'}
</section>
<p>송출·녹화 동시 구간은 합집합에 한 번만 반영합니다. 공개 배너 조회, 미리보기,
소스 표시/숨김 이벤트는 기록을 만들거나 시간을 늘리지 않습니다.</p>
<section aria-labelledby="activity-history-title" data-activity-view="${review ? 'review' : 'all'}">
<h2 id="activity-history-title">5. 최근 서버 보고 근거</h2>
<nav aria-label="저장된 근거 보기"><a href="/campaigns/activity">전체 기록</a> ·
<a href="/campaigns/activity/review">연속성·채널 표본 확인 구간만 보기</a></nav>
${review ? `<p data-review-count="${rows.length}">확인할 근거 ${rows.length}건 · 첫 보고/상태 전환·순번 누락·재연결 공백, 또는 연속 송출 구간의 채널 시청자 표본 미측정/부분 수집을 보여줍니다.
이 목록은 <strong>오류·광고 노출 실패·시청자 0명을 확정하지 않습니다.</strong>
첫 보고는 정상적인 시작 경계일 수 있습니다. 위쪽 합계는 여전히 전체 보관 근거이며, 아래 표와 CSV에만 이 필터를 적용합니다.</p>`
: '<p>전체 서버 보고 내역입니다. 연속성 공백과 채널 시청자 표본 미측정을 따로 확인할 수 있습니다.</p>'}
<p><a href="${download}" download="chatview-nonpayable-activity.csv">${review ? '확인 구간만' : '내 전체'} 비지급 출력 근거 CSV 다운로드 (최대 1,000건)</a><br>
송출·녹화 보고와 <strong>조건부 채널 시청자 추정</strong>만 포함합니다. 광고 실측 노출·HP·수익은 값이 없고, 미측정은 0으로 바꾸지 않습니다.
위 시험 HP·포인트는 별도 기록이며 이 출력 근거 CSV에 포함하지 않습니다. 계정 소유자만 다운로드할 수 있습니다.</p>
<p>최근 7일 이내에 시작한 구간 중 최대 1,000건을 보관합니다. 동일 상태는 시간 단위로 합칩니다.
보관 중 ${summary.intervals}건, 수신 보고 ${summary.reports}개, 계산 제외 ${summary.unknownIntervals}건.
아래는 ${review ? '점검 대상 중 최근 50건' : '최근 50건'}입니다. 각 행의 채널 시청자 시간은 별도 근거의 <strong>추정</strong>이며 광고 실측값은 아닙니다. 전체 방송 이력이나 평생 합계가 아닙니다.</p>
${listedRows.length ? `<div style="overflow-x:auto"><table style="width:100%;text-align:left;font-size:14px;border-collapse:collapse">
<caption>${review ? '연속성·채널 표본 확인 구간' : '최근 활동 근거'} (서버 수신 시각, UTC)</caption>
<thead><tr><th scope="col">시작 → 마지막 보고</th><th scope="col">시험 캠페인 / 구분 / 근거</th><th scope="col">계산 구간</th><th scope="col">공식 채널 표본 · 시청자 시간 추정</th>${review ? '<th scope="col">점검 안내</th>' : ''}</tr></thead>
<tbody>${listedRows.map(row => {
  const sample = channelEvidence(row);
  return `<tr><td>${date(row.from)}<br>→ ${date(row.to)}</td>
<td>${testCampaign(row.campaignId)?.title ?? '알 수 없는 시험 캠페인'}<br>${states[row.state]}<br>${reasons[row.reason]}</td>
<td>${row.state === 'unknown' ? '미산정' : duration(row.durationMs)}</td>
<td data-sample-coverage="${sample.state}" data-sample-covered-ms="${row.audienceCoverageMs ?? ''}" data-estimated-viewer-ms="${row.estimatedViewerMs ?? ''}">${sample.coverage}<br>${sample.estimate}</td>
${review ? `<td>${reviewGuidance(row)}</td>` : ''}</tr>`;
}).join('')}</tbody></table></div>`
: review ? '<p>보관된 출력 보고 중 확인할 구간이 없습니다. 실제 광고 노출이나 시청자 주목이 검증됐다는 뜻은 아닙니다.</p>'
  : '<p>아직 저장된 보고가 없습니다. 캠페인을 선택하고 승인된 송출 연결에서 새 보고를 받아야 합니다.</p>'}
<p>중지·철회 뒤에도 보관 기간 안의 기록은 남습니다. 재선택·연결 복귀·서버 재시작 전후의
공백은 이어 계산하지 않습니다. 이 화면은 새로고침 시 저장된 기록만 다시 읽습니다.</p>
<p><a href="/campaigns/activity">기록 새로고침</a> · <a href="/campaigns">캠페인 선택·중지</a> · <a href="/account">내 연결</a></p></section>`;
}
