// SPDX-License-Identifier: GPL-2.0-or-later
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { TEST_CAMPAIGN } from './campaigns.mts';
import type { Campaigns, CampaignStatus } from './campaigns.mts';

const escape = (value: string) => value.replace(/[&<>"']/gu,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]!));
const assets = new Map([
  ['/public/ads/ad-source.js', { type: 'text/javascript; charset=utf-8',
    body: readFileSync(new URL('../../web/ad-source.js', import.meta.url), 'utf8') }],
  ['/public/ads/ad-renderer.js', { type: 'text/javascript; charset=utf-8',
    body: readFileSync(new URL('../../web/ad-renderer.js', import.meta.url), 'utf8') }],
]);
// Keep the first opaque/hidden public HTML load independent of potentially
// stalled CSS or JS assets; the banner remains hidden until the async entry
// module fetches a fresh, authorized nonpayable test snapshot.
// HTML parsing normalizes CRLF to LF before checking inline CSP hashes.
// Hash and embed identical LF source even on Windows Git checkouts.
const criticalStyle = readFileSync(new URL('../../web/ad-source.css', import.meta.url), 'utf8')
  .replace(/\r\n?/gu, '\n');
const bootstrap = "void import('/public/ads/ad-source.js').catch(() => {});";
const hash = (source: string) => `'sha256-${createHash('sha256').update(source).digest('base64')}'`;
const publicPolicy = `default-src 'none'; script-src 'self' ${hash(bootstrap)}; style-src ${hash(criticalStyle)}; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`;
// The authenticated campaign page is the ONLY management route that opts
// into this tiny script. All other management pages remain script-disabled.
const campaignSetupScript = readFileSync(new URL('../../web/campaign-setup.js', import.meta.url), 'utf8')
  .replace(/\r\n?/gu, '\n');
export const campaignSetupScriptHash = hash(campaignSetupScript);
const document = `<!doctype html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width"><title>ChatView 시험 공개 배너</title>
<style>${criticalStyle}</style>
<script type="module">${bootstrap}</script></head><body>
<section id="banner" hidden aria-label="지급 없는 시험 광고">
<div class="brand-block"><div id="brand"></div><div class="badge">시험 광고 · 지급 없음</div></div>
<div class="copy"><h1 id="title"></h1><p id="description"></p></div>
</section></body></html>`;

// These pages deliberately ignore browser cookies and all native credentials.
// The identifier discloses only public test artwork, never creator/private state.
export function servePublicAd(request: IncomingMessage, response: ServerResponse,
  path: string, campaigns: Campaigns): void {
  response.setHeader('Content-Security-Policy', publicPolicy);
  response.setHeader('Cross-Origin-Resource-Policy', 'same-origin');
  if (request.method !== 'GET') { response.writeHead(405); response.end(); return; }
  if (request.headers['transfer-encoding'] !== undefined ||
      (request.headers['content-length'] !== undefined && request.headers['content-length'] !== '0')) {
    response.writeHead(400); response.end(); return;
  }
  const asset = assets.get(path);
  if (asset) { response.writeHead(200, { 'Content-Type': asset.type }); response.end(asset.body); return; }
  const match = /^\/public\/ads\/([a-f0-9]{32})(\/state)?$/u.exec(path);
  if (!match) { response.writeHead(404); response.end(); return; }
  if (match[2]) {
    response.writeHead(200, { 'Content-Type': 'application/json' });
    response.end(JSON.stringify(campaigns.snapshot(match[1]!))); return;
  }
  response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); response.end(document);
}

// Read-only selection guidance, not a capability or an OBS/output assertion.
export type CampaignSelectionState = 'provider-required' | 'streaming-required' | 'ready';

export function campaignsPage(status: CampaignStatus, csrf: string, origin: string,
  { selectionState, streamingConnected, previewReady }: {
    selectionState: CampaignSelectionState; streamingConnected: boolean; previewReady: boolean;
  }): string {
  const form = (path: string, label: string) => `<form method="post" action="${path}"><input type="hidden" name="csrf" value="${csrf}"><button>${label}</button></form>`;
  const source = status.sourceId ? `${origin}/public/ads/${status.sourceId}` : undefined;
  const selectionText = selectionState === 'provider-required'
    ? '치지직 재승인 필요 · 이 계정의 치지직 승인이 유효하지 않습니다.'
    : selectionState === 'streaming-required'
      ? '송출 역할 준비 필요 · 이 계정의 유효한 송출 PC 승인이 없습니다.'
      : '선택 가능 · 이 계정의 치지직 승인과 송출 역할 승인이 확인됐습니다.';
  const preview = !status.selected ? { state: 'not-selected', text: '캠페인이 선택되지 않아 공개 소스는 투명하게 대기합니다.' }
    : selectionState === 'ready' && streamingConnected && previewReady ? { state: 'report-ready', text: '캠페인 선택과 최근 송출 PC 출력 보고가 확인됐습니다. 공개 시험 배너를 표시할 조건은 충족됐지만 OBS나 시청자 화면에서 보였다는 뜻은 아닙니다.' }
    : { state: 'waiting-report', text: '캠페인은 선택됐지만 최근 송출 PC 출력 보고가 없어 공개 소스는 투명하게 대기합니다.' };
  // Explain the next action using this owner's existing approval/presence and
  // the public snapshot predicate. No management read reconnects or reselects.
  const recovery = selectionState === 'provider-required' ? {
    state: 'provider-required', title: '치지직 재승인이 필요합니다.',
    help: '<p>위의 승인 준비 안내에서 같은 채널·송출 역할을 확인하세요. 브라우저 관리 로그인만으로 앱 승인이 복구되지는 않습니다.</p><p><a href="#campaign-selection-title">승인 준비 안내 보기</a></p>',
  } : selectionState === 'streaming-required' ? {
    state: 'streaming-required', title: '이 계정의 송출 PC 승인이 없습니다.',
    help: '<p>게임 PC companion은 그대로 두고 송출 PC에서 역할을 승인하세요. 새 승인만으로 광고가 선택되지는 않습니다.</p><p><a href="#campaign-selection-title">승인 준비 안내 보기</a></p>',
  } : !status.selected ? {
    state: 'not-selected', title: '시험 캠페인을 직접 선택하세요.',
    help: `<p>연결과 출력 보고가 있어도 선택하지 않은 광고는 표시하지 않습니다. ${source ? '기존 공개 URL은 그대로 사용합니다.' : '처음 선택하면 공개 URL이 생성됩니다.'}</p><p><a href="#campaign-selection-title">캠페인 선택으로 이동</a></p>`,
  } : !streamingConnected ? {
    state: 'streaming-disconnected', title: '송출 승인은 있지만 열린 채팅 연결이 없습니다.',
    help: '<p>송출 PC의 OBS 관리 런타임에서 ChatView 연결창을 열고 <strong>현재 승인으로 자체 채팅 복귀</strong>를 직접 선택하세요. 복귀할 승인이 없거나 만료·철회 안내가 나오면 내 연결 관리에서 기존 송출 승인을 먼저 확인하세요.</p><p>선택은 유지돼 있습니다. 같은 승인으로 연결을 복구하고 새 보고를 받으면 같은 공개 URL로 돌아옵니다. 캠페인 재선택이나 URL 교체는 필요하지 않습니다.</p>',
  } : !previewReady ? {
    state: 'waiting-report', title: '송출 채팅 연결은 열려 있지만 유효한 출력 보고가 없습니다.',
    help: '<p>송출 PC의 OBS와 ChatView 런타임·서비스 연결 상태를 확인한 뒤 현재 상태를 새로고침하세요. 게임 PC companion이나 브라우저 새로고침은 출력 보고를 만들지 않습니다.</p><p>선택과 공개 URL은 유지됩니다. 캠페인 재선택이나 URL 교체는 필요하지 않습니다. 배너 확인을 위해 방송·녹화를 시작할 필요는 없습니다.</p>',
  } : {
    state: 'report-ready', title: '서버 표시 조건이 충족됐습니다. OBS에서 직접 확인하세요.',
    help: '<p>같은 공개 URL의 시험 배너를 OBS 미리보기에서 확인하세요. 실제 배너 표시·송출·시청자 노출은 아직 확인된 것이 아닙니다.</p><p><a href="#obs-source-setup">기존 공개 소스 설정 보기</a></p>',
  };
  return `<p><a href="/account">내 연결 관리</a> · <a href="/campaigns/activity">비지급 활동 기록 · 근거 구분 보기</a></p>
<p><strong>시험 광고 · 지급 없음</strong> — 이 단계에서는 시청 실적, HP, 수익을 계산하지 않습니다.</p>
<section aria-labelledby="campaign-selection-title">
<h2 id="campaign-selection-title">시험 캠페인 선택 준비</h2>
<p data-campaign-selection-state="${selectionState}">${selectionText}</p>
${selectionState !== 'ready' ? `<ol>
<li><strong>송출 PC의 OBS</strong>에서 ChatView 연결창을 엽니다. 게임 PC companion은 그대로 두며, 게임 PC에 OBS를 설치하거나 실행하지 않습니다.</li>
<li><strong>로그인 / 연결</strong>에서 이 관리 화면과 같은 치지직 채널을 선택하고 <strong>송출 PC · OBS 역할</strong>을 승인하세요. 브라우저 관리 로그인이나 게임 역할 승인만으로는 송출 역할이 생기지 않습니다.</li>
<li>아래 링크로 준비 상태를 다시 확인한 뒤 시험 캠페인을 직접 선택하세요.</li>
</ol>` : '<p>아래에서 캠페인을 직접 선택하세요. 송출 역할 승인은 현재 열린 채팅 연결이나 최신 출력 보고, 실제 OBS 배너 표시를 뜻하지 않습니다.</p>'}
<p><a href="/campaigns">선택 준비 상태 다시 확인</a> · <a href="/account">이 계정의 PC 역할 확인</a></p>
${source && !status.selected ? '<p>기존 공개 URL은 그대로 사용할 수 있습니다. 송출 역할을 다시 승인해도 광고는 자동 선택되지 않습니다. 준비 상태 확인 후 캠페인을 다시 선택하세요.</p>' : ''}
</section>
<section><h2>${escape(TEST_CAMPAIGN.brand)} · ${escape(TEST_CAMPAIGN.title)}</h2>
<p>${escape(TEST_CAMPAIGN.description)}</p><p>권장 브라우저 소스 크기: <strong>960 × 180</strong></p>
${selectionState === 'ready' ? form(`/campaigns/${TEST_CAMPAIGN.id}/select`, status.selected ? '이 시험 캠페인 다시 선택' : '이 시험 캠페인 선택') : ''}</section>
<p>현재 선택: <strong>${status.selected ? '시험 캠페인 선택됨' : '없음'}</strong></p>
<section id="public-banner-status" aria-labelledby="public-banner-status-title">
<h3 id="public-banner-status-title">공개 배너 서버 준비 상태</h3>
<p data-public-banner-state="${preview.state}">${preview.text}</p>
<div data-banner-recovery-state="${recovery.state}">
<p><strong>${recovery.title}</strong></p>${recovery.help}
</div>
<p><a href="/account#two-pc-status">송출 승인·현재 연결 확인</a></p>
<p>최근 출력 보고에는 녹화·미리보기 상태도 포함될 수 있습니다. 열린 연결은 서버의 채팅 WebSocket이며 PC 실행·채팅 글자 표시의 증거가 아닙니다. 이 상태는 실제 OBS 브라우저 화면, 방송 송출 또는 시청자 광고 노출을 측정하지 않습니다.
<a href="/campaigns">현재 상태 새로고침</a></p>
</section>
${form('/campaigns/stop', '공개 배너 중지')}
<p>선택하려면 이 계정의 송출 역할 승인이 필요합니다. 선택은 해당 승인에 묶이고, 그 연결의 로그아웃·철회 시 해제됩니다. 선택 중 받은 출력 보고의 비지급 활동 구간은 계정별로 최대 7일·1,000건 보관하며, 광고 시청시간으로 계산하지 않습니다.</p>
${source ? `<section id="obs-source-setup" aria-labelledby="obs-source-title">
<h2 id="obs-source-title">OBS 공개 시험 배너 연결</h2>
<p>방송에 배치할 공개 시험 배너입니다. 개인 채팅 HUD가 아니며 로그인·채팅 권한을 부여하지 않습니다.</p>
<ol>
<li><strong>공개 URL 복사</strong>
<label for="source-url" style="display:block">OBS 브라우저 소스 URL</label>
<input id="source-url" type="url" readonly spellcheck="false" autocomplete="off" value="${escape(source)}" style="width:100%;max-width:100%;box-sizing:border-box">
<button id="copy-public-source" type="button">공개 주소 복사</button>
<p id="copy-public-result" role="status" aria-live="polite">주소 복사 버튼을 누르거나 주소 칸을 선택해 복사하세요.</p>
</li>
<li><strong>송출 PC의 OBS에 브라우저 소스 추가</strong>
<p>사용할 장면의 소스 목록에서 <strong>+ → 브라우저</strong>를 선택합니다. 로컬 파일은 끄고, 복사한 주소를 URL 칸에 붙여넣으세요.</p>
<dl><dt>너비 (Width)</dt><dd><strong>960</strong></dd>
<dt>높이 (Height)</dt><dd><strong>180</strong></dd>
<dt>페이지 권한 (Page permissions)</dt><dd><strong>없음 (None)</strong></dd>
<dt>소스가 보이지 않을 때 종료</dt><dd><strong>켜기</strong></dd>
<dt>장면 활성화 시 브라우저 새로고침</dt><dd><strong>켜기</strong></dd></dl>
</li>
<li><strong>OBS 화면에서 직접 확인</strong>
<p>장면에서 배너의 위치와 크기를 조정한 뒤 OBS 미리보기 및 필요한 경우 테스트 녹화로 화면을 직접 확인하세요. 챗뷰는 장면을 자동 편집하지 않습니다.</p>
<p data-obs-setup-readiness="${preview.state}">현재 서버 판단: ${preview.text}</p>
<p>표시 조건이 충족돼도 실제 OBS 합성·송출·시청자 노출 확인이 아닙니다. 송출 전 미리보기 역시 광고 노출 근거가 아닙니다.</p>
</li>
</ol>
<details><summary>배너가 투명하거나 보이지 않을 때 확인할 항목</summary>
<ul>
<li>캠페인이 선택됐는지, 위 서버 준비 상태가 최신 출력 보고를 기다리는지 확인하고 관리 페이지를 새로고침하세요.</li>
<li>OBS 브라우저 소스의 로컬 파일 사용이 꺼져 있고 URL·너비 960·높이 180이 맞는지 확인하세요.</li>
<li>송출 역할로 승인한 PC가 실행 중이며 최신 출력 보고를 보내는지 확인하세요. 실제 방송을 시작하지 않아도 미리보기에서 보일 수 있습니다.</li>
<li>보고 만료·연결 단절만으로 캠페인 선택이 해제되지는 않습니다. 같은 승인으로 연결을 복원하고 새 보고를 받으면 같은 URL로 돌아옵니다. 중지·로그아웃·철회로 선택이 해제된 경우에만 직접 다시 선택하세요.</li>
</ul></details>
<p><a href="${escape(source)}" target="_blank" rel="noopener noreferrer">공개 시험 배너 별도 미리보기</a> ·
<a href="https://obsproject.com/kb/browser-source" target="_blank" rel="noopener noreferrer">OBS 브라우저 소스 공식 안내</a></p>
</section>`
  : '<p>캠페인을 선택하면 OBS에 넣을 공개 배너 주소가 표시됩니다.</p>'}
<p>최근 송출 PC 보고가 있을 때만 배너를 표시합니다. 방송 시작 전 미리보기에서도 보일 수 있으며, 이것은 실제 시청자 노출의 증거가 아닙니다. 보고/서버 연결이 끊기면 마지막 확인으로부터 최대 15초 뒤 투명해집니다. 중지는 정상 연결에서 다음 조회(약 2초)에 반영됩니다.</p>
<p>공개 주소는 선택을 중지해도 유지됩니다. 송출 연결을 로그아웃·철회한 뒤 새로 승인하면 광고를 다시 선택해야 합니다. 일반 서비스 재시작은 유효한 기존 선택을 보존하지만 새 보고 전에는 표시하지 않습니다. 개인 채팅 HUD에는 광고를 넣지 않습니다.</p>
${source ? `<script>${campaignSetupScript}</script>` : ''}`;
}
