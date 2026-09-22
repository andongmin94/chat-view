// SPDX-License-Identifier: GPL-2.0-or-later
import { DisplayAccess, DisplayAccessError } from '../chat/display-access.mts';
import { SessionStore, hashSecret, validSecret } from '../chat/session-store.mts';
import { ChzzkError } from '../chzzk/api.mts';
import type { ChzzkApi, Channel, Tokens } from '../chzzk/api.mts';
import type { ChzzkChatSession } from '../chzzk/session.mts';
import type { DisplayGateway } from '../chat/display-gateway.mts';
import { ProviderGrants } from './provider-grants.mts';
import type { StoredProviderGrant } from './provider-grants.mts';

type Chat = Pick<ChzzkChatSession, 'start' | 'stop' | 'snapshot'>;
type Gateway = Pick<DisplayGateway, 'changed' | 'upgrade' | 'close'>;
type Provider = Pick<ChzzkApi, 'exchangeCode' | 'getUser' | 'refresh' | 'revoke'>;
type Creator = {
  channel: Channel; tokens?: Tokens; expires: number; epoch: number; revision?: string;
  restored: boolean; validating?: Promise<void>;
  access: DisplayAccess; gateway: Gateway; chat?: Chat; ready?: Promise<void>;
  stopping: Promise<unknown>; refreshing?: Promise<void>;
  timer?: ReturnType<typeof setTimeout>; retry?: ReturnType<typeof setTimeout>; retries: number;
};
type Options = {
  api: Provider; sessions: SessionStore; grants: ProviderGrants; now?: () => number;
  createChat: (changed: () => void) => Chat;
  createDisplay: (access: DisplayAccess, snapshot: () => unknown) => Gateway;
};

