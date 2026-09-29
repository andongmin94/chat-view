// SPDX-License-Identifier: GPL-2.0-or-later
// Actual public HTTP/SQLite/ws -> actual WebView2 document, not OBS/paid exposure.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { EventEmitter, once } from 'node:events';
import { WebSocket } from 'ws';
import { fixture } from '../tests/fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const executable = process.argv[2]; assert(executable, 'browser test executable required');
const f = await fixture({ createDisplay: (access, origin, get) => new DisplayGateway(access, origin, get) });
const child = spawn(executable, [], { stdio: ['pipe', 'pipe', 'pipe'] });
const exited = once(child, 'exit');
const events = new EventEmitter(), lines = new Set<string>();
let buffer = '', errors = '', publicRequests = 0, stalled = false;
const handle = f.app.handle.bind(f.app);
f.app.handle = async (request, response) => {
  if (request.url?.startsWith('/public/ads/')) {
    assert.equal(request.headers.authorization, undefined);
    assert.equal(request.headers.cookie, undefined);
    publicRequests++;
    if (stalled && request.url.endsWith('/state')) return; // explicit fixture network loss
  }
  return handle(request, response);
};
child.stdout.setEncoding('utf8');
child.stdout.on('data', (text: string) => {
  buffer += text;
  for (;;) {
    const end = buffer.indexOf('\n'); if (end < 0) break;
    const line = buffer.slice(0, end).trim(); buffer = buffer.slice(end + 1);
    lines.add(line); events.emit('line');
  }
  assert(buffer.length < 4096);
});
child.stderr.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
child.once('exit', () => events.emit('line'));
const wait = async (line: string) => {
  const signal = AbortSignal.timeout(line === 'expired' ? 20000 : 15000);
  while (!lines.has(line)) {
    assert.equal(child.exitCode, null, errors || 'browser child exited early');
    await once(events, 'line', { signal });
  }
};
const send = (line: string) => child.stdin.write(line + '\n');
const timeout = setTimeout(() => child.kill(), 70000);
try {
  const alice = await f.connect('alice-private', 'streaming');
  await f.page('/campaigns', alice.b);
  const select = async () => {
    const response = await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, alice.b);
    assert.equal(response.status, 303);
  };
  await select();
  const html = await f.page('/campaigns', alice.b);
  const id = /\/public\/ads\/([a-f0-9]{32})/u.exec(html)?.[1]; assert(id);
  const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${alice.lease.token}` }, handshakeTimeout: 3000,
  });
  peer.on('error', () => {}); await once(peer, 'open');
  send(`${f.origin}/public/ads/${id}`);
  await wait('loaded');
  const report = async (sequence: number) => {
    const response = await f.request('/broadcast/output', { method: 'POST', headers: {
      Authorization: `ChatView-Output ${alice.lease.outputToken}`, 'Content-Type': 'application/json',
    }, body: JSON.stringify({ sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
    assert.equal(response.status, 200);
  };
  await report(1); send('reported'); await wait('visible');
  assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
  send('stopped'); await wait('hidden');
  await select(); await report(2); send('selected'); await wait('visible-again');
  stalled = true; send('outage'); await wait('expired');
  assert(publicRequests >= 6);
  const message = once(peer, 'message');
  f.chats.get('alice-private')!.publish('private chat unaffected by public renderer outage');
  await message; assert.equal(peer.readyState, WebSocket.OPEN);
  send('finish'); child.stdin.end();
  const [code] = await exited; assert.equal(code, 0, errors);
  console.log('Actual public ad browser rendering, stop and fail-closed expiry passed');
} finally { clearTimeout(timeout); child.kill(); await f.close(); }
