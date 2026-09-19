// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash, randomBytes, randomUUID } from 'node:crypto';

export const DISPLAY_SCOPE = 'chat:read';
export const DISPLAY_TICKET_MS = 60_000;
export const DISPLAY_LEASE_MS = 300_000;
export const MAX_DISPLAY_DEVICES = 4;
export type DisplayOwner = Readonly<{ id: string; expiresAt: number }>;
export type DisplayGrant = Readonly<{ id: string; expiresAt: number }>;
type Entry = { id: string; owner: string; digest: string; kind: 'ticket' | 'lease'; expiresAt: number };

export class DisplayAccessError extends Error {
  readonly status: number;
  constructor(status: number) { super('ChatView display access denied'); this.status = status; }
}
const secret = () => randomBytes(32).toString('hex');
const digest = (value: string) => createHash('sha256').update(value).digest('hex');
const validSecret = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/u.test(value);

// One already-authenticated creator context, not an account/login provider.
// Store only hashes of bearer material. Restart intentionally invalidates leases.
export class DisplayAccess {
  #entries = new Map<string, Entry>();
  #owner: () => DisplayOwner | undefined;
  #now: () => number;
  constructor(owner: () => DisplayOwner | undefined, now: () => number = () => performance.now()) {
    this.#owner = owner; this.#now = now;
  }
  #current() {
    const now = this.#now();
    const owner = this.#owner();
    const valid = owner && owner.id && Number.isFinite(owner.expiresAt) && owner.expiresAt > now ? owner : undefined;
    for (const [id, entry] of this.#entries) {
      if (!valid || entry.owner !== valid.id || now >= entry.expiresAt) this.#entries.delete(id);
    }
    return { now, owner: valid };
  }
  // Invoke only after the account layer checks creator authentication and CSRF.
  issue() {
    const { now, owner } = this.#current();
    if (!owner) throw new DisplayAccessError(401);
    if (this.#entries.size >= MAX_DISPLAY_DEVICES) throw new DisplayAccessError(429);
    const ticket = secret();
    const id = randomUUID();
    const expiresAt = Math.min(owner.expiresAt, now + DISPLAY_TICKET_MS);
    this.#entries.set(id, { id, owner: owner.id, digest: digest(ticket), kind: 'ticket', expiresAt });
    return { id, ticket, expiresInMs: Math.floor(expiresAt - now), scope: DISPLAY_SCOPE };
  }
  // Possession of the one-use ticket authorizes ONLY this short display lease.
  exchange(ticket: unknown) {
    const { now, owner } = this.#current();
    if (!owner || !validSecret(ticket)) throw new DisplayAccessError(401);
    const ticketHash = digest(ticket);
    const entry = [...this.#entries.values()].find(value => value.kind === 'ticket' && value.digest === ticketHash);
    if (!entry) throw new DisplayAccessError(401);
    const token = secret();
    const expiresAt = Math.min(owner.expiresAt, now + DISPLAY_LEASE_MS);
    // Synchronous replacement consumes the ticket before any HTTP response.
    this.#entries.set(entry.id, { ...entry, digest: digest(token), kind: 'lease', expiresAt });
    return { id: entry.id, token, expiresInMs: Math.floor(expiresAt - now), scope: DISPLAY_SCOPE };
  }
  authenticate(token: unknown): DisplayGrant {
    this.#current();
    if (!validSecret(token)) throw new DisplayAccessError(401);
    const tokenHash = digest(token);
    const entry = [...this.#entries.values()].find(value => value.kind === 'lease' && value.digest === tokenHash);
    if (!entry) throw new DisplayAccessError(401);
    return { id: entry.id, expiresAt: entry.expiresAt };
  }
  active(id: string): boolean {
    this.#current();
    return this.#entries.get(id)?.kind === 'lease';
  }
  remaining(id: string): number {
    const { now, owner } = this.#current();
    const entry = this.#entries.get(id);
    return owner && entry?.kind === 'lease' ? Math.max(0, Math.min(entry.expiresAt, owner.expiresAt) - now) : 0;
  }
  revoke(id: string) { this.#entries.delete(id); }
  clear() { this.#entries.clear(); }
}
