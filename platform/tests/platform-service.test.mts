// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic provider + real HTTP routes/SQLite. Not a CHZZK, WebSocket wire,
// TLS certificate, native HUD, or dual-PC hardware acceptance test.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { randomBytes } from 'node:crypto';
import { Creators } from '../server/service/creators.mts';
import { PlatformApplication } from '../server/service/application.mts';
import { SessionStore, hashSecret } from '../server/chat/session-store.mts';
import { DisplayAccessError } from '../server/chat/display-access.mts';
import type { Tokens } from '../server/chzzk/api.mts';
import type { ChatSnapshot } from '../server/chzzk/session.mts';
const nonce = () => randomBytes(32).toString('hex');
const denied = (code: number) => (e: unknown) => e instanceof DisplayAccessError && e.status === code;
const tokens = (owner: string, generation = 0): Tokens => ({
  accessToken: `access:${owner}:${generation}`, refreshToken: `refresh:${owner}:${generation}`, expiresIn: 3600,
});
const ownerOf = (token: string) => token.split(':')[1]!;
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(yes => { resolve = yes; });
  return { promise, resolve };
}

async function fixture() {
  const store = new SessionStore();
  const chats = new Map<string, { publish: (message: string) => void }>();
  const gateways: { snapshot: () => unknown; changed: number }[] = [];
  const active = new Map<string, number>();
  let refreshCalls = 0, refreshHook: ((token: string) => Promise<Tokens>) | undefined;
  let exchangeHook: ((code: string) => Promise<Tokens>) | undefined;
  let revokeFails = false;
  const api = {
    exchangeCode: async (code: string) => exchangeHook ? exchangeHook(code) : tokens(code),
    getUser: async (access: string) => ({ channelId: ownerOf(access), channelName: `channel <${ownerOf(access)}>` }),
    refresh: async (token: string) => { refreshCalls++; return refreshHook ? refreshHook(token) : tokens(ownerOf(token), refreshCalls); },
    revoke: async () => { if (revokeFails) throw new Error('private upstream error body'); },
  };
  const creators = new Creators({ api, sessions: store,
    createChat(changed) {
      let owner = '', snapshot: ChatSnapshot = { state: 'idle', received: 0, messages: [] }, running = false;
      return {
        snapshot: () => snapshot,
        async start(_token: string, channel: string) {
          owner = channel; running = true;
          active.set(owner, (active.get(owner) ?? 0) + 1);
          assert.equal(active.get(owner), 1, 'only one active upstream per creator');
          snapshot = { state: 'subscribed', received: 0, messages: [] }; changed();
          chats.set(owner, { publish(message) {
            // Text snapshots are intentionally synthetic, not provider evidence.
            snapshot = { state: 'subscribed', received: snapshot.received + 1, messages: [] };
            Object.defineProperty(snapshot, 'fixtureText', { value: message, enumerable: true }); changed();
          } });
        },
        async stop() {
          if (running) { running = false; active.set(owner, active.get(owner)! - 1); }
          snapshot = { state: 'stopped', received: 0, messages: [] }; changed(); return true;
        },
      };
    },
    createDisplay(_access, snapshot) {
      const capture = { snapshot, changed: 0 }; gateways.push(capture);
      return { changed() { capture.changed++; }, close() {},
        upgrade() { throw new Error('Synthetic fixture does not claim a WebSocket handshake'); } };
    },
  });
  let app: PlatformApplication;
  const server = createServer((request, response) => { void app.handle(request, response); });
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Missing HTTP fixture port');
  const origin = `http://127.0.0.1:${address.port}`;
  app = new PlatformApplication(creators, origin, state => `https://chzzk.naver.com/account-interlock?state=${state}`);
  const request = (path: string, init: RequestInit = {}) => fetch(origin + path, { ...init, redirect: 'manual' });
  const native = (path: string, scheme: string, token: string) => request(path, {
    method: 'POST', headers: { Authorization: `${scheme} ${token}` },
  });
  const browser: { cookie: string; csrf: string } = { cookie: '', csrf: '' };
  const cookie = (response: Response) => response.headers.get('set-cookie')?.split(';')[0] ?? '';
  async function page(path: string, b = browser) {
    let response = await request(path, { headers: { Cookie: b.cookie } });
    if (response.status === 303) {
      b.cookie = cookie(response);
      response = await request(path, { headers: { Cookie: b.cookie } });
    }
    assert.equal(response.status, 200);
    const html = await response.text(); b.csrf = /name="csrf" value="([a-f0-9]{64})"/u.exec(html)?.[1] ?? '';
    return html;
  }
  const post = (path: string, b = browser) => request(path, { method: 'POST',
    headers: { Cookie: b.cookie, Origin: origin, 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ csrf: b.csrf }),
  });
  async function start() {
    const verifier = nonce(), response = await native('/display/login', 'ChatView-Challenge', hashSecret(verifier));
    assert.equal(response.status, 200);
    const data = await response.json() as { id: string; verificationPath: string };
    return { ...data, verifier };
  }
  async function authenticate(owner: string, pending: Awaited<ReturnType<typeof start>>, b = browser) {
    await page(pending.verificationPath, b);
    const redirect = await post(`/login/${pending.id}/connect`, b);
    assert.equal(redirect.status, 303);
    const state = new URL(redirect.headers.get('location')!).searchParams.get('state')!;
    const old = b.cookie;
    const response = await request(`/callback?code=${owner}&state=${state}`, { headers: { Cookie: old } });
    assert.equal(response.status, 303); b.cookie = cookie(response); assert.notEqual(b.cookie, old);
    await page(pending.verificationPath, b);
  }
  async function approve(pending: Awaited<ReturnType<typeof start>>, role: 'gaming' | 'streaming', b = browser) {
    await page(pending.verificationPath, b);
    const response = await post(`/login/${pending.id}/approve/${role}`, b);
    assert.equal(response.status, 200);
    const polled = await native(`/display/login/${pending.id}`, 'ChatView-Login', pending.verifier);
    assert.equal(polled.status, 200);
    return (await polled.json() as { lease: { id: string; token: string; sessionToken: string;
      membership: { role: string; connectionId: string; broadcastSessionId: string } } }).lease;
  }
  async function connect(owner: string, role: 'gaming' | 'streaming') {
    const b = { cookie: '', csrf: '' }, pending = await start();
    await authenticate(owner, pending, b);
    return { b, lease: await approve(pending, role, b) };
  }
  return { store, creators, app, origin, request, native, page, post, start, authenticate, approve, connect, browser,
    chats, gateways, active, get refreshCalls() { return refreshCalls; },
    setRefresh(hook: typeof refreshHook) { refreshHook = hook; },
    setExchange(hook: typeof exchangeHook) { exchangeHook = hook; },
    failRevoke() { revokeFails = true; },
    async close() {
      app.close(); server.closeAllConnections();
      const stopped = new Promise<void>(resolve => server.close(() => resolve()));
      await creators.close(); await stopped; store.close();
    },
  };
}

