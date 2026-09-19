// SPDX-License-Identifier: GPL-2.0-or-later
import { createRequire } from 'node:module';
import { ChzzkError, validateSessionUrl } from './api.mts';

// Only the documented API subset we consume, not an alternate wire protocol.
export interface SessionSocket {
  on(event: string, listener: (data?: unknown) => void): unknown;
  connect(): unknown;
  disconnect(): unknown;
  removeAllListeners(): unknown;
}
export type SocketFactory = (url: string) => SessionSocket;
const require = createRequire(import.meta.url);

export const createChzzkSocket: SocketFactory = url => {
  // Socket.IO's debug output can expose URL tickets and private messages.
  // Refuse diagnostic logging rather than silently leaking or changing DEBUG.
  if (process.env.DEBUG) throw new ChzzkError('invalid-input');
  const checked = validateSessionUrl(url);
  const io = require('socket.io-client') as (url: string, options: Record<string, unknown>) => SessionSocket;
  return io(checked, {
    autoConnect: false, forceNew: true, reconnection: false,
    transports: ['websocket'], upgrade: false, timeout: 3000,
    forceNode: true, rejectUnauthorized: true, perMessageDeflate: false,
  });
};
