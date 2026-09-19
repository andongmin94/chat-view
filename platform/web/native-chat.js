// SPDX-License-Identifier: GPL-2.0-or-later
// Embedded alongside the shared renderer, never injected into external pages.
const NATIVE_CHAT_STATES = new Set([
  'idle', 'connecting', 'subscribing', 'subscribed', 'disconnected',
  'revoked', 'unsubscribed', 'error', 'stopped',
]);

// This is a display DTO, not credentials, executable markup, or window commands.
export function nativeChatSnapshot(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      !NATIVE_CHAT_STATES.has(value.state) || !Number.isSafeInteger(value.received) ||
      value.received < 0 || !Array.isArray(value.messages) || value.messages.length > 100) {
    throw new Error('Invalid chat snapshot');
  }
  if (value.state !== 'subscribed') return { state: value.state, received: value.received, messages: [] };
  if (value.received < value.messages.length) throw new Error('Invalid message count');
  const cleanText = (text, maximum) => {
    if (typeof text !== 'string' || !text.length || text.length > maximum ||
        /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/u.test(text)) throw new Error('Invalid message text');
    return text;
  };
  const messages = value.messages.map(message => {
    if (!message || typeof message !== 'object' ||
        !Number.isSafeInteger(message.messageTime) || message.messageTime < 0 ||
        message.messageTime > 8640000000000000) throw new Error('Invalid message time');
    return {
      nickname: cleanText(message.nickname, 200),
      content: cleanText(message.content, 10000),
      messageTime: message.messageTime,
    };
  });
  return { state: value.state, received: value.received, messages };
}

export function connectNativeChat(document, bridge, render, nonce, clock = globalThis) {
  const clear = () => render(document, { state: 'disconnected', received: 0, messages: [] });
  let closed = false;
  let lastUpdate = clock.performance.now();
  const listener = event => {
    if (closed || !event.isTrusted || event.source !== bridge) return;
    // Host chrome has its own message type and does not count as chat liveness.
    if (!event.data || event.data.type !== 'chat-snapshot') return;
    try {
      if (event.data.version !== 1) throw new Error('Unknown chat protocol');
      const snapshot = nativeChatSnapshot(event.data.snapshot);
      render(document, snapshot);
      lastUpdate = clock.performance.now();
      bridge.postMessage(`chat-rendered:${nonce}:${document.getElementById('messages').children.length}`);
    } catch {
      clear();
      bridge.postMessage(`chat-invalid:${nonce}`);
    }
  };
  bridge.addEventListener('message', listener);
  const watchdog = clock.setInterval(() => {
    if (clock.performance.now() - lastUpdate > 15000) clear();
  }, 1000);
  render(document, { state: 'idle', received: 0, messages: [] });
  bridge.postMessage(`chat-ready:${nonce}`);
  return () => {
    if (closed) return;
    closed = true; clock.clearInterval(watchdog);
    bridge.removeEventListener('message', listener); clear();
  };
}
