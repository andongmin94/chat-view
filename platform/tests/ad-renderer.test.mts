// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { adSnapshot, paintAd, connectAd } from '../web/ad-renderer.js';

const visible = () => ({ type: 'ad-snapshot', version: 1, mode: 'test', state: 'visible', expiresInMs: 15000,
  campaign: { id: 'chatview-test', brand: 'ChatView', title: '테스트 😀 <img onerror=evil()>', description: '지급 없음' } });
const hidden = () => ({ type: 'ad-snapshot', version: 1, mode: 'test', state: 'hidden', expiresInMs: 0 });
function document() {
  const elements = new Map(['banner', 'brand', 'title', 'description'].map(id => [id, { hidden: false, textContent: '' }]));
  return { getElementById: (id: string) => elements.get(id)! };
}
function clock() {
  let now = 0, id = 0;
  const tasks = new Map<number, { at: number; interval: number; fn: () => void }>();
  const add = (fn: () => void, delay: number, interval: number) => { tasks.set(++id, { at: now + delay, interval, fn }); return id; };
  return { performance: { now: () => now },
    setTimeout: (fn: () => void, delay: number) => add(fn, delay, 0),
    clearTimeout: (id: number) => tasks.delete(id),
    setInterval: (fn: () => void, delay: number) => add(fn, delay, delay),
    clearInterval: (id: number) => tasks.delete(id),
    advance(milliseconds: number) {
      const end = now + milliseconds;
      for (;;) {
        const entry = [...tasks].filter(([, task]) => task.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
        if (!entry) break;
        const [key, task] = entry; now = task.at;
        if (task.interval) task.at += task.interval; else tasks.delete(key);
        task.fn();
      }
      now = end;
    }, get pending() { return tasks.size; },
  };
}
const flush = () => new Promise<void>(resolve => setImmediate(resolve));

test('only bounded test snapshots render; advertiser text is inert and hidden state erases it', () => {
  const d = document(); paintAd(d, adSnapshot(visible()));
  assert.equal(d.getElementById('banner').hidden, false);
  assert.equal(d.getElementById('title').textContent, visible().campaign.title);
  paintAd(d, adSnapshot(hidden()));
  assert.equal(d.getElementById('banner').hidden, true); assert.equal(d.getElementById('title').textContent, '');
  for (const input of [null, {}, { ...visible(), mode: 'paid' }, { ...visible(), expiresInMs: 15001 },
    { ...visible(), expiresInMs: -1 }, { ...visible(), expiresInMs: 1.5 }, { ...visible(), script: 'evil' },
    { ...visible(), campaign: { ...visible().campaign, trackingUrl: 'evil' } },
    { ...visible(), campaign: { ...visible().campaign, title: 'x'.repeat(101) } },
    { ...visible(), campaign: { ...visible().campaign, title: '\0' } }, { ...hidden(), expiresInMs: 1 }])
    assert.throws(() => adSnapshot(input));
});

test('read-only polling omits cookies and credentials and stop hides the actual elements', async () => {
  const d = document(), c = clock(); let count = 0;
  const stop = connectAd(d, 'a'.repeat(32), async (path: string, init: RequestInit) => {
    count++; assert.equal(path, `/public/ads/${'a'.repeat(32)}/state`);
    assert.equal(init.credentials, 'omit'); assert.equal(init.cache, 'no-store'); assert.equal(init.redirect, 'error');
    assert.deepEqual(init.headers, { Accept: 'application/json' });
    return Response.json(count === 1 ? visible() : hidden());
  }, c);
  assert.equal(d.getElementById('banner').hidden, true, 'initially transparent');
  await flush(); assert.equal(d.getElementById('banner').hidden, false);
  c.advance(2000); await flush(); assert.equal(d.getElementById('banner').hidden, true);
  stop(); stop(); assert.equal(c.pending, 0);
});

test('a stalled fetch cannot keep an old ad alive or grow concurrent requests', async () => {
  const d = document(), c = clock(); let calls = 0;
  const stop = connectAd(d, 'b'.repeat(32), async () => {
    calls++; if (calls === 1) return Response.json(visible());
    return await new Promise<Response>(() => {}); // deliberately ignores abort
  }, c);
  await flush(); assert.equal(d.getElementById('banner').hidden, false);
  c.advance(14900); await flush(); assert.equal(d.getElementById('banner').hidden, false);
  c.advance(100); assert.equal(d.getElementById('banner').hidden, true);
  c.advance(60000); assert.equal(calls, 2, 'at most one pending request');
  stop(); assert.equal(c.pending, 0);
});

test('slow responses do not extend expiry and stopped pages ignore late completion', async () => {
  const d = document(), c = clock(); let finish!: (response: Response) => void;
  const stop = connectAd(d, 'c'.repeat(32), () => new Promise<Response>(yes => { finish = yes; }), c);
  c.advance(1000); finish(Response.json({ ...visible(), expiresInMs: 1500 })); await flush();
  assert.equal(d.getElementById('banner').hidden, false);
  c.advance(500); assert.equal(d.getElementById('banner').hidden, true);
  stop();
  const d2 = document(), c2 = clock();
  const stop2 = connectAd(d2, 'd'.repeat(32), () => new Promise<Response>(yes => { finish = yes; }), c2);
  stop2(); finish(Response.json(visible())); await flush();
  assert.equal(d2.getElementById('banner').hidden, true); assert.equal(c2.pending, 0);
});

test('HTTP failures, bad MIME, oversized payloads, invalid UTF-8 and bad snapshots remain transparent', async () => {
  for (const response of [new Response('{}', { status: 503 }), new Response(JSON.stringify(visible())),
    new Response('x'.repeat(2049), { headers: { 'Content-Type': 'application/json' } }),
    new Response(new Uint8Array([0xff]), { headers: { 'Content-Type': 'application/json' } }),
    Response.json({ ...visible(), mode: 'paid' })]) {
    const d = document(), c = clock();
    const stop = connectAd(d, 'e'.repeat(32), async () => response, c);
    await flush(); assert.equal(d.getElementById('banner').hidden, true); stop();
  }
});
