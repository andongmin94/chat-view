// SPDX-License-Identifier: GPL-2.0-or-later
// Actual HTTP/SQLite and one real WebSocket, synthetic provider identity.
// Browser cookies/forms are exercised explicitly; this is not live CHZZK/SSO.
import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import type { Tokens } from '../server/chzzk/api.mts';
import { fixture, tokens, nonce, deferred } from './fixtures/service.mts';

type Fixture = Awaited<ReturnType<typeof fixture>>;
const pages = ['/account', '/campaigns', '/campaigns/activity'] as const;
function browser(f: Fixture, cookie = '') {
  const state: { cookie: string; csrf: string; view?: string } = { cookie, csrf: '' };
  const collect = (response: Response) => {
    const next = response.headers.get('set-cookie');
    if (next) state.cookie = next.split(';')[0]!;
    return response;
  };
  const get = async (path: string) => collect(await f.request(path, { headers: { Cookie: state.cookie } }));
  const page = async (path: string, status = 200) => {
    const response = await get(path); assert.equal(response.status, status);
    const html = await response.text();
    state.csrf = /name="csrf" value="([a-f0-9]{64})"/u.exec(html)?.[1] ?? '';
    state.view = path === '/campaigns' ? /name="view" value="([a-f0-9]{64})"/u.exec(html)?.[1] : undefined;
    return html;
  };
  const post = async (path: string) => collect(await f.post(path, state));
  const begin = async (path: typeof pages[number]) => {
    const html = await page(path, path === '/account' ? 200 : 401);
    assert.match(html, /브라우저 관리 로그인/u);
    assert.match(html, /PC 승인은 그대로/u);
    const response = await post(`${path}/login`);
    assert.equal(response.status, 303);
    const url = new URL(response.headers.get('location')!);
    assert.equal(url.origin, 'https://chzzk.naver.com');
    const oauth = url.searchParams.get('state'); assert.match(oauth!, /^[a-f0-9]{64}$/u);
    return oauth!;
  };
  const callback = (oauth: string, owner: string) => get(`/callback?code=${encodeURIComponent(owner)}&state=${oauth}`);
  const identify = async (path: typeof pages[number], owner: string) => {
    const oauth = await begin(path), previousCookie = state.cookie;
    const response = await callback(oauth, owner);
    assert.equal(response.status, 303);
    assert.equal(response.headers.get('location'), '/account/confirm');
    assert.notEqual(state.cookie, previousCookie, 'identity rotates the anonymous cookie');
    return page('/account/confirm');
  };
  const confirm = async (path: typeof pages[number]) => {
    const previousCookie = state.cookie;
    const response = await post('/account/confirm');
    assert.equal(response.status, 303); assert.equal(response.headers.get('location'), path);
    assert.notEqual(state.cookie, previousCookie, 'consent rotates the cookie and CSRF again');
    return page(path);
  };
  return { state, get, page, post, begin, callback, identify, confirm };
}

