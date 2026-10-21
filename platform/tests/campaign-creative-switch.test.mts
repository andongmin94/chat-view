// SPDX-License-Identifier: GPL-2.0-or-later
// Real owner-cookie/CSRF + SQLite + streaming WebSocket + public renderer DTO.
// All provider, OBS output and campaign creatives are synthetic and non-payable.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { fixture } from './fixtures/service.mts';
import { TEST_CAMPAIGN, TEST_CAMPAIGN_ALT, HIDDEN_AD } from '../server/ads/campaigns.mts';

test('creator chooses a second approved nonpayable banner while OBS source URL and owner boundaries stay intact', async () => {
  const f = await fixture({
    createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot),
  });
  const readers: WebSocket[] = [];
  try {
    const alice = await f.connect('alice', 'streaming');
    const bob = await f.connect('bob', 'streaming');
    const select = async (b: typeof alice.b, campaignId: string) => {
      await f.page('/campaigns', b); // Fresh CSRF-bound review of the actual streaming approval.
      const response = await f.post(`/campaigns/${campaignId}/select`, b);
      if (response.status === 200) {
        const prompt = await response.text();
        assert.match(prompt, /data-campaign-switch="confirm"/u);
        assert.match(prompt, /취소하고 기존 소재 유지/u);
        assert.match(prompt, /시청자 광고 실측·HP·수익을 기록하지 않습니다/u);
        const newView = /name="view" value="([a-f0-9]{64})"/u.exec(prompt)?.[1];
        assert(newView && newView !== b.view, 'a one-use review replaces the older form token');
        b.view = newView;
        assert.equal((await f.post(`/campaigns/${campaignId}/confirm`, b)).status, 303);
      } else assert.equal(response.status, 303);
      return await f.page('/campaigns', b);
    };
    const choicePreview = await f.page('/campaigns', alice.b);
    assert.equal((choicePreview.match(/data-test-creative-preview=/gu) ?? []).length, 2,
      'creator sees both fixed artwork samples before the first selection');
    assert.doesNotMatch(choicePreview, /id="source-url"|<iframe|<img|<video/iu,
      'preview does not create a public source or embed a live advertisement');
    assert.equal(f.store.database.prepare('SELECT COUNT(*) AS n FROM ad_sources WHERE owner = ?')
      .get('alice')?.n, 0, 'reading static previews cannot allocate a public source ID');

    const first = await select(alice.b, TEST_CAMPAIGN.id);
    const bobPage = await select(bob.b, TEST_CAMPAIGN.id);
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(first)?.[1];
    const bobSource = /\/public\/ads\/([a-f0-9]{32})/u.exec(bobPage)?.[1];
    assert(source && bobSource && source !== bobSource);
    assert.match(first, /data-selected-test-campaign="chatview-test"/u);
    assert.match(first, /data-test-campaign="chatview-test-chat" data-campaign-selected="false"/u);
    assert.match(first, /action="\/campaigns\/chatview-test-chat\/select"/u);
    assert.match(first, /시험 광고 · 지급 없음/u);
    for (const preview of [TEST_CAMPAIGN.id, TEST_CAMPAIGN_ALT.id])
      assert(first.includes(`data-test-creative-preview="${preview}"`));
    assert.match(first, /고정 시험 소재 미리보기 · OBS 화면·송출·실제 시청자 노출 아님/u);
    assert.doesNotMatch(first, /<iframe|<img|<video/iu, 'inert previews never load public artwork');

    for (const token of [alice.lease.token, bob.lease.token]) {
      const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
        headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000,
      });
      peer.on('error', () => {}); readers.push(peer); await once(peer, 'open');
    }
    const report = async (sequence: number, token = alice.lease.outputToken!) => {
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${token}`, 'Content-Type': 'application/json',
      }, body: JSON.stringify({ sequence, streaming: true, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200);
    };
    const snapshot = async (id: string) => {
      const response = await f.request(`/public/ads/${id}/state`);
      assert.equal(response.status, 200);
      assert.equal(response.headers.get('set-cookie'), null);
      return await response.json();
    };
    await report(1);
    await delay(620); await report(2);
    await report(1, bob.lease.outputToken!);
    const before = f.creators.activity.summary('alice').streamingMs;
    assert(before > 0);
    assert.deepEqual((await snapshot(source)).campaign, TEST_CAMPAIGN);
    assert.deepEqual((await snapshot(bobSource)).campaign, TEST_CAMPAIGN);

    await f.page('/campaigns', alice.b);
    assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, alice.b)).status, 303,
      'reselecting the same material is an immediate no-op, not a destructive switch');
    assert.equal(f.creators.activity.summary('alice').streamingMs, before,
      'same-creative click cannot erase the previous nonpayable interval');

    const oldView = alice.b.view!;
    // A first click on another creative is only a preview of consequences:
    // the public artwork, active sender and stored interval remain unchanged.
    await f.page('/campaigns', alice.b);
    const firstClick = await f.post(`/campaigns/${TEST_CAMPAIGN_ALT.id}/select`, alice.b);
    assert.equal(firstClick.status, 200, 'switching requires a second explicit browser confirmation');
    const pendingHtml = await firstClick.text();
    assert.match(pendingHtml, /data-campaign-switch="confirm"/u);
    assert.match(pendingHtml, /방송과 함께하는 챗뷰/u);
    assert.match(pendingHtml, /채팅은 내 화면에, 영상은 방송에/u);
    assert.equal((pendingHtml.match(/data-test-creative-preview=/gu) ?? []).length, 2,
      'confirmation displays both current and proposed artwork without applying either');
    assert.match(pendingHtml, /data-test-creative-preview="chatview-test"/u);
    assert.match(pendingHtml, /data-test-creative-preview="chatview-test-chat"/u);
    assert.match(pendingHtml, /background:#141e32/u);
    assert.match(pendingHtml, /background:#103b40/u);
    assert.doesNotMatch(pendingHtml, /<iframe|<img|<video|public\/ads\//iu,
      'confirmation neither embeds a live OBS source nor requests public ad artwork');
    const pendingView = /name="view" value="([a-f0-9]{64})"/u.exec(pendingHtml)?.[1];
    assert(pendingView && pendingView !== alice.b.view);
    assert.deepEqual((await snapshot(source)).campaign, TEST_CAMPAIGN);
    assert.equal(f.creators.activity.summary('alice').streamingMs, before,
      'unconfirmed creative switch cannot retire or invent an activity bracket');
    await f.page('/campaigns', bob.b);
    assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN_ALT.id}/confirm`, bob.b)).status, 409,
      'a different creator cannot confirm Alice\'s pending switch');
    assert.deepEqual((await snapshot(bobSource)).campaign, TEST_CAMPAIGN,
      'cross-owner confirm cannot alter a separate OBS source');

    // Cancel via the ordinary management link, then attempt the old Yes.
    await f.page('/campaigns', alice.b);
    const staleConfirmation = await f.request(`/campaigns/${TEST_CAMPAIGN_ALT.id}/confirm`, {
      method: 'POST', headers: { Cookie: alice.b.cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf: alice.b.csrf, view: pendingView }),
    });
    assert.equal(staleConfirmation.status, 409);
    assert.deepEqual((await snapshot(source)).campaign, TEST_CAMPAIGN,
      'cancelling keeps the original public OBS creative and stable URL');
    const wrongConfirm = await f.post(`/campaigns/${TEST_CAMPAIGN_ALT.id}/confirm`, alice.b);
    assert.equal(wrongConfirm.status, 409, 'a confirm without a pending switch cannot modify the public source');

    const after = await select(alice.b, TEST_CAMPAIGN_ALT.id);
    assert.match(after, /data-selected-test-campaign="chatview-test-chat"/u);
    assert.match(after, /data-test-campaign="chatview-test" data-campaign-selected="false"/u);
    assert.match(after, /채팅은 내 화면에, 영상은 방송에/u);
    assert(after.includes(`/public/ads/${source}`), 'the previous OBS Browser Source URL is unchanged');
    const second = await snapshot(source);
    assert.equal(second.state, 'visible');
    assert.deepEqual(second.campaign, TEST_CAMPAIGN_ALT, 'live source uses the newly selected fixed creative');
    assert.deepEqual((await snapshot(bobSource)).campaign, TEST_CAMPAIGN, 'other owner retains their own creative');
    for (const secret of [alice.lease.token, alice.lease.outputToken!, alice.lease.sessionToken,
      alice.lease.membership.broadcastSessionId, 'alice'])
      assert(!JSON.stringify(second).includes(secret), 'public creative never contains private session data');

    const stale = await f.request(`/campaigns/${TEST_CAMPAIGN.id}/select`, {
      method: 'POST', headers: { Cookie: alice.b.cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf: alice.b.csrf, view: oldView }),
    });
    assert.equal(stale.status, 409, 'an old management tab cannot undo the freshly chosen creative');
    assert.deepEqual((await snapshot(source)).campaign, TEST_CAMPAIGN_ALT);

    await delay(620); await report(3);
    assert.equal(f.creators.activity.summary('alice').streamingMs, before,
      'creative switch retires the previous output bracket without fabricating activity');
    await delay(620); await report(4);
    const evidence = f.creators.activity.evidence('alice');
    assert(evidence.some(row => row.campaignId === TEST_CAMPAIGN.id && row.reason === 'continuous'));
    assert(evidence.some(row => row.campaignId === TEST_CAMPAIGN_ALT.id && row.reason === 'continuous'));
    const history = await f.page('/campaigns/activity', alice.b);
    assert.match(history, /방송과 함께하는 챗뷰/u);
    assert.match(history, /채팅은 내 화면에, 영상은 방송에/u);
    assert.match(history, /시험 소재를 전환하면 다음 보고/u);
    assert.doesNotMatch(history, /확정 지급|광고 실측 노출 확인/u);

    await f.page('/campaigns', alice.b);
    assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
    assert.deepEqual(await snapshot(source), HIDDEN_AD, 'Stop hides whichever creative was selected');
    assert.equal(await f.page('/campaigns', alice.b).then(html => /data-selected-test-campaign="none"/u.test(html)), true);
    const restored = await select(alice.b, TEST_CAMPAIGN.id);
    assert(restored.includes(`/public/ads/${source}`));
    assert.deepEqual((await snapshot(source)).campaign, TEST_CAMPAIGN,
      'an explicit new selection restores the same OBS public source without another URL');
    assert.deepEqual((await snapshot(bobSource)).campaign, TEST_CAMPAIGN);
    assert.equal(readers[0]!.readyState, WebSocket.OPEN, 'creative selection leaves private chat connected');
  } finally {
    for (const reader of readers) reader.terminate();
    await f.close();
  }
});
