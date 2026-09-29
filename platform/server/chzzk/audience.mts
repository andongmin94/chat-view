// SPDX-License-Identifier: GPL-2.0-or-later
import { randomUUID } from 'node:crypto';
import { ChzzkError } from './api.mts';
import type { ChzzkApi } from './api.mts';

export const AUDIENCE_INTERVAL_MS = 60_000;
export const AUDIENCE_MAX_AGE_MS = 90_000;
export const AUDIENCE_PAGE_LIMIT = 20;
export const AUDIENCE_SCAN_MS = 15_000;
export const AUDIENCE_BACKOFF_MS = 300_000;
export type AudienceReason = 'disabled' | 'pending' | 'stale' | 'not-found' |
  'page-limit' | 'provider-error' | 'rate-limited' | 'not-authorized';
export type AudienceView = Readonly<{ state: 'unavailable'; reason: AudienceReason }> | Readonly<{
  state: 'sampled'; liveId: number; viewers: number; requestedAt: number;
  validForMs: number; continuity: string;
}>;
type Sample = { liveId: number; viewers: number; requestedAt: number; tick: number; continuity: string };

export function audienceSamplingEnabled(value: string | undefined): boolean {
  if (value === undefined || value === '0') return false;
  if (value === '1') return true;
  throw new Error('Invalid audience sampling configuration');
}

// One shared, bounded official-list scan for all selected, reporting creators.
// No per-channel scraping, extra user scope, public-page trigger or viewer IDs.
// Opt-in requires the operator to review the app's terms, permissions and quota.
export class AudienceSampler {
  #api?: Pick<ChzzkApi, 'listLives'>;
  #targets: () => readonly string[];
  #now: () => number;
  #tick: () => number;
  #samples = new Map<string, Sample>();
  #reasons = new Map<string, AudienceReason>();
  #tickets = new Map<string, object>();
  #next = -Infinity;
  #task?: Promise<void>;
  #closed = false;
  #lifetime = new AbortController();
  constructor(api: Pick<ChzzkApi, 'listLives'> | undefined, targets: () => readonly string[],
    now: () => number = Date.now, tick: () => number = () => performance.now()) {
    this.#api = api; this.#targets = targets; this.#now = now; this.#tick = tick;
  }
  current(owner: string): AudienceView {
    if (!this.#api || this.#closed) return { state: 'unavailable', reason: 'disabled' };
    const sample = this.#samples.get(owner);
    if (!sample) return { state: 'unavailable', reason: this.#reasons.get(owner) ?? 'pending' };
    const age = this.#tick() - sample.tick, wallAge = this.#now() - sample.requestedAt;
    if (age < 0 || wallAge < 0 || Math.abs(age - wallAge) > 1000 || age >= AUDIENCE_MAX_AGE_MS)
      return { state: 'unavailable', reason: 'stale' };
    return { state: 'sampled', liveId: sample.liveId, viewers: sample.viewers,
      requestedAt: sample.requestedAt, validForMs: Math.floor(AUDIENCE_MAX_AGE_MS - age), continuity: sample.continuity };
  }
  refresh(): Promise<void> {
    if (this.#closed || !this.#api) return Promise.resolve();
    if (this.#task) return this.#task;
    if (this.#tick() < this.#next) return Promise.resolve();
    this.#next = this.#tick() + AUDIENCE_INTERVAL_MS;
    const task = this.#scan();
    this.#task = task;
    void task.finally(() => { if (this.#task === task) this.#task = undefined; });
    return task;
  }
  async #scan(): Promise<void> {
    let owners: readonly string[] = [];
    try {
      owners = [...new Set(this.#targets())];
      if (owners.length > 128) throw new ChzzkError('invalid-input');
      const tickets = new Map(owners.map(owner => [owner, {}]));
      this.#tickets = new Map(tickets);
      const wanted = new Set(owners), found = new Map<string, Omit<Sample, 'continuity'>>();
      // Drop nonparticipants; this cache does not grow with the platform list.
      for (const owner of this.#samples.keys()) if (!wanted.has(owner)) this.#samples.delete(owner);
      this.#reasons.clear();
      if (!owners.length) return;
      const signal = AbortSignal.any([this.#lifetime.signal, AbortSignal.timeout(AUDIENCE_SCAN_MS)]);
      const cursors = new Set<string>();
      let next: string | undefined, exhausted = false;
      for (let index = 0; index < AUDIENCE_PAGE_LIMIT; index++) {
        const tick = this.#tick(), requestedAt = this.#now();
        const page = await this.#api!.listLives(next, signal);
        signal.throwIfAborted();
        for (const live of page.data) if (wanted.has(live.channelId)) {
          if (found.has(live.channelId)) throw new ChzzkError('response');
          found.set(live.channelId, { liveId: live.liveId, viewers: live.concurrentUserCount, requestedAt, tick });
        }
        if (!page.next) { exhausted = true; break; }
        if (found.size === wanted.size) break;
        if (cursors.has(page.next)) throw new ChzzkError('response');
        cursors.add(page.next); next = page.next;
      }
      const stillWanted = new Set(this.#targets());
      for (const owner of owners) {
        const sample = found.get(owner), previous = this.current(owner);
        if (!stillWanted.has(owner) || this.#tickets.get(owner) !== tickets.get(owner)) {
          this.#samples.delete(owner); continue;
        }
        if (!sample) {
          this.#samples.delete(owner);
          this.#reasons.set(owner, exhausted ? 'not-found' : 'page-limit');
        } else {
          this.#samples.set(owner, { ...sample, continuity: previous.state === 'sampled' &&
            previous.liveId === sample.liveId ? previous.continuity : randomUUID() });
        }
      }
    } catch (error) {
      // Provider failure never propagates into private chat or token refresh.
      // Reject the whole incomplete sweep and break subsequent estimation.
      this.#samples.clear(); this.#reasons.clear();
      if (this.#closed) return;
      const reason: AudienceReason = error instanceof ChzzkError && error.kind === 'http'
        ? error.status === 429 ? 'rate-limited' : [401, 403].includes(error.status) ? 'not-authorized' : 'provider-error'
        : 'provider-error';
      for (const owner of owners.slice(0, 128)) this.#reasons.set(owner, reason);
      if (reason === 'rate-limited' || reason === 'not-authorized') this.#next = this.#tick() + AUDIENCE_BACKOFF_MS;
    }
  }
  clear(owner: string): void {
    this.#samples.delete(owner); this.#reasons.delete(owner); this.#tickets.delete(owner);
  }
  close(): void {
    this.#closed = true; this.#lifetime.abort(); this.#samples.clear(); this.#reasons.clear(); this.#tickets.clear();
  }
}
