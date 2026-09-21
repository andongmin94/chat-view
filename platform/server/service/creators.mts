// SPDX-License-Identifier: GPL-2.0-or-later
import { DisplayAccess, DisplayAccessError } from '../chat/display-access.mts';
import { SessionStore, hashSecret, validSecret } from '../chat/session-store.mts';
import type { ChzzkApi, Channel, Tokens } from '../chzzk/api.mts';
import type { ChzzkChatSession } from '../chzzk/session.mts';
import type { DisplayGateway } from '../chat/display-gateway.mts';

type Chat = Pick<ChzzkChatSession, 'start' | 'stop' | 'snapshot'>;
type Gateway = Pick<DisplayGateway, 'changed' | 'upgrade' | 'close'>;
type Provider = Pick<ChzzkApi, 'exchangeCode' | 'getUser' | 'refresh' | 'revoke'>;
type Creator = {
  channel: Channel; tokens?: Tokens; expires: number; epoch: number;
  access: DisplayAccess; gateway: Gateway; chat?: Chat; ready?: Promise<void>;
  stopping: Promise<unknown>; refreshing?: Promise<void>;
  timer?: ReturnType<typeof setTimeout>; retry?: ReturnType<typeof setTimeout>; retries: number;
};
type Options = {
  api: Provider; sessions: SessionStore; now?: () => number;
  createChat: (changed: () => void) => Chat;
  createDisplay: (access: DisplayAccess, snapshot: () => unknown) => Gateway;
};

