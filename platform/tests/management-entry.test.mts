// SPDX-License-Identifier: GPL-2.0-or-later
// The native entry opens these fixed routes without credentials. Actual HTTP
// and SQLite, synthetic provider; this does not launch a system browser.
import test from 'node:test';
import assert from 'node:assert/strict';
import { fixture } from './fixtures/service.mts';

const routes = ['/account', '/campaigns', '/campaigns/activity'];

test('management entry reuses browser identity and never exchanges a display approval', async () => {
  const f = await fixture();
  try {
    const font = await f.request('/assets/PretendardVariable.woff2');
    assert.equal(font.status, 200);
    assert.equal(font.headers.get('content-type'), 'font/woff2');
    assert.equal(font.headers.get('set-cookie'), null, 'font reads do not create browser authority');
    assert.equal(Buffer.from(await font.arrayBuffer()).subarray(0, 4).toString('ascii'), 'wOF2');
    assert.equal((await f.request('/assets/PretendardVariable.woff2?path=other')).status, 400);
    assert.equal((await f.request('/assets/PretendardVariable.woff2', { method: 'POST' })).status, 405);
    assert.equal((await f.request('/assets/PretendardVariable.woff2', {
      headers: { Origin: 'https://attacker.invalid' },
    })).status, 403);
    const alice = await f.connect('alice', 'streaming');
    const before = f.store.connections('alice');
    const exchanges = f.exchangeCalls;
    for (const path of routes) {
      const response = await f.request(path, { headers: { Cookie: alice.b.cookie } });
      assert.equal(response.status, 200);
      assert.match(response.headers.get('content-type') ?? '', /text\/html/u);
      assert.match(response.headers.get('content-security-policy') ?? '', /font-src 'self'/u);
      const html = await response.text();
      assert(!html.includes(alice.lease.sessionToken));
      assert(!html.includes(alice.lease.token));
      assert.doesNotMatch(html, /access:alice|refresh:alice/u);
      if (path === '/account') {
        assert.match(html, /channel &lt;alice&gt;/u);
        assert.match(html, /href="\/campaigns"/u);
        assert.match(html, /href="\/campaigns\/activity"/u);
      }
    }
    assert.deepEqual(f.store.connections('alice'), before);
    assert.equal(f.exchangeCalls, exchanges);
    assert.ok(f.creators.gateway(alice.lease.token));
  } finally { await f.close(); }
});

test('opening a service address or sending native credentials cannot log a browser into management', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const variants: Record<string, string>[] = [{}, { Authorization: `ChatView-Session ${alice.lease.sessionToken}` },
      { Authorization: `Bearer ${alice.lease.token}` }];
    for (const headers of variants) {
      const account = await f.request('/account', { headers });
      assert.equal(account.status, 200);
      const html = await account.text();
      assert.doesNotMatch(html, /channel &lt;alice&gt;|\/connections\//u);
      assert(!html.includes(alice.lease.membership.connectionId));
      for (const path of routes.slice(1))
        assert.equal((await f.request(path, { headers })).status, 401);
    }
    assert.ok(f.store.find(alice.lease.sessionToken));
  } finally { await f.close(); }
});

test('another browser account stays distinct and browser logout does not revoke native chat', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'gaming');
    const response = await f.request('/account', { headers: {
      Cookie: bob.b.cookie, Authorization: `ChatView-Session ${alice.lease.sessionToken}`,
    } });
    assert.equal(response.status, 200);
    const html = await response.text();
    assert.match(html, /channel &lt;bob&gt;/u);
    assert.doesNotMatch(html, /channel &lt;alice&gt;/u);
    assert(!html.includes(alice.lease.membership.connectionId));
    await f.page('/account', alice.b);
    assert.equal((await f.post('/logout', alice.b)).status, 303);
    for (const path of routes.slice(1))
      assert.equal((await f.request(path, { headers: { Cookie: alice.b.cookie } })).status, 401);
    assert.ok(f.creators.gateway(alice.lease.token));
    assert.ok(f.creators.gateway(bob.lease.token));
  } finally { await f.close(); }
});
