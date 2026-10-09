// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic provider + real HTTP routes/SQLite. Not live CHZZK or native OBS.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { PlatformApplication } from '../server/service/application.mts';
import type { Tokens } from '../server/chzzk/api.mts';
import { fixture, tokens, nonce, denied, deferred } from './fixtures/service.mts';

test('HTTP login of two creators/two PCs routes only each owner chat and role', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const second = await f.start('streaming');
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
    const duplicate = await f.start('streaming'); await f.page(duplicate.verificationPath, alice.b);
    assert.equal((await f.post(`/login/${duplicate.id}/approve`, alice.b)).status, 409);
    assert.equal(f.store.connections('alice').length, 2);
  } finally { await f.close(); }
});

test('one creator sees approved roles separately from live gaming/OBS chat peers and returns without a new role',
  async () => {
  // Same actual HTTP/SQLite/WebSocket implementation used by the two role
  // runtimes; provider and physical machines are deliberately synthetic.
  const f = await fixture({ createDisplay: (access, origin, snapshot) =>
    new DisplayGateway(access, origin, snapshot) });
  const sockets: WebSocket[] = [];
  const open = async (token: string) => {
    const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
      { headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: 3000 });
    sockets.push(peer); peer.on('error', () => {});
    await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
    return peer;
  };
  const row = (html: string, role: 'gaming' | 'streaming') => {
    const value = new RegExp(`<tr data-pc-role="${role}">([\\s\\S]*?)<\\/tr>`, 'u').exec(html)?.[1];
    assert(value, `owner account has an explicit ${role} row`); return value;
  };
  const expectRole = (html: string, role: 'gaming' | 'streaming', approved: number, online: number) => {
    const content = row(html, role);
    assert(content.includes(`data-approved-count="${approved}"`), `${role} approved independently`);
    assert(content.includes(`data-online-count="${online}"`), `${role} actual sockets counted independently`);
  };
  try {
    const gaming = await f.connect('alice', 'gaming'), streaming = await f.connect('alice', 'streaming');
    const bob = await f.connect('bob', 'streaming');
    assert.equal(gaming.lease.membership.broadcastSessionId, streaming.lease.membership.broadcastSessionId);
    let html = await f.page('/account', gaming.b);
    expectRole(html, 'gaming', 1, 0); expectRole(html, 'streaming', 1, 0);
    assert.match(html, /현재 승인으로 자체 채팅 복귀/u);
    assert.match(html, /게임 PC에서는 OBS 없이/u);
    assert.match(html, /data-output-report="unknown"/u);
    assert.doesNotMatch(html, /channel &lt;bob&gt;|access:bob|refresh:bob/u);
    const player = await open(gaming.lease.token);
    html = await f.page('/account', streaming.b);
    expectRole(html, 'gaming', 1, 1); expectRole(html, 'streaming', 1, 0);
    const broadcaster = await open(streaming.lease.token);
    html = await f.page('/account', gaming.b);
    expectRole(html, 'gaming', 1, 1); expectRole(html, 'streaming', 1, 1);
    assert.equal((await f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: `ChatView-Output ${streaming.lease.outputToken}`,
      'Content-Type': 'application/json',
    }, body: JSON.stringify({sequence:1,streaming:false,recording:false,sampleAgeMs:0}) })).status, 200);
    html = await f.page('/account', gaming.b);
    assert.match(html, /data-output-report="reported"/u);
    assert.match(html, /출력 비활성 보고 수신/u);
    assert.match(html, /OBS 합성\/송출 영상이나 개인 HUD 제외의 검증이 아닙니다/u);
    const bobHtml = await f.page('/account', bob.b);
    expectRole(bobHtml, 'gaming', 0, 0); expectRole(bobHtml, 'streaming', 1, 0);
    assert.match(bobHtml, /data-output-report="unknown"/u);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session',
      gaming.lease.sessionToken, 'streaming')).status, 409, 'wrong role cannot replace the open gaming lease');
    const oldClosed = once(player, 'close', { signal: AbortSignal.timeout(3000) });
    const resumed = await f.native('/display/refresh', 'ChatView-Session',
      gaming.lease.sessionToken, 'gaming');
    assert.equal(resumed.status, 200);
    const renewed = await resumed.json() as { token: string; membership: typeof gaming.lease.membership };
    assert.deepEqual(renewed.membership, gaming.lease.membership, 'same membership and role on explicit refresh');
    await oldClosed;
    html = await f.page('/account', gaming.b);
    expectRole(html, 'gaming', 1, 0); expectRole(html, 'streaming', 1, 1);
    const returning = await open(renewed.token);
    html = await f.page('/account', streaming.b);
    expectRole(html, 'gaming', 1, 1); expectRole(html, 'streaming', 1, 1);
    assert.equal(f.refreshCalls, 0, 'same approved role does not require provider refresh');
    await f.page('/account', gaming.b);
    assert.equal((await f.post('/logout', gaming.b)).status, 303);
    html = await f.page('/account', streaming.b);
    expectRole(html, 'gaming', 1, 1); expectRole(html, 'streaming', 1, 1);
    const broadcasterClosed = once(broadcaster, 'close', { signal: AbortSignal.timeout(3000) });
    assert.equal((await f.post(`/connections/${streaming.lease.membership.connectionId}/revoke`,
      streaming.b)).status, 303);
    await broadcasterClosed;
    html = await f.page('/account', streaming.b);
    expectRole(html, 'gaming', 1, 1); expectRole(html, 'streaming', 0, 0);
    assert.match(html, /data-output-report="unknown"/u);
    assert.equal(returning.readyState, WebSocket.OPEN, 'other approved role remains usable');
    expectRole(await f.page('/account', bob.b), 'streaming', 1, 0);
  } finally { for (const peer of sockets) peer.terminate(); await f.close(); }
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
    const pending = await f.start('streaming'); await f.page(pending.verificationPath, alice.b);
    assert.equal((await f.post(`/login/${pending.id}/approve`, alice.b)).status, 200);
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
    assert.ok(f.creators.gateway(gaming.lease.token));
    assert.ok(f.creators.gateway(streaming.lease.token));
  } finally { await f.close(); }
});

