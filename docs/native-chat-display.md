# P2a — first-party native display boundary

Date: 2026-09-20 (Asia/Seoul). Starting OBS commit: 6598edc1feeda288ecb6c5063e72c87f94957698.

## Implemented, and deliberately not claimed

NativeChatSurface renders ChatView's own chat using the existing WebViewHost/DirectComposition/WebView2 engine. The same `platform/web/chat-renderer.js` implements both the browser preview and the native document. The browser's SSE bootstrap is separate; the native document has no network transport, provider credentials, server process, or OBS requirement of its own.

This is the native **display layer**, not a completed user-selectable first-party chat feature. The production HUD's settings and OBS-parent lifecycle remain unchanged. The class is compiled with the native HUD and exercised by a real WebView2 CTest harness; account/device authorization and an authenticated chat-delivery worker must connect it before enabling the feature for streamers. Do not ship the developer probe, insert its loopback URL into native settings, or report this as live CHZZK/native-HUD end-to-end certification. No new placeholder login button or fake connected state is added.

## Display contract

A UI-thread owner opens a NativeChatSurface on a ready WebViewHost. CMake embeds the shared renderer and native receiver as a fixed HTML document. Each open generates a fresh document nonce used for CSP and readiness correlation. This nonce is **not a device credential or user authentication**. No new allowed external URLs, exposed host objects, general native commands, advertiser scripts, or browser engine are added.

After the actual document load and its matching readiness acknowledgement, `publish` forwards a bounded JSON envelope with PostWebMessageAsJson, never ExecuteScript or string interpolation into markup:

```json
{"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"Synthetic example","content":"Hello","messageTime":1700000000000}]}}
```

The native method rejects pre-handshake, foreign-document, closed-host, malformed JSON, embedded-NUL and oversized input. The JavaScript receiver additionally accepts only trusted native-bridge events, protocol version 1, the known chat states, a safe nonnegative receive count and at most 100 bounded text rows. Only nickname, content and timestamp reach DOM text nodes. Unknown provider fields are not rendered; no HTML or remote media is interpreted. Non-subscribed states clear the display. Unknown versions/malformed snapshots clear old content rather than leaving a misleading live chat.

The native document expects chat updates or snapshots at least every 15 seconds while connected. Host chrome messages do not reset chat liveness. A monotonic watchdog clears stale chat after that interval; page unload detaches the bridge/timer. Outgoing acknowledgements contain only document correlation and rendered row counts, never chat text or tokens. Counts are render diagnostics, not audience data or ad evidence.

Navigation invalidates the document binding; moving to setup/external content, close, WebView recreation and renderer process failure require a fresh open/handshake. Existing URL allowlists, capture suppression, input/placement and crash recovery remain intact. This display component does not decide whether a HUD may be shown or whether a capture path is private.

## Verification

Local Node 22.16: 20 new native-receiver/shared-renderer tests passed; assembled embedded module syntax checked. These local tests simulate DOM/bridge events, not Windows. The repository Windows build also runs `chat-view-native-chat-surface`, using the real production WebViewHost and WebView2 to render synthetic Unicode/malicious-looking text, reject a wrong protocol version, clear after revoke, invalidate on another document, and recreate/tear down host and surface in both orders. Inspect that commit's CI result before claiming these Windows tests passed.

Existing CHZZK tests remain registered. The shared renderer is served as an additional authenticated preview asset; it does not create a second upstream subscription. The native test is not installed in the release package. No actual CHZZK account, gameplay, HDMI capture card, billable metric or real-money behavior was tested by this slice.

## Next product boundary

Implement creator/device authorization and bounded authenticated delivery into this display layer, keeping provider tokens server-side. Bind output lifetime to that session rather than to an assumed local OBS process on a gaming PC. Qualify actual CHZZK messages and OBS-free dual-PC clean video separately. Do not spend the next milestone adding generic renderer diagnostics or reopening already validated parser selection.

Primary references checked 2026-09-20:
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content