test('HTTP login of two creators/two PCs routes only each owner chat and role', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const second = await f.start();
    const stream = await f.approve(second, 'streaming', alice.b);
    const bob = await f.connect('bob', 'streaming');
    assert.equal(alice.lease.membership.broadcastSessionId, stream.membership.broadcastSessionId);
    assert.notEqual(bob.lease.membership.broadcastSessionId, stream.membership.broadcastSessionId);
    assert.equal(f.creators.gateway(alice.lease.token), f.creators.gateway(stream.token));
    assert.notEqual(f.creators.gateway(stream.token), f.creators.gateway(bob.lease.token));
    f.chats.get('alice')!.publish('alice private text'); f.chats.get('bob')!.publish('bob private text');
    assert.match(JSON.stringify(f.gateways[0]!.snapshot()), /alice private text/u);
    assert.doesNotMatch(JSON.stringify(f.gateways[0]!.snapshot()), /bob private text/u);
    assert.doesNotMatch(JSON.stringify(f.gateways[1]!.snapshot()), /alice private text/u);
    const account = await f.page('/account', alice.b);
    assert.match(account, /channel &lt;alice&gt;/u); assert.doesNotMatch(account, /bob|access:|refresh:/u);
    const own = await f.native('/broadcast/session', 'ChatView-Session', alice.lease.sessionToken);
    assert.deepEqual(await own.json(), { membership: alice.lease.membership, captureState: 'unverified' });
    assert.equal((await f.native('/broadcast/session', 'ChatView-Session', alice.lease.token)).status, 403);
    const duplicate = await f.start(); await f.page(duplicate.verificationPath, alice.b);
    assert.equal((await f.post(`/login/${duplicate.id}/approve/streaming`, alice.b)).status, 409);
    assert.equal(f.store.connections('alice').length, 2);
  } finally { await f.close(); }
});

