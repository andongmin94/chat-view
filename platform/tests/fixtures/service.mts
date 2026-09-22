// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic provider/chat with real HTTP and SQLite. Callers may supply the
// actual display gateway for wire tests; no fixture claims live CHZZK or OBS.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { randomBytes } from 'node:crypto';
import { Creators } from '../../server/service/creators.mts';
import { PlatformApplication } from '../../server/service/application.mts';
import { SessionStore, hashSecret } from '../../server/chat/session-store.mts';
import { DisplayAccessError } from '../../server/chat/display-access.mts';
import { ProviderGrants } from '../../server/service/provider-grants.mts';
import type { DisplayAccess } from '../../server/chat/display-access.mts';
import type { DisplayGateway } from '../../server/chat/display-gateway.mts';
import type { Channel, Tokens } from '../../server/chzzk/api.mts';
import type { ChatSnapshot } from '../../server/chzzk/session.mts';
export const nonce = () => randomBytes(32).toString('hex');
export const denied = (code: number) => (e: unknown) => e instanceof DisplayAccessError && e.status === code;
export const tokens = (owner: string, generation = 0): Tokens => ({
  accessToken: `access:${owner}:${generation}`, refreshToken: `refresh:${owner}:${generation}`, expiresIn: 3600,
});
export const ownerOf = (token: string) => token.split(':')[1]!;
export function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(yes => { resolve = yes; });
  return { promise, resolve };
}
type Options = {
  path?: string; key?: Uint8Array; now?: () => number;
  createDisplay?: (access: DisplayAccess, origin: () => string, snapshot: () => unknown) =>
    Pick<DisplayGateway, 'changed' | 'upgrade' | 'close'>;
};
export async function fixture(options: Options = {}) {
  const now = options.now ?? Date.now;
  const store = new SessionStore(options.path, now);
  let grants: ProviderGrants;
  try { grants = new ProviderGrants(store.database, options.key ?? randomBytes(32)); }
  catch (error) { store.close(); throw error; }
  const chats = new Map<string, { publish: (message: string) => void; revoke: () => void }>();
  const gateways: { snapshot: () => unknown; changed: number }[] = [];
  const active = new Map<string, number>();
  const startedWith: string[] = [];
  let refreshCalls = 0, exchangeCalls = 0, userCalls = 0;
  let refreshHook: ((token: string) => Promise<Tokens>) | undefined;
  let exchangeHook: ((code: string) => Promise<Tokens>) | undefined;
  let userHook: ((token: string) => Promise<Channel>) | undefined;
  let revokeFails = false, chatFails = false, origin = '', closed = false;
  const api = {
    exchangeCode: async (code: string) => { exchangeCalls++; return exchangeHook ? exchangeHook(code) : tokens(code); },
    getUser: async (access: string) => { userCalls++; return userHook ? userHook(access)
      : { channelId: ownerOf(access), channelName: `channel <${ownerOf(access)}>` }; },
    refresh: async (token: string) => { refreshCalls++; return refreshHook ? refreshHook(token) : tokens(ownerOf(token), refreshCalls); },
    revoke: async () => { if (revokeFails) throw new Error('private upstream error body'); },
  };
  const creators = new Creators({ api, sessions: store, grants, now,
    createChat(changed) {
      let owner = '', snapshot: ChatSnapshot = { state: 'idle', received: 0, messages: [] }, running = false;
      const finish = (state: 'stopped' | 'revoked') => {
        if (running) { running = false; active.set(owner, active.get(owner)! - 1); }
        snapshot = { state, received: 0, messages: [] }; changed();
      };
      return {
        snapshot: () => snapshot,
        async start(token: string, channel: string) {
          if (chatFails) throw new Error('synthetic chat transport failure');
          startedWith.push(token); owner = channel; running = true;
          active.set(owner, (active.get(owner) ?? 0) + 1);
          assert.equal(active.get(owner), 1, 'only one active upstream per creator');
          snapshot = { state: 'subscribed', received: 0, messages: [] }; changed();
          chats.set(owner, { publish(content) {
            snapshot = { state: 'subscribed', received: snapshot.received + 1, messages: [{
              provider: 'chzzk', channelId: owner, senderChannelId: 'fixture-viewer', nickname: 'Fixture',
              verifiedMark: false, userRoleCode: 'common-user', content, messageTime: now(),
            }] }; changed();
          }, revoke: () => finish('revoked') });
        },
        async stop() { finish('stopped'); return true; },
      };
    },
    createDisplay(access, snapshot) {
      if (options.createDisplay) return options.createDisplay(access, () => origin, snapshot);
      const capture = { snapshot, changed: 0 }; gateways.push(capture);
      return { changed() { capture.changed++; }, close() {},
        upgrade() { throw new Error('Synthetic fixture does not claim a WebSocket handshake'); } };
    },
  });
  let app: PlatformApplication;
  const server = createServer((request, response) => { void app.handle(request, response); });
  server.on('upgrade', (request, socket, head) => app.upgrade(request, socket, head));
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Missing HTTP fixture port');
  origin = `http://127.0.0.1:${address.port}`;
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
    assert.match(redirect.headers.get('content-security-policy')!, /form-action 'self' https:\/\/chzzk\.naver\.com;/u);
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
  return { store, grants, creators, app, origin, request, native, page, post, start, authenticate, approve, connect, browser,
    chats, gateways, active, startedWith, get refreshCalls() { return refreshCalls; },
    get userCalls() { return userCalls; }, get exchangeCalls() { return exchangeCalls; },
    setRefresh(hook: typeof refreshHook) { refreshHook = hook; },
    setExchange(hook: typeof exchangeHook) { exchangeHook = hook; },
    setUser(hook: typeof userHook) { userHook = hook; },
    failChat(value = true) { chatFails = value; },
    failRevoke() { revokeFails = true; },
    async close() {
      if (closed) return; closed = true;
      app.close(); server.closeAllConnections();
      const stopped = new Promise<void>(resolve => server.close(() => resolve()));
      await creators.close(); await stopped; grants.close(); store.close();
    },
  };
}
