// SPDX-License-Identifier: GPL-2.0-or-later
import type { DatabaseSync } from 'node:sqlite';
import type { Membership } from '../chat/session-store.mts';
import type { OutputAcceptance } from '../chat/broadcast-output.mts';

export const ACTIVITY_RETENTION_MS = 7 * 24 * 60 * 60 * 1000;
export const ACTIVITY_ROW_LIMIT = 1000;
export type ActivityState = 'streaming' | 'recording' | 'both' | 'idle' | 'unknown';
export type ActivityReason = 'first-report' | 'continuous' | 'connection-change' |
  'sequence-gap' | 'stale-gap' | 'state-change' | 'clock-change';
export type ActivityInterval = Readonly<{ from: number; to: number; durationMs: number;
  state: ActivityState; reason: ActivityReason; reports: number }>;
type Head = OutputAcceptance & { at: number; tick: number; row: number; campaign: string };
const stateOf = (r: OutputAcceptance['report']): ActivityState =>
  r.streaming ? (r.recording ? 'both' : 'streaming') : r.recording ? 'recording' : 'idle';

// This is a bounded, NON-PAYABLE history of accepted client reports, not an
// exposure ledger. Only intervals bracketed by two timely, consecutive reports
// of the same state are counted. No extrapolation beyond the last receipt.
export class CampaignActivity {
  #db: DatabaseSync;
  #now: () => number;
  #monotonic: () => number;
  #heads = new Map<string, Head>();
  constructor(db: DatabaseSync, now: () => number = Date.now,
    monotonic: () => number = () => performance.now()) {
    this.#db = db; this.#now = now; this.#monotonic = monotonic;
    db.exec(`CREATE TABLE IF NOT EXISTS campaign_activity (
      id INTEGER PRIMARY KEY, owner TEXT NOT NULL, campaign_id TEXT NOT NULL,
      from_at INTEGER NOT NULL, to_at INTEGER NOT NULL,
      duration_ms INTEGER NOT NULL CHECK(duration_ms >= 0),
      state TEXT NOT NULL CHECK(state IN ('streaming','recording','both','idle','unknown')),
      reason TEXT NOT NULL CHECK(reason IN ('first-report','continuous','connection-change',
        'sequence-gap','stale-gap','state-change','clock-change')),
      reports INTEGER NOT NULL CHECK(reports > 0),
      CHECK(to_at >= from_at)
    ) STRICT;
    CREATE INDEX IF NOT EXISTS campaign_activity_owner ON campaign_activity(owner, id);
    CREATE INDEX IF NOT EXISTS campaign_activity_time ON campaign_activity(from_at);`);
    this.#expire();
  }
  #expire() {
    this.#db.prepare('DELETE FROM campaign_activity WHERE from_at < ?')
      .run(this.#now() - ACTIVITY_RETENTION_MS);
  }
  // Clearing never extends or deletes history. A new run/selection/connection
  // needs two new receipts; process-local heads are intentionally not restored.
  clear(owner: string): void { this.#heads.delete(owner); }
  close(): void { this.#heads.clear(); }
  record(owner: string, membership: Membership, input: OutputAcceptance): void {
    const previous = this.#heads.get(owner);
    // Extra idempotency at the store boundary; the gateway rejects these first.
    if (previous?.leaseId === input.leaseId && previous.generation === input.generation &&
        input.report.sequence <= previous.report.sequence) return;
    // A failed read/write must also break interpolation across the missing row.
    this.clear(owner);
    const at = this.#now(), tick = Math.floor(this.#monotonic());
    if (!Number.isSafeInteger(at) || !Number.isSafeInteger(tick) || at <= 0 || tick < 0)
      throw new Error('Activity clock unavailable');
    if (membership.role !== 'streaming') return;
    // Selection and current approval are checked at receipt, not inferred from
    // a public page hit. No tokens, private messages or audience IDs are stored.
    const selected = this.#db.prepare(`SELECT s.campaign_id FROM ad_selections s
      JOIN ad_sources a ON a.source_id = s.source_id
      JOIN chat_sessions c ON c.id = s.connection_id
      JOIN connection_roles r ON r.connection_id = c.id
      JOIN broadcast_sessions b ON b.owner = c.owner
      WHERE a.owner = ? AND c.owner = a.owner AND c.id = ? AND c.expires_at > ?
        AND r.role = 'streaming' AND b.id = ?`)
      .get(owner, membership.connectionId, at, membership.broadcastSessionId);
    if (!selected) return;
    const campaign = String(selected.campaign_id);
    let from = at, duration = 0, state: ActivityState = 'unknown', reason: ActivityReason = 'first-report';
    if (previous && previous.campaign === campaign) {
      const elapsed = tick - previous.tick, wallElapsed = at - previous.at;
      if (previous.leaseId !== input.leaseId || previous.generation !== input.generation) {
        reason = 'connection-change';
      } else if (elapsed <= 0 || wallElapsed <= 0 || Math.abs(wallElapsed - elapsed) > 1000) {
        reason = 'clock-change';
      } else {
        from = previous.at;
        if (elapsed >= previous.validForMs) reason = 'stale-gap';
        else if (input.report.sequence !== previous.report.sequence + 1) reason = 'sequence-gap';
        else if (stateOf(input.report) !== stateOf(previous.report)) reason = 'state-change';
        else { state = stateOf(input.report); reason = 'continuous'; duration = elapsed; }
      }
    }
    this.#db.exec('BEGIN IMMEDIATE');
    try {
      this.#expire();
      // Coalesce homogeneous receipt intervals within an hour. Stop/reselect,
      // gaps and restart cannot merge. Counts cover retained rows, not lifetime.
      const last = previous ? this.#db.prepare(`SELECT * FROM campaign_activity
        WHERE owner = ? AND id = ?`).get(owner, previous.row) : undefined;
      let row: number;
      if (reason === 'continuous' && last?.reason === 'continuous' && last.state === state &&
          last.campaign_id === campaign && Number(last.to_at) === from &&
          Math.floor(Number(last.from_at) / 3600000) === Math.floor(at / 3600000)) {
        this.#db.prepare(`UPDATE campaign_activity SET to_at = ?, duration_ms = duration_ms + ?,
          reports = reports + 1 WHERE owner = ? AND id = ?`).run(at, duration, owner, previous!.row);
        row = previous!.row;
      } else {
        const inserted = this.#db.prepare(`INSERT INTO campaign_activity
          (owner, campaign_id, from_at, to_at, duration_ms, state, reason, reports)
          VALUES (?, ?, ?, ?, ?, ?, ?, 1)`).run(owner, campaign, from, at, duration, state, reason);
        row = Number(inserted.lastInsertRowid);
      }
      this.#db.prepare(`DELETE FROM campaign_activity WHERE owner = ? AND id NOT IN
        (SELECT id FROM campaign_activity WHERE owner = ? ORDER BY id DESC LIMIT ?)`)
        .run(owner, owner, ACTIVITY_ROW_LIMIT);
      this.#db.exec('COMMIT');
      this.#heads.set(owner, { ...input, at, tick, row, campaign });
    } catch (error) { this.#db.exec('ROLLBACK'); throw error; }
  }
  summary(owner: string) {
    this.#expire();
    const totals = this.#db.prepare(`SELECT COUNT(*) AS intervals, COALESCE(SUM(reports),0) AS reports,
      COALESCE(SUM(CASE WHEN state IN ('streaming','both') THEN duration_ms ELSE 0 END),0) AS streaming,
      COALESCE(SUM(CASE WHEN state IN ('recording','both') THEN duration_ms ELSE 0 END),0) AS recording,
      COALESCE(SUM(CASE WHEN state IN ('streaming','recording','both') THEN duration_ms ELSE 0 END),0) AS active,
      COALESCE(SUM(CASE WHEN state = 'idle' THEN duration_ms ELSE 0 END),0) AS idle,
      COALESCE(SUM(CASE WHEN state = 'unknown' THEN 1 ELSE 0 END),0) AS unknown,
      MIN(from_at) AS first_at, MAX(to_at) AS last_at FROM campaign_activity WHERE owner = ?`).get(owner)!;
    const rows = this.#db.prepare(`SELECT from_at, to_at, duration_ms, state, reason, reports
      FROM campaign_activity WHERE owner = ? ORDER BY id DESC LIMIT 50`).all(owner);
    return { mode: 'test' as const, payable: false as const,
      estimatedViewerMs: null, measuredAdViewerMs: null, hp: null, revenue: null,
      intervals: Number(totals.intervals), reports: Number(totals.reports),
      streamingMs: Number(totals.streaming), recordingMs: Number(totals.recording),
      activeMs: Number(totals.active), idleMs: Number(totals.idle), unknownIntervals: Number(totals.unknown),
      firstAt: totals.first_at === null ? null : Number(totals.first_at),
      lastAt: totals.last_at === null ? null : Number(totals.last_at),
      rows: rows.map(row => ({ from: Number(row.from_at), to: Number(row.to_at), durationMs: Number(row.duration_ms),
        state: row.state as ActivityState, reason: row.reason as ActivityReason,
        reports: Number(row.reports) } satisfies ActivityInterval)) };
  }
}
export type ActivitySummary = ReturnType<CampaignActivity['summary']>;