test('native renewal/logout and browser per-connection revocation are creator scoped', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'gaming');
    await f.page('/account', bob.b);
    assert.equal((await f.post(`/connections/${alice.lease.membership.connectionId}/revoke`, bob.b)).status, 303);
    assert.ok(f.creators.gateway(alice.lease.token));
    const response = await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken);
    assert.equal(response.status, 200);
    const renewed = await response.json() as { token: string; membership: unknown };
    assert.deepEqual(renewed.membership, alice.lease.membership);
    assert.throws(() => f.creators.gateway(alice.lease.token), denied(401));
    assert.ok(f.creators.gateway(renewed.token));
    assert.equal((await f.native('/display/signout', 'ChatView-Session', alice.lease.sessionToken)).status, 200);
    assert.throws(() => f.creators.gateway(renewed.token), denied(401));
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 401);
    assert.ok(f.creators.gateway(bob.lease.token));
    await f.page('/account', bob.b);
    await f.post(`/connections/${bob.lease.membership.connectionId}/revoke`, bob.b);
    assert.throws(() => f.creators.gateway(bob.lease.token), denied(401));
  } finally { await f.close(); }
});

test('provider revoke failure still removes local roles and pending approvals, not another creator', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming'), bob = await f.connect('bob', 'gaming');
    const pending = await f.start(); await f.page(pending.verificationPath, alice.b);
    assert.equal((await f.post(`/login/${pending.id}/approve/streaming`, alice.b)).status, 200);
    f.failRevoke(); await f.page('/account', alice.b);
    const response = await f.post('/account/revoke', alice.b), html = await response.text();
    assert.equal(response.status, 200); assert.match(html, /확인하지 못했습니다/u);
    assert.doesNotMatch(html, /private upstream/u);
    assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    assert.equal((await f.native(`/display/login/${pending.id}`, 'ChatView-Login', pending.verifier)).status, 401);
    assert.throws(() => f.creators.gateway(alice.lease.token), denied(401));
    assert.ok(f.creators.gateway(bob.lease.token));
  } finally { await f.close(); }
});

test('browser OAuth state cannot cross cookies or replay; CSRF and native origin/body guards remain', async () => {
  const f = await fixture();
  try {
    const p = await f.start(), other = { cookie: '', csrf: '' };
    await f.page(p.verificationPath); await f.page(p.verificationPath, other);
    const redirect = await f.post(`/login/${p.id}/connect`);
    const state = new URL(redirect.headers.get('location')!).searchParams.get('state')!;
    const callback = `/callback?code=alice&state=${state}`;
    assert.equal((await f.request(callback, { headers: { Cookie: other.cookie } })).status, 400);
    const oldCookie = f.browser.cookie;
    const accepted = await f.request(callback, { headers: { Cookie: oldCookie } });
    assert.equal(accepted.status, 303);
    assert.match(accepted.headers.get('set-cookie')!, /Secure; HttpOnly; SameSite=Lax; Path=\//u);
    assert.equal((await f.request(callback, { headers: { Cookie: oldCookie } })).status, 400);
    assert.equal((await f.post(`/login/${p.id}/deny`, other)).status, 200);
    assert.equal((await f.request('/display/login', { method: 'POST', headers: {
      Authorization: `ChatView-Challenge ${nonce()}`, Origin: 'https://untrusted.invalid',
    } })).status, 403);
    assert.equal((await f.request('/display/login', { method: 'POST', headers: {
      Authorization: `ChatView-Challenge ${nonce()}` }, body: 'not-empty' })).status, 400);
    assert.equal((await f.native('/display/exchange', 'ChatView-Ticket', nonce())).status, 404);
    const csrfDenied = await f.request('/logout', { method: 'POST', headers: {
      Cookie: other.cookie, Origin: f.origin, 'Content-Type': 'application/x-www-form-urlencoded',
    }, body: `csrf=${nonce()}` });
    assert.equal(csrfDenied.status, 403);
  } finally { await f.close(); }
});

test('one-use provider refresh is single-flight and failure is not replayed', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const next = deferred<Tokens>(); f.setRefresh(() => next.promise);
    const a = f.creators.refresh('alice'), b = f.creators.refresh('alice');
    assert.equal(f.refreshCalls, 1);
    next.resolve(tokens('alice', 1)); await Promise.all([a, b]);
    assert.equal(f.active.get('alice'), 1);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 200);
    f.setRefresh(async () => { throw new Error('ambiguous transport failure'); });
    await assert.rejects(f.creators.refresh('alice'));
    await assert.rejects(f.creators.refresh('alice'), denied(503));
    assert.equal(f.refreshCalls, 2);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', alice.lease.sessionToken)).status, 503);
  } finally { await f.close(); }
});

