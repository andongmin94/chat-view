// SPDX-License-Identifier: GPL-2.0-or-later
import type { IncomingMessage } from 'node:http';
import { DisplayAccessError } from './display-access.mts';
import type { ConnectionRole } from './session-store.mts';

// Launch-role intent is not proof that OBS is running. Browser consent grants
// the role; a request header cannot change an existing approval.
export function requestedRole(request: IncomingMessage): ConnectionRole {
  let count = 0;
  for (let i = 0; i < request.rawHeaders.length; i += 2)
    if (request.rawHeaders[i]?.toLowerCase() === 'x-chatview-role') count++;
  const role = request.headers['x-chatview-role'];
  if (count !== 1 || (role !== 'gaming' && role !== 'streaming')) throw new DisplayAccessError(400);
  return role;
}
