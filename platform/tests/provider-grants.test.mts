// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { SessionStore } from '../server/chat/session-store.mts';
import { ProviderGrants, ProviderGrantError, readProviderKey } from '../server/service/provider-grants.mts';
import type { ProviderGrant } from '../server/service/provider-grants.mts';
const sample = (owner = 'alice'): ProviderGrant => ({
  channel: { channelId: owner, channelName: `채널 <${owner}>` },
  tokens: { accessToken: `private-access-${owner}-${'a'.repeat(64)}`,
    refreshToken: `private-refresh-${owner}-${'b'.repeat(64)}`, expiresIn: 3600 },
  expiresAt: 1800000000000,
});
function fixture() {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-grants-')), path = join(dir, 'sessions.sqlite');
  const key = randomBytes(32);
  let sessions = new SessionStore(path), grants = new ProviderGrants(sessions.database, key);
  return { path, key, get sessions() { return sessions; }, get grants() { return grants; },
    restart() { grants.close(); sessions.close(); sessions = new SessionStore(path); grants = new ProviderGrants(sessions.database, key); },
    close() { grants.close(); sessions.close(); key.fill(0); rmSync(dir, { recursive: true, force: true }); } };
}

test('encrypted grants and absolute expiry survive restart without plaintext provider tokens or key', () => {
  const f = fixture();
  try {
    const expected = f.grants.save(sample());
    const row = f.sessions.database.prepare('SELECT sealed FROM provider_grants').get()!;
    f.restart();
    assert.deepEqual(f.grants.load('alice'), expected);
    for (const bytes of [f.key, Buffer.from(expected.tokens.accessToken), Buffer.from(expected.tokens.refreshToken)])
      assert.equal(readFileSync(f.path).includes(bytes), false);
    const newer = f.grants.save(sample());
    const next = f.sessions.database.prepare('SELECT sealed FROM provider_grants').get()!;
    assert.notDeepEqual(row.sealed, next.sealed);
    assert.notEqual(expected.revision, newer.revision);
  } finally { f.close(); }
});

test('a wrong/missing key fails closed without erasing approvals or replacing the key check', () => {
  const f = fixture();
  try {
    const saved = f.grants.save(sample()), session = f.sessions.create('alice', 'gaming');
    assert.throws(() => new ProviderGrants(f.sessions.database, new Uint8Array()), ProviderGrantError);
    assert.throws(() => new ProviderGrants(f.sessions.database, randomBytes(32)), ProviderGrantError);
    f.restart();
    assert.deepEqual(f.grants.load('alice'), saved); assert.ok(f.sessions.find(session.token));
  } finally { f.close(); }
});

test('ciphertext tampering, wrong owner and revision substitution never decrypt a grant', () => {
  const f = fixture();
  try {
    const alice = f.grants.save(sample()), bob = f.grants.save(sample('bob'));
    const db = f.sessions.database;
    const sealed = db.prepare("SELECT sealed FROM provider_grants WHERE owner = 'alice'").get()!.sealed as Uint8Array;
    db.prepare("UPDATE provider_grants SET sealed = ? WHERE owner = 'bob'").run(sealed);
    assert.throws(() => f.grants.load('bob'), ProviderGrantError);
    assert.deepEqual(f.grants.load('alice'), alice);
    const broken = Buffer.from(sealed); broken[broken.length - 1]! ^= 1;
    db.prepare("UPDATE provider_grants SET sealed = ? WHERE owner = 'alice'").run(broken);
    assert.throws(() => f.grants.load('alice'), ProviderGrantError);
    db.prepare("UPDATE provider_grants SET sealed = ?, revision = ? WHERE owner = 'alice'").run(sealed, bob.revision);
    assert.throws(() => f.grants.load('alice'), ProviderGrantError);
  } finally { f.close(); }
});

test('a committed refresh claim is single-use and survives an ungraceful child-process exit', () => {
  const f = fixture();
  try {
    const saved = f.grants.save(sample());
    // Only synthetic key material enters the child's stdin; no CLI credentials.
    const child = spawnSync(process.execPath, ['--experimental-strip-types', '--input-type=module', '-e', `
      import { readFileSync } from 'node:fs';
      import { SessionStore } from ${JSON.stringify(new URL('../server/chat/session-store.mts', import.meta.url).href)};
      import { ProviderGrants } from ${JSON.stringify(new URL('../server/service/provider-grants.mts', import.meta.url).href)};
      const input = JSON.parse(readFileSync(0, 'utf8'));
      const sessions = new SessionStore(input.path);
      const grants = new ProviderGrants(sessions.database, Buffer.from(input.key, 'hex'));
      if (!grants.claimRefresh('alice', input.revision)) process.exit(2);
      process.exit(23); // No store.close(), process shutdown handler or refresh result.
    `], { input: JSON.stringify({ path: f.path, key: f.key.toString('hex'), revision: saved.revision }), timeout: 10000 });
    assert.equal(child.error, undefined); assert.equal(child.status, 23);
    f.restart();
    assert.equal(f.grants.load('alice'), undefined);
    assert.equal(f.grants.claimRefresh('alice', saved.revision), false);
    assert.equal(f.sessions.database.prepare("SELECT sealed FROM provider_grants WHERE owner = 'alice'").get()!.sealed, null);
  } finally { f.close(); }
});

