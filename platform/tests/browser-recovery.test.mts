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
  const state = { cookie, csrf: '' };
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