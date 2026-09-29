// SPDX-License-Identifier: GPL-2.0-or-later
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Duplex } from 'node:stream';
import { WebSocket, WebSocketServer } from 'ws';
import { DisplayAccess, DisplayAccessError } from './display-access.mts';
import { BroadcastOutput, UNKNOWN_OUTPUT, OUTPUT_LIFETIME_MS, parseOutputReport } from './broadcast-output.mts';
import type { OutputView, OutputAcceptance } from './broadcast-output.mts';
import type { Membership } from './session-store.mts';
import { nativeChatSnapshot } from '../../web/native-chat.js';

export const MAX_DISPLAY_FRAME_BYTES = 2 * 1024 * 1024;
const DENIED = 'ChatView display request denied';
export type DisplayConnectionState = Membership & Readonly<{
  gamingConnections: number; streamingConnections: number; captureState: 'unverified'; output: OutputView;
}>;

// Connection metadata is for native controls, not the chat renderer. No provider
// secrets, other owners, privileged commands or accounting fields are included.
export function displayFrame(snapshot: unknown, connection?: Omit<DisplayConnectionState, 'output'>,
  output: OutputView = UNKNOWN_OUTPUT): string {
  const frame = JSON.stringify({ type: 'chat-snapshot', version: 1, snapshot: nativeChatSnapshot(snapshot),
    ...(connection ? { connection: { ...connection, output } } : {}) });
  if (Buffer.byteLength(frame, 'utf8') > MAX_DISPLAY_FRAME_BYTES) throw new Error('Display frame too large');
  return frame;
}

function authorization(request: IncomingMessage, scheme: string): string {
  let count = 0;
  for (let i = 0; i < request.rawHeaders.length; i += 2) {
    if (request.rawHeaders[i]?.toLowerCase() === 'authorization') count++;
  }
  const value = request.headers.authorization;
  if (count !== 1 || typeof value !== 'string' || !value.startsWith(`${scheme} `)) throw new DisplayAccessError(401);
  const token = value.slice(scheme.length + 1);
  if (!/^[a-f0-9]{64}$/u.test(token)) throw new DisplayAccessError(401);
  return token;
}

export class DisplayGateway {
  #access: DisplayAccess;
  #origin: () => string;
  #snapshot: () => unknown;
  #server = new WebSocketServer({ noServer: true, maxPayload: 1024, perMessageDeflate: false });
  #clients = new Map<string, WebSocket>();
  #flush?: ReturnType<typeof setTimeout>;
  #pulse?: ReturnType<typeof setInterval>;
  #closed = false;
  #output: BroadcastOutput;