test('expired browser recovers its activity page without new PC approval or live-chat restart', { timeout: 15000 }, async t => {
  const nativeNow = Date.now(); let browserNow = nativeNow;
  t.mock.method(Date, 'now', () => browserNow);
  const f = await fixture({ now: () => nativeNow,
    createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  let peer: WebSocket | undefined;
  try {
    const alice = await f.connect('alice', 'streaming');
    const html = await f.page('/campaigns', alice.b);
    const select = /action="([^"]+\/select)"/u.exec(html)?.[1]; assert(select);
    assert.equal((await f.post(select, alice.b)).status, 303);
    const selections = f.store.database.prepare('SELECT * FROM ad_selections').all();
    const connections = f.store.connections('alice'), grant = f.grants.load('alice');
    const starts = f.startedWith.length;
    peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
      { headers: { Authorization: `Bearer ${alice.lease.token}` } });
    peer.on('error', () => {}); await once(peer, 'message');
    const b = browser(f, alice.b.cookie);
    browserNow += 3600001;
    const stale = await f.post('/campaigns/stop', alice.b);
    assert.equal(stale.status, 401); assert.match(await stale.text(), /자동으로 다시 실행하지 않습니다/u);
    f.app.login.start = () => { throw new Error('management must not start native approval'); };
    f.setExchange(async () => tokens('alice', 99));
    const confirmation = await b.identify('/campaigns/activity', 'alice');
    assert.match(confirmation, /channel &lt;alice&gt;/u);
    assert.doesNotMatch(confirmation, /access:|refresh:|\/connections\//u);
    assert.equal((await b.post('/campaigns/stop')).status, 401, 'identity before consent cannot change campaigns');
    const activity = await b.confirm('/campaigns/activity');
    assert.match(activity, /현재 브라우저 관리 채널/u);
    assert.deepEqual(f.store.connections('alice'), connections);
    assert.deepEqual(f.store.database.prepare('SELECT * FROM ad_selections').all(), selections);
    assert.deepEqual(f.grants.load('alice'), grant, 'browser proof never replaces the provider grant');
    assert.equal(f.startedWith.length, starts); assert.equal(f.active.get('alice'), 1);
    assert.equal(f.refreshCalls, 0); assert.equal(peer.readyState, WebSocket.OPEN);
    const frame = once(peer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('chat survives browser recovery');
    assert.match(String((await frame)[0]), /chat survives browser recovery/u);
    assert.equal((await f.request('/account', { headers: { Cookie: alice.b.cookie } })).status, 200);
    assert.ok(f.creators.gateway(alice.lease.token));
  } finally { peer?.terminate(); await f.close(); }
});

test('each fixed management destination resumes only after explicit channel confirmation', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    for (const path of pages) {
      const b = browser(f);
      await b.identify(path, 'alice');
      const oldCsrf = b.state.csrf;
      const gated = await b.get(path);
      assert.equal(gated.status, 303); assert.equal(gated.headers.get('location'), '/account/confirm');
      await b.confirm(path);
      assert.equal((await f.post('/account/confirm', { ...b.state, csrf: oldCsrf })).status, 403);
      assert.equal((await b.post('/account/confirm')).status, 401, 'confirmed proof is consumed once');
    }
    assert.equal(f.store.connections('alice').length, 1);
    assert.ok(f.creators.gateway(alice.lease.token));
  } finally { await f.close(); }
});

test('switching browser accounts preserves native accounts and rejects forms from the old identity', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'streaming'), bob = await f.connect('bob', 'gaming');
    const b = browser(f, alice.b.cookie);
    const html = await b.page('/account');
    assert.match(html, /브라우저 관리 계정 바꾸기/u);
    const oldForm = { ...b.state };
    assert.equal((await b.post('/account/switch')).status, 303);
    assert.notEqual(b.state.cookie, oldForm.cookie);
    await b.identify('/campaigns', 'bob');
    const switched = await b.confirm('/campaigns');
    assert.match(switched, /channel &lt;bob&gt;/u); assert.doesNotMatch(switched, /channel &lt;alice&gt;/u);
    const stale = await f.post('/account/revoke', { cookie: b.state.cookie, csrf: oldForm.csrf });
    assert.equal(stale.status, 403, 'a pre-switch form cannot revoke the newly selected account');
    assert.equal((await f.post('/account/revoke', oldForm)).status, 401);
    assert.ok(f.creators.gateway(alice.lease.token)); assert.ok(f.creators.gateway(bob.lease.token));
    assert.equal(f.store.connections('alice').length, 1); assert.equal(f.store.connections('bob').length, 1);
  } finally { await f.close(); }
});

test('cancelled or expired confirmation gives no management or provider/PC grant', async t => {
  const nativeNow = Date.now(); let browserNow = nativeNow;
  t.mock.method(Date, 'now', () => browserNow);
  const f = await fixture({ now: () => nativeNow });
  try {
    const b = browser(f);
    await b.identify('/campaigns', 'new-owner');
    const pending = { ...b.state };
    assert.equal((await b.post('/account/cancel')).headers.get('location'), '/campaigns');
    assert.equal((await f.post('/account/confirm', pending)).status, 401);
    assert.equal(f.grants.load('new-owner'), undefined); assert.deepEqual(f.store.connections('new-owner'), []);
    await b.identify('/account', 'new-owner'); browserNow += 300000;
    assert.equal((await b.post('/account/confirm')).status, 401);
    await b.identify('/account', 'new-owner');
    const account = await b.confirm('/account');
    assert.match(account, /channel &lt;new-owner&gt;/u);
    assert.equal(f.creators.describe('new-owner').authorized, false);
    assert.equal(f.grants.load('new-owner'), undefined); assert.deepEqual(f.store.connections('new-owner'), []);
    assert.equal(f.startedWith.length, 0, 'browser identity does not start an upstream');
  } finally { await f.close(); }
});

