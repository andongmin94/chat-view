// SPDX-License-Identifier: GPL-2.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { ChzzkApi, ChzzkError } from '../server/chzzk/api.mts';
import type { LivePage } from '../server/chzzk/api.mts';
import { AudienceSampler, audienceSamplingEnabled, AUDIENCE_INTERVAL_MS, AUDIENCE_MAX_AGE_MS,
  AUDIENCE_PAGE_LIMIT, AUDIENCE_BACKOFF_MS } from '../server/chzzk/audience.mts';
const credentials = { clientId: 'fixture-client', clientSecret: 'fixture-secret' };
const live = (channelId = 'alice', concurrentUserCount = 12, liveId = 123) => ({ channelId, concurrentUserCount, liveId });
const ok = (content: unknown) => Response.json({ code: 200, content });
function deferred<T>() { let resolve!: (value: T) => void; const promise = new Promise<T>(yes => { resolve = yes; }); return { promise, resolve }; }

test('official live list uses Client authentication, fixed origin and only documented pagination', async () => {
  const calls: string[] = [];
  const api = new ChzzkApi(credentials, async (url, init) => {
    calls.push(url); const parsed = new URL(url), headers = new Headers(init.headers);
    assert.equal(parsed.origin, 'https://openapi.chzzk.naver.com');
    assert.equal(parsed.pathname, '/open/v1/lives'); assert.equal(parsed.searchParams.get('size'), '20');
    assert.deepEqual([...parsed.searchParams.keys()].sort(), ['next', 'size']);
    assert.equal(parsed.searchParams.get('next'), 'opaque+/= &page');
    assert.equal(headers.get('Client-Id'), credentials.clientId); assert.equal(headers.get('Client-Secret'), credentials.clientSecret);
    assert.equal(headers.get('Authorization'), null); assert.equal(headers.get('Cookie'), null);
    assert.equal(init.method, 'GET'); assert.equal(init.body, undefined); assert.equal(init.redirect, 'error');
    return ok({ data: [{ ...live('alice', 0), liveTitle: 'discard', channelImageUrl: 'discard' }], page: { next: null } });
  });
  assert.deepEqual(await api.listLives('opaque+/= &page'), { data: [live('alice', 0)] });
  assert.equal(calls.length, 1);
  for (const cursor of ['', '\nprivate', 'x'.repeat(2049)]) await assert.rejects(api.listLives(cursor), ChzzkError);
  assert.equal(calls.length, 1, 'bad input rejected before any credentials leave');
});

test('invalid and missing counts never become zero; only minimal bounded live records are returned', async () => {
  const bad = [undefined, null, '12', -1, 0.5, 0x80000000, Number.MAX_SAFE_INTEGER];
  for (const count of bad) {
    const api = new ChzzkApi(credentials, async () => ok({ data: [{ ...live(), concurrentUserCount: count }], page: {} }));
    await assert.rejects(api.listLives(), ChzzkError);
  }
  for (const content of [{ data: [live(), live()], page: {} }, { data: Array.from({length: 21}, (_, i) => live(String(i))), page: {} },
    { data: [live('', 1)], page: {} }, { data: [live('alice', 1, 0)], page: {} }, { data: [], page: { next: 12 } }, { data: [] }]) {
    const api = new ChzzkApi(credentials, async () => ok(content)); await assert.rejects(api.listLives(), ChzzkError);
  }
  const tooLarge = new ChzzkApi(credentials, async () => ok({ data: [], page: {}, unknown: 'x'.repeat(65536) }));
  await assert.rejects(tooLarge.listLives(), ChzzkError);
});