  constructor(access: DisplayAccess, origin: () => string, snapshot: () => unknown,
    output = new BroadcastOutput()) {
    this.#access = access; this.#origin = origin; this.#snapshot = snapshot; this.#output = output;
  }
  report(token: unknown, value: unknown, record?: (receipt: OutputAcceptance) => void) {
    if (this.#closed) throw new DisplayAccessError(503);
    const grant = this.#access.authenticateOutput(token);
    if (this.#clients.get(grant.id)?.readyState !== WebSocket.OPEN) throw new DisplayAccessError(409);
    const report = parseOutputReport(value), sequence = this.#output.record(grant.id, report);
    // Persistence observes only authenticated, ordered, live-lease reports.
    // Failure rejects this HTTP request but never stops private chat delivery.
    try {
      record?.({ leaseId: grant.id, generation: this.#output.generation, report,
        validForMs: Math.min(OUTPUT_LIFETIME_MS - report.sampleAgeMs, this.#access.remaining(grant.id)) });
    } finally { this.changed(); }
    return { accepted: true, sequence };
  }
  // Read-only availability for this exact approved sender. Reuses the same
  // observation/expiry as private connection status, never inventing ad evidence.
  outputFor(broadcastSessionId: string, connectionId: string): OutputView {
    if (this.#closed) return UNKNOWN_OUTPUT;
    return this.#output.snapshot(leaseId => {
      const membership = this.#access.membership(leaseId);
      return this.#clients.get(leaseId)?.readyState === WebSocket.OPEN &&
        membership?.role === 'streaming' && membership.broadcastSessionId === broadcastSessionId &&
        membership.connectionId === connectionId;
    });
  }
  #checkOrigin(request: IncomingMessage) {
    const origin = new URL(this.#origin());
    const encrypted = (request.socket as typeof request.socket & { encrypted?: boolean }).encrypted === true;
    if (origin.protocol === 'https:' ? !encrypted :
        origin.protocol !== 'http:' || origin.hostname !== '127.0.0.1') throw new DisplayAccessError(403);
    if (request.headers.host !== origin.host ||
        (request.headers.origin !== undefined && request.headers.origin !== origin.origin) ||
        request.headers['sec-fetch-site'] === 'cross-site') throw new DisplayAccessError(403);
  }
  handle(request: IncomingMessage, response: ServerResponse): boolean {
    if (request.url !== '/display/exchange') return false;
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Referrer-Policy', 'no-referrer');
    response.setHeader('X-Content-Type-Options', 'nosniff');
    response.setHeader('Content-Type', 'application/json');
    try {
      if (this.#closed) throw new DisplayAccessError(503);
      this.#checkOrigin(request);
      if (request.method !== 'POST') throw new DisplayAccessError(405);
      if (request.headers['transfer-encoding'] !== undefined ||
          (request.headers['content-length'] !== undefined && request.headers['content-length'] !== '0')) throw new DisplayAccessError(400);
      const lease = this.#access.exchange(authorization(request, 'ChatView-Ticket'));
      response.writeHead(200); response.end(JSON.stringify(lease));
    } catch (error) {
      response.setHeader('Connection', 'close');
      response.writeHead(error instanceof DisplayAccessError ? error.status : 500);
      response.end(JSON.stringify({ error: DENIED }));
    }
    return true;
  }
  upgrade(request: IncomingMessage, socket: Duplex, head: Buffer): void {
    socket.once('error', () => socket.destroy());
    try {
      if (this.#closed) throw new DisplayAccessError(503);
      this.#checkOrigin(request);
      if (request.method !== 'GET' || request.url !== '/display/events' ||
          request.headers['sec-websocket-protocol'] !== undefined) throw new DisplayAccessError(403);
      const grant = this.#access.authenticate(authorization(request, 'Bearer'));
      if (this.#clients.has(grant.id)) throw new DisplayAccessError(409);
      this.#server.handleUpgrade(request, socket, head, peer => {
        this.#clients.set(grant.id, peer);
        const expiry = setTimeout(() => {
          this.#access.revoke(grant.id); peer.terminate();
        }, Math.ceil(this.#access.remaining(grant.id)));
        expiry.unref();
        peer.on('error', () => peer.terminate());
        // Receive-only: this is not an OBS output or role-change API.
        peer.on('message', () => peer.terminate());
        peer.once('close', () => {
          clearTimeout(expiry);
          this.#output.clear(grant.id);
          if (this.#clients.get(grant.id) === peer) this.#clients.delete(grant.id);
          if (!this.#clients.size) { clearInterval(this.#pulse); this.#pulse = undefined; }
          this.changed();
        });
        this.#send(grant.id, peer);
        this.changed();
        this.#pulse ??= setInterval(() => this.#broadcast(), 5000);
        this.#pulse.unref();
      });
    } catch (error) {
      const status = error instanceof DisplayAccessError ? error.status : 500;
      socket.end(`HTTP/1.1 ${status} Rejected\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
    }
  }
  changed(): void {
    if (this.#closed) return;
    for (const [id, peer] of this.#clients) if (!this.#access.active(id)) peer.terminate();
    if (!this.#clients.size) return;
    this.#flush ??= setTimeout(() => { this.#flush = undefined; this.#broadcast(); }, 50);
  }
  #connection(id: string): DisplayConnectionState {
    const membership = this.#access.membership(id);
    if (!membership) throw new DisplayAccessError(401);
    const gaming = new Set<string>(), streaming = new Set<string>();
    for (const [peerId, peer] of this.#clients) {
      if (peer.readyState !== WebSocket.OPEN) continue;
      const other = this.#access.membership(peerId);
      if (!other || other.broadcastSessionId !== membership.broadcastSessionId) continue;
      (other.role === 'gaming' ? gaming : streaming).add(other.connectionId);
    }
    return { ...membership, gamingConnections: gaming.size, streamingConnections: streaming.size,
      captureState: 'unverified', output: this.#output.snapshot(leaseId => {
        const other = this.#access.membership(leaseId);
        return this.#clients.get(leaseId)?.readyState === WebSocket.OPEN &&
          other?.role === 'streaming' && other.broadcastSessionId === membership.broadcastSessionId;
      }) };
  }
  #send(id: string, peer: WebSocket): void {
    if (peer.readyState !== WebSocket.OPEN) return;
    if (!this.#access.active(id) || peer.bufferedAmount !== 0) { peer.terminate(); return; }
    try {
      const connection = this.#connection(id);
      const frame = displayFrame(this.#snapshot(), connection, connection.output);
      peer.send(frame, { binary: false, compress: false }, error => { if (error) peer.terminate(); });
    } catch { peer.terminate(); }
  }
  #broadcast(): void { for (const [id, peer] of this.#clients) this.#send(id, peer); }
  close(): void {
    if (this.#closed) return;
    this.#closed = true; clearTimeout(this.#flush); clearInterval(this.#pulse);
    this.#access.clear();
    for (const peer of this.#clients.values()) peer.terminate();
    this.#clients.clear(); this.#server.close();
  }
}

