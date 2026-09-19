// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { inspect } from 'node:util';
import { test } from 'node:test';
import { authorizationUrl, ChzzkApi, ChzzkError } from '../server/chzzk/api.mts';
import type { Fetch } from '../server/chzzk/api.mts';

const credentials = { clientId: 'client-id', clientSecret: 'private-client-secret' };
const tokenResponse = { accessToken: 'private-access', refreshToken: 'private-refresh', tokenType: 'Bearer', expiresIn: '86400' };
const ok = (content: unknown) => Response.json({ code: 200, message: null, content });
const client = (fetcher: Fetch) => new ChzzkApi(credentials, fetcher);

test('authorization uses CHZZK camelCase parameters and the exact redirect URI', () => {
  const redirect = 'http://127.0.0.1:47831/callback';
  const url = new URL(authorizationUrl('client-id', redirect, 'opaque-state'));
  assert.equal(url.origin + url.pathname, 'https://chzzk.naver.com/account-interlock');
  assert.deepEqual(Object.fromEntries(url.searchParams), { clientId: 'client-id', redirectUri: redirect, state: 'opaque-state' });
  assert.doesNotMatch(url.href, /clientSecret|response_type|code_challenge|scope=/u);
});
for (const redirect of ['javascript:alert(1)', 'http://example.com/callback', 'https://u:p@example.com/callback', 'https://example.com/#token', 'https://example.com/?next=evil', 'not-a-url']) {
  test(`reject unsafe or ambiguous redirect ${redirect.split(':')[0]}`, () => {
    assert.throws(() => authorizationUrl('client', redirect, 'state'), ChzzkError);
  });
}

test('authorization-code exchange uses JSON, fixed HTTPS origin, cancellation and no redirects', async () => {
  let calls = 0;
  const result = await client(async (url, init) => {
    calls++;
    assert.equal(url, 'https://openapi.chzzk.naver.com/auth/v1/token');
    assert.equal(init.method, 'POST');
    assert.equal(init.redirect, 'error');
    assert.ok(init.signal instanceof AbortSignal);
    assert.deepEqual(JSON.parse(String(init.body)), { ...credentials, grantType: 'authorization_code', code: 'code', state: 'state' });
    assert.equal(new Headers(init.headers).get('Content-Type'), 'application/json');
    return ok(tokenResponse);
  }).exchangeCode('code', 'state');
  assert.deepEqual(result, { accessToken: 'private-access', refreshToken: 'private-refresh', expiresIn: 86400 });
  assert.equal(calls, 1);
});

test('refresh consumes the supplied refresh token, with no guessed OAuth fields', async () => {
  const result = await client(async (url, init) => {
    assert.equal(url, 'https://openapi.chzzk.naver.com/auth/v1/token');
    assert.deepEqual(JSON.parse(String(init.body)), { ...credentials, grantType: 'refresh_token', refreshToken: 'old-refresh' });
    return ok({ ...tokenResponse, refreshToken: 'rotated' });
  }).refresh('old-refresh');
  assert.equal(result.refreshToken, 'rotated');
});

test('revocation is explicit and uses the upstream token type hint', async () => {
  await client(async (url, init) => {
    assert.equal(url, 'https://openapi.chzzk.naver.com/auth/v1/token/revoke');
    assert.deepEqual(JSON.parse(String(init.body)), { ...credentials, token: 'access', tokenTypeHint: 'access_token' });
    return ok(null);
  }).revoke('access');
});

