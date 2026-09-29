// SPDX-License-Identifier: GPL-2.0-or-later
import type { ActivitySummary, ActivityReason, ActivityState } from './activity.mts';
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

// Only server-derived numbers and fixed labels enter this document. No query
// selector, tokens, private chat, client HTML, or public access to the history.
export function activityPage(summary: ActivitySummary): string {
  const audience = summary.audience;
  return `<p><strong>비지급 시험 기록 · 출력 활동은 광고 노출·시청시간이 아닙니다.</strong></p>
<p>선택한 시험 캠페인의 승인된 송출 연결이 보낸 보고만 기록합니다.
같은 상태의 연속 보고 두 개 사이를 보수적으로 근사하며, 실제 지속 송출을 보증하지 않습니다.
첫 보고·상태 전환·누락 구간은 계산하지 않고 마지막 보고 이후 시간도 더하지 않습니다.</p>
<dl>
<dt>출력 활성 구간 합집합</dt><dd data-metric="active-ms" data-value="${summary.activeMs}">${duration(summary.activeMs)}</dd>
<dt>송출 활성 보고 구간</dt><dd data-metric="streaming-ms" data-value="${summary.streamingMs}">${duration(summary.streamingMs)}</dd>
<dt>녹화 활성 보고 구간</dt><dd data-metric="recording-ms" data-value="${summary.recordingMs}">${duration(summary.recordingMs)}</dd>
<dt>출력 비활성 보고 구간</dt><dd>${duration(summary.idleMs)} (활동 합계 제외)</dd>
<dt>공식 현재 시청자 표본</dt><dd data-metric="audience-state">${audience.state === 'sampled'
  ? `${audience.viewers}명 · 서버 조회 시작 ${date(audience.requestedAt)} (제공자 표본 시각은 미제공)`
  : audienceReasons[audience.reason]}</dd>
<dt>비지급 추정 시청자 시간</dt><dd data-metric="viewer-ms" data-value="${summary.estimatedViewerMs ?? ''}">${summary.estimatedViewerMs === null
  ? summary.audienceCoverageMs > 0 ? '수치 범위 초과 — 합계 표시 불가' : '미측정 — 시청자 데이터 없음'
  : `${(summary.estimatedViewerMs / 60000).toFixed(3)} 시청자·분 (추정)`}</dd>
<dt>추정에 포함한 송출 보고 구간</dt><dd data-metric="audience-coverage-ms" data-value="${summary.audienceCoverageMs}">${duration(summary.audienceCoverageMs)}</dd>
<dt>시청자 표본 없이 남은 송출 보고 구간</dt><dd>${duration(summary.audienceUnmeasuredMs)} (0명으로 대체하지 않음)</dd>
<dt>실측 광고 시청시간</dt><dd>미측정 — 노출·시청 계측 없음</dd>
<dt>HP·수익·지급</dt><dd>계산하지 않음 — 지급 가능한 실적 없음</dd>
</dl>
<p>추정 방식: 각 연속 송출 보고 구간 시작에 알려진 마지막 공식 시청자 수를 해당 구간 동안 유지하는 근사입니다.
같은 라이브의 유효 표본이 양 끝에 있는 연속 송출 보고 구간만 계산하며, 표본 유효시간은 조회 시작 후 최대 90초입니다.
녹화만 켠 구간·조회 실패·라이브 교체·재시작 공백은 시청자 시간으로 채우지 않습니다.
치지직 채널 전체의 시청자 수이지 배너를 본 사람 수가 아니며, 광고 가시성과 주목 여부는 미검증입니다.</p>
<p>송출·녹화 동시 구간은 합집합에 한 번만 반영합니다. 공개 배너 조회, 미리보기,
소스 표시/숨김 이벤트는 기록을 만들거나 시간을 늘리지 않습니다.</p>
<p>최근 7일 이내에 시작한 구간 중 최대 1,000건을 보관합니다. 동일 상태는 시간 단위로 합칩니다.
보관 중 ${summary.intervals}건, 수신 보고 ${summary.reports}개, 계산 제외 ${summary.unknownIntervals}건.
아래는 최근 50건입니다. 전체 방송 이력이나 평생 합계가 아닙니다.</p>
${summary.rows.length ? `<div style="overflow-x:auto"><table style="width:100%;text-align:left;font-size:14px;border-collapse:collapse">
<caption>최근 활동 근거 (서버 수신 시각, UTC)</caption>
<thead><tr><th>시작 → 마지막 보고</th><th>구분 / 근거</th><th>계산 구간</th></tr></thead>
<tbody>${summary.rows.map(row => `<tr><td>${date(row.from)}<br>→ ${date(row.to)}</td>
<td>${states[row.state]}<br>${reasons[row.reason]}</td>
<td>${row.state === 'unknown' ? '미산정' : duration(row.durationMs)}</td></tr>`).join('')}</tbody></table></div>`
: '<p>아직 저장된 보고가 없습니다. 캠페인을 선택하고 승인된 송출 연결에서 새 보고를 받아야 합니다.</p>'}
<p>중지·철회 뒤에도 보관 기간 안의 기록은 남습니다. 재선택·연결 복귀·서버 재시작 전후의
공백은 이어 계산하지 않습니다. 이 화면은 새로고침 시 저장된 기록만 다시 읽습니다.</p>
<p><a href="/campaigns/activity">기록 새로고침</a> · <a href="/campaigns">캠페인 선택·중지</a> · <a href="/account">내 연결</a></p>`;
}
