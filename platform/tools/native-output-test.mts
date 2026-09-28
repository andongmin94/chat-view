// SPDX-License-Identifier: GPL-2.0-or-later
// Actual native sender/receiver, IPC, HTTP/SQLite/gateway; synthetic OBS sample
// and provider/chat. No live broadcasting, TLS deployment or capture claim.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import type { ServerResponse } from 'node:http';
import { WebSocket } from 'ws';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { BroadcastOutput } from '../server/chat/broadcast-output.mts';
import { fixture } from '../tests/fixtures/service.mts';

const executable = process.argv[2]; assert(executable, 'native executable required');
let offset = 0, hanging = false, attempts = 0, atExpiry = 0, finished = false;
const blocked = new Set<ServerResponse>();
const f = await fixture({ createDisplay: (a, o, s) => new DisplayGateway(a, o, s,
  new BroadcastOutput(() => performance.now() + offset)) });
const original = f.app.handle.bind(f.app);
f.app.handle = async (req, res) => {
  if (req.url === '/broadcast/output') {
    attempts++;
    if (hanging) { req.resume(); blocked.add(res); res.once('close', () => blocked.delete(res)); return; }
  }
  await original(req, res);
};
const game = await f.connect('alice', 'gaming'), stream = await f.connect('alice', 'streaming');
const other = await f.connect('bob', 'gaming');
let bobFrames = 0;
const bob = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
  headers: { Authorization: `Bearer ${other.lease.token}` }, handshakeTimeout: 2000,
});
bob.on('message', data => {
  const frame = JSON.parse(String(data));
  assert.deepEqual(frame.connection.output, { state: 'unknown' }); bobFrames++;
});
await once(bob, 'open');
const child = spawn(executable, [], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: false });
const exited = once(child, 'exit');
child.stdin.write(`${f.origin}\n${game.lease.sessionToken}\n${stream.lease.sessionToken}\n`);
let lines = 0, errors = '';
const input = createInterface({ input: child.stdout });
input.on('line', line => {
  if (!['hang', 'resume', 'expire', 'stale-check', 'done'].includes(line)) return;
  lines++;
  if (line === 'hang') hanging = true;
  if (line === 'resume') { hanging = false; assert(attempts > 0); for (const res of blocked) res.destroy(); }
  if (line === 'expire') { atExpiry = attempts; offset += 16000; }
  if (line === 'stale-check') assert.equal(attempts, atExpiry, 'stale local samples must not produce fresh server reports');
  if (line === 'done') {
    assert.equal(f.store.find(stream.lease.sessionToken), undefined);
    assert.ok(f.store.find(game.lease.sessionToken)); assert.ok(f.store.find(other.lease.sessionToken));
    assert.equal(bob.readyState, WebSocket.OPEN); assert(bobFrames >= 2);
    finished = true;
  }
  child.stdin.write(line + '\n');
});
child.stderr.on('data', chunk => { errors = (errors + String(chunk)).slice(-4096); });
let sequence = 0;
const chat = setInterval(() => {
  f.chats.get('alice')!.publish(`independent chat ${++sequence}`);
  f.chats.get('bob')!.publish('isolated account');
}, 150);
const deadline = setTimeout(() => child.kill(), 80000);
try {
  const [code] = await exited;
  assert.equal(code, 0, errors); assert(finished && lines === 5);
  const replacement = await f.start('streaming');
  assert.ok((await f.approve(replacement, 'streaming', stream.b)).outputToken);
  console.log('Native OBS-output observation/IPC/report/connection UI flow passed');
} finally {
  clearTimeout(deadline); clearInterval(chat); input.close(); child.kill(); bob.terminate();
  for (const res of blocked) res.destroy(); await f.close();
}
