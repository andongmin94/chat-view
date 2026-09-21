// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes, randomUUID } from 'node:crypto';
import { SessionStore, SessionRoleError, hashSecret, validSecret } from './session-store.mts';
import type { ConnectionRole } from './session-store.mts';

export const DISPLAY_SCOPE = 'chat:read';
export const DISPLAY_TICKET_MS = 60_000;
export const DISPLAY_LEASE_MS = 300_000;
export const MAX_DISPLAY_DEVICES = 4;
export type DisplayOwner = Readonly<{ id: string; expiresAt: number }>;
export type DisplayGrant = Readonly<{ id: string; expiresAt: number }>;
type Entry = { id: string; owner: string; digest: string; kind: 'ticket' | 'lease'; expiresAt: number; session?: string };

export class DisplayAccessError extends Error {
  readonly status: number;
  constructor(status: number) { super('ChatView display access denied'); this.status = status; }
}
const secret = () => randomBytes(32).toString('hex');

// One authenticated creator context. Display leases confer only private chat.
export class DisplayAccess {
  #entries = new Map<string, Entry>();
  #owner: () => DisplayOwner | undefined;
  #now: () => number;
  #sessions: SessionStore;
  #ownsStore: boolean;
  #closed = false;
  #generation = 0;
  constructor(owner: () => DisplayOwner | undefined, now: () => number = () => performance.now(), sessions?: SessionStore) {
    this.#owner = owner; this.#now = now;
    this.#sessions = sessions ?? new SessionStore(); this.#ownsStore = !sessions;
  }
  #current() {
    if (this.#closed) throw new DisplayAccessError(503);
    const now = this.#now(), owner = this.#owner();
    const valid = owner && owner.id && Number.isFinite(owner.expiresAt) && owner.expiresAt > now ? owner : undefined;
    for (const [id, entry] of this.#entries) {
      if (!valid || entry.owner !== valid.id || now >= entry.expiresAt ||
          (entry.session && this.#sessions.remaining(entry.session, entry.owner) <= 0)) this.#entries.delete(id);
    }
    return { now, owner: valid };
  }
  get generation(): number { return this.#generation; }
  ownerId(): string | undefined { return this.#current().owner?.id; }
  issue() {
    const { now, owner } = this.#current();
    if (!owner) throw new DisplayAccessError(401);
    if (this.#entries.size >= MAX_DISPLAY_DEVICES) throw new DisplayAccessError(429);
    const ticket = secret(), id = randomUUID();
    const expiresAt = Math.min(owner.expiresAt, now + DISPLAY_TICKET_MS);
    this.#entries.set(id, { id, owner: owner.id, digest: hashSecret(ticket), kind: 'ticket', expiresAt });
    return { id, ticket, expiresInMs: Math.floor(expiresAt - now), scope: DISPLAY_SCOPE };
  }
  exchange(ticket: unknown, remember = false, role?: ConnectionRole) {
    const { now, owner } = this.#current();
    if (!owner || !validSecret(ticket)) throw new DisplayAccessError(401);
    const hash = hashSecret(ticket);
    const entry = [...this.#entries.values()].find(value => value.kind === 'ticket' && value.digest === hash);
    if (!entry) throw new DisplayAccessError(401);
    this.#entries.delete(entry.id);
    // Every explicit approval is renewable during this run. `remember` is only
    // the native persistence choice. Roles require separate browser consent.
    let session: ReturnType<SessionStore['create']>;
    try { session = this.#sessions.create(owner.id, role); }
    catch (error) { if (error instanceof SessionRoleError) throw new DisplayAccessError(409); throw error; }
    const token = secret();
    const expiresAt = Math.min(owner.expiresAt, now + DISPLAY_LEASE_MS, now + session.expiresInMs);
    this.#entries.set(entry.id, { ...entry, digest: hashSecret(token), kind: 'lease', expiresAt, session: session.id });
    return { id: entry.id, token, expiresInMs: Math.floor(expiresAt - now), scope: DISPLAY_SCOPE,
      sessionToken: session.token, sessionExpiresInMs: session.expiresInMs, sessionScope: 'chat:renew',
      ...(session.membership ? { membership: session.membership } : {}) };
  }
  resume(token: unknown) {
    const session = this.#sessions.find(token);
    if (!session) throw new DisplayAccessError(401);
    const { now, owner } = this.#current();
    if (!owner) throw new DisplayAccessError(503);
    if (owner.id !== session.owner) throw new DisplayAccessError(403);
    for (const [id, entry] of this.#entries) if (entry.session === session.id) this.#entries.delete(id);
    if (this.#entries.size >= MAX_DISPLAY_DEVICES) throw new DisplayAccessError(429);
    const id = randomUUID(), access = secret();
    const expiresAt = Math.min(owner.expiresAt, now + DISPLAY_LEASE_MS, now + session.remainingMs);
    this.#entries.set(id, { id, owner: owner.id, digest: hashSecret(access), kind: 'lease', expiresAt, session: session.id });
    return { id, token: access, expiresInMs: Math.floor(expiresAt - now), scope: DISPLAY_SCOPE,
      ...(session.membership ? { membership: session.membership } : {}) };
  }
  signout(token: unknown) {
    if (!validSecret(token)) throw new DisplayAccessError(401);
    this.#sessions.remove(token);
    for (const [id, entry] of this.#entries)
      if (entry.session && this.#sessions.remaining(entry.session, entry.owner) <= 0) this.#entries.delete(id);
  }
  revokeSessions() {
    const { owner } = this.#current();
    if (!owner) throw new DisplayAccessError(401);
    this.#sessions.removeOwner(owner.id); this.clear();
  }
  authenticate(token: unknown): DisplayGrant {
    this.#current();
    if (!validSecret(token)) throw new DisplayAccessError(401);
    const hash = hashSecret(token);
    const entry = [...this.#entries.values()].find(value => value.kind === 'lease' && value.digest === hash);
    if (!entry) throw new DisplayAccessError(401);
    return { id: entry.id, expiresAt: entry.expiresAt };
  }
  active(id: string): boolean { if (this.#closed) return false; this.#current(); return this.#entries.get(id)?.kind === 'lease'; }
  remaining(id: string): number {
    const { now, owner } = this.#current();
    const entry = this.#entries.get(id);
    return owner && entry?.kind === 'lease' ? Math.max(0, Math.min(entry.expiresAt, owner.expiresAt) - now) : 0;
  }
  revoke(id: string) { this.#entries.delete(id); }
  clear() { this.#generation++; this.#entries.clear(); }
  close() { this.clear(); this.#closed = true; if (this.#ownsStore) this.#sessions.close(); }
}
