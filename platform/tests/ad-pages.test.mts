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
import { campaignsPage, servePublicAd } from '../server/ads/pages.mts';

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
    const management = campaignsPage(selected, 'a'.repeat(64), origin, true, true);
    assert.match(management, /시험 광고 · 지급 없음/u);
    assert.match(management, /method="post"/u);
    assert.match(management, /name="csrf"/u);
    assert.match(management, /960 × 180/u);
    assert.match(management, /data-public-banner-state="report-ready"/u);
    assert.match(management, /OBS나 시청자 화면에서 보였다는 뜻은 아닙니다/u);
    const waiting = campaignsPage(selected, 'a'.repeat(64), origin, true, false);
    assert.match(waiting, /data-public-banner-state="waiting-report"/u);
    assert.match(waiting, /투명하게 대기/u);
    ads.stop('private-account');
    const stopped = campaignsPage(ads.status('private-account'), 'a'.repeat(64), origin, true, false);
    assert.match(stopped, /data-public-banner-state="not-selected"/u);
    assert(!stopped.includes('data-public-banner-state="report-ready"'));
    assert.doesNotMatch(stopped, /<script/u);
    sessions.remove(stream.token);
    assert.deepEqual(await (await get(`/public/ads/${id}/state`)).json(), HIDDEN_AD);
  } finally {
    server.closeAllConnections();
    await new Promise<void>(resolve => server.close(() => resolve())); sessions.close();
  }
});
