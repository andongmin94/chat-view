// SPDX-License-Identifier: GPL-2.0-or-later
import { createCipheriv, createDecipheriv, createSecretKey, randomBytes, randomUUID } from 'node:crypto';
import type { KeyObject } from 'node:crypto';
import { closeSync, fstatSync, openSync, readSync } from 'node:fs';
import { isAbsolute } from 'node:path';
import type { DatabaseSync } from 'node:sqlite';
import type { Channel, Tokens } from '../chzzk/api.mts';

export type ProviderGrant = Readonly<{ channel: Channel; tokens: Tokens; expiresAt: number }>;
export type StoredProviderGrant = ProviderGrant & Readonly<{ revision: string }>;
export class ProviderGrantError extends Error {
  constructor() { super('ChatView provider storage unavailable'); }
}
// Operator-managed raw 32-byte key, independent of the SQLite volume. Never
// generate a replacement on startup or interpret a missing key as an empty DB.
export function readProviderKey(path: string): Buffer {
  let fd: number | undefined;
  const bytes = Buffer.alloc(33);
  try {
    if (!isAbsolute(path)) throw new ProviderGrantError();
    fd = openSync(path, 'r');
    const stat = fstatSync(fd);
    if (!stat.isFile() || stat.size !== 32 ||
        (process.platform !== 'win32' && (stat.mode & 0o077) !== 0)) throw new ProviderGrantError();
    let length = 0, count: number;
    while (length < bytes.length && (count = readSync(fd, bytes, length, bytes.length - length, null)) > 0) length += count;
    if (length !== 32) throw new ProviderGrantError();
    return Buffer.from(bytes.subarray(0, 32));
  } catch { throw new ProviderGrantError(); }
  finally { bytes.fill(0); if (fd !== undefined) closeSync(fd); }
}
const MAX_SEALED_BYTES = 64 * 1024;
const KEY_CHECK = Buffer.from('ChatView provider key check v1');
const context = (owner: string, revision: string) =>
  Buffer.from(JSON.stringify(['chatview/chzzk-grant/v1', owner, revision]));
function text(value: unknown, max: number, secret = false): value is string {
  return typeof value === 'string' && !!value.trim() && value.length <= max &&
    !/[\x00-\x1f\x7f]/u.test(value) && (!secret || !/\s/u.test(value));
}
function grant(value: ProviderGrant, owner: string): ProviderGrant {
  const { channel, tokens, expiresAt } = value;
  if (!channel || channel.channelId !== owner || !text(owner, 256, true) ||
      !text(channel.channelName, 200) || !tokens || !text(tokens.accessToken, 8192, true) ||
      !text(tokens.refreshToken, 8192, true) || !Number.isSafeInteger(tokens.expiresIn) || tokens.expiresIn <= 0 ||
      !Number.isSafeInteger(expiresAt) || expiresAt <= 0) throw new ProviderGrantError();
  return { channel: { channelId: owner, channelName: channel.channelName },
    tokens: { accessToken: tokens.accessToken, refreshToken: tokens.refreshToken, expiresIn: tokens.expiresIn }, expiresAt };
}

