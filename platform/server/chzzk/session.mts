// SPDX-License-Identifier: GPL-2.0-or-later
import { ChzzkError } from './api.mts';
import type { ChzzkApi } from './api.mts';
import { parseSystemEvent, parseChatMessage } from './events.mts';
import type { ChatMessage } from './events.mts';
import { createChzzkSocket } from './socket.mts';
import type { SessionSocket, SocketFactory } from './socket.mts';

export type ChatState = 'idle' | 'connecting' | 'subscribing' | 'subscribed' |
  'disconnected' | 'revoked' | 'unsubscribed' | 'error' | 'stopped';
export type ChatSnapshot = Readonly<{ state: ChatState; received: number; messages: readonly ChatMessage[] }>;
export type ChatApi = Pick<ChzzkApi, 'createUserSession' | 'subscribeChat' | 'unsubscribeChat'>;
export const MAX_MESSAGES = 100;

// One owner/channel and one upstream socket per instance. Restart means a new
// instance and a fresh API-issued ticket, never reconnecting an expired URL.
export class ChzzkChatSession {
  #api: ChatApi;
  #factory: SocketFactory;
  #changed: () => void;
  #socket?: SessionSocket;
  #lifetime = new AbortController();
  #state: ChatState = 'idle';
  #messages: ChatMessage[] = [];
  #received = 0;
  #channelId = '';
  #token?: string;
  #key?: string;
  #closed = false;
  #httpSubscribed = false;
  #acknowledged = false;
  #timer?: ReturnType<typeof setTimeout>;
  #abortCleanup?: () => void;
  #resolve?: () => void;
  #reject?: (error: Error) => void;

  constructor(api: ChatApi, changed: () => void, factory: SocketFactory = createChzzkSocket) {
    this.#api = api; this.#changed = changed; this.#factory = factory;
  }
  snapshot(): ChatSnapshot {
    return { state: this.#state, received: this.#received, messages: [...this.#messages] };
  }
  #notify() { this.#changed(); }
  #ready() {
    if (this.#closed || !this.#httpSubscribed || !this.#acknowledged || this.#state === 'subscribed') return;
    this.#state = 'subscribed'; clearTimeout(this.#timer);
    this.#resolve?.(); this.#resolve = undefined; this.#reject = undefined;
    this.#notify();
  }
  #finish(state: ChatState) {
    if (this.#closed) return;
    this.#closed = true; this.#state = state; this.#messages = [];
    this.#lifetime.abort(); clearTimeout(this.#timer);
    this.#abortCleanup?.(); this.#abortCleanup = undefined;
    this.#token = undefined; this.#key = undefined;
    this.#socket?.removeAllListeners(); this.#socket?.disconnect(); this.#socket = undefined;
    this.#reject?.(new ChzzkError('transport')); this.#reject = undefined; this.#resolve = undefined;
    this.#notify();
  }
  async #system(raw: unknown) {
    if (this.#closed) return;
    const event = parseSystemEvent(raw);
    if (!event) return;
    if (event.type === 'connected') {
      if (this.#state !== 'connecting') return; // Duplicate connected is not another subscription.
      this.#key = event.sessionKey; this.#state = 'subscribing'; this.#notify();
      try {
        await this.#api.subscribeChat(this.#token!, event.sessionKey, this.#lifetime.signal);
        if (this.#closed) return;
        this.#httpSubscribed = true; this.#ready();
      } catch { this.#finish('error'); }
      return;
    }
    if (event.eventType !== 'CHAT' || event.channelId !== this.#channelId) return;
    if (event.type === 'subscribed' && this.#state === 'subscribing') {
      this.#acknowledged = true; this.#ready();
    } else if (event.type === 'revoked' || event.type === 'unsubscribed') {
      this.#finish(event.type);
    }
  }
  async start(accessToken: string, channelId: string, signal?: AbortSignal): Promise<void> {
    if (this.#state !== 'idle' || this.#closed || !accessToken || !channelId) throw new ChzzkError('invalid-input');
    this.#token = accessToken; this.#channelId = channelId; this.#state = 'connecting';
    const ready = new Promise<void>((resolve, reject) => { this.#resolve = resolve; this.#reject = reject; });
    // Attach immediately: cancellation can settle this promise during HTTP I/O.
    void ready.catch(() => {});
    const abort = () => this.#finish('stopped');
    signal?.addEventListener('abort', abort, { once: true });
    this.#abortCleanup = () => signal?.removeEventListener('abort', abort);
    this.#timer = setTimeout(() => this.#finish('error'), 15000);
    this.#notify();
    try {
      if (signal?.aborted) this.#finish('stopped');
      if (!this.#closed) {
        const url = await this.#api.createUserSession(accessToken, this.#lifetime.signal);
        if (!this.#closed) {
          const socket = this.#factory(url); this.#socket = socket;
          socket.on('SYSTEM', data => { void this.#system(data).catch(() => this.#finish('error')); });
          socket.on('CHAT', data => {
            if (this.#closed || this.#state !== 'subscribed') return;
            const message = parseChatMessage(data, this.#channelId);
            if (!message) return;
            this.#received++; this.#messages.push(message);
            if (this.#messages.length > MAX_MESSAGES) this.#messages.shift();
            this.#notify();
          });
          socket.on('disconnect', () => this.#finish('disconnected'));
          for (const event of ['error', 'connect_error', 'connect_timeout']) socket.on(event, () => this.#finish('error'));
          socket.connect();
        }
      }
    } catch { this.#finish('error'); }
    await ready;
  }
  async stop(): Promise<boolean> {
    if (this.#closed) return true;
    const token = this.#token; const key = this.#key;
    // Delivery is closed immediately, even if unsubscribe fails. This only
    // removes this chat subscription; it NEVER revokes the user's other devices.
    const socket = this.#socket;
    socket?.removeAllListeners(); this.#socket = undefined;
    this.#finish('stopped');
    if (!key || !token) { socket?.disconnect(); return true; }
    try {
      await this.#api.unsubscribeChat(token, key, AbortSignal.timeout(2000));
      return true;
    } catch { return false; } finally { socket?.disconnect(); }
  }
}
