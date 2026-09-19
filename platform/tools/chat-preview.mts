// SPDX-License-Identifier: GPL-2.0-or-later
import { readFile } from 'node:fs/promises';
import type { ServerResponse } from 'node:http';
import type { ChatSnapshot } from '../server/chzzk/session.mts';

export const CHAT_CSP = "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
const assets = new Map([
  ['/chat', ['chat.html', 'text/html; charset=utf-8']],
  ['/chat.js', ['chat.js', 'text/javascript; charset=utf-8']],
  ['/chat-renderer.js', ['chat-renderer.js', 'text/javascript; charset=utf-8']],
  ['/chat.css', ['chat.css', 'text/css; charset=utf-8']],
]);

// Same-browser, loopback-only preview. Not a public chat delivery service.
export class ChatPreview {
  #snapshot: () => ChatSnapshot;
  #clients = new Set<ServerResponse>();
  #flush?: ReturnType<typeof setTimeout>;
  #heartbeat?: ReturnType<typeof setInterval>;
  constructor(snapshot: () => ChatSnapshot) { this.#snapshot = snapshot; }
  handles(path: string): boolean { return path === '/chat/events' || assets.has(path); }
  async asset(path: string, response: ServerResponse): Promise<boolean> {
    const asset = assets.get(path);
    if (!asset) return false;
    const content = await readFile(new URL(`../web/${asset[0]}`, import.meta.url));
    response.writeHead(200, {
      'Content-Type': asset[1]!, 'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
      'Content-Security-Policy': CHAT_CSP,
    });
    response.end(content); return true;
  }
  open(response: ServerResponse) {
    if (this.#clients.size >= 4) { response.writeHead(429); response.end(); return; }
    response.writeHead(200, {
      'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
    });
    this.#clients.add(response);
    response.once('close', () => {
      this.#clients.delete(response);
      if (!this.#clients.size) { clearInterval(this.#heartbeat); this.#heartbeat = undefined; }
    });
    this.#send(response);
    this.#heartbeat ??= setInterval(() => this.#broadcast(), 10000);
    this.#heartbeat.unref();
  }
  changed() {
    // Coalesce bursts. Never queue an unbounded list for a slow renderer.
    this.#flush ??= setTimeout(() => {
      this.#flush = undefined; this.#broadcast();
    }, 50);
  }
  #send(response: ServerResponse) {
    if (response.destroyed || response.writableEnded) return;
    if (!response.write(`data: ${JSON.stringify(this.#snapshot())}\n\n`)) response.destroy();
  }
  #broadcast() { for (const response of this.#clients) this.#send(response); }
  close() {
    clearTimeout(this.#flush); clearInterval(this.#heartbeat);
    for (const response of this.#clients) response.end();
    this.#clients.clear();
  }
}