test('state is cookie-bound, single-use and cannot supply a redirect or management owner', async () => {
  const f = await fixture();
  try {
    const b = browser(f), other = browser(f);
    const oauth = await b.begin('/account'); await other.page('/account');
    assert.equal((await other.callback(oauth, 'alice')).status, 400);
    assert.equal((await b.get(`/callback?code=alice&state=${oauth}&state=${oauth}`)).status, 400);
    assert.equal((await b.get(`/callback?code=alice&code=bob&state=${oauth}`)).status, 400);
    const before = b.state.cookie;
    assert.equal((await b.callback(oauth, 'alice')).status, 303);
    assert.equal((await f.request(`/callback?code=alice&state=${oauth}`, { headers: { Cookie: before } })).status, 400);
    await b.page('/account/confirm');
    assert.equal((await f.request('/account/confirm', { method: 'POST', headers: {
      Cookie: b.state.cookie, Origin: f.origin, 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: new URLSearchParams({ csrf: b.state.csrf, owner: 'bob', returnTo: 'https://untrusted.invalid' }) })).status, 403);
    await b.confirm('/account');
    assert.equal(f.exchangeCalls, 1);
    assert.equal((await b.get('/account?returnTo=https://untrusted.invalid')).status, 403);
    assert.equal((await other.post('/unknown/login')).status, 401);
    assert.equal((await f.request('/account/login', { method: 'POST', headers: {
      Cookie: other.state.cookie, Origin: 'https://untrusted.invalid',
      'Content-Type': 'application/x-www-form-urlencoded',
    }, body: new URLSearchParams({ csrf: other.state.csrf }) })).status, 403);
  } finally { await f.close(); }
});

test('provider cancellation/failure allows explicit retry without leaking upstream errors', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming'), b = browser(f);
    const first = await b.begin('/account');
    const cancelled = await b.get(`/callback?state=${first}&error=PRIVATE_PROVIDER_MESSAGE`);
    assert.equal(cancelled.status, 400); assert.doesNotMatch(await cancelled.text(), /PRIVATE_PROVIDER_MESSAGE/u);
    assert.equal((await b.callback(first, 'alice')).status, 400);
    const second = await b.begin('/account');
    f.setExchange(async () => { throw new Error('PRIVATE_TOKEN_OR_ERROR'); });
    const failed = await b.callback(second, 'alice');
    assert.equal(failed.status, 503); assert.doesNotMatch(await failed.text(), /PRIVATE_TOKEN_OR_ERROR/u);
    assert.equal(f.refreshCalls, 0); assert.ok(f.creators.gateway(alice.lease.token));
    f.setExchange(undefined); await b.identify('/account', 'alice'); await b.confirm('/account');
    assert.equal(f.store.connections('alice').length, 1);
  } finally { await f.close(); }
});

test('a newer browser attempt fences a late identity response and does not replace its cookie', async () => {
  const f = await fixture();
  try {
    const b = browser(f), pending = deferred<Tokens>(), started = deferred<void>();
    const oldState = await b.begin('/account');
    f.setExchange(() => { started.resolve(); return pending.promise; });
    const late = b.callback(oldState, 'alice'); await started.promise;
    const newState = await b.begin('/campaigns');
    const expectedCookie = b.state.cookie;
    pending.resolve(tokens('alice'));
    const response = await late; assert.equal(response.status, 401);
    assert.equal(response.headers.get('set-cookie'), null); assert.equal(b.state.cookie, expectedCookie);
    f.setExchange(undefined);
    assert.equal((await b.callback(newState, 'bob')).status, 303);
    await b.page('/account/confirm'); const html = await b.confirm('/campaigns');
    assert.match(html, /channel &lt;bob&gt;/u); assert.doesNotMatch(html, /channel &lt;alice&gt;/u);
    assert.equal(f.grants.load('alice'), undefined); assert.equal(f.grants.load('bob'), undefined);
  } finally { await f.close(); }
});

test('revocation between identity and confirmation prevents stale browser restoration', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming'), b = browser(f);
    await b.identify('/account', 'alice');
    await f.creators.revoke('alice');
    assert.equal((await b.post('/account/confirm')).status, 401);
    assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    assert.equal(f.grants.load('alice'), undefined);
    assert.equal(f.creators.describe('alice').authorized, false);
  } finally { await f.close(); }
});

test('expired attempt during provider I/O cannot install a late browser identity', async t => {
  const nativeNow = Date.now(); let browserNow = nativeNow;
  t.mock.method(Date, 'now', () => browserNow);
  const f = await fixture({ now: () => nativeNow });
  try {
    const b = browser(f), pending = deferred<Tokens>(), started = deferred<void>();
    const oauth = await b.begin('/account');
    f.setExchange(() => { started.resolve(); return pending.promise; });
    const response = b.callback(oauth, 'alice'); await started.promise;
    browserNow += 300000; pending.resolve(tokens('alice'));
    const expired = await response;
    assert.equal(expired.status, 401); assert.equal(expired.headers.get('set-cookie'), null);
    assert.equal(f.grants.load('alice'), undefined);
    assert.deepEqual(f.store.connections('alice'), []);
    assert.equal((await b.get('/account/confirm')).status, 401);
  } finally { await f.close(); }
});