test('revocation defeats late refresh and late OAuth completion', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const pendingRefresh = deferred<Tokens>(); f.setRefresh(() => pendingRefresh.promise);
    const refresh = f.creators.refresh('alice'); void refresh.catch(() => {});
    const pendingLogin = deferred<Tokens>(); f.setExchange(() => pendingLogin.promise);
    const login = f.creators.authorize('alice', nonce()); void login.catch(() => {});
    await f.creators.revoke('alice');
    pendingRefresh.resolve(tokens('alice', 2)); pendingLogin.resolve(tokens('alice', 3));
    await assert.rejects(refresh, denied(503)); await assert.rejects(login, denied(403));
    assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    assert.equal(f.creators.describe('alice').authorized, false);
    assert.equal(f.active.get('alice'), 0);
  } finally { await f.close(); }
});

test('refresh identity mismatch cannot replace a creator or steal the streaming role', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'streaming'), bob = await f.connect('bob', 'streaming');
    f.setRefresh(async () => tokens('bob', 1));
    await assert.rejects(f.creators.refresh('alice'), denied(403));
    assert.equal(f.store.find(alice.lease.sessionToken), undefined);
    assert.ok(f.store.find(bob.lease.sessionToken)); assert.ok(f.creators.gateway(bob.lease.token));
  } finally { await f.close(); }
});

test('HTTPS origin refuses plaintext even with spoofed proxy headers', async () => {
  const f = await fixture();
  const app = new PlatformApplication(f.creators, 'https://service.example', () => 'https://chzzk.naver.com');
  const server = createServer((req, res) => { void app.handle(req, res); });
  try {
    await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
    const address = server.address(); if (!address || typeof address === 'string') throw new Error('No port');
    const response = await fetch(`http://127.0.0.1:${address.port}/healthz`, {
      headers: { Host: 'service.example', 'X-Forwarded-Proto': 'https' },
    });
    assert.equal(response.status, 403);
    assert.equal(response.headers.get('cache-control'), 'no-store');
  } finally {
    app.close(); server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); await f.close();
  }
});

test('independent PC browsers join the same creator without revoking the first HUD', async () => {
  const f = await fixture();
  try {
    const gaming = await f.connect('alice', 'gaming');
    const streaming = await f.connect('alice', 'streaming');
    assert.notEqual(gaming.b.cookie, streaming.b.cookie);
    assert.equal(gaming.lease.membership.broadcastSessionId, streaming.lease.membership.broadcastSessionId);
    assert.ok(f.creators.gateway(gaming.lease.token));
    assert.equal(f.creators.gateway(gaming.lease.token), f.creators.gateway(streaming.lease.token));
    assert.equal(f.active.get('alice'), 1);
    await f.page('/account', gaming.b);
    await f.post('/logout', gaming.b);
    assert.ok(f.creators.gateway(gaming.lease.token)); // Browser logout is not device/provider revoke.
    assert.ok(f.creators.gateway(streaming.lease.token));
  } finally { await f.close(); }
});
