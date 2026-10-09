# Native private-chat connection

Implementation: `DisplayClient` (WinHTTP), `NativeChatConnection` (native controls), `NativeChatSurface` (immutable renderer). [PRODUCT.md](../PRODUCT.md) owns goals and [development-plan.md](development-plan.md) owns evidence/remaining work. This contract does not certify live CHZZK, physical video exclusion or distribution.

## Entry and role

**Ctrl+Alt+Shift+C** and **ChatView connection...** in the OBS Control Center open the same native panel. Capture exclusion, lock/suppression, input/placement and lifecycle protections remain in force. Opening/reconnecting cannot override them. Explicit `--companion` requires no local OBS; OBS-controlled launch validates its parent and transport. Invalid OBS arguments never fall back to companion.

Launch fixes request intent: companion is `gaming`, OBS-managed runtime is `streaming`. The read-only label neither grants authority nor changes execution mode. One-PC OBS-managed use also has the streaming role; it does not imply a second physical PC.

The panel accepts a canonical HTTPS service origin without a path. Literal `http://127.0.0.1` requires developer opt-in. WinHTTP disables cookies, automatic authentication and redirects, without disabling certificate validation. Provider secrets never enter the native client. Native credentials stay out of URLs, command lines, logs, UI mailboxes, OBS and chat HTML.

**로그인 / 연결** creates a verifier/challenge with fixed role, opens the validated `/login/<id>` in the system browser and displays the confirmation code. The browser authenticates and explicitly confirms the channel/requested role. Polling requires the original verifier; the landing URL cannot collect credentials. There is no role picker, device enrollment or manual display key. Management content never runs in the private chat WebView.

## Control Center and display status

Control Center separates **ChatView service connection** from **External chat page URL (optional)**. Opening the connection panel does not save/apply a URL, start login, change approval or navigate chat. It targets the current HUD using bounded local messages and revalidates current state. The HUD independently checks suppression, capture risk and its exclusion affinity, including an existing panel's setting. Refusal or timeout is not a successful open or server approval.

The native panel is an opaque `WS_EX_LAYERED` window with `LWA_ALPHA` 255. Creation must set and read back `WDA_EXCLUDEFROMCAPTURE` before showing. Hidden-panel reopening refuses a lost setting rather than resetting it silently. This follows the documented layered-window prerequisite of [GetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowdisplayaffinity), not a guarantee of physical video exclusion. It does not replace the main DirectComposition HUD.

`native-chat-control.hpp` defines `ChatViewOBS.OpenNativeChat.v1` and `ChatViewOBS.QueryNativeChat.v2`. Both require zero WPARAM/LPARAM. No pointers, URLs, credentials, account/session IDs, role changes, output commands or accounting authority cross this channel. Open returns 1 only for a visible protected panel, otherwise 0. Query returns a bounded enum and validates full reply width. The old v1 query is removed; matching native/config builds are required.

Existing 40 ms query/1500 ms open bounds remain. Foreground permission targets only that HUD process and cannot override protection. Same-thread SetWindowSubclass keeps handling with NativeChatConnection; close/WM_NCDESTROY detaches it. Local window messages coordinate UI; they do not authenticate arbitrary local processes.

Both windows derive presentation from the same enum: idle, approval pending, connecting, receiving, reconnecting, signing out, protection, worker stopping, stopped with return available, external selected, and external selected with return available. Unknown replies clear previous indicators. Receiving requires the active client, validated metadata, ready own document and acknowledged subscribed frame; an old external page's Ready state cannot satisfy new consent. The connection panel separately marks the **first local text render acknowledgement** only after a subscribed nonempty text row is accepted and WebView2 acknowledges its DOM render. An empty subscribed frame never grants first-text status. Stop, logout, page switching and a new own document clear the marker. This is neither verified on-screen readable pixels nor clean OBS output or audience exposure.

Inactive selection comes from the actual WebViewHost document, not the URL editor or saved config. Selected does not mean loaded successfully. Empty URL Apply selects setup. A retained in-run approval and completed worker make return locally requestable, not guaranteed server-valid. Protection, draining work and logout disable return. The same enum controls return and read-only status without another approval store; text changes only with phase changes.

## External switch and same-approval return

**Apply external page** stops native display/report delivery and detaches the own document before HudWindow navigates, even during initial approval/frame receipt. It cancels automatic native reconnect intent. Ordinary switch/stop does not revoke server approval.

