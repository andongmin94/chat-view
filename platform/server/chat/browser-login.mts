// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { DisplayAccess, DisplayAccessError } from './display-access.mts';
import { hashSecret, validSecret } from './session-store.mts';

export const LOGIN_WINDOW_MS = 300_000;
export const LOGIN_POLL_MS = 1000;
const MAX_PENDING = 16;
type Pending = { challenge: string; remember: boolean; expires: number; polled: number;
  state: 'pending' | 'approved' | 'denied'; owner?: string; generation?: number };

// A request-bound browser handoff for the existing chat connection, not a new
// account/device directory. The URL id cannot poll or obtain display secrets.
export class BrowserLogin {
  #requests = new Map<string, Pending>();
  #access: DisplayAccess;
  #now: () => number;
  constructor(access: DisplayAccess, now: () => number = () => performance.now()) {
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
  // Only the authenticated browser route, after explicit consent and CSRF,
  // invokes approve. The native polling capability never grants this action.
  approve(id: string) {
    const entry = this.#get(id);
    if (entry.state !== 'pending') throw new DisplayAccessError(409);
    const owner = this.#access.ownerId();
    if (!owner) throw new DisplayAccessError(401);
    entry.owner = owner; entry.generation = this.#access.generation; entry.state = 'approved';
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
    // Consume before returning credentials, also on denial/changed ownership.
    this.#requests.delete(id);
    if (entry.state === 'denied') throw new DisplayAccessError(403);
    // Revocation/reauthorization also invalidates approvals awaiting collection.
    if (entry.generation !== this.#access.generation || entry.owner !== this.#access.ownerId()) throw new DisplayAccessError(401);
    return { status: 'approved' as const,
      lease: this.#access.exchange(this.#access.issue().ticket, entry.remember) };
  }
  clear() { this.#requests.clear(); }
}
