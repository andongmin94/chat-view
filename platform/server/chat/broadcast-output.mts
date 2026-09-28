// SPDX-License-Identifier: GPL-2.0-or-later
import type { IncomingMessage } from 'node:http';
import { DisplayAccessError } from './display-access.mts';

export const OUTPUT_LIFETIME_MS = 15_000;
export const OUTPUT_SAMPLE_MAX_AGE_MS = 3000;
export const UNKNOWN_OUTPUT = Object.freeze({ state: 'unknown' as const });
export type OutputView = typeof UNKNOWN_OUTPUT | Readonly<{
  state: 'reported'; streaming: boolean; recording: boolean; expiresInMs: number;
}>;
export type OutputReport = Readonly<{
  sequence: number; streaming: boolean; recording: boolean; sampleAgeMs: number;
}>;
export function parseOutputReport(value: unknown): OutputReport {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new DisplayAccessError(400);
  const v = value as Record<string, unknown>;
  if (Object.keys(v).length !== 4 || !Number.isSafeInteger(v.sequence) ||
      (v.sequence as number) < 1 || (v.sequence as number) > 0xffffffff ||
      typeof v.streaming !== 'boolean' || typeof v.recording !== 'boolean' ||
      !Number.isSafeInteger(v.sampleAgeMs) || (v.sampleAgeMs as number) < 0 ||
      (v.sampleAgeMs as number) >= OUTPUT_SAMPLE_MAX_AGE_MS) throw new DisplayAccessError(400);
  return { sequence: v.sequence as number, streaming: v.streaming,
    recording: v.recording, sampleAgeMs: v.sampleAgeMs as number };
}
// Only this tiny JSON schema is accepted. No owner/session selector, capture
// claim, source metadata, scene name, audience count or accounting data.
export async function readOutputReport(request: IncomingMessage): Promise<OutputReport> {
  const length = request.headers['content-length'];
  if (!/^application\/json(?:;\s*charset=utf-8)?$/iu.test(request.headers['content-type'] ?? '') ||
      request.headers['transfer-encoding'] !== undefined || typeof length !== 'string' ||
      !/^[1-9]\d{0,3}$/u.test(length) || Number(length) > 1024) throw new DisplayAccessError(400);
  const began = performance.now();
  const timeout = setTimeout(() => request.destroy(), OUTPUT_SAMPLE_MAX_AGE_MS);
  timeout.unref();
  try {
    const chunks: Buffer[] = []; let total = 0;
    for await (const chunk of request) {
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      total += bytes.length;
      if (total > 1024) throw new DisplayAccessError(413);
      chunks.push(bytes);
    }
    if (total !== Number(length)) throw new DisplayAccessError(400);
    const report = parseOutputReport(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(chunks))));
    const sampleAgeMs = report.sampleAgeMs + Math.ceil(performance.now() - began);
    if (sampleAgeMs >= OUTPUT_SAMPLE_MAX_AGE_MS) throw new DisplayAccessError(400);
    return { ...report, sampleAgeMs };
  } catch (error) {
    if (error instanceof DisplayAccessError) throw error;
    throw new DisplayAccessError(400);
  } finally { clearTimeout(timeout); }
}

// One bounded, non-durable observation per creator gateway. Authentication and
// an open streaming lease are checked by DisplayGateway, including after I/O.
// Receiver timestamps express a recent client report, not verified broadcast.
export class BroadcastOutput {
  #now: () => number;
  #last?: { leaseId: string; sequence: number; at: number; expires: number;
    streaming: boolean; recording: boolean };
  constructor(now: () => number = () => performance.now()) { this.#now = now; }
  record(leaseId: string, input: unknown): number {
    const report = parseOutputReport(input), now = this.#now(), previous = this.#last;
    if (previous?.leaseId === leaseId) {
      if (report.sequence <= previous.sequence) throw new DisplayAccessError(409);
      if (now - previous.at < 500) throw new DisplayAccessError(429);
    }
    this.#last = { leaseId, sequence: report.sequence, at: now,
      expires: now + OUTPUT_LIFETIME_MS - report.sampleAgeMs,
      streaming: report.streaming, recording: report.recording };
    return report.sequence;
  }
  snapshot(live: (leaseId: string) => boolean): OutputView {
    const last = this.#last;
    if (!last || !live(last.leaseId)) return UNKNOWN_OUTPUT;
    const remaining = Math.floor(last.expires - this.#now());
    return remaining > 0 ? { state: 'reported', streaming: last.streaming,
      recording: last.recording, expiresInMs: remaining } : UNKNOWN_OUTPUT;
  }
  clear(leaseId: string): void {
    // Retire visibility, not ordering: reconnecting the same still-valid lease
    // must not accept an old report sequence or resurrect its old observation.
    if (this.#last?.leaseId === leaseId) this.#last.expires = 0;
  }
}