**현재 승인으로 자체 채팅 복귀** waits for prior workers, then renews the exact current client's private origin, role and pinned membership. It ignores edited controls/unrelated saved accounts, opens no fresh browser consent and does not persist a memory-only approval. Missing approval, duplicates and protection are rejected. A fresh own document and receive connection must become ready before rendering; external content cannot acknowledge native readiness.

Renewal preserves role, broadcast-session ID and connection ID. Server denial removes authority; substituted membership never displays private chat. Logout intent forbids return even after failure, leaving explicit logout retry. Switching stops the report worker but sends no OBS stop command; remote observations expire normally.

## Existing management pages in the system browser

**계정·시험 광고·활동 관리** explicitly opens the original validated service plus fixed `/account`; existing links lead to `/campaigns` and `/campaigns/activity`. Original origin is retained only after accepted DisplayClient.start. No edited service field, external URL or different DPAPI account is used. The navigation has no native token, account/connection ID, query, fragment or caller-selected redirect path.

The button is available for receiving or locally resumable stopped/external connections. It rechecks protection/affinity, rejects reentry and does not act during approval, draining, protection or logout. Browser-launch failure does not cancel chat, reset membership or change the WebView; another attempt requires a user action. The browser is outside the HUD's capture protection, and opening it proves neither page loading nor authentication.

The browser cookie determines the management account. Native credentials are never accepted in its place. Without a live browser session, the service reuses CHZZK OAuth for browser-only identity verification, followed by explicit channel confirmation before returning to the fixed requested page. Those transient provider tokens do not replace stored grants or restart existing chat. Recovery creates no new PC/role approval. Provider-side token behavior still requires live-account acceptance.

Current browser channel is shown on management pages. Switching/cancelling drops that browser's cookie, CSRF and in-flight attempt. The user selects the desired provider account at CHZZK; the service invents no provider account-selector parameter. Neither browser identity nor navigation automatically matches or changes the native account. Expired management mutations are denied, never replayed after recovery. Ordinary browser logout leaves native approvals intact.

### Browser document navigation

HTML form documents use `Referrer-Policy: same-origin`, while exact Origin and CSRF checks remain mandatory. Do not accept `Origin: null` or missing/foreign Origin to make a form work. Applying `no-referrer` to form documents suppresses the browser's POST Origin; same-origin policy keeps it without forwarding a Referer to external sites. Callback redirects and failure documents retain `no-referrer`, including recovery links, so authorization queries are not forwarded. JSON/public routes retain their existing policy and authority.

Fetch Metadata can retain a cross-site classification through the provider redirect chain. Successful callback creates one short return allowance on the newly rotated browser cookie, for the exact next `/account/confirm` or initiating `/login/<id>` top-level GET. It expires after at most thirty seconds or original authorization expiry, whichever is earlier. The first matching navigation consumes it even if labelled same-origin. Other paths, queries, fetches, iframes, POSTs and old cookies remain denied. New attempts/cancel/logout invalidate it with the browser context. This is navigation permission, not channel/PC consent.

Browser Back can revisit a consumed confirmation with the original cross-site metadata. That still returns 403, but a top-level confirmation navigation receives a generic recovery link instead of raw JSON. It contains no owner, pending identity, form or CSRF value, and does not regenerate the allowance. Normal same-origin refresh still checks current pending consent. Cancel, Back, expiry and provider failure cannot restore old management authority.

## Renewal, persistence and logout

Every approved connection receives in-run `chat:renew`. **이 PC에서 연결 유지** only selects user-scoped DPAPI storage. Login/renewal require one role header. Wrong-role renewal returns 409 before replacing a valid lease; native RoleMismatch retains saved approval for the original mode or explicit logout/reconsent. Gaming is not silently promoted.

Normal service restart restores the same approval/membership through protected provider storage. Provider grants/native approvals have different scopes. Confirmed 401/403 during renewal deletes matching current/remembered authority. Temporary failure uses bounded one-to-thirty-second retry without replaying a one-use ticket.

