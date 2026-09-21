// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { DisplayAccess, DisplayAccessError } from './display-access.mts';
import { hashSecret, validSecret } from './session-store.mts';
import type { ConnectionRole } from './session-store.mts';

export const LOGIN_WINDOW_MS = 300_000;
export const LOGIN_POLL_MS = 1000;
const MAX_PENDING = 16;
type Pending = { challenge: string; remember: boolean; expires: number; polled: number;
  state: 'pending' | 'approved' | 'denied'; owner?: string; generation?: number;
  access?: DisplayAccess; role?: ConnectionRole };

// The URL id cannot poll or obtain display secrets. A service binds the target
// creator only AFTER browser authentication and explicit channel/role consent.
export class BrowserLogin {
  #requests = new Map<string, Pending>();
  #access?: DisplayAccess;
  #now: () => number;
  constructor(access?: DisplayAccess, now: () => number = () => performance.now()) {
    this.#access = access; this.#now = now;
  }
  #prune() {
    for (const [id, entry] of this.#requests)
      if (entry.expires <= this.#now()) this.#requests.delete(id);
  }
  #get(id: string) {
    this.#prune();
    if (!/^[a-f0-9]{32}$/u.test(id)) throw new DisplayAccessError(404);
    const entry = this.#requests.get(id);
    if (!entry) throw new DisplayAccessError(410);
    return entry;
  }
  start(challenge: unknown, remember: boolean) {
    this.#prune();
    if (!validSecret(challenge)) throw new DisplayAccessError(400);
    if (this.#requests.size >= MAX_PENDING) throw new DisplayAccessError(429);
    const id = randomBytes(16).toString('hex');
    this.#requests.set(id, { challenge, remember, expires: this.#now() + LOGIN_WINDOW_MS,
      polled: -Infinity, state: 'pending' });
    return { id, verificationPath: `/login/${id}`, expiresInMs: LOGIN_WINDOW_MS, intervalMs: LOGIN_POLL_MS };
  }
  view(id: string) {
    const entry = this.#get(id);
    if (entry.state !== 'pending') throw new DisplayAccessError(409);
    return { remember: entry.remember, code: id.slice(-6).toUpperCase() };
  }
  approve(id: string, access = this.#access, role?: ConnectionRole) {
    const entry = this.#get(id);
    if (entry.state !== 'pending') throw new DisplayAccessError(409);
    if (role !== undefined && role !== 'gaming' && role !== 'streaming') throw new DisplayAccessError(400);
    const owner = access?.ownerId();
    if (!access || !owner) throw new DisplayAccessError(401);
    entry.access = access; entry.role = role;
    entry.owner = owner; entry.generation = access.generation; entry.state = 'approved';
  }
  deny(id: string) {
    const entry = this.#get(id);
    if (entry.state !== 'pending') throw new DisplayAccessError(409);
    entry.state = 'denied';
  }
  poll(id: string, verifier: unknown) {
    const entry = this.#get(id);
    if (!validSecret(verifier) || !timingSafeEqual(Buffer.from(hashSecret(verifier)), Buffer.from(entry.challenge)))
      throw new DisplayAccessError(401);
    const now = this.#now();
    if (now - entry.polled < LOGIN_POLL_MS) throw new DisplayAccessError(429);
    entry.polled = now;
    if (entry.state === 'pending') return { status: 'pending' as const };
    this.#requests.delete(id);
    if (entry.state === 'denied') throw new DisplayAccessError(403);
    const access = entry.access;
    if (!access || entry.generation !== access.generation || entry.owner !== access.ownerId()) throw new DisplayAccessError(401);
    return { status: 'approved' as const,
      lease: access.exchange(access.issue().ticket, entry.remember, entry.role) };
  }
  clear() { this.#requests.clear(); }
}
