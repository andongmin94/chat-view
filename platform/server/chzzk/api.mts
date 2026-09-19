// SPDX-License-Identifier: GPL-2.0-or-later
// Official CHZZK HTTP boundary. Never send these credentials to a renderer.
const API_ORIGIN = 'https://openapi.chzzk.naver.com';
const MAX_RESPONSE_BYTES = 64 * 1024;

export type Credentials = Readonly<{ clientId: string; clientSecret: string }>;
export type Tokens = Readonly<{
  accessToken: string;
  refreshToken: string;
  expiresIn: number;
}>;
export type Channel = Readonly<{ channelId: string; channelName: string }>;
export type Fetch = (url: string, init: RequestInit) => Promise<Response>;

// Deliberately omit upstream bodies, URLs, headers and exception causes:
// each can contain a code, a token, or a session URL.
export class ChzzkError extends Error {
  readonly kind: 'invalid-input' | 'transport' | 'response' | 'http';
  readonly status: number;
  constructor(kind: ChzzkError['kind'], status = 0) {
    super(`CHZZK ${kind}${status ? ` (${status})` : ''}`);
    this.name = 'ChzzkError';
    this.kind = kind;
    this.status = status;
  }
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new ChzzkError('response');
  }
  return value as Record<string, unknown>;
}

function text(value: unknown, max: number, kind: 'invalid-input' | 'response'): string {
  if (typeof value !== 'string' || !value.trim() || value.length > max || /[\x00-\x1f\x7f]/u.test(value)) {
    throw new ChzzkError(kind);
  }
  return value;
}

function secret(value: unknown, kind: 'invalid-input' | 'response' = 'invalid-input'): string {
  const result = text(value, 8192, kind);
  if (/\s/u.test(result)) throw new ChzzkError(kind);
  return result;
}

function tokens(value: unknown): Tokens {
  const data = record(value);
  const expires = data.expiresIn;
  if (data.tokenType !== 'Bearer' || typeof expires !== 'string' || !/^[1-9]\d{0,8}$/u.test(expires)) {
    throw new ChzzkError('response');
  }
  return Object.freeze({
    accessToken: secret(data.accessToken, 'response'),
    refreshToken: secret(data.refreshToken, 'response'),
    expiresIn: Number(expires),
  });
}

export function authorizationUrl(clientId: string, redirectUri: string, state: string): string {
  let redirect: URL;
  try { redirect = new URL(redirectUri); } catch { throw new ChzzkError('invalid-input'); }
  const loopback = ['127.0.0.1', 'localhost', '[::1]'].includes(redirect.hostname);
  if ((redirect.protocol !== 'https:' && !(redirect.protocol === 'http:' && loopback)) ||
      redirect.username || redirect.password || redirect.hash || redirect.search) {
    throw new ChzzkError('invalid-input');
  }
  const url = new URL('https://chzzk.naver.com/account-interlock');
  url.search = new URLSearchParams({
    clientId: secret(clientId), redirectUri, state: secret(state),
  }).toString();
  return url.href;
}

// An API-issued session URL is short-lived and confidential. Accept only the
// documented NAVER chat service, never an arbitrary endpoint/redirect.
export function validateSessionUrl(value: unknown): string {
  const raw = text(value, 8192, 'response');
  let url: URL;
  try { url = new URL(raw); } catch { throw new ChzzkError('response'); }
  if (url.protocol !== 'https:' || !url.hostname.endsWith('.nchat.naver.com') ||
      url.username || url.password || url.hash || (url.port && url.port !== '443') ||
      url.pathname !== '/' || !url.searchParams.get('auth')) {
    throw new ChzzkError('response');
  }
  return url.href;
}

async function readResponse(response: Response): Promise<unknown> {
  if (!response.body) throw new ChzzkError('response');
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let length = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > MAX_RESPONSE_BYTES) throw new ChzzkError('response');
      chunks.push(value);
    }
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  } catch {
    try { await reader.cancel(); } catch { /* The transport may already be closed. */ }
    throw new ChzzkError('response');
  } finally {
    reader.releaseLock();
  }
}