**First-display failure and explicit recovery:** A rejected native renderer frame, failed own-document navigation, loading/ACK timeout or publish error clears the private view and stops delivery. The panel explains that after the worker ends, **현재 승인으로 자체 채팅 복귀** can be selected if the *same in-run approval* remains valid. This is an explicit action, not an automatic provider login or fallback to another DPAPI account; its enabled state is governed by the existing `IdleResumable` status and retained approval. A missing/revoked approval still requires new consent. A synthetic native switch case sends a deliberately invalid versioned frame to the actual WebView2 renderer before first text, asserts blank/private state and stopped worker, then uses the actual button to renew the same role/session/connection and render the first text after recovery. No timeouts or server protocol are widened.

**연결 중지** and process exit clear delivery, not authority. **로그아웃** calls DisplayClient::sign_out for the exact current approval, remembered or not. It immediately clears chat/status/reconnect intent; after display/report I/O ends, the worker performs matching cleanup/server signout. Worker completion and logout intent share a mutex so exit cannot lose a request.

The worker holds the DPAPI file while comparing/deleting only matching credentials. Another saved account remains intact. Locked, corrupt or unreadable storage is an error. A never-started client can explicitly sign out dormant saved approval; a current context cannot substitute another account when its approval is missing.

Only authenticated `signedOut: true` yields SignedOut. Cleanup/server failure retains the exact credential only in memory for explicit retry without recreating storage/restarting chat. New login/process exit discards retry authority. Repeated clicks do not duplicate requests; ordinary stop cannot cancel active logout. Shutdown may cancel bounded I/O; lost replies do not prove whether revocation occurred. Account connection management recovers when the process ended or approval was lost before confirmation.

## Connection, video and renderer boundaries

Authenticated frames contain own membership, same-session distinct open-display counts, unverified capture state and scoped recent output reports. These are not OBS attestation, audience measurement or reception proof. Half-open sockets may remain counted until retired. Streaming/recording reports expire and inspect no receiver pixels.

WinHTTP validates UUIDs, exact membership, bounded integer counts (four total/one streaming including itself) and unverified capture state before publication. It removes connection metadata before chat rendering. Stop/error/retry/expiry clears old metadata/chat under existing lease and fifteen-second local liveness limits.

The persistent **수신 영상의 HUD 제외: 미검증** notice is independent of phase and temporary messages. Control Center retains **LOCAL HUD READY / HUD NOT READY**, scoped **OBS capture check**, and **Audience video: NOT VERIFIED**. Local readiness/return eligibility do not extend capture support.

The immutable document renders at most 100 bounded text rows, without arbitrary-page injection, under nonce-bound readiness. On a fresh own-document open, a cancelled prior setup navigation cannot invalidate the new receiver before its reserved host URL is installed. Unowned/cancelled prior navigation never confers readiness; subsequent unexpected navigation after binding still invalidates private delivery. The WebView2 fixture queues setup/reload and new own-page transitions without pumping between requests, then checks first actual DOM rows. External callbacks cannot authorize native actions. Rendering acknowledgement is local only. UTF-8 accumulation is limited to 2 MiB; binary/malformed/oversized input terminates delivery. Async WinHTTP retains callback buffers through handle closing and keeps network waits out of UI/OBS callbacks. UI/receipt/acknowledgement remain bounded separate stages.

## Regression boundaries

Portable tests cover enum decoding, full-width invalid replies, selection/worker/return combinations and text limits. Actual config/IPC fixtures use synthetic HUD replies. Native switch tests retain six scenarios and launch the actual config executable beside production HUD components hosted in a test executable. They cover open/reopen, edit versus Apply, empty Apply, same-approval return, stop/logout, protected panel properties, original management target, launch failure and text bounds. Browser launch is a callback seam there, not a real browser session.

The separate Chromium management flow uses actual browser-generated cookies, Origin and Fetch Metadata with real service HTTP/SQLite/WebSocket. It exercises cross-site provider round trips, confirmation, account switch, expiry, cancellation, Back and native-role consent, while checking original approvals/grants/campaign selection and existing chat remain intact. Only the external provider document/API is synthetic. Because Playwright route() skips redirected URLs, its documented CDPSession Fetch adapter supplies that document; service requests/responses and security policies are not overridden. Artifacts contain synthetic screenshots and redacted metadata, not cookie values, queries, tokens, HAR or browser traces.

This browser fixture uses explicit literal-loopback HTTP and one pinned Chromium. It does not certify real CHZZK, production TLS/certificates, other browsers, a native-to-system-browser launch, full OBS/packaged HUD or physical two-PC video. Exact checks, hashes and open failures belong in the development plan. Routine and flow checks are not distribution qualification.