test('API errors are scrubbed, caller cancellation propagates and quota failure is not replayed', async () => {
  for (const status of [401, 403, 429, 500]) {
    let calls = 0;
    const api = new ChzzkApi(credentials, async () => { calls++; return new Response('private-body', { status }); });
    await assert.rejects(api.listLives(), (e: unknown) => e instanceof ChzzkError && e.status === status && !e.message.includes('private'));
    assert.equal(calls, 1);
  }
  const controller = new AbortController();
  const api = new ChzzkApi(credentials, async (_url, init) => {
    assert(init.signal); controller.abort(); assert.equal(init.signal.aborted, true); throw new Error('private-cause');
  });
  await assert.rejects(api.listLives(undefined, controller.signal), (e: unknown) => e instanceof ChzzkError && e.kind === 'transport');
});

function fixture() {
  let tick = 1000, now = 1790640000000, owners = ['alice', 'bob'], calls = 0;
  let fetchPage: (next?: string, signal?: AbortSignal) => Promise<LivePage> = async () => ({ data: [live(), live('bob', 5)] });
  const sampler = new AudienceSampler({ listLives: async (next, signal) => { calls++; return fetchPage(next, signal); } },
    () => owners, () => now, () => tick);
  return { sampler, get calls() { return calls; }, set(hook: typeof fetchPage) { fetchPage = hook; },
    owners(value: string[]) { owners = value; }, advance(ms: number) { tick += ms; now += ms; }, jump(ms: number) { now += ms; } };
}

test('all selected creators share one scan, first-party reads do not trigger it, and only requested owners are retained', async () => {
  const f = fixture(), pending = deferred<LivePage>();
  try {
    f.set(async next => next ? { data: [live('bob', 5)] } : pending.promise);
    assert.deepEqual(f.sampler.current('alice'), {state: 'unavailable', reason: 'pending'}); assert.equal(f.calls, 0);
    const first = f.sampler.refresh(), second = f.sampler.refresh(); assert.equal(first, second);
    pending.resolve({ data: [live(), live('unselected')], next: 'page-2' }); await first;
    assert.equal(f.calls, 2); assert.equal(f.sampler.current('alice').state, 'sampled');
    assert.equal(f.sampler.current('bob').state, 'sampled'); assert.equal(f.sampler.current('unselected').state, 'unavailable');
    for (let i = 0; i < 20; i++) { f.sampler.current('alice'); await f.sampler.refresh(); }
    assert.equal(f.calls, 2, 'not one scan per report, viewer, browser or creator');
    f.advance(AUDIENCE_INTERVAL_MS); f.owners([]); await f.sampler.refresh();
    assert.equal(f.calls, 2); assert.equal(f.sampler.current('alice').state, 'unavailable');
  } finally { f.sampler.close(); }
});

test('bounded scans distinguish unlisted, unscanned and explicit zero; no unofficial lookup fallback', async () => {
  const f = fixture();
  try {
    f.set(async () => ({ data: [live('alice', 0)] })); await f.sampler.refresh();
    const zero = f.sampler.current('alice'); assert(zero.state === 'sampled'); assert.equal(zero.viewers, 0);
    assert.deepEqual(f.sampler.current('bob'), { state: 'unavailable', reason: 'not-found' });
    f.advance(AUDIENCE_INTERVAL_MS); let page = 0;
    f.set(async () => ({ data: [], next: `page-${++page}` })); await f.sampler.refresh();
    assert.equal(page, AUDIENCE_PAGE_LIMIT); assert.deepEqual(f.sampler.current('alice'), { state: 'unavailable', reason: 'page-limit' });
    f.advance(AUDIENCE_INTERVAL_MS); f.set(async () => ({ data: [], next: 'loop' })); await f.sampler.refresh();
    assert.deepEqual(f.sampler.current('bob'), { state: 'unavailable', reason: 'provider-error' });
  } finally { f.sampler.close(); }
});