export class ChzzkApi {
  #credentials: Credentials;
  #fetch: Fetch;
  constructor(credentials: Credentials, fetcher: Fetch = globalThis.fetch) {
    this.#credentials = Object.freeze({
      clientId: secret(credentials.clientId), clientSecret: secret(credentials.clientSecret),
    });
    this.#fetch = fetcher;
  }

  async #request(path: string, accessToken?: string, body?: Record<string, string>, signal?: AbortSignal, method: 'GET' | 'POST' = body ? 'POST' : 'GET'): Promise<unknown> {
    const headers: Record<string, string> = { Accept: 'application/json', 'Content-Type': 'application/json' };
    if (accessToken !== undefined) headers.Authorization = `Bearer ${secret(accessToken)}`;
    const deadline = AbortSignal.timeout(10_000);
    let response: Response;
    try {
      response = await this.#fetch(`${API_ORIGIN}${path}`, {
        method, headers,
        body: body ? JSON.stringify(body) : undefined,
        redirect: 'error',
        signal: signal ? AbortSignal.any([signal, deadline]) : deadline,
      });
    } catch {
      // In particular, do not automatically retry a one-use refresh token.
      throw new ChzzkError('transport');
    }
    if (!response.ok) {
      try { await response.body?.cancel(); } catch { /* No upstream body is logged. */ }
      throw new ChzzkError('http', response.status);
    }
    const envelope = record(await readResponse(response));
    if (envelope.code !== 200 || !Object.hasOwn(envelope, 'content')) {
      throw new ChzzkError('response');
    }
    return envelope.content;
  }

  async exchangeCode(code: string, state: string, signal?: AbortSignal): Promise<Tokens> {
    return tokens(await this.#request('/auth/v1/token', undefined, {
      ...this.#credentials, grantType: 'authorization_code', code: secret(code), state: secret(state),
    }, signal));
  }

  async refresh(refreshToken: string, signal?: AbortSignal): Promise<Tokens> {
    return tokens(await this.#request('/auth/v1/token', undefined, {
      ...this.#credentials, grantType: 'refresh_token', refreshToken: secret(refreshToken),
    }, signal));
  }

  // Explicit account action only: upstream revokes ALL tokens for this app/user.
  async revoke(accessToken: string, signal?: AbortSignal): Promise<void> {
    await this.#request('/auth/v1/token/revoke', undefined, {
      ...this.#credentials, token: secret(accessToken), tokenTypeHint: 'access_token',
    }, signal);
  }

  async getUser(accessToken: string, signal?: AbortSignal): Promise<Channel> {
    const data = record(await this.#request('/open/v1/users/me', accessToken, undefined, signal));
    return Object.freeze({
      channelId: secret(data.channelId, 'response'),
      channelName: text(data.channelName, 200, 'response'),
    });
  }

  async createUserSession(accessToken: string, signal?: AbortSignal): Promise<string> {
    const data = record(await this.#request('/open/v1/sessions/auth', accessToken, undefined, signal));
    return validateSessionUrl(data.url);
  }

  // CHZZK documents sessionKey as a request parameter, not a JSON body.
  async subscribeChat(accessToken: string, sessionKey: string, signal?: AbortSignal): Promise<void> {
    const query = new URLSearchParams({ sessionKey: secret(sessionKey) });
    await this.#request(`/open/v1/sessions/events/subscribe/chat?${query}`, accessToken, undefined, signal, 'POST');
  }

  async unsubscribeChat(accessToken: string, sessionKey: string, signal?: AbortSignal): Promise<void> {
    const query = new URLSearchParams({ sessionKey: secret(sessionKey) });
    await this.#request(`/open/v1/sessions/events/unsubscribe/chat?${query}`, accessToken, undefined, signal, 'POST');
  }
}
