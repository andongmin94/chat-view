// SPDX-License-Identifier: GPL-2.0-or-later
import { readFileSync } from 'node:fs';
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
  ['/public/ads/ad-source.css', { type: 'text/css; charset=utf-8',
    body: readFileSync(new URL('../../web/ad-source.css', import.meta.url), 'utf8') }],
]);
const document = `<!doctype html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width"><title>ChatView 시험 공개 배너</title>
<link rel="stylesheet" href="/public/ads/ad-source.css">
<script type="module" src="/public/ads/ad-source.js"></script></head><body>
<section id="banner" hidden aria-label="지급 없는 시험 광고">
<div class="brand-block"><div id="brand"></div><div class="badge">시험 광고 · 지급 없음</div></div>
<div class="copy"><h1 id="title"></h1><p id="description"></p></div>
</section></body></html>`;

// These pages deliberately ignore browser cookies and all native credentials.
// The identifier discloses only public test artwork, never creator/private state.
export function servePublicAd(request: IncomingMessage, response: ServerResponse,
  path: string, campaigns: Campaigns): void {
  response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
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

export function campaignsPage(status: CampaignStatus, csrf: string, origin: string, authorized: boolean): string {
  const form = (path: string, label: string) => `<form method="post" action="${path}"><input type="hidden" name="csrf" value="${csrf}"><button>${label}</button></form>`;
  const source = status.sourceId ? `${origin}/public/ads/${status.sourceId}` : undefined;
  return `<p><a href="/account">내 연결 관리</a></p>
<p><strong>시험 광고 · 지급 없음</strong> — 이 단계에서는 시청 실적, HP, 수익을 계산하지 않습니다.</p>
<section><h2>${escape(TEST_CAMPAIGN.brand)} · ${escape(TEST_CAMPAIGN.title)}</h2>
<p>${escape(TEST_CAMPAIGN.description)}</p><p>권장 브라우저 소스 크기: <strong>960 × 180</strong></p>
${authorized ? form(`/campaigns/${TEST_CAMPAIGN.id}/select`, status.selected ? '이 시험 캠페인 다시 선택' : '이 시험 캠페인 선택')
  : '<p>캠페인을 선택하려면 앱에서 치지직을 다시 승인하세요.</p>'}</section>
<p>현재 선택: <strong>${status.selected ? '시험 캠페인 선택됨' : '없음'}</strong></p>
${form('/campaigns/stop', '공개 배너 중지')}
<p>선택하려면 이 계정의 송출 역할 승인이 필요합니다. 선택은 해당 승인에 묶이고, 그 연결의 로그아웃·철회 시 해제됩니다.</p>
${source ? `<h2>OBS 공개 배너 연결</h2><p>이 주소는 공개 배너만 읽습니다. 로그인·채팅 표시키가 아니며, 계정 관리나 개인 채팅에는 접근할 수 없습니다.</p>
<label for="source-url">OBS 브라우저 소스 URL</label><textarea id="source-url" readonly rows="3" style="width:100%">${escape(source)}</textarea>
<p><a href="${escape(source)}" target="_blank" rel="noopener noreferrer">공개 배너 미리보기</a></p>
<p>송출 PC의 OBS에서 소스 추가 → 브라우저를 선택하고 위 URL과 크기 960 × 180을 지정하세요. 페이지 권한은 없음으로 설정하고, 소스가 보이지 않을 때 종료 및 장면 활성화 시 새로고침을 켜세요. 위치·크기를 직접 확인한 뒤 사용할 장면에 배치하세요. 챗뷰는 장면을 자동으로 수정하지 않습니다.</p>`
  : '<p>캠페인을 선택하면 OBS에 넣을 공개 배너 주소가 표시됩니다.</p>'}
<p>최근 송출 PC 보고가 있을 때만 배너를 표시합니다. 방송 시작 전 미리보기에서도 보일 수 있으며, 이것은 실제 시청자 노출의 증거가 아닙니다. 보고/서버 연결이 끊기면 마지막 확인으로부터 최대 15초 뒤 투명해집니다. 중지는 정상 연결에서 다음 조회(약 2초)에 반영됩니다.</p>
<p>공개 주소는 선택을 중지해도 유지됩니다. 송출 연결을 로그아웃·철회한 뒤 새로 승인하면 광고를 다시 선택해야 합니다. 일반 서비스 재시작은 유효한 기존 선택을 보존하지만 새 보고 전에는 표시하지 않습니다. 개인 채팅 HUD에는 광고를 넣지 않습니다.</p>`;
}