// Shares the existing session DB; owns encrypted provider grants, not chat text.
// A NULL sealed value is a durable in-flight/uncertain refresh, NEVER resumable.
// Run one service instance per DB. SQL compare-and-set also fences stale tasks.
export class ProviderGrants {
  #db: DatabaseSync;
  #key?: KeyObject;
  constructor(database: DatabaseSync, key: Uint8Array) {
    if (!(key instanceof Uint8Array) || key.byteLength !== 32) throw new ProviderGrantError();
    this.#db = database; this.#key = createSecretKey(key);
    try {
      this.#db.exec('BEGIN IMMEDIATE');
      try {
        this.#db.exec(`CREATE TABLE IF NOT EXISTS provider_grants (
          owner TEXT PRIMARY KEY, revision TEXT NOT NULL, sealed BLOB
        ) STRICT;
        CREATE TABLE IF NOT EXISTS provider_key_check (
          id INTEGER PRIMARY KEY CHECK(id = 1), sealed BLOB NOT NULL
        ) STRICT;`);
        const row = this.#db.prepare('SELECT sealed FROM provider_key_check WHERE id = 1').get();
        if (row) {
          const check = this.#open(row.sealed, 'key-check', 'v1');
          try { if (!check.equals(KEY_CHECK)) throw new ProviderGrantError(); }
          finally { check.fill(0); }
        } else {
          if (this.#db.prepare('SELECT 1 FROM provider_grants LIMIT 1').get()) throw new ProviderGrantError();
          this.#db.prepare('INSERT INTO provider_key_check VALUES (1, ?)')
            .run(this.#seal(KEY_CHECK, 'key-check', 'v1'));
        }
        this.#db.exec('COMMIT');
      } catch (error) { this.#db.exec('ROLLBACK'); throw error; }
    } catch { this.#key = undefined; throw new ProviderGrantError(); }
  }
  #run<T>(operation: () => T): T {
    if (!this.#key) throw new ProviderGrantError();
    try { return operation(); } catch { throw new ProviderGrantError(); }
  }
  #seal(plain: Uint8Array, owner: string, revision: string): Buffer {
    const iv = randomBytes(12);
    const cipher = createCipheriv('aes-256-gcm', this.#key!, iv, { authTagLength: 16 });
    cipher.setAAD(context(owner, revision));
    const encrypted = Buffer.concat([cipher.update(plain), cipher.final()]);
    const sealed = Buffer.concat([iv, cipher.getAuthTag(), encrypted]);
    if (sealed.byteLength > MAX_SEALED_BYTES) throw new ProviderGrantError();
    return sealed;
  }
  #open(value: unknown, owner: string, revision: string): Buffer {
    if (!(value instanceof Uint8Array) || value.byteLength < 29 || value.byteLength > MAX_SEALED_BYTES)
      throw new ProviderGrantError();
    const decipher = createDecipheriv('aes-256-gcm', this.#key!, value.subarray(0, 12), { authTagLength: 16 });
    decipher.setAAD(context(owner, revision)); decipher.setAuthTag(value.subarray(12, 28));
    const first = decipher.update(value.subarray(28));
    try { return Buffer.concat([first, decipher.final()]); } finally { first.fill(0); }
  }
  load(owner: string): StoredProviderGrant | undefined {
    return this.#run(() => {
      const row = this.#db.prepare('SELECT revision, sealed FROM provider_grants WHERE owner = ?').get(owner);
      if (!row || row.sealed === null) return undefined;
      const revision = String(row.revision), plain = this.#open(row.sealed, owner, revision);
      try { return { ...grant(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(plain)), owner), revision }; }
      finally { plain.fill(0); }
    });
  }
  #encoded(value: ProviderGrant) {
    const normalized = grant(value, value.channel.channelId), revision = randomUUID();
    const plain = Buffer.from(JSON.stringify(normalized));
    try { return { saved: { ...normalized, revision }, sealed: this.#seal(plain, normalized.channel.channelId, revision) }; }
    finally { plain.fill(0); }
  }
  save(value: ProviderGrant): StoredProviderGrant {
    return this.#run(() => {
      const { saved, sealed } = this.#encoded(value);
      this.#db.prepare(`INSERT INTO provider_grants VALUES (?, ?, ?)
        ON CONFLICT(owner) DO UPDATE SET revision = excluded.revision, sealed = excluded.sealed`)
        .run(saved.channel.channelId, saved.revision, sealed);
      return saved;
    });
  }
  claimRefresh(owner: string, revision: string): boolean {
    return this.#run(() => Number(this.#db.prepare(`UPDATE provider_grants SET sealed = NULL
      WHERE owner = ? AND revision = ? AND sealed IS NOT NULL`).run(owner, revision).changes) === 1);
  }
  finishRefresh(value: ProviderGrant, revision: string): StoredProviderGrant {
    return this.#run(() => {
      const { saved, sealed } = this.#encoded(value);
      const changed = this.#db.prepare(`UPDATE provider_grants SET revision = ?, sealed = ?
        WHERE owner = ? AND revision = ? AND sealed IS NULL`)
        .run(saved.revision, sealed, saved.channel.channelId, revision).changes;
      if (Number(changed) !== 1) throw new ProviderGrantError();
      return saved;
    });
  }
  discard(owner: string, revision: string): void {
    this.#run(() => this.#db.prepare('DELETE FROM provider_grants WHERE owner = ? AND revision = ?').run(owner, revision));
  }
  revokeOwner(owner: string): void {
    this.#run(() => {
      this.#db.exec('BEGIN IMMEDIATE');
      try {
        // Provider revocation and all dependent app approvals commit together.
        // A crash cannot leave an old PC approval able to revive after re-login.
        this.#db.prepare('DELETE FROM chat_sessions WHERE owner = ?').run(owner);
        this.#db.prepare('DELETE FROM broadcast_sessions WHERE owner = ?').run(owner);
        this.#db.prepare('DELETE FROM provider_grants WHERE owner = ?').run(owner);
        this.#db.exec('COMMIT');
      } catch (error) { this.#db.exec('ROLLBACK'); throw error; }
    });
  }
  close(): void { this.#key = undefined; }
}
