// SPDX-License-Identifier: GPL-2.0-or-later
// Provider payloads are untrusted. No HTML, URLs, or arbitrary profile objects
// cross into this first text-only renderer. Local sequence numbers are NOT IDs.
export type ChatMessage = Readonly<{
  provider: 'chzzk'; channelId: string; senderChannelId: string;
  chatChannelId?: string; nickname: string; verifiedMark: boolean;
  userRoleCode: string; content: string; messageTime: number;
}>;
export type SystemEvent =
  | { type: 'connected'; sessionKey: string }
  | { type: 'subscribed' | 'unsubscribed' | 'revoked'; eventType: string; channelId: string };

function object(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('Invalid event');
  return value as Record<string, unknown>;
}
function payload(value: unknown): Record<string, unknown> {
  if (typeof value === 'string') {
    if (Buffer.byteLength(value, 'utf8') > 65536) throw new Error('Invalid event');
    value = JSON.parse(value);
  }
  return object(value);
}
function text(value: unknown, limit: number): string {
  if (typeof value !== 'string' || value.length === 0 || value.length > limit ||
      /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/u.test(value)) throw new Error('Invalid event');
  return value;
}
function id(value: unknown, limit = 256): string {
  const result = text(value, limit);
  if (/\s/u.test(result)) throw new Error('Invalid event');
  return result;
}
export function parseSystemEvent(value: unknown): SystemEvent | undefined {
  try {
    const event = payload(value); const data = object(event.data);
    if (event.type === 'connected') return { type: 'connected', sessionKey: id(data.sessionKey, 8192) };
    if (event.type === 'subscribed' || event.type === 'unsubscribed' || event.type === 'revoked') {
      return { type: event.type, eventType: id(data.eventType, 32), channelId: id(data.channelId) };
    }
  } catch { /* Drop malformed data without logging a payload or session key. */ }
  return undefined;
}
export function parseChatMessage(value: unknown, expectedChannelId: string): ChatMessage | undefined {
  try {
    const event = payload(value);
    const channelId = id(event.channelId);
    if (channelId !== expectedChannelId) return undefined;
    const profile = object(event.profile);
    if (!Number.isSafeInteger(event.messageTime) || (event.messageTime as number) < 0 ||
        typeof profile.verifiedMark !== 'boolean') return undefined;
    const message: ChatMessage = {
      provider: 'chzzk', channelId, senderChannelId: id(event.senderChannelId),
      ...(event.chatChannelId === undefined ? {} : { chatChannelId: id(event.chatChannelId) }),
      nickname: text(profile.nickname, 200), verifiedMark: profile.verifiedMark,
      userRoleCode: id(event.userRoleCode, 64), content: text(event.content, 10000),
      messageTime: event.messageTime as number,
    };
    return Object.freeze(message);
  } catch { return undefined; }
}