test('confirmation escapes provider channel text and GET requests never perform account switch', async () => {
  const f = await fixture();
  try {
    const b = browser(f);
    f.setUser(async () => ({ channelId: 'owner', channelName: '<script>private()</script>' }));
    const confirmation = await b.identify('/account', 'owner');
    assert.match(confirmation, /&lt;script&gt;private\(\)&lt;\/script&gt;/u);
    assert.doesNotMatch(confirmation, /<script>/u);
    const html = await b.confirm('/account'); assert.doesNotMatch(html, /<script>/u);
    assert.equal((await b.get('/account/switch')).status, 404);
    assert.equal((await b.get('/account/cancel')).status, 404);
    const account = await b.get('/account');
    assert.equal(account.status, 200); assert.match(await account.text(), /현재 브라우저 관리 채널/u);
    assert.equal(account.headers.get('referrer-policy'), 'same-origin');
    assert.equal(account.headers.get('cache-control'), 'no-store');
    assert.equal(f.store.connections('owner').length, 0);
  } finally { await f.close(); }
});

test('browser expiry during sender outage never reauthorizes native streaming or restores an old campaign form', { timeout: 15000 }, async t => {
  const nativeNow = Date.now(); let browserNow = nativeNow;
  t.mock.method(Date, 'now', () => browserNow);
  const f = await fixture({ now: () => nativeNow,
    createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const gaming = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const bob = await f.connect('bob', 'gaming');
    const open = async (token: string) => {
      const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
        headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      peer.on('error', () => {}); peers.push(peer);
      await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
      return peer;
    };
    const streamingPeer = await open(sender.lease.token);
    const gamePeer = await open(gaming.token);
    await open(bob.lease.token);
    const select = '/campaigns/chatview-test/select';
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(select, sender.b)).status, 303);
    const b = browser(f, sender.b.cookie);
    const old = await b.page('/campaigns');
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(old)?.[1]; assert(source);
    const view = /name="view" value="([a-f0-9]{64})"/u.exec(old)?.[1]; assert(view);
    const sourceUrl = f.origin + '/public/ads/' + source;
    const ad = async () => (await f.request('/public/ads/' + source + '/state')).json() as Promise<{ state: string }>;
    const selection = () => f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source)?.connection_id;
    const initialConnection = sender.lease.membership.connectionId;
    const initialGrant = f.grants.load('alice');
    const initialStarts = f.startedWith.length;
    const output = (token: string) => f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
    const oldBrowser = { cookie: b.state.cookie, csrf: b.state.csrf };
    const staleAction = (cookie: string, csrf: string, path = '/campaigns/stop') => f.request(path, {
      method: 'POST', headers: { Cookie: cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf, view }),
    });
    assert.equal((await output(sender.lease.outputToken!)).status, 200);
    assert.equal((await ad()).state, 'visible');
    const closed = once(streamingPeer, 'close', { signal: AbortSignal.timeout(3000) });
    streamingPeer.close(); await closed;
    assert.equal(f.creators.describe('alice').presence.streamingConnections, 0);
    assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
    assert.equal(f.creators.describe('alice').output.state, 'unknown');
    assert.equal((await ad()).state, 'hidden');
    assert.equal(selection(), initialConnection);

    browserNow += 3_600_001;
    const blocked = await staleAction(oldBrowser.cookie, oldBrowser.csrf);
    assert.equal(blocked.status, 401, 'expired browser cannot Stop the chosen sender campaign');
    assert.match(await blocked.text(), /data-management-expired-action="blocked"/u);
    const login = await b.page('/campaigns', 401);
    assert.match(login, /data-management-session-boundary="browser-only"/u);
    assert.match(login, /로그인 만료·계정 전환 자체는 송출 PC 승인, 시험 광고 선택, 공개 URL을 변경하지 않습니다/u);
    assert.match(login, /승인 자체를 별도로 철회했다면 광고 선택은 해제됩니다/u);
    assert.doesNotMatch(login, new RegExp(source, 'u'), 'anonymous login must not leak private public-source association');
    assert.notEqual(b.state.cookie, oldBrowser.cookie);
    assert.equal((await staleAction(oldBrowser.cookie, oldBrowser.csrf, select)).status, 401);
    assert.equal((await staleAction(b.state.cookie, oldBrowser.csrf)).status, 403);
    assert.equal(selection(), initialConnection);
    assert.equal((await ad()).state, 'hidden');

    const returning = await f.native('/display/refresh', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming');
    assert.equal(returning.status, 200);
    const lease = await returning.json() as typeof sender.lease;
    assert.deepEqual(lease.membership, sender.lease.membership);
    const returnedPeer = await open(lease.token);
    assert.equal(f.creators.describe('alice').output.state, 'unknown');
    assert.equal((await ad()).state, 'hidden', 'socket return cannot reuse the pre-expiry output report');
    assert.equal((await output(lease.outputToken!)).status, 200);
    assert.equal((await ad()).state, 'visible');
    assert.equal(selection(), initialConnection);

    await b.identify('/campaigns', 'alice');
    assert.equal((await staleAction(oldBrowser.cookie, oldBrowser.csrf)).status, 401);
    assert.equal((await b.post('/campaigns/stop')).status, 401, 'provider identity alone is not management consent');
    const current = await b.confirm('/campaigns');
    assert(current.includes(sourceUrl));
    assert.match(current, /data-public-banner-state="report-ready"/u);
    assert.equal(selection(), initialConnection);
    assert.equal(f.store.find(gaming.sessionToken)?.membership?.role, 'gaming');
    assert(f.store.find(bob.lease.sessionToken));
    assert.deepEqual(f.grants.load('alice'), initialGrant);
    assert.equal(f.startedWith.length, initialStarts);
    assert.equal(f.refreshCalls, 0, 'browser login never refreshes provider grants for streaming approval');
    const gameMessage = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    const senderMessage = once(returnedPeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('chat survives expired manager and returned sender');
    assert.match(String((await gameMessage)[0]), /chat survives expired manager and returned sender/u);
    assert.match(String((await senderMessage)[0]), /chat survives expired manager and returned sender/u);
  } finally { for (const peer of peers) peer.terminate(); await f.close(); }
});

