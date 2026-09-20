// SPDX-License-Identifier: GPL-2.0-or-later
import type { IncomingMessage, ServerResponse } from 'node:http';
import { BrowserLogin } from './browser-login.mts';
import { DisplayAccess, DisplayAccessError } from './display-access.mts';

const DENIED = 'ChatView display request denied';

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

export class DisplaySessionGateway {
  readonly login: BrowserLogin;
  #access: DisplayAccess;
  #origin: () => string;
  #changed: () => void;
  #closed = false;

  constructor(access: DisplayAccess, origin: () => string, changed: () => void) {
    this.#access = access;
    this.#origin = origin;
    this.#changed = changed;
    this.login = new BrowserLogin(access);
  }

  #checkOrigin(request: IncomingMessage) {
    const origin = new URL(this.#origin());
    const encrypted = (request.socket as typeof request.socket & { encrypted?: boolean }).encrypted === true;
    if (origin.protocol === 'https:' ? !encrypted : origin.protocol !== 'http:' || origin.hostname !== '127.0.0.1')
      throw new DisplayAccessError(403);
    if (request.headers.host !== origin.host ||
        (request.headers.origin !== undefined && request.headers.origin !== origin.origin) ||
        request.headers['sec-fetch-site'] === 'cross-site') throw new DisplayAccessError(403);
  }

  handle(request: IncomingMessage, response: ServerResponse): boolean {
    const polling = /^\/display\/login\/([a-f0-9]{32})$/u.exec(request.url ?? '');
    const managed = polling || ['/display/login', '/display/exchange', '/display/refresh', '/display/signout'].includes(request.url ?? '');
    if (!managed) return false;
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Referrer-Policy', 'no-referrer');
    response.setHeader('X-Content-Type-Options', 'nosniff');
    response.setHeader('Content-Type', 'application/json');
    try {
      if (this.#closed) throw new DisplayAccessError(503);
      this.#checkOrigin(request);
      if (request.method !== 'POST') throw new DisplayAccessError(405);
      if (request.headers['transfer-encoding'] !== undefined ||
          (request.headers['content-length'] !== undefined && request.headers['content-length'] !== '0'))
        throw new DisplayAccessError(400);
      const remember = request.headers['x-chatview-remember'];
      if (remember !== undefined && (request.url !== '/display/login' || remember !== '1'))
        throw new DisplayAccessError(400);
      const result = polling ? this.login.poll(polling[1]!, authorization(request, 'ChatView-Login'))
        : request.url === '/display/login' ? this.login.start(authorization(request, 'ChatView-Challenge'), remember === '1')
        : request.url === '/display/refresh' ? this.#access.resume(authorization(request, 'ChatView-Session'))
        : request.url === '/display/signout'
          ? (this.#access.signout(authorization(request, 'ChatView-Session')), { signedOut: true })
          : this.#access.exchange(authorization(request, 'ChatView-Ticket'));
      this.#changed();
      response.writeHead(200);
      response.end(JSON.stringify(result));
    } catch (error) {
      response.setHeader('Connection', 'close');
      response.writeHead(error instanceof DisplayAccessError ? error.status : 500);
      response.end(JSON.stringify({ error: DENIED }));
    }
    return true;
  }

  close() {
    if (this.#closed) return;
    this.#closed = true;
    this.login.clear();
    this.#access.close();
  }
}
