// SPDX-License-Identifier: GPL-2.0-or-later
const MAX_AGE_MS = 15000;
const MAX_BYTES = 2048;

export function adSnapshot(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      value.type !== 'ad-snapshot' || value.version !== 1 || value.mode !== 'test')
    throw new Error('Invalid ad snapshot');
  if (value.state === 'hidden' && value.expiresInMs === 0 && Object.keys(value).length === 5)
    return { state: 'hidden', expiresInMs: 0 };
  const campaign = value.campaign;
  if (value.state !== 'visible' || Object.keys(value).length !== 6 ||
      !Number.isSafeInteger(value.expiresInMs) || value.expiresInMs <= 0 || value.expiresInMs > MAX_AGE_MS ||
      !campaign || typeof campaign !== 'object' || Array.isArray(campaign) || Object.keys(campaign).length !== 4)
    throw new Error('Invalid ad snapshot');
  const text = (value, max) => {
    if (typeof value !== 'string' || !value.trim() || value.length > max || /[\x00-\x1f\x7f]/u.test(value))
      throw new Error('Invalid ad text');
    return value;
  };
  return { state: 'visible', expiresInMs: value.expiresInMs, campaign: {
    id: text(campaign.id, 64), brand: text(campaign.brand, 60),
    title: text(campaign.title, 100), description: text(campaign.description, 160),
  } };
}

export function paintAd(document, snapshot) {
  const banner = document.getElementById('banner');
  banner.hidden = true;
  for (const name of ['brand', 'title', 'description'])
    document.getElementById(name).textContent = snapshot.state === 'visible' ? snapshot.campaign[name] : '';
  banner.hidden = snapshot.state !== 'visible';
}

async function boundedJson(response) {
  if (!response.ok || !/^application\/json(?:;|$)/iu.test(response.headers.get('content-type') ?? '') || !response.body)
    throw new Error('Ad unavailable');
  const reader = response.body.getReader();
  let total = 0;
  const chunks = [];
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      total += value.byteLength;
      if (total > MAX_BYTES) throw new Error('Ad response too large');
      chunks.push(value);
    }
    const bytes = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  } catch (error) {
    try { await reader.cancel(); } catch { /* No response body is logged. */ }
    throw error;
  } finally { reader.releaseLock(); }
}

// A bounded independent deadline hides stale content even while fetch is stuck.
// No cookies, private display credentials, OBS API, analytics or remote media.
/**
 * @template Timer
 * @param {{getElementById(id: string): {hidden: boolean, textContent: string}}} document
 * @param {string} sourceId
 * @param {(path: string, options: RequestInit) => Promise<Response>} fetcher
 * @param {{performance: {now(): number},
 *   setTimeout(fn: () => void, delay: number): Timer, clearTimeout(id: Timer): void,
 *   setInterval(fn: () => void, delay: number): Timer, clearInterval(id: Timer): void}} clock
 */
export function connectAd(document, sourceId, fetcher = globalThis.fetch, clock = globalThis) {
  if (!/^[a-f0-9]{32}$/u.test(sourceId)) throw new Error('Invalid public source');
  let closed = false, expires = 0, pending;
  const hide = () => { expires = 0; paintAd(document, { state: 'hidden' }); };
  hide();
  const poll = async () => {
    if (closed || pending) return;
    const controller = new AbortController();
    pending = controller;
    const began = clock.performance.now();
    const timeout = clock.setTimeout(() => controller.abort(), 5000);
    try {
      const response = await fetcher(`/public/ads/${sourceId}/state`, {
        credentials: 'omit', cache: 'no-store', redirect: 'error', signal: controller.signal,
        headers: { Accept: 'application/json' },
      });
      const snapshot = adSnapshot(await boundedJson(response));
      if (closed || controller.signal.aborted) return;
      // Time spent in transit never extends the server's remaining grant.
      if (snapshot.state === 'hidden' || began + snapshot.expiresInMs <= clock.performance.now()) { hide(); return; }
      expires = began + snapshot.expiresInMs;
      paintAd(document, snapshot);
    } catch { if (!closed) hide(); }
    finally { clock.clearTimeout(timeout); if (pending === controller) pending = undefined; }
  };
  const watchdog = clock.setInterval(() => {
    if (expires && clock.performance.now() >= expires) hide();
  }, 100);
  const polling = clock.setInterval(() => { void poll(); }, 2000);
  void poll();
  return () => {
    if (closed) return;
    closed = true;
    clock.clearInterval(watchdog); clock.clearInterval(polling);
    pending?.abort(); hide();
  };
}
