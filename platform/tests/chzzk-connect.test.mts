// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { request as httpRequest } from 'node:http';
import type { TestContext } from 'node:test';
import { ChzzkApi } from '../server/chzzk/api.mts';
import type { Fetch } from '../server/chzzk/api.mts';
import { startProbe } from '../tools/chzzk-connect.mts';

const credentials = { clientId: 'app', clientSecret: 'PRIVATE_CLIENT' };
const tokenContent = { accessToken: 'PRIVATE_ACCESS', refreshToken: 'PRIVATE_REFRESH', tokenType: 'Bearer', expiresIn: '86400' };
const ok = (content: unknown) => Response.json({ code: 200, content });
const standard: Fetch = async url => url.endsWith('/users/me') ?
  ok({ channelId: 'my-channel', channelName: '<script>alert(1)</script> 한국어' }) :
  url.endsWith('/sessions/auth') ? ok({ url: 'https://ssio08.nchat.naver.com/?auth=PRIVATE_SESSION' }) :
  url.endsWith('/revoke') ? ok(null) : ok(tokenContent);

async function fixture(t: TestContext, provider: Fetch = standard, now?: () => number) {
  const calls: { url: string; init: RequestInit }[] = [];
  const api = new ChzzkApi(credentials, async (url, init) => { calls.push({ url, init }); return provider(url, init); });
  const probe = await startProbe({ credentials, port: 0, api, now });
  t.after(() => probe.close());
  const first = await fetch(`${probe.origin}/`);
  const cookie = first.headers.get('set-cookie')!.split(';')[0];
  await first.text();
  const root = await fetch(`${probe.origin}/`, { headers: { Cookie: cookie } });
  const html = await root.text();
  const csrf = html.match(/csrf=([a-f0-9]{64})/u)![1];
  const get = (path: string, headers = {}) => fetch(`${probe.origin}${path}`, { headers: { Cookie: cookie, ...headers }, redirect: 'manual' });
  const post = (path: string, headers = {}) => fetch(`${probe.origin}${path}?csrf=${csrf}`, {
    method: 'POST', headers: { Cookie: cookie, Origin: probe.origin, ...headers }, redirect: 'manual',
  });
  async function begin() {
    const response = await post('/connect');
    assert.equal(response.status, 303);
    const auth = new URL(response.headers.get('location')!);
    assert.equal(auth.searchParams.get('redirectUri'), `${probe.origin}/callback`);
    return auth.searchParams.get('state')!;
  }
  async function login() {
    const state = await begin();
    const result = await get(`/callback?code=PRIVATE_CODE&state=${state}`);
    assert.equal(result.status, 303); assert.equal(result.headers.get('location'), '/');
    return state;
  }
  return { probe, calls, get, post, begin, login, cookie, csrf };
}

test('loopback browser flow reaches authenticated user and session API without leaking secrets', async t => {
  const f = await fixture(t);
  await f.login();
  const response = await f.get('/');
  const html = await response.text();
  assert.match(html, /my-channel/u);
  assert.match(html, /&lt;script&gt;alert\(1\)&lt;\/script&gt;/u);
  assert.doesNotMatch(html, /<script>|PRIVATE_/u);
  assert.equal(response.headers.get('cache-control'), 'no-store');
  assert.match(response.headers.get('content-security-policy')!, /frame-ancestors 'none'/u);
  assert.match(response.headers.get('content-security-policy')!, /form-action 'self' https:\/\/chzzk.naver.com/u);
  assert.equal(response.headers.get('access-control-allow-origin'), null);
  assert.equal((await f.post('/session')).status, 303);
  const sessionPage = await (await f.get('/')).text();
  assert.match(sessionPage, /소켓 연결용 URL 발급 성공/u);
  assert.match(sessionPage, /아직 하지 않았습니다/u);
  assert.doesNotMatch(sessionPage, /PRIVATE_SESSION|nchat\.naver/u);
  assert.equal(f.calls.length, 3);
});

test('invalid state, cookie and duplicated callback parameters never exchange tokens', async t => {
  const f = await fixture(t);
  const state = await f.begin();
  for (const path of [`/callback?code=x&state=wrong`, `/callback?code=x&state=${state}&state=${state}`, `/callback?code=x&code=y&state=${state}`, `/callback?code=&state=${state}`, `/callback?code=x&state=${'한'.repeat(64)}`]) {
    assert.equal((await f.get(path)).status, 400);
  }
  assert.equal((await fetch(`${f.probe.origin}/callback?code=x&state=${state}`)).status, 403);
  assert.equal(f.calls.length, 0);
  assert.equal((await f.get(`/callback?code=x&state=${state}`)).status, 303);
  assert.equal((await f.get(`/callback?code=x&state=${state}`)).status, 400);
  assert.equal(f.calls.length, 2);
});