test('launch role is fixed before consent and old role-selector routes are removed', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    const pending = await f.start('streaming');
    const html = await f.page(pending.verificationPath, alice.b);
    assert.match(html, /앱이 요청한 역할: <strong>송출 PC · OBS 관리 런타임/u);
    assert.match(html, new RegExp(`/login/${pending.id}/approve"`, 'u'));
    assert.doesNotMatch(html, /\/approve\/(gaming|streaming)/u);
    for (const role of ['gaming', 'streaming'])
      assert.equal((await f.post(`/login/${pending.id}/approve/${role}`, alice.b)).status, 404);
    assert.equal(f.app.login.view(pending.id).role, 'streaming');
    const lease = await f.approve(pending, 'streaming', alice.b);
    assert.equal(lease.membership.role, 'streaming');
    const before = f.store.find(lease.sessionToken);
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', lease.sessionToken, 'gaming')).status, 409);
    assert.deepEqual(f.store.find(lease.sessionToken)?.membership, before?.membership);
    assert.ok(f.creators.gateway(lease.token), 'wrong-mode resume must not evict the original lease');
    assert.equal((await f.native('/display/refresh', 'ChatView-Session', lease.sessionToken, 'streaming')).status, 200);
  } finally { await f.close(); }
});

test('native role header is mandatory and singular, not inferred from credentials', async () => {
  const f = await fixture();
  try {
    const alice = await f.connect('alice', 'gaming');
    for (const path of ['/display/login', '/display/refresh']) {
      const authorization = path === '/display/login'
        ? `ChatView-Challenge ${nonce()}` : `ChatView-Session ${alice.lease.sessionToken}`;
      for (const role of [undefined, '', 'admin', 'Gaming', 'gaming, streaming']) {
        const headers: Record<string, string> = { Authorization: authorization };
        if (role !== undefined) headers['X-ChatView-Role'] = role;
        assert.equal((await f.request(path, { method: 'POST', headers })).status, 400);
      }
    }
    assert.ok(f.creators.gateway(alice.lease.token));
    assert.equal(f.store.connections('alice').length, 1);
  } finally { await f.close(); }
});