test('refresh completion is durable but cannot overwrite reauthorization or revive a revoked owner', () => {
  const f = fixture();
  try {
    const first = f.grants.save(sample());
    assert.equal(f.grants.claimRefresh('alice', first.revision), true);
    assert.equal(f.grants.claimRefresh('alice', first.revision), false);
    const rotated = f.grants.finishRefresh({ ...sample(), expiresAt: first.expiresAt + 1000 }, first.revision);
    f.restart(); assert.deepEqual(f.grants.load('alice'), rotated);
    f.grants.claimRefresh('alice', rotated.revision);
    const reauthorized = f.grants.save(sample());
    assert.throws(() => f.grants.finishRefresh(sample(), rotated.revision), ProviderGrantError);
    f.grants.discard('alice', rotated.revision);
    assert.deepEqual(f.grants.load('alice'), reauthorized);
    f.grants.claimRefresh('alice', reauthorized.revision);
    f.grants.revokeOwner('alice');
    assert.throws(() => f.grants.finishRefresh(sample(), reauthorized.revision), ProviderGrantError);
    f.restart(); assert.equal(f.grants.load('alice'), undefined);
  } finally { f.close(); }
});

test('provider and both PC approvals revoke atomically, while another creator remains usable', () => {
  const f = fixture();
  try {
    f.grants.save(sample()); f.grants.save(sample('bob'));
    const gaming = f.sessions.create('alice', 'gaming'), streaming = f.sessions.create('alice', 'streaming');
    const bob = f.sessions.create('bob', 'streaming');
    f.sessions.database.exec(`CREATE TRIGGER fail_revoke BEFORE DELETE ON provider_grants
      WHEN OLD.owner = 'alice' BEGIN SELECT RAISE(ABORT, 'simulated disk failure'); END;`);
    assert.throws(() => f.grants.revokeOwner('alice'), ProviderGrantError);
    assert.ok(f.sessions.find(gaming.token)); assert.ok(f.sessions.find(streaming.token));
    assert.ok(f.grants.load('alice'));
    f.sessions.database.exec('DROP TRIGGER fail_revoke');
    f.grants.revokeOwner('alice'); f.restart();
    assert.equal(f.sessions.find(gaming.token), undefined); assert.equal(f.sessions.find(streaming.token), undefined);
    assert.equal(f.grants.load('alice'), undefined); assert.ok(f.grants.load('bob')); assert.ok(f.sessions.find(bob.token));
    f.grants.save(sample());
    assert.equal(f.sessions.find(gaming.token), undefined);
  } finally { f.close(); }
});

test('malformed grants and a closed vault fail without leaking input or SQL diagnostics', () => {
  const f = fixture();
  try {
    const forbidden = 'credential-never-log-this';
    for (const invalid of [null, { ...sample(), expiresAt: Infinity },
      { ...sample(), tokens: { accessToken: forbidden, refreshToken: 'bad token', expiresIn: 0 } }]) {
      assert.throws(() => f.grants.save(invalid as ProviderGrant), e =>
        e instanceof ProviderGrantError && e.message === 'ChatView provider storage unavailable' && !('cause' in e));
    }
    f.grants.close();
    assert.throws(() => f.grants.load('alice'), ProviderGrantError);
  } finally { f.close(); }
});


test('server key files are bounded, explicit and permission checked without exposing paths', () => {
  const dir = mkdtempSync(join(tmpdir(), 'chatview-key-')), path = join(dir, 'provider.key');
  try {
    const key = randomBytes(32);
    writeFileSync(path, key, { mode: 0o600 });
    const loaded = readProviderKey(path); assert.deepEqual(loaded, key); loaded.fill(0);
    const rejected = (value: string) => assert.throws(() => readProviderKey(value), e =>
      e instanceof ProviderGrantError && e.message === 'ChatView provider storage unavailable' && !('cause' in e));
    rejected('relative.key'); rejected(join(dir, 'missing.key')); rejected(dir);
    for (const size of [0, 31, 33, 1024]) { writeFileSync(path, Buffer.alloc(size)); rejected(path); }
    writeFileSync(path, key);
    // POSIX mode bits are not Windows ACLs; Windows key permissions are an
    // operator prerequisite, not a falsely passing Unix permission test.
    if (process.platform !== 'win32') { chmodSync(path, 0o644); rejected(path); }
    key.fill(0);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
