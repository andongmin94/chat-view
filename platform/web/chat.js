// SPDX-License-Identifier: GPL-2.0-or-later
// Browser-only transport; the native HUD embeds chat-renderer.js without SSE.
import { renderChat } from './chat-renderer.js';
if (typeof document !== 'undefined') {
  const stream = new EventSource('/chat/events');
  let lastUpdate = Date.now();
  const clear = () => renderChat(document, { state: 'disconnected', received: 0, messages: [] });
  stream.onmessage = event => {
    try { renderChat(document, JSON.parse(event.data)); lastUpdate = Date.now(); }
    catch { clear(); }
  };
  stream.onerror = clear;
  const watchdog = setInterval(() => { if (Date.now() - lastUpdate > 15000) clear(); }, 5000);
  addEventListener('pagehide', () => { clearInterval(watchdog); stream.close(); clear(); }, { once: true });
}