test('manager switching to another creator during sender outage never transfers ad or streaming authority', { timeout: 15000 }, async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const gaming = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const bob = await f.connect('bob', 'gaming');
    const open = async (token: string) => {
      const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
        headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      peer.on('error', () => {}); peers.push(peer);
      await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
      return peer;
    };
    const streamingPeer = await open(sender.lease.token);
    const gamePeer = await open(gaming.token);
    await open(bob.lease.token);
    const select = '/campaigns/chatview-test/select';
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(select, sender.b)).status, 303);
    const b = browser(f, sender.b.cookie);
    const alicePage = await b.page('/campaigns');
    const source = /\/public\/ads\/([a-f0-9]{32})/u.exec(alicePage)?.[1]; assert(source);
    const view = /name="view" value="([a-f0-9]{64})"/u.exec(alicePage)?.[1]; assert(view);
    const aliceForm = { ...b.state, view };
    const sourceUrl = f.origin + '/public/ads/' + source;
    const snapshot = async () => (await f.request('/public/ads/' + source + '/state')).json() as Promise<{ state: string }>;
    const selection = () => f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id = ?')
      .get(source)?.connection_id;
    const streamId = sender.lease.membership.connectionId;
    const connections = f.store.connections('alice'), bobConnections = f.store.connections('bob');
    const aliceGrant = f.grants.load('alice'), bobGrant = f.grants.load('bob');
    const starts = f.startedWith.length;
    const output = (token: string) => f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
    const staleAction = (cookie: string, csrf: string, path = '/campaigns/stop') => f.request(path, {
      method: 'POST', headers: { Cookie: cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf, view }),
    });
    assert.equal((await output(sender.lease.outputToken!)).status, 200);
    assert.equal((await snapshot()).state, 'visible');
    const closed = once(streamingPeer, 'close', { signal: AbortSignal.timeout(3000) });
    streamingPeer.close(); await closed;
    assert.equal((await snapshot()).state, 'hidden');
    assert.equal(selection(), streamId);
    assert.equal((await b.post('/account/switch')).status, 303);
    assert.notEqual(b.state.cookie, aliceForm.cookie);
    assert.equal((await staleAction(aliceForm.cookie, aliceForm.csrf)).status, 401);
    assert.equal((await staleAction(b.state.cookie, aliceForm.csrf)).status, 403);
    assert.equal(selection(), streamId);

    const returning = await f.native('/display/refresh', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming');
    assert.equal(returning.status, 200);
    const lease = await returning.json() as typeof sender.lease;
    assert.deepEqual(lease.membership, sender.lease.membership);
    const returnedPeer = await open(lease.token);
    assert.equal((await snapshot()).state, 'hidden');
    assert.equal((await output(lease.outputToken!)).status, 200);
    assert.equal((await snapshot()).state, 'visible', 'old campaign returns without browser access or reselection');

    const consent = await b.identify('/campaigns', 'bob');
    assert.match(consent, /channel &lt;bob&gt;/u);
    assert.equal((await staleAction(aliceForm.cookie, aliceForm.csrf, select)).status, 401);
    assert.equal((await b.post('/campaigns/stop')).status, 401, 'unconfirmed Bob cannot manage either campaign');
    const page = await b.confirm('/campaigns');
    assert.match(page, /channel &lt;bob&gt;/u);
    assert.match(page, /data-campaign-selection-state="streaming-required"/u);
    assert.doesNotMatch(page, new RegExp(source, 'u'), 'Bob never sees Alice public source association');
    assert.equal((await staleAction(b.state.cookie, b.state.csrf, select)).status, 409,
      'a different owner cannot select Alice streaming grant with its prior view marker');
    assert.equal(selection(), streamId);
    assert.equal(f.store.database.prepare('SELECT COUNT(*) AS n FROM ad_selections').get()!.n, 1);
    assert.deepEqual(f.store.connections('alice'), connections);
    assert.deepEqual(f.store.connections('bob'), bobConnections);
    assert.deepEqual(f.grants.load('alice'), aliceGrant);
    assert.deepEqual(f.grants.load('bob'), bobGrant);
    assert.equal(f.startedWith.length, starts);
    assert.equal(f.refreshCalls, 0);
    assert.equal(f.store.find(gaming.sessionToken)?.membership?.role, 'gaming');
    const gameFrame = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    const senderFrame = once(returnedPeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('game and sender unchanged after manager switched to Bob');
    assert.match(String((await gameFrame)[0]), /game and sender unchanged after manager switched to Bob/u);
    assert.match(String((await senderFrame)[0]), /game and sender unchanged after manager switched to Bob/u);

    assert.equal((await b.post('/account/switch')).status, 303);
    await b.identify('/campaigns', 'alice');
    const restoredManager = await b.confirm('/campaigns');
    assert(restoredManager.includes(sourceUrl));
    assert.match(restoredManager, /data-public-banner-state="report-ready"/u);
    assert.equal(selection(), streamId, 'switching management accounts never silently reselects an ad');
  } finally { for (const peer of peers) peer.terminate(); await f.close(); }
});


