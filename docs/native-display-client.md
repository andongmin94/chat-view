# P2c — native authenticated chat delivery

Updated: 2026-09-20 (Asia/Seoul). Original P2c code: 436e8ec41226b8ffe93d275d2a0ad2cd867f8832. Current code and package validation: [development-plan.md](development-plan.md).

## Validation status

Historical P2c code 436e8ec passed the actual nine-scenario native delivery test initially and in five repeats in Windows #292. That full run failed a separate resource test. The same runtime with per-process tracing passed full Windows #293 at f065857, including package publication.

**Current code 82f36a6 / Windows #294 / 35464979632 is not package-validated.** Initial CTest passed 25/25. Native delivery passed initially and in its first two repeats, then failed repeat 3 at `WinHTTP gateway frame rendered`; its cause is not yet determined. Resource repeat 2 also failed on initial handle growth. The setup-document correction's actual WebView2 regression passed initially and in all five repeats. No package or official-OBS qualification was produced by #294.

https://github.com/andongmin94/chat-view/actions/runs/35464979632

Do not use older successes as evidence that the intermittent current display failure is resolved. See the handoff and resource-growth-investigation.md for the exact next checks. No timeout, resource budget, cancellation or actual DOM assertion was relaxed.

CHZZK #13 / 35462872875 at 436e8ec passed all four OS/Node jobs, 135 tests, full audit (zero findings), strict types and unchanged manifests. This resource work does not change platform code/dependencies. Synthetic tests do not establish real CHZZK authorization, public TLS deployment or physical dual-PC video.

## Product path

The creator-authorized P2b display gateway has a native consumer. DisplayClient exchanges a one-use display ticket, receives the read-only WebSocket envelope and exposes one latest snapshot. NativeChatConnection attaches it to existing HudWindow and immutable NativeChatSurface. Provider authorization, public advertising and financial authority remain separate.

This is a native end-to-end developer connection, not a deployed account service or durable device enrollment. The OBS-started HUD retains parent/lifecycle/capture supervision. Testing HudWindow without an OBS parent fixture does not prove the shipping executable is a standalone gaming companion.

## Use the developer connection

Select a package only from the exact code commit's successful full Windows workflow, or use a controlled developer build. Start OBS with that build. Run the existing platform developer probe with private application credentials, authorize the channel, start chat and issue a one-use display key from its authenticated management page. Do not enter that page URL in external-chat settings. Ordinary streamers must not receive application secrets or run an exposed probe.

Press **Ctrl + Alt + Shift + C**. Enter the service origin without a path, paste the 64-character display key and connect. HTTPS is required by default. For the local developer probe only, explicitly enable the local-server option and use `http://127.0.0.1:47831`. Localhost aliases, LAN HTTP and remote HTTP remain rejected. The native key field is masked and cleared on use/close; credentials are not saved or passed in process arguments.

Closing the panel keeps the connection. Disconnect clears the private display; management-side display revocation ends it. A consumed/expired key needs fresh approval. Five-minute leases are a developer boundary, not the final manual reconnect workflow. Production renewal requires revocable device/account enrollment, not indefinite bearer validity.

External settings remain usable. Switching them replaces the owned document and ends private delivery rather than sending chat into arbitrary pages. The embedded renderer has no network, credential, native window-control or accounting authority.

## Transport and lifetime

Use OS WinHTTP WebSocket and Windows.Data.Json through the existing Windows SDK. No handwritten framing/JSON parser, new desktop framework or downloaded native dependency. Networking is asynchronous on a worker, not OBS/UI callbacks. The owner signals cancellation and clears the mailbox; the worker closes handles. Callback buffers survive final HANDLE_CLOSING and retain no HWND, WebView or owner pointer.

Redirects, implicit authentication and cookies are disabled. Exchange and WebSocket credentials use separate Authorization schemes. Scope/lifetime is checked conservatively from before exchange; consumed tickets are not retried. Credentials, raw remote reasons and chat are not logged.

HTTP replies are bounded at 4 KiB, frames at 2 MiB UTF-8, receive chunks at 16 KiB. Strict UTF-8/JSON envelope checks precede the shared JavaScript display validator. Binary, malformed or oversized data ends delivery. Fragments share a 15-second deadline rather than extending an incomplete message indefinitely.

The worker retains one latest frame; the UI permits one WebView message in flight and one coalesced pending frame. Missing render acknowledgements end delivery. Local expiry is checked by owner and worker. Transport/lease/invalid-data/system/document loss clears the surface. Capture decisions remain with the HUD; this path cannot reveal a suppressed HUD. Simultaneous local visibility and stream exclusion remain a separate qualification.

## Actual test scope

The Node fixture uses locked dependencies. The main scenario runs actual DisplayAccess/DisplayGateway -> WinHTTP -> native controls -> HudWindow/DirectComposition/WebView2 -> Unicode/inert-markup DOM assertions -> server revocation -> DOM clearing. A heartbeat does not stand in for rendered text.

Eight more real HTTP/WebSocket scenarios cover pending-exchange cancellation, local expiry despite continued traffic, redirect rejection, wrong scope, malformed JSON, binary input, oversized frames and idle delivery. Synthetic credentials use inherited stdin, not command lines. No NAVER account or capture hardware is used.

Native test builds require Node and `cd platform && npm ci --ignore-scripts`. Node is a test prerequisite, not a desktop runtime dependency. All native tests/five repeats, resource limits and OBS/package assertions remain. Separate CHZZK CI retains full audit and strict types. Local source/fixture checks are distinct from remote Windows execution and physical-workstation acceptance.

## Primary references

- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketreceive
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketcompleteupgrade
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nc-winhttp-winhttp_status_callback
- https://learn.microsoft.com/en-us/uwp/api/windows.data.json.jsonobject
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/navigation-events
