// SPDX-License-Identifier: GPL-2.0-or-later
// Parent-controlled fixture only. Real service HTTP/SQLite/ws, synthetic CHZZK.
// No debug HTTP routes; commands and redacted results use inherited stdio.
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { WebSocket } from 'ws';
import { fixture } from '../tests/fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const realNow = Date.now.bind(Date);
let offset = 0;
Date.now = () => realNow() + offset; // Expire browser state, not native/provider grants.
const f = await fixture({ now: realNow,
  createDisplay: (access, origin, snapshot) => new DisplayGateway(access, origin, snapshot) });
let peer: WebSocket | undefined;
let pending: Awaited<ReturnType<typeof f.start>> | undefined;
const metadata: { path: string; method: string; site: string; mode: string; dest: string;
  cookie: boolean; authorization: boolean; status: number }[] = [];
const handle = f.app.handle.bind(f.app);
f.app.handle = async (request, response) => {
  // Paths and presence bits only; never retain cookie/state/code/CSRF or bodies.
  const path = new URL(request.url!, f.origin).pathname;
  const sample = { path: path.startsWith('/login/') ? '/login/:id' : path,
    method: request.method ?? '', site: String(request.headers['sec-fetch-site'] ?? ''),
    mode: String(request.headers['sec-fetch-mode'] ?? ''), dest: String(request.headers['sec-fetch-dest'] ?? ''),
    cookie: !!request.headers.cookie, authorization: !!request.headers.authorization, status: 0 };
  response.once('finish', () => {
    sample.status = response.statusCode;
    metadata.push(sample); if (metadata.length > 500) metadata.shift();
  });
  await handle(request, response);
};
const reply = (data: unknown) => process.stdout.write(JSON.stringify(data) + '\n');
const deadline = setTimeout(() => process.exit(2), 180_000);
try {
  const alice = await f.connect('alice', 'streaming'), bob = await f.connect('bob', 'gaming');
  await f.page('/campaigns', alice.b);
  assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, alice.b)).status, 303);
  const snapshot = () => JSON.stringify({
    alice: f.store.connections('alice'), bob: f.store.connections('bob'),
    selections: f.store.database.prepare('SELECT * FROM ad_selections').all(),
    aliceGrant: f.grants.load('alice'), bobGrant: f.grants.load('bob'),
  });
  const before = snapshot(), starts = f.startedWith.length;
  peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events',
    { headers: { Authorization: `Bearer ${alice.lease.token}` } });
  peer.on('error', () => {}); await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
  reply({ ready: true, origin: f.origin });
  const input = createInterface({ input: process.stdin });
  let sequence = 0;
  for await (const line of input) {
    assert(line.length <= 512, 'bounded parent command');
    const command = JSON.parse(line) as { op: string };
    if (command.op === 'close') { reply({ ok: true }); break; }
    if (command.op === 'expire-browser') { offset += 3_600_001; reply({ ok: true }); continue; }
    if (command.op === 'expire-confirmation') { offset += 300_001; reply({ ok: true }); continue; }
    if (command.op === 'metadata') { reply({ metadata }); continue; }
    if (command.op === 'native-start') {
      assert(!pending); pending = await f.start('streaming');
      reply({ path: pending.verificationPath }); continue;
    }
    if (command.op === 'native-collect') {
      assert(pending);
      const response = await f.native(`/display/login/${pending.id}`, 'ChatView-Login', pending.verifier);
      assert.equal(response.status, 200);
      const result = await response.json() as { status: string; lease: { sessionToken: string } };
      assert.equal(result.status, 'approved');
      const session = f.store.find(result.lease.sessionToken);
      assert(session?.owner === 'charlie' && session.membership?.role === 'streaming');
      reply({ ok: true }); continue;
    }
    assert.equal(command.op, 'check');
    assert(snapshot() === before, 'browser actions must preserve native approvals, grants and selection');
    assert.equal(f.startedWith.filter(token => !token.includes(':charlie:')).length, starts);
    assert.equal(f.refreshCalls, 0);
    assert.equal(peer.readyState, WebSocket.OPEN);
    const marker = `browser-fixture-${++sequence}`;
    f.chats.get('alice')!.publish(marker);
    for (;;) {
      const [frame] = await once(peer, 'message', { signal: AbortSignal.timeout(3000) });
      if (String(frame).includes(marker)) break;
    }
    reply({ ok: true });
  }
} catch {
  // Assertion objects can contain fixture credentials. Do not export them.
  reply({ error: 'Management browser fixture invariant failed' });
  process.exitCode = 1;
} finally {
  clearTimeout(deadline); peer?.terminate(); await f.close(); Date.now = realNow;
}