test('expired management browser cannot reselect a revoked sender before fresh sender consent and owner choice', { timeout: 15000 }, async t => {
  const nativeNow = Date.now(); let browserNow = nativeNow;
  t.mock.method(Date, 'now', () => browserNow);
  const f = await fixture({ now: () => nativeNow,
    createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const game = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const bob = await f.connect('bob', 'gaming');
    const open = async (token: string) => {
      const ws = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
        { headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      ws.on('error', () => {}); peers.push(ws);
      await once(ws, 'message', { signal: AbortSignal.timeout(3000) }); return ws;
    };
    const senderPeer = await open(sender.lease.token), gamePeer = await open(game.token);
    await open(bob.lease.token);
    const select = '/campaigns/chatview-test/select';
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(select, sender.b)).status, 303);
    const manager = browser(f, sender.b.cookie);
    const initial = await manager.page('/campaigns');
    const sourceId = /\/public\/ads\/([a-f0-9]{32})/u.exec(initial)?.[1]; assert(sourceId);
    const sourceUrl = f.origin + '/public/ads/' + sourceId;
    const old = { ...manager.state }; assert(old.view);
    const selection = () => f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id=?')
      .get(sourceId)?.connection_id;
    const state = async () => (await f.request('/public/ads/' + sourceId + '/state')).json() as Promise<{ state: string }>;
    const output = (token: string) => f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
    assert.equal((await output(sender.lease.outputToken!)).status, 200);
    assert.equal((await state()).state, 'visible');
    const aliceGrant = f.grants.load('alice'), bobGrant = f.grants.load('bob');
    const upstreamCount = f.startedWith.length;
    browserNow += 3_600_001;
    const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.native('/display/signout', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming')).status, 200);
    await closed;
    assert.equal(f.store.find(sender.lease.sessionToken), undefined);
    assert.equal(selection(), undefined, 'the revoked streaming approval cascades deletion of the selection FK');
    assert.equal((await state()).state, 'hidden');
    assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
    const blocked = await f.request('/campaigns/stop', {
      method: 'POST', headers: { Cookie: old.cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf: old.csrf, view: old.view }),
    });
    assert.equal(blocked.status, 401, 'expired form cannot stop or restore a revoked campaign');
    assert.match(await blocked.text(), /data-management-expired-action="blocked"/u);
    const anonymous = await manager.page('/campaigns', 401);
    assert.match(anonymous, /data-management-revoked-sender="manual-reselect"/u);
    assert.match(anonymous, /송출 PC 승인 자체를 별도로 철회했다면 광고 선택은 해제됩니다/u);
    assert.doesNotMatch(anonymous, new RegExp(sourceId, 'u'), 'no creator-specific public source on anonymous pages');
    assert.notEqual(manager.state.cookie, old.cookie);
    assert.equal((await f.post(select, manager.state)).status, 401);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming')).status, 401);
    assert.equal((await output(sender.lease.outputToken!)).status, 401);
    assert.equal(selection(), undefined);
    assert.equal((await state()).state, 'hidden');

    const login = await manager.identify('/campaigns', 'alice');
    assert.match(login, /channel &lt;alice&gt;/u);
    assert.equal((await manager.post(select)).status, 401, 'provider identification alone is not owner management consent');
    const confirmed = await manager.confirm('/campaigns');
    assert.match(confirmed, /data-campaign-selection-state="streaming-required"/u);
    assert.match(confirmed, /data-banner-manual-reselect="sender-required"/u);
    assert(confirmed.includes(sourceUrl));
    const pending = await f.start('streaming');
    assert.equal(f.app.login.view(pending.id).role, 'streaming');
    const renewed = await f.approve(pending, 'streaming', manager.state);
    assert.notEqual(renewed.membership.connectionId, sender.lease.membership.connectionId);
    assert.equal(renewed.membership.broadcastSessionId, game.membership.broadcastSessionId);
    const renewedPeer = await open(renewed.token);
    assert.equal((await output(renewed.outputToken!)).status, 200);
    assert.equal((await state()).state, 'hidden', 'a new sender and fresh output cannot select a revoked campaign');
    assert.equal(selection(), undefined);
    const ready = await manager.page('/campaigns');
    assert.match(ready, /data-banner-manual-reselect="ready"/u);
    assert.match(ready, /data-public-banner-state="not-selected"/u);
    assert(ready.includes(sourceUrl));
    assert.equal((await manager.post(select)).status, 303, 'owner explicitly reselects the existing URL');
    assert.equal(selection(), renewed.membership.connectionId);
    assert.equal((await state()).state, 'visible');
    assert.deepEqual(f.grants.load('alice'), aliceGrant);
    assert.deepEqual(f.grants.load('bob'), bobGrant);
    assert.equal(f.startedWith.length, upstreamCount);
    assert.equal(f.refreshCalls, 0);
    assert(f.store.find(bob.lease.sessionToken));
    const gameFrame = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    const senderFrame = once(renewedPeer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('gaming and replacement sender stay live after manual reselect');
    assert.match(String((await gameFrame)[0]), /gaming and replacement sender stay live after manual reselect/u);
    assert.match(String((await senderFrame)[0]), /gaming and replacement sender stay live after manual reselect/u);
  } finally { for (const ws of peers) ws.terminate(); await f.close(); }
});

test('switching management to Bob cannot transfer a revoked Alice streaming grant or choose her previous campaign', { timeout: 15000 }, async () => {
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const peers: WebSocket[] = [];
  try {
    const sender = await f.connect('alice', 'streaming');
    const game = await f.approve(await f.start('gaming'), 'gaming', sender.b);
    const bob = await f.connect('bob', 'gaming');
    const open = async (token: string) => {
      const ws = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
        { headers: { Authorization: 'Bearer ' + token }, handshakeTimeout: 3000 });
      ws.on('error', () => {}); peers.push(ws);
      await once(ws, 'message', { signal: AbortSignal.timeout(3000) }); return ws;
    };
    const senderPeer = await open(sender.lease.token);
    const gamePeer = await open(game.token); await open(bob.lease.token);
    const select = '/campaigns/chatview-test/select';
    await f.page('/campaigns', sender.b);
    assert.equal((await f.post(select, sender.b)).status, 303);
    const manager = browser(f, sender.b.cookie);
    const prior = await manager.page('/campaigns');
    const sourceId = /\/public\/ads\/([a-f0-9]{32})/u.exec(prior)?.[1]; assert(sourceId);
    const originalUrl = f.origin + '/public/ads/' + sourceId;
    const old = { ...manager.state }; assert(old.view);
    const state = async () => (await f.request('/public/ads/' + sourceId + '/state')).json() as Promise<{ state: string }>;
    const selection = () => f.store.database.prepare('SELECT connection_id FROM ad_selections WHERE source_id=?')
      .get(sourceId)?.connection_id;
    const output = (token: string) => f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: 'ChatView-Output ' + token, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence: 1, streaming: false, recording: false, sampleAgeMs: 0 }) });
    assert.equal((await output(sender.lease.outputToken!)).status, 200);
    assert.equal((await state()).state, 'visible');
    const aliceGrant = f.grants.load('alice'), bobGrant = f.grants.load('bob');
    const started = f.startedWith.length, bobConnections = f.store.connections('bob');
    assert.equal((await manager.post('/account/switch')).status, 303);
    assert.notEqual(manager.state.cookie, old.cookie);
    const closed = once(senderPeer, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.native('/display/signout', 'ChatView-Session',
      sender.lease.sessionToken, 'streaming')).status, 200);
    await closed;
    assert.equal((await state()).state, 'hidden');
    assert.equal(selection(), undefined);
    assert.equal(f.creators.describe('alice').presence.gamingConnections, 1);
    const stale = async (cookie: string, csrf: string, view: string, path = select) => f.request(path, {
      method: 'POST', headers: { Cookie: cookie, Origin: f.origin,
        'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ csrf, view }),
    });
    assert.equal((await stale(old.cookie, old.csrf, old.view)).status, 401);
    assert.equal((await stale(manager.state.cookie, old.csrf, old.view)).status, 403);
    const bobLanding = await manager.identify('/campaigns', 'bob');
    assert.match(bobLanding, /channel &lt;bob&gt;/u);
    assert.equal((await manager.post(select)).status, 401);
    const bobPage = await manager.confirm('/campaigns');
    assert.match(bobPage, /channel &lt;bob&gt;/u);
    assert.match(bobPage, /data-campaign-selection-state="streaming-required"/u);
    assert.doesNotMatch(bobPage, new RegExp(sourceId, 'u'));
    assert.equal((await manager.post(select)).status, 409, 'Bob has no authorized streaming grant to select');
    assert.equal((await manager.post('/campaigns/stop')).status, 303, 'Bob Stop only affects Bob');
    assert.equal(selection(), undefined);
    const pending = await f.start('streaming');
    assert.equal(f.app.login.view(pending.id).role, 'streaming');
    assert.equal(f.store.connections('alice').filter(c => c.role === 'streaming').length, 0);
    assert.deepEqual(f.store.connections('bob'), bobConnections, 'Bob browser never inherits Alice sender');

    assert.equal((await manager.post('/account/switch')).status, 303);
    const aliceLanding = await manager.identify('/campaigns', 'alice');
    assert.match(aliceLanding, /channel &lt;alice&gt;/u);
    const regained = await manager.confirm('/campaigns');
    assert.match(regained, /data-banner-manual-reselect="sender-required"/u);
    assert(regained.includes(originalUrl));
    const sender2 = await f.approve(pending, 'streaming', manager.state);
    assert.notEqual(sender2.membership.connectionId, sender.lease.membership.connectionId);
    const sender2Peer = await open(sender2.token);
    assert.equal((await output(sender2.outputToken!)).status, 200);
    assert.equal((await state()).state, 'hidden', 'Alice still must explicitly select the campaign');
    assert.equal(selection(), undefined);
    const fresh = await manager.page('/campaigns');
    assert.match(fresh, /data-banner-manual-reselect="ready"/u);
    assert(fresh.includes(originalUrl));
    assert.equal((await manager.post(select)).status, 303);
    assert.equal(selection(), sender2.membership.connectionId);
    assert.equal((await state()).state, 'visible');
    assert.deepEqual(f.store.connections('bob'), bobConnections);
    assert.deepEqual(f.grants.load('alice'), aliceGrant);
    assert.deepEqual(f.grants.load('bob'), bobGrant);
    assert.equal(f.startedWith.length, started);
    assert.equal(f.refreshCalls, 0);
    const gameFrame = once(gamePeer, 'message', { signal: AbortSignal.timeout(3000) });
    const senderFrame = once(sender2Peer, 'message', { signal: AbortSignal.timeout(3000) });
    f.chats.get('alice')!.publish('Alice gaming chat survives Bob management and sender replacement');
    assert.match(String((await gameFrame)[0]), /Alice gaming chat survives Bob management and sender replacement/u);
    assert.match(String((await senderFrame)[0]), /Alice gaming chat survives Bob management and sender replacement/u);
  } finally { for (const ws of peers) ws.terminate(); await f.close(); }
});
