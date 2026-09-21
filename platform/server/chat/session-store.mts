// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdirSync, chmodSync } from 'node:fs';
import { dirname } from 'node:path';
import { DatabaseSync } from 'node:sqlite';

// App approvals, never hardware enrollment or audience/advertising identities.
export const SESSION_MS = 30 * 24 * 60 * 60 * 1000;
export const hashSecret = (value: string) => createHash('sha256').update(value).digest('hex');
export const validSecret = (value: unknown): value is string =>
  typeof value === 'string' && /^[a-f0-9]{64}$/u.test(value);
export type ConnectionRole = 'gaming' | 'streaming';
export type Membership = Readonly<{ broadcastSessionId: string; connectionId: string; role: ConnectionRole }>;
export type ChatSession = Readonly<{ id: string; owner: string; remainingMs: number; membership?: Membership }>;
export class SessionRoleError extends Error {
  constructor() { super('ChatView connection role unavailable'); }
}

export class SessionStore {
  #db: DatabaseSync;
  #now: () => number;
  #closed = false;
  constructor(path = ':memory:', now: () => number = Date.now) {
    if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true, mode: 0o700 });
    this.#db = new DatabaseSync(path);
    this.#now = now;
    try {
      if (path !== ':memory:') chmodSync(path, 0o600);
      this.#db.exec(`PRAGMA busy_timeout=1000;
        PRAGMA foreign_keys=ON;
        CREATE TABLE IF NOT EXISTS chat_sessions (
          id TEXT PRIMARY KEY, owner TEXT NOT NULL, digest TEXT NOT NULL UNIQUE,
          expires_at INTEGER NOT NULL
        ) STRICT;
        CREATE INDEX IF NOT EXISTS chat_sessions_owner ON chat_sessions(owner);
        CREATE TABLE IF NOT EXISTS broadcast_sessions (
          owner TEXT PRIMARY KEY, id TEXT NOT NULL UNIQUE
        ) STRICT;
        CREATE TABLE IF NOT EXISTS connection_roles (
          connection_id TEXT PRIMARY KEY REFERENCES chat_sessions(id) ON DELETE CASCADE,
          owner TEXT NOT NULL REFERENCES broadcast_sessions(owner),
          role TEXT NOT NULL CHECK(role IN ('gaming', 'streaming'))
        ) STRICT;
        CREATE UNIQUE INDEX IF NOT EXISTS one_streaming_role
          ON connection_roles(owner) WHERE role = 'streaming';`);
    } catch (error) { this.#db.close(); throw error; }
  }
  #membership(id: string): Membership | undefined {
    const row = this.#db.prepare(`SELECT b.id, r.role FROM connection_roles r
      JOIN broadcast_sessions b ON b.owner = r.owner WHERE r.connection_id = ?`).get(id);
    return row ? { broadcastSessionId: String(row.id), connectionId: id, role: row.role as ConnectionRole } : undefined;
  }
  create(owner: string, role?: ConnectionRole) {
    if (!owner || owner.length > 256 || this.#closed) throw new Error('Session unavailable');
    if (role !== undefined && role !== 'gaming' && role !== 'streaming') throw new SessionRoleError();
    const now = this.#now(), id = randomUUID(), token = randomBytes(32).toString('hex');
    // The streaming slot and its renewable approval are committed together.
    // A conflicting approval never leaves a hidden renewable session behind.
    this.#db.exec('BEGIN IMMEDIATE');
    try {
      this.#db.prepare('DELETE FROM chat_sessions WHERE expires_at <= ?').run(now);
      if (role) {
        const count = this.#db.prepare('SELECT COUNT(*) AS n FROM chat_sessions WHERE owner = ?').get(owner);
        if (Number(count?.n) >= 4 || (role === 'streaming' &&
          this.#db.prepare("SELECT 1 FROM connection_roles WHERE owner = ? AND role = 'streaming'").get(owner)))
          throw new SessionRoleError();
        this.#db.prepare('INSERT OR IGNORE INTO broadcast_sessions VALUES (?, ?)').run(owner, randomUUID());
      }
      this.#db.prepare('INSERT INTO chat_sessions VALUES (?, ?, ?, ?)').run(id, owner, hashSecret(token), now + SESSION_MS);
      if (role) this.#db.prepare('INSERT INTO connection_roles VALUES (?, ?, ?)').run(id, owner, role);
      const membership = this.#membership(id);
      this.#db.exec('COMMIT');
      return { id, token, expiresInMs: SESSION_MS, ...(membership ? { membership } : {}) };
    } catch (error) { this.#db.exec('ROLLBACK'); throw error; }
  }
  find(token: unknown): ChatSession | undefined {
    if (this.#closed || !validSecret(token)) return undefined;
    const now = this.#now();
    const row = this.#db.prepare('SELECT id, owner, expires_at FROM chat_sessions WHERE digest = ? AND expires_at > ?')
      .get(hashSecret(token), now);
    if (!row) return undefined;
    const membership = this.#membership(String(row.id));
    return { id: String(row.id), owner: String(row.owner), remainingMs: Number(row.expires_at) - now,
      ...(membership ? { membership } : {}) };
  }
  remaining(id: string, owner: string): number {
    if (this.#closed) return 0;
    const row = this.#db.prepare('SELECT expires_at FROM chat_sessions WHERE id = ? AND owner = ?').get(id, owner);
    return row ? Math.max(0, Number(row.expires_at) - this.#now()) : 0;
  }
  connections(owner: string): readonly Membership[] {
    if (this.#closed) return [];
    const rows = this.#db.prepare(`SELECT s.id FROM chat_sessions s
      JOIN connection_roles r ON r.connection_id = s.id
      WHERE s.owner = ? AND s.expires_at > ? ORDER BY s.id`).all(owner, this.#now());
    return rows.map(row => this.#membership(String(row.id))!);
  }
  removeConnection(owner: string, id: string): void {
    if (!this.#closed) this.#db.prepare('DELETE FROM chat_sessions WHERE owner = ? AND id = ?').run(owner, id);
  }
  remove(token: unknown): void {
    if (!this.#closed && validSecret(token))
      this.#db.prepare('DELETE FROM chat_sessions WHERE digest = ?').run(hashSecret(token));
  }
  removeOwner(owner: string): void {
    if (this.#closed) return;
    this.#db.exec('BEGIN IMMEDIATE');
    try {
      this.#db.prepare('DELETE FROM chat_sessions WHERE owner = ?').run(owner);
      this.#db.prepare('DELETE FROM broadcast_sessions WHERE owner = ?').run(owner);
      this.#db.exec('COMMIT');
    } catch (error) { this.#db.exec('ROLLBACK'); throw error; }
  }
  close(): void { if (!this.#closed) { this.#closed = true; this.#db.close(); } }
}