// One upstream per creator. The existing renewable app approval locates a
// protected provider grant after restart; no new hardware/pairing credential.
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
  #context(channel: Channel): Creator {
    const existing = this.#creators.get(channel.channelId);
    if (existing) return existing;
    if (this.#creators.size >= 128) throw new DisplayAccessError(503);
    let c: Creator;
    const access = new DisplayAccess(() => c.tokens && c.expires > this.#now()
      ? { id: c.channel.channelId, expiresAt: c.expires } : undefined, this.#now, this.sessions);
    const gateway = this.#options.createDisplay(access,
      () => c.chat?.snapshot() ?? { state: 'stopped', received: 0, messages: [] });
    c = { channel, expires: 0, epoch: 0, restored: false, access, gateway, stopping: Promise.resolve(), retries: 0 };
    this.#creators.set(channel.channelId, c);
    return c;
  }
  #get(owner: string): Creator {
    if (this.#closed) throw new DisplayAccessError(503);
    const existing = this.#creators.get(owner);
    if (existing) return existing;
    const saved = this.#options.grants.load(owner);
    if (!saved) throw new DisplayAccessError(503);
    const c = this.#context(saved.channel);
    this.#install(c, saved, true);
    return c;
  }
  describe(owner: string) {
    const c = this.#get(owner);
    return { channel: c.channel, authorized: !!c.tokens && c.expires > this.#now(),
      chatState: c.chat?.snapshot().state ?? 'stopped', connections: this.sessions.connections(owner) };
  }
  access(owner: string): DisplayAccess { return this.#get(owner).access; }
  #stopChat(c: Creator) {
    const chat = c.chat; c.chat = undefined; c.ready = undefined;
    c.stopping = Promise.allSettled([c.stopping, ...(chat ? [chat.stop()] : [])]);
    c.gateway.changed();
  }
  #retire(c: Creator) {
    c.epoch++; c.tokens = undefined; c.expires = 0; c.refreshing = undefined; c.validating = undefined;
    clearTimeout(c.timer); clearTimeout(c.retry); c.retry = undefined;
    c.access.clear(); this.#stopChat(c); c.gateway.changed();
  }
  #suspend(c: Creator, revoke: boolean) {
    const revision = c.revision;
    this.#retire(c);
    if (revoke) {
      this.#revokedAt.set(c.channel.channelId, ++this.#revision);
      this.#options.grants.revokeOwner(c.channel.channelId);
    } else if (revision) this.#options.grants.discard(c.channel.channelId, revision);
  }
  async authorize(code: string, state: string, signal?: AbortSignal): Promise<Channel> {
    if (this.#closed) throw new DisplayAccessError(503);
    const began = this.#revision;
    const lifetime = signal ? AbortSignal.any([signal, this.#lifetime.signal]) : this.#lifetime.signal;
    const tokens = await this.#options.api.exchangeCode(code, state, lifetime);
    const expiresAt = this.#now() + tokens.expiresIn * 1000;
    const channel = await this.#options.api.getUser(tokens.accessToken, lifetime);
    lifetime.throwIfAborted();
    if (!channel.channelId || channel.channelId.length > 256 ||
        (this.#revokedAt.get(channel.channelId) ?? 0) > began) throw new DisplayAccessError(403);
    const c = this.#context(channel);
    // Persist before accepting the authorization, not at graceful shutdown.
    const saved = this.#options.grants.save({ channel, tokens, expiresAt });
    c.epoch++; c.refreshing = undefined; c.validating = undefined;
    clearTimeout(c.retry); c.retry = undefined; c.retries = 0;
    this.#stopChat(c); this.#install(c, saved);
    return channel;
  }
  #install(c: Creator, saved: StoredProviderGrant, restored = false) {
    c.channel = saved.channel; c.tokens = saved.tokens; c.expires = saved.expiresAt;
    c.revision = saved.revision; c.restored = restored;
    clearTimeout(c.timer);
    // Absolute expiry survives downtime. Loading does NOT grant a new lifetime.
    const delay = Math.max(1, Math.min(2147483647,
      saved.expiresAt - this.#now() - Math.min(60_000, saved.tokens.expiresIn * 500)));
    c.timer = setTimeout(() => { void this.refresh(c.channel.channelId).catch(() => {}); }, delay);
    c.timer.unref();
  }
  async #validateRestored(c: Creator): Promise<void> {
    if (!c.restored) return;
    if (c.validating) return c.validating;
    const epoch = c.epoch, token = c.tokens!.accessToken;
    const task = (async () => {
      try {
        const channel = await this.#options.api.getUser(token, this.#lifetime.signal);
        if (this.#closed || c.epoch !== epoch) throw new DisplayAccessError(503);
        if (channel.channelId !== c.channel.channelId) { this.#suspend(c, true); throw new DisplayAccessError(403); }
        c.restored = false;
      } catch (error) {
        if (!this.#closed && c.epoch === epoch && error instanceof ChzzkError &&
            error.kind === 'http' && [401, 403].includes(error.status)) {
          this.#suspend(c, true); throw new DisplayAccessError(401);
        }
        // An ordinary provider outage leaves the encrypted grant intact.
        throw error;
      }
    })();
    c.validating = task;
    try { await task; } finally { if (c.validating === task) c.validating = undefined; }
  }
  async #rotate(c: Creator): Promise<void> {
    if (!c.tokens || !c.revision) throw new DisplayAccessError(503);
    const owner = c.channel.channelId, revision = c.revision, refreshToken = c.tokens.refreshToken;
    // Commit the single-use claim BEFORE any provider I/O. A crash or lost
    // reply leaves no reusable token on disk, even if the provider consumed it.
    if (!this.#options.grants.claimRefresh(owner, revision)) { this.#retire(c); throw new DisplayAccessError(503); }
    c.epoch++; const epoch = c.epoch;
    clearTimeout(c.timer); this.#stopChat(c);
    try {
      const tokens = await this.#options.api.refresh(refreshToken, this.#lifetime.signal);
      const expiresAt = this.#now() + tokens.expiresIn * 1000;
      const channel = await this.#options.api.getUser(tokens.accessToken, this.#lifetime.signal);
      if (this.#closed || c.epoch !== epoch) throw new DisplayAccessError(503);
      if (channel.channelId !== owner) { this.#suspend(c, true); throw new DisplayAccessError(403); }
      const saved = this.#options.grants.finishRefresh({ channel, tokens, expiresAt }, revision);
      this.#install(c, saved);
    } catch (error) {
      if (!this.#closed && c.epoch === epoch) {
        const revoked = error instanceof ChzzkError && error.kind === 'http' && [401, 403].includes(error.status);
        this.#suspend(c, revoked);
        if (revoked) throw new DisplayAccessError(401);
      }
      throw error;
    }
  }
  async refresh(owner: string): Promise<void> {
    const c = this.#get(owner);
    const task = c.refreshing ?? this.#rotate(c);
    c.refreshing = task;
    try { await task; } finally { if (c.refreshing === task) c.refreshing = undefined; }
    // Chat transport failure is NOT provider refresh failure. The successfully
    // persisted replacement must survive it and remain usable after restart.
    await this.ensureChat(owner);
  }
  async ensureChat(owner: string): Promise<void> {
    const c = this.#get(owner);
    if (c.refreshing) await c.refreshing;
    if (!c.tokens) throw new DisplayAccessError(503);
    if (c.expires <= this.#now()) { await this.refresh(owner); return; }
    await this.#validateRestored(c);
    if (!c.tokens || this.#closed) throw new DisplayAccessError(503);
    if (c.expires <= this.#now()) { await this.refresh(owner); return; }
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
    this.#suspend(c, true);
    if (!token) return false;
    try { await this.#options.api.revoke(token, this.#lifetime.signal); return true; }
    catch { return false; }
  }
  async close(): Promise<void> {
    if (this.#closed) return;
    this.#closed = true; this.#lifetime.abort();
    for (const c of this.#creators.values()) {
      // Closing the process is not a signout or provider revocation.
      this.#retire(c); c.gateway.close(); c.access.close();
    }
    await Promise.allSettled([...this.#creators.values()].map(c => c.stopping));
    this.#leases.clear(); this.#creators.clear();
  }
}
