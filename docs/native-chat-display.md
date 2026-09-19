# P2a — first-party native display boundary

Updated: 2026-09-20 (Asia/Seoul). Original P2a baseline: 6598edc; navigation repair starts at 170a2dddd77b72746dbb314b46fbe31c2fbe4479.

## Implemented scope

NativeChatSurface uses the existing WebViewHost/DirectComposition/WebView2 engine. The shared platform/web/chat-renderer.js renders both browser preview and native chat; browser SSE startup remains separate. This display class is compiled into the HUD and exercised by a real WebView2 CTest. It is not yet a user-selectable first-party mode: account/device authorization and authenticated delivery must feed it. Do not ship the developer probe or its application secret, allow its loopback URL in native settings, or claim live CHZZK-to-HUD/two-PC acceptance.

## Host-owned in-memory documents

Windows #286 at 170a2dd proved that NavigateToString raised NavigationStarting with a data:text/html;charset=utf-8;base64 URI, while the old surface and host expected about:blank. The existing navigation guard cancelled the document before its scripts ran. All other 23 tests passed; the new native display test failed. This is a diagnosed loading defect, not evidence that larger timeouts or relaxed URL rules are needed.

The repair removes NavigateToString from the host's internal loading path. Setup, private chat and clearing now use WebResourceRequested and CreateWebResourceResponse to answer an exact, fresh https://chatview.invalid/<document-id>/index.html address from memory. The reserved origin is not a deployed service, localhost port, file mapping or network endpoint. UTF-8 conversion and document size are bounded. Every intercepted origin request receives a local response; stale paths, other methods and non-document requests receive 404. Only an explicit native operation can install a document; external URL normalization is unchanged.

A fresh document identity is installed before queuing navigation, invalidating the previous surface immediately. Source checks require that exact identity, the same host/browser instance, the correlated navigation completion and the nonce-bound readiness message. A rejected foreign navigation never becomes authorized; cancelled superseded requests do not invalidate a newer valid binding. No compatibility path accepts arbitrary data URLs or treats about:blank as private-chat authority.

The native document retains its CSP and nonce-bound embedded scripts, contains no remote media/transport or provider credentials, and exposes no native window commands. The nonce correlates a document; it is not user/device authentication. Closing a loaded surface replaces it with a blank restricted memory document. Setup/reopen/external navigation and host destruction change or remove ownership rather than leaving private delivery attached to another page.

## Display contract

After document load and matching readiness, publish uses PostWebMessageAsJson with this bounded display DTO, never executable script or markup interpolation:

```json
{"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"Synthetic example","content":"Hello","messageTime":1700000000000}]}}
```

The native method rejects unready/foreign/closed hosts, malformed JSON, embedded NUL and oversized input. The JavaScript receiver checks trusted bridge events, version, known state, safe counts/timestamps and at most 100 bounded text rows. Only nickname/content/time become text nodes. Non-subscribed or invalid frames clear the old display. Host chrome messages do not reset the 15-second monotonic chat watchdog. Unload detaches listeners/timers. Render acknowledgements are diagnostics, never authentication, audience or advertising evidence.

This component does not decide whether the HUD can be shown or whether a capture path is private. Existing input/placement, process ownership, capture interlock, installer and dependencies remain unchanged.

## Verification

The earlier P2a authoring recorded 20 local DOM/bridge tests, not Windows execution. The navigation repair was reviewed against the exact Git blob of webview-host.cpp/header; no local Windows compiler/runtime is available. Its actual Windows CI result must be checked before closing the gate.

The real-WebView2 test now asserts actual Unicode DOM text and inert HTML-looking content, version rejection, revoke clearing, immediate authority loss on setup, successful setup document loading, rejected data/about/foreign/stale document navigation, distinct reopen identities, queued clear/reopen, external-page isolation and both teardown orders. Temporary URI-prefix and script-dump diagnostics have been removed, not converted into product logging. All data is synthetic; the test does not contact a real channel or certify captured video.

## Next boundary

Once this exact commit passes Windows CI, connect scoped creator/device authorization and a bounded authenticated delivery worker to this display. Reuse CHZZK session/renderer modules; keep provider tokens on the service. Implement the game-PC companion lifecycle without OBS and separately qualify clean video. Public advertising, HP and rewards remain separate platform goals.

Primary references rechecked 2026-09-20:
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security

Failing baseline run: https://github.com/andongmin94/chat-view/actions/runs/35456426730
