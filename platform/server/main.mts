// SPDX-License-Identifier: GPL-2.0-or-later
import { readFileSync } from 'node:fs';
import { createServer } from 'node:https';
import { isAbsolute } from 'node:path';
import { pathToFileURL } from 'node:url';
import { authorizationUrl, ChzzkApi } from './chzzk/api.mts';
import { ChzzkChatSession } from './chzzk/session.mts';
import { SessionStore } from './chat/session-store.mts';
import { DisplayGateway } from './chat/display-gateway.mts';
import { Creators } from './service/creators.mts';
import { PlatformApplication } from './service/application.mts';

export async function startPlatform(env: NodeJS.ProcessEnv = process.env) {
  const required = (name: string) => {
    const value = env[name];
    if (!value?.trim()) throw new Error('ChatView service configuration incomplete');
    return value;
  };
  const origin = required('CHATVIEW_ORIGIN'), url = new URL(origin);
  if (url.protocol !== 'https:' || url.origin !== origin) throw new Error('ChatView requires a canonical HTTPS origin');
  const database = required('CHATVIEW_SESSION_DATABASE');
  if (!isAbsolute(database)) throw new Error('ChatView requires an absolute session database path');
  const credentials = { clientId: required('CHZZK_CLIENT_ID'), clientSecret: required('CHZZK_CLIENT_SECRET') };
  const api = new ChzzkApi(credentials);
  const tls = { key: readFileSync(required('CHATVIEW_TLS_KEY_FILE')), cert: readFileSync(required('CHATVIEW_TLS_CERT_FILE')),
    minVersion: 'TLSv1.2' as const, maxHeaderSize: 16384 };
  // Direct TLS termination only. Forwarded headers never authorize plain HTTP.
  const port = Number(url.port || '443');
  const sessions = new SessionStore(database);
  const creators = new Creators({ api, sessions,
    createChat: changed => new ChzzkChatSession(api, changed),
    createDisplay: (access, snapshot) => new DisplayGateway(access, () => origin, snapshot) });
  const app = new PlatformApplication(creators, origin, state => authorizationUrl(credentials.clientId, `${origin}/callback`, state));
  const server = createServer(tls, (request, response) => { void app.handle(request, response); });
  server.requestTimeout = 15000; server.headersTimeout = 10000; server.keepAliveTimeout = 5000;
  server.maxHeadersCount = 64; server.maxRequestsPerSocket = 100;
  server.on('upgrade', (request, socket, head) => app.upgrade(request, socket, head));
  server.on('clientError', (_error, socket) => socket.destroy());
  let closed = false;
  const close = async () => {
    if (closed) return; closed = true; app.close();
    const stopped = new Promise<void>(resolve => server.close(() => resolve()));
    server.closeAllConnections(); await creators.close(); await stopped; sessions.close();
  };
  try {
    await new Promise<void>((resolve, reject) => {
      server.once('error', reject);
      server.listen(port, env.CHATVIEW_BIND_ADDRESS ?? '0.0.0.0', () => {
        server.off('error', reject); resolve();
      });
    });
  } catch (error) { await close(); throw error; }
  return { close };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const server = await startPlatform();
    console.log('ChatView HTTPS development service started; provider/hardware acceptance is pending.');
    for (const signal of ['SIGINT', 'SIGTERM'] as const) process.once(signal, () => {
      void server.close().catch(() => { process.exitCode = 1; });
    });
  } catch {
    // Configuration may contain provider tokens, filenames or TLS key material.
    console.error('ChatView service could not start. Check the protected server configuration.');
    process.exitCode = 1;
  }
}
