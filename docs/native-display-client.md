# P2c — native authenticated chat delivery

Updated: 2026-09-20 (Asia/Seoul). Starting OBS commit: f5390988e4692dbd005fb8abf65f7930e57ddefd.

## Product path

The existing creator-authorized P2b display gateway now has a native consumer. `DisplayClient` exchanges a one-use display ticket, receives the existing read-only WebSocket envelope and exposes one latest snapshot. `NativeChatConnection` attaches that consumer to the existing `HudWindow` and immutable `NativeChatSurface`. Provider authorization/session code and the public-ad/financial boundary remain unchanged.

This is the native end-to-end **developer connection** path, not a deployed account service or durable device enrollment. The usual OBS-started HUD retains its parent/lifecycle/capture supervision. An independent game-PC executable lifecycle, OBS-free clean video, real CHZZK authorization, campaigns and payouts are still separate work. Testing HudWindow directly without a parent fixture is not proof that the shipping companion operates independently of OBS.

## Use the current connection path

Start OBS with the validated native package. Start the existing `platform` developer probe in its private environment, authorize the CHZZK channel, start chat and issue a one-use display key using its authenticated management page. **Do not enter that page URL in external-chat settings.**

Press **Ctrl + Alt + Shift + C** to open the native connection panel. Enter the service origin (no path), paste the 64-character display key and connect. HTTPS is mandatory by default. For the explicitly local developer probe only, select the unchecked-by-default local-server option and enter `http://127.0.0.1:47831`. HTTP on localhost aliases, LAN addresses or remote hosts remains rejected. The panel is a native window, not a privileged web page. Its key field is masked and cleared on connect/close; credentials are not saved to settings or passed in process arguments.

Closing the panel does not disconnect. Its Disconnect action stops local delivery and clears the owned document; revoking display connections in the creator page also ends active delivery. A consumed/expired key needs a new approval. The P2b five-minute lease is still a developer boundary, not a product requirement to reconnect manually every five minutes. Production renewal must use the future revocable device/account flow, not longer-lived public bearer strings.

The existing external-page HUD remains available. Changing its settings replaces the owned document and ends the private connection rather than forwarding private messages into another page. The internal renderer still has no network, credential, window-control or financial authority.

## Native transport and lifetime

The transport uses the OS WinHTTP WebSocket implementation and Windows.Data.Json through the Windows SDK. No handwritten wire framing/JSON parser, new desktop framework or downloaded native dependency is added. Network operations use asynchronous WinHTTP on a dedicated worker, never OBS/UI callbacks. The owner thread signals cancellation and clears its mailbox immediately; only the worker closes its handles. Callback state/buffers remain alive until WinHTTP's final HANDLE_CLOSING notification. Callbacks retain no HWND, WebView or owner pointer.

The client rejects redirects, automatic HTTP authentication and cookies; exchange and WebSocket credentials use their separate Authorization schemes. It validates scope and lease lifetime, counts local expiry conservatively from before the exchange request, and never retries a consumed ticket. No remote close reason, error body, credentials or chat text enters product logs.

HTTP replies are capped at 4 KiB and chat frames at 2 MiB UTF-8, with bounded 16 KiB receive chunks and strict UTF-8/JSON envelope checks. Full display-field validation remains shared with the immutable JavaScript receiver. Binary, malformed or oversized data terminates delivery. A fragmented message must complete within the same 15-second deadline; fragments cannot keep an incomplete frame alive indefinitely.

The worker retains one coalesced latest frame. The UI retains at most one pending latest frame and permits one WebView message in flight until its render acknowledgement; missing acknowledgements end the connection. The lease is rechecked on the owner thread as well as the worker. Transport loss, expiry, rejected display data and system/host lifecycle loss end delivery and clear the owned surface. Host navigation/capture decisions remain in the existing HUD; this feature never shows a suppressed HUD or weakens capture exclusion. A readable local HUD and clean broadcast still require the separately specified capture qualification.

## Verification contract

The new CTest launches a Node fixture using the **already locked** platform dependencies. The main scenario exercises the actual DisplayAccess/DisplayGateway, native WinHTTP client, native connection controls, existing HudWindow/DirectComposition/WebView2, actual Unicode/inert-markup DOM assertions, server revocation and actual DOM clearing. It does not use a mocked WinHTTP call or infer pixels from a heartbeat.

Other actual HTTP/WebSocket scenarios cover cancellation while exchange headers are withheld, local expiry against a server that keeps sending, forbidden redirect, wrong scope, malformed JSON, binary input, oversized frames and an idle peer. Fixture credentials travel through inherited stdin, not logs or command-line arguments. All messages and owners are synthetic; no NAVER credentials or physical capture hardware are involved.

Windows test builds now require Node and `cd platform && npm ci --ignore-scripts`, in addition to the existing native prerequisites. The existing Windows workflow installs the locked graph, confirms manifests are unchanged, then runs all native tests and five repeats plus its existing OBS/package checks. The separate CHZZK matrix retains full dependency audit and strict types. No prior resource/capture budget or assertion is relaxed.

At initial authoring only source review, exact baseline blob verification and Node syntax checking were possible locally; there is no local Windows toolchain or npm network access. Read the exact code commit's CI in development-plan.md before claiming the new native tests passed. A fixture pass will still not certify a real CHZZK channel, public TLS deployment or two-PC clean video.

## Primary references checked 2026-09-20

- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketreceive
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketcompleteupgrade
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle
- https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nc-winhttp-winhttp_status_callback
- https://learn.microsoft.com/en-us/uwp/api/windows.data.json.jsonobject