test('fresh polls preserve continuity; failure, expiry, changed live and clock jump do not bridge estimates', async () => {
  const f = fixture();
  const sample = () => { const s = f.sampler.current('alice'); assert(s.state === 'sampled'); return s; };
  try {
    await f.sampler.refresh(); const first = sample();
    f.advance(AUDIENCE_INTERVAL_MS); await f.sampler.refresh(); assert.equal(sample().continuity, first.continuity);
    f.advance(AUDIENCE_MAX_AGE_MS); assert.equal(f.sampler.current('alice').state, 'unavailable');
    await f.sampler.refresh(); const renewed = sample(); assert.notEqual(renewed.continuity, first.continuity);
    f.advance(AUDIENCE_INTERVAL_MS); f.set(async () => { throw new ChzzkError('http', 500); }); await f.sampler.refresh();
    assert.deepEqual(f.sampler.current('alice'), { state: 'unavailable', reason: 'provider-error' });
    f.advance(AUDIENCE_INTERVAL_MS); f.set(async () => ({ data: [live()] })); await f.sampler.refresh();
    const recovered = sample(); assert.notEqual(recovered.continuity, renewed.continuity);
    f.advance(AUDIENCE_INTERVAL_MS); f.set(async () => ({ data: [live('alice', 2, 999)] })); await f.sampler.refresh();
    assert.notEqual(sample().continuity, recovered.continuity);
    f.jump(-10000); assert.deepEqual(f.sampler.current('alice'), { state: 'unavailable', reason: 'stale' });
  } finally { f.sampler.close(); }
});

test('quota and permission failures back off without refreshing user grants', async () => {
  for (const status of [401, 403, 429]) {
    const f = fixture();
    try {
      f.set(async () => { throw new ChzzkError('http', status); }); await f.sampler.refresh();
      assert.equal(f.calls, 1);
      assert.deepEqual(f.sampler.current('alice'), {state:'unavailable', reason: status === 429 ? 'rate-limited' : 'not-authorized'});
      f.advance(AUDIENCE_BACKOFF_MS - 1); await f.sampler.refresh(); assert.equal(f.calls, 1);
      f.advance(1); await f.sampler.refresh(); assert.equal(f.calls, 2);
    } finally { f.sampler.close(); }
  }
});

test('revoked targets and closed service ignore in-flight results, even from a noncooperative fixture', async () => {
  const f = fixture(), pending = deferred<LivePage>();
  f.set(() => pending.promise); const task = f.sampler.refresh(); f.owners([]);
  pending.resolve({ data: [live()] }); await task; assert.equal(f.sampler.current('alice').state, 'unavailable');
  f.advance(AUDIENCE_INTERVAL_MS); f.owners(['alice']); const later = deferred<LivePage>();
  let signal: AbortSignal | undefined;
  f.set((_next, s) => { signal = s; return later.promise; }); const closing = f.sampler.refresh();
  f.sampler.close(); assert.equal(signal?.aborted, true); later.resolve({data:[live()]}); await closing;
  assert.deepEqual(f.sampler.current('alice'), {state:'unavailable',reason:'disabled'});
});

test('production collection requires explicit opt-in; disabled mode never queries targets or the provider', async () => {
  assert.equal(audienceSamplingEnabled(undefined), false); assert.equal(audienceSamplingEnabled('0'), false);
  assert.equal(audienceSamplingEnabled('1'), true);
  for (const bad of ['', 'true', 'yes', ' 1']) assert.throws(() => audienceSamplingEnabled(bad));
  const sampler = new AudienceSampler(undefined, () => { throw new Error('must not run'); });
  await sampler.refresh(); assert.deepEqual(sampler.current('alice'), {state:'unavailable',reason:'disabled'}); sampler.close();
});


test('explicit creator retirement fences a late page even after the same channel reauthorizes', async () => {
  const f = fixture(), pending = deferred<LivePage>();
  try {
    f.set(() => pending.promise); const task = f.sampler.refresh();
    f.sampler.clear('alice'); pending.resolve({data:[live(),live('bob')]}); await task;
    assert.equal(f.sampler.current('alice').state,'unavailable');
    assert.equal(f.sampler.current('bob').state,'sampled');
  } finally { f.sampler.close(); }
});
