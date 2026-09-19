# P2c — native authenticated chat delivery

Updated: 2026-09-20 (Asia/Seoul). Starting OBS commit: f5390988e4692dbd005fb8abf65f7930e57ddefd. Tested corrected code: **436e8ec41226b8ffe93d275d2a0ad2cd867f8832**.

## Validation status

The new native display CTest passed all nine actual HTTP/WebSocket/UI scenarios and all five additional repeats in **Windows #292 / 35462872896**. The initial full native suite passed 25/25. **The overall run nevertheless failed:** the last repeat of the pre-existing resource-soak test measured late handle growth of 104 against the unchanged limit of 64. Official OBS/package stages were skipped. No validated P2c release package is claimed. See [development-plan.md](development-plan.md) for exact samples and the remaining investigation.

CHZZK contract #13 / 35462872875 passed all four OS/Node jobs, 135 tests, full audit (zero findings), strict types and unchanged manifests. All provider identities/messages in these tests are synthetic. Real CHZZK authorization, public TLS deployment and physical dual-PC video remain unverified.

## Product path

The existing creator-authorized P2b display gateway now has a native consumer. DisplayClient exchanges a one-use display ticket, receives the existing read-only WebSocket envelope and exposes one latest snapshot. NativeChatConnection attaches that consumer to the existing HudWindow and immutable NativeChatSurface. Provider authorization/session code and the public-ad/financial boundary remain separate.

This is the native end-to-end **developer connection** path, not a deployed account service or durable device enrollment. The usual OBS-started HUD retains its parent/lifecycle/capture supervision. Independent game-PC executable lifecycle, OBS-free clean video, real CHZZK authorization, campaigns and payouts remain separate work. Testing HudWindow directly without a parent fixture does not prove that the shipping companion operates independently of OBS.

## Use the current developer connection

Until the full package gate passes, use a controlled developer build rather than treating this commit as a verified release. Start OBS with that native build. Start the existing platform developer probe with private credentials, authorize the CHZZK channel, start chat and issue a one-use display key from its authenticated management page. **Do not enter that page URL in external-chat settings.**

Press **Ctrl + Alt + Shift + C** to open the native connection panel. Enter the service origin (no path), paste the 64-character display key and connect. HTTPS is mandatory by default. For the explicit local developer probe only, select the unchecked-by-default local-server option and enter `http://127.0.0.1:47831`. HTTP on localhost aliases, LAN addresses or remote hosts remains rejected. The panel is a native window, not a privileged web page. Its key field is masked and cleared on connect/close; credentials are not saved in settings or passed in process arguments.

Closing the panel does not disconnect. Disconnect stops local delivery and clears the owned document; revoking display connections in the creator page also ends delivery. A consumed/expired key needs new approval. The P2b five-minute lease is a developer boundary, not a product requirement to reconnect manually every five minutes. Production renewal needs the future revocable device/account flow, not indefinite bearer validity.

The external-page HUD remains available. Changing its settings replaces the owned document and ends the private connection rather than forwarding private messages into another page. The internal renderer has no network, credential, window-control or financial authority.

## Native transport and lifetime

The transport uses OS WinHTTP WebSocket and Windows.Data.Json through the Windows SDK. No handwritten framing/JSON parser, new desktop framework or downloaded native dependency is added. Network operations use asynchronous WinHTTP on a dedicated worker, never OBS/UI callbacks. The owner thread signals cancellation and clears its mailbox immediately; only the worker closes its handles. Callback state/buffers survive until WinHTTP's final HANDLE_CLOSING notification. Callbacks retain no HWND, WebView or owner pointer.

The client rejects redirects, automatic HTTP authentication and cookies. Exchange and WebSocket credentials use separate Authorization schemes. It checks scope/lifetime, counts local expiry conservatively from before exchange, and never retries a consumed ticket. Remote close reasons, error bodies, credentials and chat text are not added to product logs.

HTTP replies are capped at 4 KiB and chat frames at 2 MiB UTF-8, with 16 KiB receive chunks and strict UTF-8/JSON envelope checks. Full display-field validation remains shared with the immutable JavaScript receiver. Binary, malformed or oversized data ends delivery. Fragments must complete within the same 15-second deadline; fragments cannot keep an incomplete message alive indefinitely.

The worker retains one coalesced latest frame. The UI retains at most one newest pending frame and permits one WebView message in flight until its render acknowledgement; missing acknowledgements end the connection. The lease is checked on the owner thread and worker. Transport/lease/invalid-data/system/document loss ends delivery and clears the owned surface. Host navigation/capture decisions remain with the HUD; this feature does not change the capture interlock or show a suppressed HUD. Simultaneous local readability and clean output still need the separate capture qualification.

## Actual test scope

The Node fixture uses the already locked platform dependencies. The main scenario exercises actual DisplayAccess/DisplayGateway, native WinHTTP, native connection controls, HudWindow/DirectComposition/WebView2, actual Unicode/inert-markup DOM assertions, server revocation and actual DOM clearing. It does not mock WinHTTP or infer rendered text from a heartbeat.

Eight other actual HTTP/WebSocket scenarios cover cancellation with exchange headers withheld, local expiry against continuing server messages, redirect rejection, wrong scope, malformed JSON, binary input, oversized frames and idle delivery. Fixture credentials travel through inherited stdin, not logs or command-line arguments. All owners/messages are synthetic; no NAVER credentials or capture hardware are involved. These nine scenarios are part of the CTest that passed its initial run and five repeats.

Windows test builds require Node and `cd platform && npm ci --ignore-scripts`, in addition to the existing native prerequisites. The Windows workflow installs the locked graph, checks unchanged manifests, then retains all CTests/five repeats and existing OBS/package checks. The separate CHZZK matrix retains full dependency audit and strict types. Node is a test prerequisite, not a runtime bundled for streamers. No prior resource/capture budget or assertion was relaxed.

Local authoring performed source/blob verification and Node syntax/LF-CRLF checks. Actual Windows execution above is remote CI evidence, not a local desktop test. Native tests do not close the live-provider, public-account, game-PC lifecycle, physical-capture or advertising gates.

## Primary references checked 2026-09-20

- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketreceive
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketcompleteupgrade
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nc-winhttp-winhttp_status_callback
- https://learn.microsoft.com/en-us/uwp/api/windows.data.json.jsonobject
