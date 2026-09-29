// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the real entry module with synthetic DOM/lifecycle events and time.
// This is not a browser, OBS scene transition, or exposure acceptance test.
import test from 'node:test';
import assert from 'node:assert/strict';

const visible = { type: 'ad-snapshot', version: 1, mode: 'test', state: 'visible', expiresInMs: 15000,
  campaign: { id: 'chatview-test', brand: 'ChatView', title: '시험', description: '지급 없음' } };
const flush = () => new Promise<void>(resolve => setImmediate(resolve));
let moduleNumber = 0;
async function fixture(path = `/public/ads/${'a'.repeat(32)}`) {
  const events = new EventTarget();
  const elements = new Map(['banner', 'brand', 'title', 'description'].map(id => [id, { hidden: true, textContent: '' }]));
  let timerId = 0, calls = 0, now = 0;
  const tasks = new Map<number, { fn: () => void; at: number; interval: number }>();
  const timer = (fn: () => void, delay: number, interval: number) => {
    tasks.set(++timerId, { fn, at: now + delay, interval }); return timerId;
  };
  const globals: Record<string, unknown> = {
    location: { pathname: path },
    document: { getElementById: (id: string) => elements.get(id)! },
    addEventListener: events.addEventListener.bind(events),
    performance: { now: () => now },
    fetch: async () => { calls++; return Response.json(visible); },
    setTimeout: (fn: () => void, delay: number) => timer(fn, delay, 0),
    clearTimeout: (id: number) => { tasks.delete(id); },
    setInterval: (fn: () => void, delay: number) => timer(fn, delay, delay),
    clearInterval: (id: number) => { tasks.delete(id); },
  };
  const previous = new Map(Object.keys(globals).map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  const restore = () => {
    for (const [key, descriptor] of previous) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else Reflect.deleteProperty(globalThis, key);
    }
  };
  for (const [key, value] of Object.entries(globals))
    Object.defineProperty(globalThis, key, { configurable: true, writable: true, value });
  try {
    const url = new URL('../web/ad-source.js', import.meta.url);
    url.searchParams.set('test', String(++moduleNumber));
    await import(url.href);
    await flush();
  } catch (error) { restore(); throw error; }
  return {
    get calls() { return calls; }, get timers() { return tasks.size; },
    get hidden() { return elements.get('banner')!.hidden; },
    event(type: string, detail?: unknown) {
      const event = new Event(type); Object.defineProperty(event, 'detail', { value: detail });
      events.dispatchEvent(event);
    },
    advance(ms: number) {
      const until = now + ms;
      for (;;) {
        const entry = [...tasks].filter(([, t]) => t.at <= until).sort((a, b) => a[1].at - b[1].at)[0];
        if (!entry) break;
        const [id, t] = entry; now = t.at;
        if (t.interval) t.at += t.interval; else tasks.delete(id);
        t.fn();
      }
      now = until;
    },
    close() { events.dispatchEvent(new Event('pagehide')); restore(); },
  };
}

test('public page restoration resumes a fresh read instead of remaining permanently hidden', async () => {
  const f = await fixture();
  try {
    assert.equal(f.hidden, false); assert.equal(f.calls, 1);
    f.event('pagehide');
    assert.equal(f.hidden, true); assert.equal(f.timers, 0);
    f.advance(20000); assert.equal(f.calls, 1);
    f.event('pageshow');
    assert.equal(f.hidden, true, 'restoration cannot reuse expired artwork');
    await flush();
    assert.equal(f.calls, 2, 'restoration starts a new public read');
    assert.equal(f.hidden, false);
  } finally { f.close(); }
});

test('duplicate show events and repeated hide/show cycles never multiply pollers', async () => {
  const f = await fixture();
  try {
    for (let i = 0; i < 3; i++) f.event('pageshow');
    await flush(); assert.equal(f.calls, 1); assert.equal(f.timers, 2);
    f.advance(2000); await flush(); assert.equal(f.calls, 2);
    for (let i = 0; i < 3; i++) {
      f.event('pagehide'); f.event('pagehide');
      assert.equal(f.hidden, true); assert.equal(f.timers, 0);
      f.event('pageshow'); f.event('pageshow'); await flush();
      assert.equal(f.calls, 3 + i); assert.equal(f.timers, 2);
    }
  } finally { f.close(); }
});

test('invalid public source paths do not start reads on load or restoration', async () => {
  const f = await fixture('/public/ads/not-a-source');
  try {
    f.event('pageshow'); await flush();
    assert.equal(f.calls, 0); assert.equal(f.timers, 0); assert.equal(f.hidden, true);
  } finally { f.close(); }
});


test('OBS hide clears retained artwork and pollers; show requires a new response', async () => {
  const f = await fixture();
  try {
    assert.equal(f.hidden, false); assert.equal(f.calls, 1);
    f.event('obsSourceVisibleChanged', { visible: false });
    assert.equal(f.hidden, true); assert.equal(f.timers, 0);
    f.advance(30000); await flush(); assert.equal(f.calls, 1);
    f.event('obsSourceVisibleChanged', { visible: true });
    assert.equal(f.hidden, true, 'no previous artwork is reused');
    await flush(); assert.equal(f.calls, 2); assert.equal(f.hidden, false);
    for (let i = 0; i < 5; i++) f.event('obsSourceVisibleChanged', { visible: true });
    await flush(); assert.equal(f.calls, 2); assert.equal(f.timers, 2);
  } finally { f.close(); }
});

test('OBS visibility and document restoration cannot resurrect each other', async () => {
  const f = await fixture();
  try {
    f.event('obsSourceVisibleChanged', { visible: false });
    f.event('pagehide'); f.event('pageshow'); await flush();
    assert.equal(f.hidden, true); assert.equal(f.calls, 1); assert.equal(f.timers, 0);
    f.event('pagehide'); f.event('obsSourceVisibleChanged', { visible: true });
    await flush(); assert.equal(f.calls, 1); assert.equal(f.timers, 0);
    f.event('pageshow'); await flush(); assert.equal(f.calls, 2); assert.equal(f.hidden, false);
  } finally { f.close(); }
});

test('inactive studio preview is not treated as invisible or as paid exposure', async () => {
  const f = await fixture();
  try {
    f.event('obsSourceActiveChanged', { active: false });
    assert.equal(f.hidden, false);
    f.advance(2000); await flush(); assert.equal(f.calls, 2);
    f.event('obsSourceVisibleChanged', { visible: false });
    f.event('obsSourceActiveChanged', { active: true });
    await flush(); assert.equal(f.hidden, true); assert.equal(f.timers, 0);
  } finally { f.close(); }
});

test('malformed visibility fails closed and OBS exit is terminal for this document', async () => {
  const f = await fixture();
  try {
    for (const detail of [undefined, null, true, {}, [], { visible: 'true' }, { visible: 1 }]) {
      f.event('obsSourceVisibleChanged', detail);
      assert.equal(f.hidden, true); assert.equal(f.timers, 0);
      f.event('obsSourceVisibleChanged', { visible: true }); await flush();
      assert.equal(f.hidden, false);
    }
    f.event('obsExit'); const calls = f.calls;
    f.event('obsSourceVisibleChanged', { visible: true });
    f.event('pagehide'); f.event('pageshow');
    f.advance(30000); await flush();
    assert.equal(f.hidden, true); assert.equal(f.calls, calls); assert.equal(f.timers, 0);
  } finally { f.close(); }
});
