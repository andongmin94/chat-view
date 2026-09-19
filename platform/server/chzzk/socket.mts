// SPDX-License-Identifier: GPL-2.0-or-later
import { createRequire } from 'node:module';
import { ChzzkError, validateSessionUrl } from './api.mts';

export interface SessionSocket {
  on(event: string, listener: (data?: unknown) => void): unknown;
  connect(): unknown;
  disconnect(): unknown;
  removeAllListeners(): unknown;
}
export type SocketFactory = (url: string) => SessionSocket;
const require = createRequire(import.meta.url);

export const createChzzkSocket: SocketFactory = url => {
  if (process.env.DEBUG) throw new ChzzkError('invalid-input');
  const endpoint = new URL(validateSessionUrl(url));
  const { Manager } = require('socket.io-client') as {
    Manager: new (options: Record<string, unknown>) => { socket(namespace: string): SessionSocket };
  };
  // The public object-options API avoids both legacy URI constructors.
  // parseuri 3.x has a different export shape; it is not called here.
  // Node URL is the sole URI parser. Each session owns a separate Manager.
  const manager = new Manager({
    hostname: endpoint.hostname, port: 443, secure: true,
    query: Object.fromEntries(endpoint.searchParams),
    path: '/socket.io', autoConnect: false, reconnection: false,
    transports: ['websocket'], upgrade: false, timeout: 3000,
    forceNode: true, rejectUnauthorized: true, perMessageDeflate: false,
  });
  return manager.socket('/');
};
