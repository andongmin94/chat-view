// SPDX-License-Identifier: GPL-2.0-or-later
// Actual local HTTP for the public router; account authentication is exercised
// separately by campaign-flow.test.mts, not simulated as evidence here.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { SessionStore } from '../server/chat/session-store.mts';
import { Campaigns, HIDDEN_AD, TEST_CAMPAIGN } from '../server/ads/campaigns.mts';
import { campaignSetupScriptHash, campaignsPage, servePublicAd } from '../server/ads/pages.mts';

test('public HTTP serves only transparent-first artwork and fixed assets; management form stays separate', async () => {
  const sessions = new SessionStore();
  const ads = new Campaigns({ sessions, output: () => ({ state: 'reported', streaming: false,
    recording: false, expiresInMs: 15000 }) });
  const stream = sessions.create('private-account', 'streaming');
  ads.select('private-account', TEST_CAMPAIGN.id);
  const id = ads.status('private-account').sourceId!;
  const server = createServer((request, response) => {
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Referrer-Policy', 'no-referrer');
    servePublicAd(request, response, request.url!, ads);
  });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const address = server.address(); assert(address && typeof address !== 'string');
  const origin = `http://127.0.0.1:${address.port}`;
  const get = (path: string) => fetch(origin + path, { signal: AbortSignal.timeout(3000) });
  try {
    const page = await get(`/public/ads/${id}`), html = await page.text();
    assert.equal(page.status, 200);
    assert.match(html, /id="banner" hidden/u);
    const csp = page.headers.get('content-security-policy')!;
    const style = /<style>([\s\S]*?)<\/style>/u.exec(html)?.[1];
    const bootstrap = /<script type="module">([\s\S]*?)<\/script>/u.exec(html)?.[1];
    assert(style && bootstrap, 'transparent HTML has critical CSS and a fixed asynchronous bootstrap');
    assert(!style.includes('\r'), 'inline CSS is normalized before hashing on Windows');
    const digest = (value: string) => `'sha256-${createHash('sha256').update(value).digest('base64')}'`;
    assert(csp.includes(`script-src 'self' ${digest(bootstrap)};`), 'exact bootstrap CSP hash');
    assert(csp.includes(`style-src ${digest(style)};`), 'exact CSS CSP hash');
    assert.doesNotMatch(csp, /unsafe-inline|unsafe-eval|data:/u);
    assert.match(style, /background: transparent/u);
    assert.match(bootstrap, /import\('\/public\/ads\/ad-source\.js'\)/u);
    assert.doesNotMatch(html, /(?:src|href)="\/public\/ads\/(?:ad-source\.js|ad-source\.css)"/u);
    assert.equal(page.headers.get('set-cookie'), null);
    for (const value of ['private-account', stream.token, stream.id, 'csrf', 'ChatView-Session'])
      assert(!html.includes(value));
    for (const [asset, mime] of [['ad-source.js', 'text/javascript'], ['ad-renderer.js', 'text/javascript']]) {
      const response = await get(`/public/ads/${asset}`);
      assert.equal(response.status, 200);
      assert(response.headers.get('content-type')?.startsWith(mime!));
      assert((await response.text()).length > 0);
    }
    assert.equal((await get('/public/ads/ad-source.css')).status, 404, 'removed asset is not a compatibility endpoint');
    const state = await get(`/public/ads/${id}/state`);
    assert.equal((await state.json()).state, 'visible');
    assert.equal((await fetch(`${origin}/public/ads/${id}`, { method: 'POST' })).status, 405);
    assert.equal((await get('/public/ads/unlisted.js')).status, 404);
    const selected = ads.status('private-account');
    const management = campaignsPage(selected, 'a'.repeat(64), origin, { selectionState: 'ready', streamingConnected: true, previewReady: true,
      output: { state: 'reported', streaming: false, recording: false, expiresInMs: 15000 },
      viewToken: 'b'.repeat(64) });
    assert.match(management, /시험 광고 · 지급 없음/u);
    assert.match(management, /method="post"/u);
    assert.match(management, /name="csrf"/u);
    assert.match(management, /name="view" value="b{64}"/u);
    assert.match(management, /960 × 180/u);
    assert.match(management, /data-public-banner-state="report-ready"/u);
    assert.match(management, /data-selected-test-campaign="chatview-test"/u);
    assert.equal((management.match(/data-test-creative-preview=/gu) ?? []).length, 2);
    assert.match(management, /data-test-creative-preview="chatview-test"/u);
    assert.match(management, /data-test-creative-preview="chatview-test-chat"/u);
    assert.match(management, /background:#141e32/u);
    assert.match(management, /background:#103b40/u);
    assert.match(management, /border:2px solid #6377a2/u);
    assert.match(management, /border:2px solid #2bb5a1/u);
    assert.match(management, /고정 시험 소재 미리보기 · OBS 화면·송출·실제 시청자 노출 아님/u);
    assert.doesNotMatch(management, /<iframe|<img|<video|url\(https?:|data:.*base64/iu,
      'static owner-preview needs no embeddable public browser source, image fetch or tracking media');
    assert.match(management, /별도의 교체 확인 화면/u);
    assert.match(management, /확인 전에 취소하면 기존 배너와 비지급 구간을 그대로 유지/u);
    assert.match(management, /data-test-campaign="chatview-test-chat" data-campaign-selected="false"/u);
    assert.match(management, /action="\/campaigns\/chatview-test-chat\/select"/u,
      'owner may explicitly switch to a second operator-defined nonpayable creative');
    assert.match(management, /id="obs-source-setup"/u);
    assert.match(management, /<input id="source-url" type="url" readonly/u);
    assert.match(management, /<button id="copy-public-source" type="button">공개 주소 복사<\/button>/u);
    assert.match(management, /role="status" aria-live="polite"/u);
    assert.match(management, /페이지 권한 \(Page permissions\)/u);
    assert.match(management, /장면 활성화 시 브라우저 새로고침/u);
    assert.match(management, /배너가 투명하거나 보이지 않을 때 확인할 항목/u);
    assert.match(management, /직접 확인/u);
    const inline = /<script>([\s\S]*?)<\/script>/u.exec(management)?.[1];
    assert(inline && !inline.includes('\r'), 'only fixed management-copy script is inline');
    const exactHash = `'sha256-${createHash('sha256').update(inline).digest('base64')}'`;
    assert.equal(campaignSetupScriptHash, exactHash);
    assert.equal((management.match(/<script>/gu) ?? []).length, 1);
    assert(!inline.includes('fetch(') && !inline.includes('localStorage') && !inline.includes('sessionStorage'),
      'copy helper cannot fetch or persist private state');
    assert.match(management, /OBS나 시청자 화면에서 보였다는 뜻은 아닙니다/u);
    const waiting = campaignsPage(selected, 'a'.repeat(64), origin, { selectionState: 'ready', streamingConnected: true, previewReady: false, output: { state: 'unknown' }, viewToken: 'b'.repeat(64) });
    assert.match(waiting, /data-public-banner-state="waiting-report"/u);
    assert.match(waiting, /투명하게 대기/u);
    assert.match(waiting, /data-obs-setup-readiness="waiting-report"/u);
    ads.stop('private-account');
    const stopped = campaignsPage(ads.status('private-account'), 'a'.repeat(64), origin, { selectionState: 'ready', streamingConnected: true, previewReady: false, output: { state: 'unknown' }, viewToken: 'b'.repeat(64) });
    assert.match(stopped, /data-public-banner-state="not-selected"/u);
    assert.match(stopped, /data-selected-test-campaign="none"/u);
    assert(!stopped.includes('data-public-banner-state="report-ready"'));
    assert.match(stopped, /id="source-url"/u, 'public URL persists after Stop');
    assert.match(stopped, /data-obs-setup-readiness="not-selected"/u);
    const noSource = campaignsPage({ selected: false }, 'a'.repeat(64), origin, { selectionState: 'ready', streamingConnected: true, previewReady: false, output: { state: 'unknown' }, viewToken: 'b'.repeat(64) });
    assert.doesNotMatch(noSource, /id="copy-public-source"|<script|id="source-url"/u,
      'before the first selection there is no copy control or active management script');
    assert.equal((noSource.match(/data-test-creative-preview=/gu) ?? []).length, 2,
      'first-time owner sees fixed artwork before creating any public OBS URL');
    assert.doesNotMatch(noSource, /<iframe|<img|<video/u,
      'the choice preview never starts a public OBS source or privileged renderer');
    sessions.remove(stream.token);
    assert.deepEqual(await (await get(`/public/ads/${id}/state`)).json(), HIDDEN_AD);
  } finally {
    server.closeAllConnections();
    await new Promise<void>(resolve => server.close(() => resolve())); sessions.close();
  }
});

for (const selection of ['provider-required', 'streaming-required', 'ready'] as const) {
  for (const sourceId of [undefined, '1'.repeat(32)]) {
    test(`selection guidance ${selection}, existing source ${!!sourceId} keeps independent public setup`, () => {
      const origin = 'https://service.invalid';
      const html = campaignsPage({ selected: false, sourceId }, 'a'.repeat(64), origin, { selectionState: selection, streamingConnected: false, previewReady: false, output: { state: 'unknown' }, viewToken: 'b'.repeat(64) });
      assert(html.includes(`data-campaign-selection-state="${selection}"`));
      const selectionForm = `action="/campaigns/${TEST_CAMPAIGN.id}/select"`;
      assert.equal(html.includes(selectionForm), selection === 'ready', 'only a ready account is offered selection');
      assert.match(html, /data-public-banner-state="not-selected"/u, 'role approval does not select a banner');
      assert.match(html, /action="\/campaigns\/stop"/u, 'stopping remains available independently');
      assert.match(html, /href="\/campaigns">선택 준비 상태 다시 확인/u);
      assert.match(html, /href="\/account">이 계정의 PC 역할 확인/u);
      assert.match(html, /시험 광고 · 지급 없음/u);
      assert.equal((html.match(/data-test-creative-preview=/gu) ?? []).length, 2,
        'unapproved creators can compare the two inert, nonpayable fixed previews');
      assert.match(html, /data-test-campaign="chatview-test-chat"/u,
        'second campaign is only a fixed catalog entry, not an implicit selection');
      assert.equal(html.includes('action="/campaigns/chatview-test-chat/select"'), selection === 'ready',
        'the second campaign follows exactly the same role prerequisites');
      if (selection !== 'ready') {
        assert.match(html, /게임 PC에 OBS를 설치하거나 실행하지 않습니다/u);
        assert.match(html, /이 관리 화면과 같은 치지직 채널/u);
        assert.match(html, /송출 PC · OBS 역할/u);
        assert.match(html, /브라우저 관리 로그인이나 게임 역할 승인만으로는/u);
      }
      if (sourceId) {
        assert(html.includes(`value="${origin}/public/ads/${sourceId}"`));
        assert.match(html, /기존 공개 URL은 그대로/u);
        assert.match(html, /광고는 자동 선택되지 않습니다/u);
        assert.match(html, /id="copy-public-source"/u);
        assert.match(html, /data-obs-setup-readiness="not-selected"/u);
        const inline = /<script>([\s\S]*?)<\/script>/u.exec(html)?.[1];
        assert(inline);
        assert.equal(campaignSetupScriptHash, `'sha256-${createHash('sha256').update(inline).digest('base64')}'`);
      } else {
        assert.doesNotMatch(html, /id="source-url"|id="copy-public-source"|<script/u);
      }
    });
  }
}