// One application, one upstream per authenticated creator, reused by all HUDs.
// Provider secrets are process-local in this first service slice. Restart
// requires provider reauthorization; hashed native approvals remain resumable.
export class Creators {
  readonly sessions: SessionStore;
  #options: Options;
  #now: () => number;
  #creators = new Map<string, Creator>();
  #leases = new Map<string, { creator: Creator; id: string }>();
  #lifetime = new AbortController();
  #closed = false;
  #revision = 0;
  #revokedAt = new Map<string, number>();
  constructor(options: Options) {
    this.#options = options; this.sessions = options.sessions; this.#now = options.now ?? Date.now;
  }
  #get(owner: string): Creator {
    if (this.#closed) throw new DisplayAccessError(503);
    const creator = this.#creators.get(owner);
    if (!creator) throw new DisplayAccessError(503);
    return creator;
  }
  describe(owner: string) {
    const c = this.#get(owner);
    return { channel: c.channel, authorized: !!c.tokens && c.expires > this.#now(),
      chatState: c.chat?.snapshot().state ?? 'stopped', connections: this.sessions.connections(owner) };
  }
  access(owner: string): DisplayAccess { return this.#get(owner).access; }
  #stopChat(c: Creator) {
    const chat = c.chat; c.chat = undefined; c.ready = undefined;
    // Old subscription teardown finishes before the next upstream starts.
    c.stopping = Promise.allSettled([c.stopping, ...(chat ? [chat.stop()] : [])]);
    c.gateway.changed();
  }
  #suspend(c: Creator, revoke: boolean) {
    c.epoch++; c.tokens = undefined; c.expires = 0; c.refreshing = undefined;
    clearTimeout(c.timer); clearTimeout(c.retry); c.retry = undefined;
    c.access.clear();
    if (revoke) {
      this.#revokedAt.set(c.channel.channelId, ++this.#revision);
      this.sessions.removeOwner(c.channel.channelId);
    }
    this.#stopChat(c); c.gateway.changed();
  }
  async authorize(code: string, state: string, signal?: AbortSignal): Promise<Channel> {
    if (this.#closed) throw new DisplayAccessError(503);
    const began = this.#revision;
    const lifetime = signal ? AbortSignal.any([signal, this.#lifetime.signal]) : this.#lifetime.signal;
    const tokens = await this.#options.api.exchangeCode(code, state, lifetime);
    const channel = await this.#options.api.getUser(tokens.accessToken, lifetime);
    lifetime.throwIfAborted();
    // This key comes only from /users/me, never a submitted owner/channel id.
    if (!channel.channelId || channel.channelId.length > 256 ||
        (this.#revokedAt.get(channel.channelId) ?? 0) > began) throw new DisplayAccessError(403);
    let c = this.#creators.get(channel.channelId);
    if (!c) {
      if (this.#creators.size >= 128) throw new DisplayAccessError(503);
      const access = new DisplayAccess(() => c?.tokens && c.expires > this.#now()
        ? { id: c.channel.channelId, expiresAt: c.expires } : undefined, this.#now, this.sessions);
      const gateway = this.#options.createDisplay(access,
        () => c?.chat?.snapshot() ?? { state: 'stopped', received: 0, messages: [] });
      c = { channel, expires: 0, epoch: 0, access, gateway, stopping: Promise.resolve(), retries: 0 };
      this.#creators.set(channel.channelId, c);
    }
    c.epoch++; c.refreshing = undefined; c.channel = channel;
    clearTimeout(c.retry); c.retry = undefined; c.retries = 0;
    this.#stopChat(c);
    this.#install(c, tokens);
    return channel;
  }
  #install(c: Creator, tokens: Tokens) {
    c.tokens = tokens; c.expires = this.#now() + tokens.expiresIn * 1000;
    if (!Number.isFinite(c.expires) || tokens.expiresIn <= 0) { this.#suspend(c, false); throw new DisplayAccessError(503); }
    clearTimeout(c.timer);
    const delay = Math.max(1, Math.min(2147483647, tokens.expiresIn * 1000 - Math.min(60_000, tokens.expiresIn * 500)));
    c.timer = setTimeout(() => { void this.refresh(c.channel.channelId).catch(() => {}); }, delay);
    c.timer.unref();
  }
  async refresh(owner: string): Promise<void> {
    const c = this.#get(owner);
    if (c.refreshing) return c.refreshing;
    if (!c.tokens) throw new DisplayAccessError(503);
    const epoch = c.epoch, refreshToken = c.tokens.refreshToken;
    // CHZZK refresh tokens are single-use: no retry after an ambiguous failure.
    const task = (async () => {
      try {
        const tokens = await this.#options.api.refresh(refreshToken, this.#lifetime.signal);
        const channel = await this.#options.api.getUser(tokens.accessToken, this.#lifetime.signal);
        if (this.#closed || c.epoch !== epoch) throw new DisplayAccessError(503);
        if (channel.channelId !== owner) { this.#suspend(c, true); throw new DisplayAccessError(403); }
        this.#stopChat(c); this.#install(c, tokens);
        await this.ensureChat(owner);
      } catch (error) {
        if (!this.#closed && c.epoch === epoch) this.#suspend(c, false);
        throw error;
      }
    })();
    c.refreshing = task;
    try { await task; } finally { if (c.refreshing === task) c.refreshing = undefined; }
  }
  async ensureChat(owner: string): Promise<void> {
    const c = this.#get(owner);
    if (!c.tokens || c.expires <= this.#now()) throw new DisplayAccessError(503);
    if (c.ready) return c.ready;
    if (c.chat?.snapshot().state === 'subscribed') return;
    clearTimeout(c.retry); c.retry = undefined;
    const epoch = c.epoch;
    const task = (async () => {
      await c.stopping;
      if (this.#closed || c.epoch !== epoch || !c.tokens) throw new DisplayAccessError(503);
      const chat = this.#options.createChat(() => {
        if (c.chat !== chat || c.epoch !== epoch || this.#closed) return;
        const state = chat.snapshot().state;
        if (state === 'revoked') { this.#suspend(c, true); return; }
        c.gateway.changed();
        if (['disconnected', 'error', 'unsubscribed'].includes(state) && !c.retry && c.retries < 5) {
          c.retry = setTimeout(() => {
            c.retry = undefined;
            void this.ensureChat(owner).catch(() => {});
          }, 1000 * 2 ** c.retries++);
          c.retry.unref();
        }
      });
      c.chat = chat;
      await chat.start(c.tokens.accessToken, owner, this.#lifetime.signal);
      if (c.epoch !== epoch || this.#closed) throw new DisplayAccessError(503);
      // The retry budget is per provider authorization, not reset by a brief
      // connection that immediately drops again.
      c.gateway.changed();
    })();
    c.ready = task;
    try { await task; } finally { if (c.ready === task) c.ready = undefined; }
  }
  track(lease: { id: string; token: string }, owner: string): void {
    const c = this.#get(owner);
    for (const [key, value] of this.#leases) if (!value.creator.access.active(value.id)) this.#leases.delete(key);
    c.access.authenticate(lease.token);
    this.#leases.set(hashSecret(lease.token), { creator: c, id: lease.id });
    c.gateway.changed();
  }
  gateway(token: unknown): Gateway {
    if (this.#closed || !validSecret(token)) throw new DisplayAccessError(401);
    const entry = this.#leases.get(hashSecret(token));
    if (!entry || !entry.creator.access.active(entry.id)) throw new DisplayAccessError(401);
    entry.creator.access.authenticate(token);
    return entry.creator.gateway;
  }
  async resume(token: unknown) {
    const session = this.sessions.find(token);
    if (!session) throw new DisplayAccessError(401);
    await this.ensureChat(session.owner);
    const lease = this.#get(session.owner).access.resume(token);
    this.track(lease, session.owner); return lease;
  }
  signout(token: unknown) {
    if (!validSecret(token)) throw new DisplayAccessError(401);
    const session = this.sessions.find(token);
    this.sessions.remove(token);
    if (session) this.#creators.get(session.owner)?.gateway.changed();
  }
  removeConnection(owner: string, id: string) {
    this.sessions.removeConnection(owner, id);
    this.#creators.get(owner)?.gateway.changed();
  }
  async revoke(owner: string): Promise<boolean> {
    const c = this.#get(owner), token = c.tokens?.accessToken;
    this.#suspend(c, true); // Local delivery/roles stop even if the provider is down.
    if (!token) return false;
    try { await this.#options.api.revoke(token, this.#lifetime.signal); return true; }
    catch { return false; }
  }
  async close(): Promise<void> {
    if (this.#closed) return;
    this.#closed = true; this.#lifetime.abort();
    for (const c of this.#creators.values()) {
      this.#suspend(c, false); c.gateway.close(); c.access.close();
    }
    await Promise.allSettled([...this.#creators.values()].map(c => c.stopping));
    this.#leases.clear(); this.#creators.clear();
  }
}
