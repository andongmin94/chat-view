// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { EventEmitter, once } from 'node:events';
import { randomBytes } from 'node:crypto';
import { WebSocket } from 'ws';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { startProbe } from '../tools/chzzk-connect.mts';
import { hashSecret } from '../server/chat/session-store.mts';

test('app login -> same-browser CHZZK consent -> own chat -> refresh -> signout, without copying a key', async t => {
  const credentials = { clientId: 'app', clientSecret: 'PRIVATE_CLIENT' };
  class Socket extends EventEmitter {
    connect() { queueMicrotask(() => this.emit('SYSTEM', { type: 'connected', data: { sessionKey: 'PRIVATE_KEY' } })); }
    disconnect() {}
  }
  let upstream!: Socket, subscriptions = 0;
  const ok = (content: unknown) => Response.json({ code: 200, content });
  const api = new ChzzkApi(credentials, async url => {
    if (url.endsWith('/token')) return ok({ accessToken: 'PRIVATE_ACCESS', refreshToken: 'PRIVATE_REFRESH', tokenType: 'Bearer', expiresIn: '86400' });
    if (url.endsWith('/users/me')) return ok({ channelId: 'mine', channelName: '내 채널 <script>' });
    if (url.endsWith('/sessions/auth')) return ok({ url: 'https://ssio08.nchat.naver.com/?auth=PRIVATE_TICKET' });
    if (url.includes('/subscribe/chat?')) {
      subscriptions++;
      queueMicrotask(() => upstream.emit('SYSTEM', { type: 'subscribed', data: { eventType: 'CHAT', channelId: 'mine' } }));
    }
    return ok(null);
  });
  const probe = await startProbe({ credentials, port: 0, api, socketFactory: () => { upstream = new Socket(); return upstream; } });
  t.after(() => probe.close());
  const postNative = (path: string, scheme: string, value: string, remember = false) => fetch(probe.origin + path, {
    method: 'POST', headers: { Authorization: `${scheme} ${value}`, ...(remember ? { 'X-ChatView-Remember': '1' } : {}) },
    redirect: 'manual', signal: AbortSignal.timeout(5000),
  });
  const verifier = randomBytes(32).toString('hex');
  const response = await postNative('/display/login', 'ChatView-Challenge', hashSecret(verifier), true);
  assert.equal(response.status, 200);
  const request = await response.json() as { id: string; verificationPath: string };
  assert(!JSON.stringify(request).includes(verifier));
  const forbidden = await fetch(probe.origin + request.verificationPath, { headers: { 'Sec-Fetch-Site': 'cross-site' } });
  assert.equal(forbidden.status, 403);
  const landing = await fetch(probe.origin + request.verificationPath, { redirect: 'manual' });
  assert.equal(landing.status, 303);
  const cookie = landing.headers.get('set-cookie')!.split(';')[0]!;
  const get = (path: string, extra: Record<string, string> = {}) => fetch(probe.origin + path, {
    headers: { Cookie: cookie, ...extra }, redirect: 'manual', signal: AbortSignal.timeout(5000),
  });
  const html = await (await get(request.verificationPath)).text();
  assert.doesNotMatch(html, /PRIVATE_|sessionToken/u);
  const csrf = html.match(/csrf=([a-f0-9]{64})/u)![1]!;
  const action = (name: string, token = csrf) => fetch(`${probe.origin}${request.verificationPath}/${name}?csrf=${token}`, {
    method: 'POST', headers: { Cookie: cookie, Origin: probe.origin }, redirect: 'manual', signal: AbortSignal.timeout(5000),
  });
  assert.equal((await action('approve', '0'.repeat(64))).status, 403);
  const login = await action('connect'); assert.equal(login.status, 303);
  const state = new URL(login.headers.get('location')!).searchParams.get('state');
  assert.equal((await get('/callback?code=code&state=bad')).status, 400);
  const callback = await get(`/callback?code=code&state=${state}`);
  assert.equal(callback.status, 303); assert.equal(callback.headers.get('location'), request.verificationPath);
  const afterCallback = await get(request.verificationPath, { 'Sec-Fetch-Site': 'cross-site' });
  assert.equal(afterCallback.status, 200); assert.match(await afterCallback.text(), /내 채널 &lt;script&gt;/u);
  const approved = await action('approve'); assert.equal(approved.status, 200);
  assert.doesNotMatch(await approved.text(), /PRIVATE_|sessionToken/u);
  const poll = await postNative(`/display/login/${request.id}`, 'ChatView-Login', verifier);
  assert.equal(poll.status, 200);
  const result = await poll.json() as { status: string; lease: { token: string; sessionToken: string; sessionScope: string } };
  assert.equal(result.status, 'approved'); assert.equal(result.lease.sessionScope, 'chat:renew');
  assert.equal((await postNative(`/display/login/${request.id}`, 'ChatView-Login', verifier)).status, 410);
  assert.equal(subscriptions, 1);
  upstream.emit('CHAT', JSON.stringify({ channelId: 'mine', senderChannelId: 'sender', profile: { nickname: '한글', verifiedMark: false },
    userRoleCode: 'common_user', content: '승인 완료 😀', messageTime: 1700000000000 }));
  const peer = new WebSocket(probe.origin.replace('http:', 'ws:') + '/display/events', { headers: { Authorization: `Bearer ${result.lease.token}` } });
  t.after(() => peer.terminate());
  const [frame] = await once(peer, 'message');
  const envelope = JSON.parse(String(frame));
  assert.equal(envelope.snapshot.messages[0].content, '승인 완료 😀');
  const closed = once(peer, 'close');
  const renewed = await postNative('/display/refresh', 'ChatView-Session', result.lease.sessionToken);
  assert.equal(renewed.status, 200); await closed;
  assert.equal((await postNative('/display/signout', 'ChatView-Session', result.lease.sessionToken)).status, 200);
  assert.equal((await postNative('/display/refresh', 'ChatView-Session', result.lease.sessionToken)).status, 401);
  assert.equal(subscriptions, 1);
});