test('user and session requests use Bearer, not client credentials', async () => {
  const api = client(async (url, init) => {
    assert.equal(init.method, 'GET');
    assert.equal(init.body, undefined);
    assert.equal(new Headers(init.headers).get('Authorization'), 'Bearer access');
    assert.equal(new Headers(init.headers).get('Client-Secret'), null);
    return url.endsWith('/users/me') ? ok({ channelId: 'channel', channelName: '한국어 채널' }) :
      ok({ url: 'https://ssio08.nchat.naver.com:443?auth=private-session' });
  });
  assert.deepEqual(await api.getUser('access'), { channelId: 'channel', channelName: '한국어 채널' });
  assert.equal(await api.createUserSession('access'), 'https://ssio08.nchat.naver.com/?auth=private-session');
  assert.doesNotMatch(inspect(api), /private-client-secret/u);
});
for (const url of ['http://ssio08.nchat.naver.com/?auth=x', 'https://nchat.naver.com.evil.test/?auth=x', 'https://127.0.0.1/?auth=x', 'https://u:p@ssio08.nchat.naver.com/?auth=x', 'https://ssio08.nchat.naver.com:123/?auth=x', 'https://ssio08.nchat.naver.com/other?auth=x', 'https://ssio08.nchat.naver.com/', 'https://ssio08.nchat.naver.com/?auth=x#secret']) {
  test(`session URL is restricted: ${new URL(url).host}`, async () => {
    await assert.rejects(client(async () => ok({ url })).createUserSession('access'), ChzzkError);
  });
}
for (const status of [401, 403, 429, 500]) {
  test(`HTTP ${status} is safe and never automatically retried`, async () => {
    let calls = 0;
    const api = client(async () => { calls++; return new Response('private-upstream-secret', { status }); });
    await assert.rejects(api.refresh('private-refresh'), (error: unknown) => {
      assert.ok(error instanceof ChzzkError);
      assert.equal(error.status, status);
      assert.doesNotMatch(inspect(error), /private-/u);
      return true;
    });
    assert.equal(calls, 1);
  });
}
for (const content of [null, [], {}, { ...tokenResponse, expiresIn: 'NaN' }, { ...tokenResponse, expiresIn: '-1' }, { ...tokenResponse, expiresIn: '0' }, { ...tokenResponse, tokenType: 'Basic' }, { ...tokenResponse, refreshToken: '' }, { ...tokenResponse, accessToken: 'has\r\ninjection' }]) {
  test('invalid token fields are rejected without body disclosure', async () => {
    await assert.rejects(client(async () => ok(content)).exchangeCode('code', 'state'), ChzzkError);
  });
}
for (const envelope of ['not-json private-secret', JSON.stringify({ code: 200 }), JSON.stringify({ code: 401, message: 'private-secret', content: tokenResponse }), 'x'.repeat(65537)]) {
  test('malformed, incomplete or oversized response fails closed', async () => {
    await assert.rejects(client(async () => new Response(envelope)).exchangeCode('code', 'state'), (error: unknown) => {
      assert.ok(error instanceof ChzzkError); assert.doesNotMatch(inspect(error), /private-secret/u); return true;
    });
  });
}

test('network errors are scrubbed and one-use token refresh is not replayed', async () => {
  let calls = 0;
  await assert.rejects(client(async () => { calls++; throw new Error('https://upstream/?auth=private-secret'); }).refresh('refresh'), (error: unknown) => {
    assert.ok(error instanceof ChzzkError); assert.equal(error.kind, 'transport');
    assert.doesNotMatch(inspect(error), /private-secret|upstream/u); return true;
  });
  assert.equal(calls, 1);
});

test('caller cancellation reaches the fetch operation without disclosing its reason', async () => {
  const controller = new AbortController();
  const api = client(async (_url, init) => {
    return await new Promise((_resolve, reject) => {
      init.signal!.addEventListener('abort', () => reject(init.signal!.reason), { once: true });
      controller.abort('private-reason');
    });
  });
  await assert.rejects(api.getUser('access', controller.signal), (error: unknown) => {
    assert.ok(error instanceof ChzzkError); assert.doesNotMatch(inspect(error), /private-reason/u); return true;
  });
});

test('bad credentials and header injection fail before any HTTP request', async () => {
  assert.throws(() => new ChzzkApi({ clientId: '', clientSecret: 'x' }), ChzzkError);
  let calls = 0;
  await assert.rejects(client(async () => { calls++; return ok({}); }).getUser('x\r\nAuthorization:y'), ChzzkError);
  assert.equal(calls, 0);
});
