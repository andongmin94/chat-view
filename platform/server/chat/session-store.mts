// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdirSync, chmodSync } from 'node:fs';
import { dirname } from 'node:path';
import { DatabaseSync } from 'node:sqlite';

// A remembered app login, not hardware enrollment or an advertising identity.
export const SESSION_MS = 30 * 24 * 60 * 60 * 1000;
export const hashSecret = (value: string) => createHash('sha256').update(value).digest('hex');
export const validSecret = (value: unknown): value is string =>
  typeof value === 'string' && /^[a-f0-9]{64}$/u.test(value);
export type ChatSession = Readonly<{ id: string; owner: string; remainingMs: number }>;

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
        CREATE TABLE IF NOT EXISTS chat_sessions (
          id TEXT PRIMARY KEY, owner TEXT NOT NULL, digest TEXT NOT NULL UNIQUE,
          expires_at INTEGER NOT NULL
        ) STRICT;
        CREATE INDEX IF NOT EXISTS chat_sessions_owner ON chat_sessions(owner);`);
    } catch (error) { this.#db.close(); throw error; }
  }
  create(owner: string) {
    if (!owner || owner.length > 256 || this.#closed) throw new Error('Session unavailable');
    const now = this.#now(), id = randomUUID(), token = randomBytes(32).toString('hex');
    this.#db.prepare('DELETE FROM chat_sessions WHERE expires_at <= ?').run(now);
    this.#db.prepare('INSERT INTO chat_sessions VALUES (?, ?, ?, ?)')
      .run(id, owner, hashSecret(token), now + SESSION_MS);
    return { id, token, expiresInMs: SESSION_MS };
  }
  find(token: unknown): ChatSession | undefined {
    if (this.#closed || !validSecret(token)) return undefined;
    const now = this.#now();
    const row = this.#db.prepare('SELECT id, owner, expires_at FROM chat_sessions WHERE digest = ? AND expires_at > ?')
      .get(hashSecret(token), now);
    return row ? { id: String(row.id), owner: String(row.owner), remainingMs: Number(row.expires_at) - now } : undefined;
  }
  remaining(id: string, owner: string): number {
    if (this.#closed) return 0;
    const row = this.#db.prepare('SELECT expires_at FROM chat_sessions WHERE id = ? AND owner = ?').get(id, owner);
    return row ? Math.max(0, Number(row.expires_at) - this.#now()) : 0;
  }
  remove(token: unknown): void {
    if (!this.#closed && validSecret(token))
      this.#db.prepare('DELETE FROM chat_sessions WHERE digest = ?').run(hashSecret(token));
  }
  removeOwner(owner: string): void {
    if (!this.#closed) this.#db.prepare('DELETE FROM chat_sessions WHERE owner = ?').run(owner);
  }
  close(): void { if (!this.#closed) { this.#closed = true; this.#db.close(); } }
}
