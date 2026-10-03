// SPDX-License-Identifier: GPL-2.0-or-later
// HTTP boundary negatives complement the real Chromium flow. Headers here are
// deliberately supplied by the fixture, not evidence of browser behavior.
import test from 'node:test';
import assert from 'node:assert/strict';
import { fixture } from './fixtures/service.mts';

type Fixture = Awaited<ReturnType<typeof fixture>>;
const navigation = { 'Sec-Fetch-Site': 'cross-site', 'Sec-Fetch-Mode': 'navigate', 'Sec-Fetch-Dest': 'document' };
const cookie = (response: Response) => response.headers.get('set-cookie')!.split(';')[0]!;
const csrf = (html: string) => /name="csrf" value="([a-f0-9]{64})"/u.exec(html)![1]!;
async function callback(f: Fixture, native = false) {
  const pending = native ? await f.start('gaming') : undefined;
  const path = pending?.verificationPath ?? '/account';
  const entry = await f.request(path);
  const old = cookie(entry);
  const html = entry.status === 303 ? await (await f.request(path, { headers: { Cookie: old } })).text() : await entry.text();
  const redirect = await f.post(pending ? `/login/${pending.id}/connect` : '/account/login', { cookie: old, csrf: csrf(html) });
  assert.equal(redirect.status, 303);
  const state = new URL(redirect.headers.get('location')!).searchParams.get('state');
  const response = await f.request(`/callback?state=${state}&code=alice`, { headers: { ...navigation, Cookie: old } });
  assert.equal(response.status, 303);
  assert.equal(response.headers.get('referrer-policy'), 'no-referrer', 'callback does not forward its query');
  return { old, current: cookie(response), path: response.headers.get('location')!, pending };
}

test('only the rotated browser gets one exact top-level callback landing, never general management access', async () => {
  const f = await fixture();
  try {
    const b = await callback(f);
    const read = (path: string, headers: Record<string, string>) => f.request(path, { headers });
    assert.equal((await read(b.path, navigation)).status, 403);
    assert.equal((await read(b.path, { ...navigation, Cookie: b.old })).status, 403);
    assert.equal((await read('/campaigns', { ...navigation, Cookie: b.current })).status, 403);
    const invalid: Record<string, string>[] = [ { 'Sec-Fetch-Mode': 'cors' }, { 'Sec-Fetch-Dest': 'iframe' },
      { 'Sec-Fetch-Dest': 'empty' }, { 'Sec-Fetch-Mode': '' }, { Origin: 'https://foreign.invalid' } ];
    for (const extra of invalid) {
      assert.equal((await read(b.path, { ...navigation, Cookie: b.current, ...extra })).status, 403);
    }
    assert.equal((await read(b.path + '?returnTo=/campaigns', { ...navigation, Cookie: b.current })).status, 403);
    const accepted = await read(b.path, { ...navigation, Cookie: b.current });
    assert.equal(accepted.status, 200);
    assert.equal(accepted.headers.get('cache-control'), 'no-store');
    assert.match(accepted.headers.get('content-security-policy')!, /frame-ancestors 'none'/u);
    assert.equal(accepted.headers.get('referrer-policy'), 'same-origin', 'forms can supply their actual Origin');
    const proof = csrf(await accepted.text());
    assert.equal((await read(b.path, { ...navigation, Cookie: b.current })).status, 403, 'one use');
    assert.equal((await read(b.path, { Cookie: b.current })).status, 200, 'ordinary same-origin refresh works');
    const crossPost = await f.request('/account/confirm', { method: 'POST', headers: {
      ...navigation, Cookie: b.current, Origin: f.origin, 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: new URLSearchParams({ csrf: proof }) });
    assert.equal(crossPost.status, 403, 'landing cannot authorize cross-site mutation');
    assert.equal((await f.post('/campaigns/stop', { cookie: b.current, csrf: proof })).status, 401);
    assert.equal((await f.post('/account/confirm', { cookie: b.current, csrf: proof })).status, 303);
    assert.deepEqual(f.store.connections('alice'), []);
    assert.equal(f.grants.load('alice'), undefined);
  } finally { await f.close(); }
});

test('native callback lands only on its initiating request and still needs explicit role consent', async () => {
  const f = await fixture();
  try {
    const b = await callback(f, true);
    assert(b.pending);
    assert.equal((await f.request('/account/confirm', { headers: { ...navigation, Cookie: b.current } })).status, 403);
    assert.equal((await f.request('/login/' + '0'.repeat(32), { headers: { ...navigation, Cookie: b.current } })).status, 403);
    const response = await f.request(b.path, { headers: { ...navigation, Cookie: b.current } });
    assert.equal(response.status, 200);
    const proof = csrf(await response.text());
    assert.equal(f.app.login.view(b.pending.id).role, 'gaming');
    assert.deepEqual(f.store.connections('alice'), []);
    assert.equal((await f.request(b.path, { headers: { ...navigation, Cookie: b.current } })).status, 403);
    assert.equal((await f.post(`/login/${b.pending.id}/approve`, { cookie: b.current, csrf: proof })).status, 200);
    const result = await f.native(`/display/login/${b.pending.id}`, 'ChatView-Login', b.pending.verifier);
    assert.equal(result.status, 200);
    assert.equal(f.store.connections('alice').length, 1);
  } finally { await f.close(); }
});

test('expired, consumed or cancelled return allowance cannot reopen a cross-site confirmation', async t => {
  const realNow = Date.now(); let now = realNow;
  t.mock.method(Date, 'now', () => now);
  const f = await fixture({ now: () => realNow });
  try {
    let b = await callback(f);
    now += 30_000;
    assert.equal((await f.request(b.path, { headers: { ...navigation, Cookie: b.current } })).status, 403);
    const ordinary = await f.request(b.path, { headers: { Cookie: b.current } });
    assert.equal(ordinary.status, 200, 'short landing lifetime does not silently approve or erase valid pending consent');
    const cancelled = await f.post('/account/cancel', { cookie: b.current, csrf: csrf(await ordinary.text()) });
    const freshCookie = cookie(cancelled);
    for (const value of [b.current, freshCookie])
      assert.equal((await f.request(b.path, { headers: { ...navigation, Cookie: value } })).status, 403);
    b = await callback(f);
    assert.equal((await f.request(b.path, { headers: { Cookie: b.current } })).status, 200);
    assert.equal((await f.request(b.path, { headers: { ...navigation, Cookie: b.current } })).status, 403,
      'a normal first landing also consumes the cross-site allowance');
    assert.deepEqual(f.store.connections('alice'), []);
  } finally { await f.close(); }
});


test('HTML forms keep exact Origin/CSRF checks instead of accepting opaque origins', async () => {
  const f = await fixture();
  try {
    const response = await f.request('/account');
    assert.equal(response.headers.get('referrer-policy'), 'same-origin');
    const browser = { cookie: cookie(response), csrf: csrf(await response.text()) };
    for (const origin of ['null', 'https://foreign.invalid', undefined]) {
      assert.equal((await f.request('/account/login', { method: 'POST', headers: {
        Cookie: browser.cookie, 'Content-Type': 'application/x-www-form-urlencoded',
        ...(origin !== undefined ? { Origin: origin } : {}),
      }, body: new URLSearchParams({ csrf: browser.csrf }) })).status, 403);
    }
    assert.equal((await f.post('/account/login', browser)).status, 303);
  } finally { await f.close(); }
});
