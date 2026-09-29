// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes } from 'node:crypto';
import type { SessionStore, Membership } from '../chat/session-store.mts';
import type { OutputView } from '../chat/broadcast-output.mts';

// Operator-registered, deliberately non-payable. There is no advertiser script,
// tracking pixel, audience count, HP rule or reward ledger in this catalog.
export const TEST_CAMPAIGN = Object.freeze({
  id: 'chatview-test', brand: 'ChatView', title: '방송과 함께하는 챗뷰',
  description: '공개 배너 연결을 확인하는 시험 캠페인입니다.',
});
export const AD_MAX_AGE_MS = 15_000;
export const HIDDEN_AD = Object.freeze({ type: 'ad-snapshot' as const, version: 1 as const,
  mode: 'test' as const, state: 'hidden' as const, expiresInMs: 0 });
export class CampaignError extends Error {
  readonly status: number;
  constructor(status: number) { super('ChatView campaign unavailable'); this.status = status; }
}
export type CampaignStatus = Readonly<{ sourceId?: string; selected: boolean }>;
type Options = { sessions: SessionStore; output: (owner: string, membership: Membership) => OutputView };

export class Campaigns {
  #options: Options;
  constructor(options: Options) {
    this.#options = options;
    // Stable, read-only public addresses are NOT credentials. A selection is
    // separately tied to the existing streaming approval and cascades on revoke.
    options.sessions.database.exec(`
      CREATE TABLE IF NOT EXISTS ad_sources (
        owner TEXT PRIMARY KEY, source_id TEXT NOT NULL UNIQUE
      ) STRICT;
      CREATE TABLE IF NOT EXISTS ad_selections (
        source_id TEXT PRIMARY KEY REFERENCES ad_sources(source_id) ON DELETE CASCADE,
        connection_id TEXT NOT NULL REFERENCES chat_sessions(id) ON DELETE CASCADE,
        campaign_id TEXT NOT NULL
      ) STRICT;`);
  }
  status(owner: string): CampaignStatus {
    const row = this.#options.sessions.database.prepare(`SELECT a.source_id, s.connection_id
      FROM ad_sources a LEFT JOIN ad_selections s ON s.source_id = a.source_id
      WHERE a.owner = ?`).get(owner);
    if (!row) return { selected: false };
    return { sourceId: String(row.source_id), selected: row.connection_id !== null &&
      this.#options.sessions.remaining(String(row.connection_id), owner) > 0 };
  }
  select(owner: string, campaignId: string): void {
    if (campaignId !== TEST_CAMPAIGN.id) throw new CampaignError(404);
    const connection = this.#options.sessions.connections(owner).find(c => c.role === 'streaming');
    if (!connection) throw new CampaignError(409);
    const db = this.#options.sessions.database;
    db.exec('BEGIN IMMEDIATE');
    try {
      db.prepare('INSERT OR IGNORE INTO ad_sources VALUES (?, ?)').run(owner, randomBytes(16).toString('hex'));
      db.prepare(`INSERT INTO ad_selections SELECT source_id, ?, ? FROM ad_sources WHERE owner = ?
        ON CONFLICT(source_id) DO UPDATE SET connection_id = excluded.connection_id,
          campaign_id = excluded.campaign_id`).run(connection.connectionId, campaignId, owner);
      db.exec('COMMIT');
    } catch (error) { db.exec('ROLLBACK'); throw error; }
  }
  stop(owner: string): void {
    this.#options.sessions.database.prepare(`DELETE FROM ad_selections WHERE source_id IN
      (SELECT source_id FROM ad_sources WHERE owner = ?)`).run(owner);
  }
  snapshot(sourceId: string) {
    if (!/^[a-f0-9]{32}$/u.test(sourceId)) return HIDDEN_AD;
    const row = this.#options.sessions.database.prepare(`SELECT a.owner, s.connection_id, s.campaign_id
      FROM ad_sources a JOIN ad_selections s ON s.source_id = a.source_id WHERE a.source_id = ?`).get(sourceId);
    if (!row || row.campaign_id !== TEST_CAMPAIGN.id) return HIDDEN_AD;
    const owner = String(row.owner);
    const membership = this.#options.sessions.connections(owner)
      .find(c => c.role === 'streaming' && c.connectionId === row.connection_id);
    if (!membership) return HIDDEN_AD;
    // This reads the existing live gateway only. Public page requests must not
    // restore accounts, refresh provider tokens or create a chat subscription.
    const output = this.#options.output(owner, membership);
    if (output.state !== 'reported') return HIDDEN_AD;
    const expiresInMs = Math.floor(Math.min(AD_MAX_AGE_MS, output.expiresInMs,
      this.#options.sessions.remaining(membership.connectionId, owner)));
    if (!Number.isFinite(expiresInMs) || expiresInMs <= 0) return HIDDEN_AD;
    // Both stopped and active OBS reports allow arranging the banner in preview.
    // Neither public reads nor a visible instruction constitute ad exposure.
    return { type: 'ad-snapshot' as const, version: 1 as const, mode: 'test' as const,
      state: 'visible' as const, expiresInMs, campaign: TEST_CAMPAIGN };
  }
}
