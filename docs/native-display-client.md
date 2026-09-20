# Native HUD, display and companion contract

Updated: 2026-09-20. Current implementation and verification: [development-plan.md](development-plan.md). Server protocol: [display-delivery.md](display-delivery.md). This contract does not impose a separate roadmap or full-CI development gate.

## Existing native path

CHZZK/session modules -> scoped gateway -> asynchronous WinHTTP `DisplayClient` -> `NativeChatConnection` -> existing `HudWindow`/`NativeChatSurface`/WebView2 -> shared first-party text renderer. These components are implemented, not a future handoff to build again. Private provider credentials stay on the service side; the native viewer does not load the developer management page.

### Launch roles

OBS-controlled startup takes the validated parent/mapping/event arguments, retains capture-state supervision and exits with OBS. Explicit `chat-view-hud.exe --companion` has no local OBS parent/mapping requirement, shows a development warning, opens the same connection controls after readiness, rejects duplicate companion instances and exits with **Ctrl+Alt+Shift+Q**. Launching without arguments or with malformed/mixed modes fails; it never silently selects companion. No second renderer or desktop runtime is introduced.

The current companion is a developer entry point, not a finished two-PC service or installer. It does not know the streaming PC's capture state. The initial warning defaults to refusal and explains that cloned/HDMI output may contain the HUD. Preserve capture-affinity and local lifecycle checks; accepting the warning is not a clean-video certificate. Real two-PC use requires the separately verified video path in [architecture.md](architecture.md).

### Connect and control

Prepare private developer credentials using [platform/README.md](../platform/README.md) and start the service. With the HUD running, **Ctrl+Alt+Shift+C** opens origin/login/disconnect/logout controls; companion opens them initially. Enter a service origin without a path and choose **로그인 / 연결**. HTTPS is default; only the explicit local option permits literal `http://127.0.0.1:<port>` (the developer probe uses 47831). Localhost aliases and remote/LAN HTTP remain rejected.

The native client creates a random verifier, sends only its SHA-256 challenge to the service, and opens the exact request-bound `/login/<id>` URL in the system browser. The browser shows a short request code and requires explicit creator consent for the resolved channel. The verifier stays in native memory and the URL/id alone cannot collect the resulting chat credential. Revocation or owner reauthorization invalidates approvals that have not yet been collected.

Every approved connection gets a renewable `chat:renew` session for the current run. **이 PC에서 연결 유지** only controls whether that renewal credential is persisted across restarts. Persisted origin/renewal credential are protected with Windows user-scoped DPAPI; provider credentials and chat content are not stored there. Disconnect stops current display while a remembered login can reconnect later. Logout deletes local protected state and requests server-side revocation. **Ctrl+Alt+Shift+H** retains existing move/resize and click-through locking.

## Transport and UI lifetimes

DisplayClient uses OS WinHTTP WebSocket and Windows JSON/UTF-8 handling on a dedicated worker, not OBS/UI callbacks. Cancellation immediately clears the mailbox; only the worker closes network handles. Callback-owned buffers survive until HANDLE_CLOSING and hold no HWND/WebView/OBS owner pointer. Redirects, cookies and implicit HTTP authentication are disabled. Credential classes are separate. Browser verifiers, one-use compatibility tickets, short display tokens and renewal credentials are never interchangeable or replayed as another class.

Exchange responses are capped at 4 KiB, frames at 2 MiB UTF-8 with 16 KiB receive chunks. Strict UTF-8/envelope checks precede rendering, with shared field validation in the immutable JavaScript receiver. Binary, malformed and oversized input ends delivery. The 15-second operation/frame deadline also bounds fragments, and local lease/idle expiry applies even when the server does not close the socket.

The worker retains one newest snapshot; the UI has one coalesced pending frame and at most one unacknowledged WebView delivery. Missing acknowledgements end the connection. Transport, lease, system or document loss stops private delivery. External-page DOM recovery does not supervise a first-party document. A settings change replacing that document cannot send its chat into the external replacement.

## Host-owned renderer boundary

The renderer is embedded in the binary using the existing shared JavaScript. `WebResourceRequested` serves the exact fresh `https://chatview.invalid/<document-id>/index.html` from memory. It is not a public service, local HTTP listener or arbitrary external URL exception. Stale paths/methods/non-document requests get 404. Only native code installs the document.

Ownership requires the same host/browser instance, selected full document identity, correlated successful navigation and nonce-bound readiness. The nonce correlates documents, not accounts/devices. Cancelled superseded navigations do not authorize old content or invalidate a newer binding. Foreign navigation never grants authority. Closing the surface replaces a loaded private document with restricted blank content; teardown invalidates the binding. The repeated static setup request retains the same document; explicit reload still replaces it for recovery.

After readiness, `PostWebMessageAsJson` delivers the versioned snapshot, never executable script/HTML interpolation. CSP/embedded-script nonce, exact bridge/source checks and bounded validation remain. At most 100 nickname/content/time rows become text nodes; invalid or non-subscribed data clears text. No remote media, provider secrets, arbitrary native window commands or payout operations are exposed. Render acknowledgements describe DOM handling, not actual audience attention or capture exclusion.

## Tests and unresolved acceptance

Portable launch-options tests cover explicit/invalid/mixed roles, PID bounds, duplicate options and owned argument strings. The companion Windows test launches the real executable with no supplied OBS parent, answers consent, inspects the existing connection controls and loaded modules, tests duplicate launch and normal quit. It does not prove OBS is absent from the runner's filesystem or certify two-PC video.

Native display integration uses an actual local HTTP/WebSocket gateway, browser-approval fixture, WinHTTP, protected remembered state, native controls and actual WebView2 DOM for Unicode/inert markup, renewal/reconnect/revocation clearing, cancellation, expiry and invalid transport. Surface tests cover memory-document ownership, setup idempotence and both teardown orders. All identities/messages are synthetic. The historically observed intermittent first-render failure remains open unless specifically resolved; a new launch or routine CI pass alone does not close it. Exact current results and affected claims are in development-plan.md, not duplicated here.

Node is a development fixture prerequisite, not shipped as a HUD runtime. No part of this contract claims general game/fullscreen compatibility, live CHZZK authorization, a deployed multi-user account service, clean HDMI or real rewards.