test('expired and replaced authorization attempts are rejected', async t => {
  let now = 0;
  const f = await fixture(t, standard, () => now);
  const oldState = await f.begin();
  const state = await f.begin();
  assert.notEqual(oldState, state);
  assert.equal((await f.get(`/callback?code=x&state=${oldState}`)).status, 400);
  now = 300_000;
  assert.equal((await f.get(`/callback?code=x&state=${state}`)).status, 400);
  assert.equal(f.calls.length, 0);
});

test('failed exchange consumes state and never echoes provider diagnostics', async t => {
  const f = await fixture(t, async () => new Response('PRIVATE_PROVIDER_BODY', { status: 401 }));
  const state = await f.begin();
  assert.equal((await f.get(`/callback?code=x&state=${state}`)).status, 303);
  assert.equal((await f.get(`/callback?code=x&state=${state}`)).status, 400);
  const page = await (await f.get('/')).text();
  assert.match(page, /CHZZK http \(401\)/u);
  assert.doesNotMatch(page, /PRIVATE_/u);
  assert.equal(f.calls.length, 1);
});

test('CSRF and DNS rebinding checks precede actions', async t => {
  const f = await fixture(t);
  // fetch normalizes Host; use the raw HTTP client to exercise rebinding.
  const reboundStatus = await new Promise<number | undefined>((resolve, reject) => {
    const request = httpRequest(f.probe.origin, { headers: { Host: 'attacker.invalid' } }, response => {
      response.resume(); response.on('end', () => resolve(response.statusCode));
    });
    request.on('error', reject); request.end();
  });
  assert.equal(reboundStatus, 403);
  assert.equal((await fetch(`${f.probe.origin}/`, { headers: { 'Sec-Fetch-Site': 'cross-site' } })).status, 403);
  assert.equal((await f.get('/', { 'Sec-Fetch-Site': 'cross-site' })).status, 200);
  assert.equal((await f.post('/connect', { Origin: 'https://attacker.invalid' })).status, 403);
  assert.equal((await fetch(`${f.probe.origin}/connect?csrf=${f.csrf}`, { method: 'POST', headers: { Cookie: f.cookie } })).status, 403);
  assert.equal((await fetch(`${f.probe.origin}/connect?csrf=bad`, { method: 'POST', headers: { Cookie: f.cookie, Origin: f.probe.origin } })).status, 403);
  assert.equal((await f.get('/connect')).status, 404);
  assert.equal(f.calls.length, 0);
});

test('token rotation is used for subsequent requests and revocation is explicit', async t => {
  const f = await fixture(t, async (url, init) => {
    if (url.endsWith('/token') && JSON.parse(String(init.body)).grantType === 'refresh_token') return ok({ ...tokenContent, accessToken: 'ROTATED_ACCESS', refreshToken: 'ROTATED_REFRESH' });
    return standard(url, init);
  });
  await f.login();
  assert.equal((await f.post('/refresh')).status, 303);
  assert.equal((await f.post('/session')).status, 303);
  assert.equal(new Headers(f.calls.at(-1)!.init.headers).get('Authorization'), 'Bearer ROTATED_ACCESS');
  assert.equal((await f.post('/revoke')).status, 303);
  assert.equal(JSON.parse(String(f.calls.at(-1)!.init.body)).token, 'ROTATED_ACCESS');
  assert.equal((await f.post('/session')).status, 400);
  assert.doesNotMatch(await (await f.get('/')).text(), /my-channel|ROTATED_|PRIVATE_/u);
});

test('ambiguous refresh failure discards local credentials rather than retrying a one-use token', async t => {
  const f = await fixture(t, async (url, init) => {
    if (url.endsWith('/token') && JSON.parse(String(init.body)).grantType === 'refresh_token') throw new Error('PRIVATE_SOCKET_ERROR');
    return standard(url, init);
  });
  await f.login();
  await f.post('/refresh');
  assert.equal((await f.post('/refresh')).status, 400);
  assert.equal((await f.post('/session')).status, 400);
  assert.match(await (await f.get('/')).text(), /다시 로그인/u);
  assert.equal(f.calls.length, 3);
});

test('concurrent token rotation is serialized, not doubled', async t => {
  let release: () => void = () => {};
  let reached: () => void = () => {};
  const started = new Promise<void>(resolve => { reached = resolve; });
  const f = await fixture(t, async (url, init) => {
    if (url.endsWith('/token') && JSON.parse(String(init.body)).grantType === 'refresh_token') {
      reached(); await new Promise<void>(resolve => { release = resolve; });
    }
    return standard(url, init);
  });
  await f.login();
  const refreshing = f.post('/refresh');
  await started;
  assert.equal((await f.post('/refresh')).status, 409);
  assert.equal((await f.post('/connect')).status, 409);
  release();
  assert.equal((await refreshing).status, 303);
  assert.equal(f.calls.length, 3);
});

test('closing drops local state but never silently revokes other devices', async t => {
  const f = await fixture(t);
  await f.login();
  await f.probe.close();
  assert.equal(f.calls.length, 2);
  assert.ok(!f.calls.some(call => call.url.endsWith('/revoke')));
});
