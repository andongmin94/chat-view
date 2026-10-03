# Native private-chat connection

Implementation: `DisplayClient` (WinHTTP), `NativeChatConnection` (native controls), `NativeChatSurface` (immutable renderer). [PRODUCT.md](../PRODUCT.md) owns goals and [development-plan.md](development-plan.md) owns evidence/remaining work. This contract does not certify live CHZZK, physical video exclusion or distribution.

## Entry and role

**Ctrl+Alt+Shift+C** and **ChatView connection...** in the OBS Control Center open the same native panel. Existing capture exclusion, lock/suppression, display/input/placement and lifecycle protections remain in force. Opening or reconnecting cannot override them. Explicit `--companion` does not require local OBS; OBS-controlled launch validates its parent and transport. Invalid OBS arguments never fall back to companion.

The launch fixes request intent: companion is `gaming`, OBS-managed runtime is `streaming`. The read-only label neither grants authority nor changes execution mode. One-PC OBS-managed use also has the streaming role; it does not imply a second physical PC.

The panel accepts a canonical HTTPS service origin without a path. Literal `http://127.0.0.1` requires explicit developer opt-in. WinHTTP disables cookies, automatic authentication and redirects; certificate validation is not disabled. Provider secrets never enter the native client. Native credentials stay out of URLs, command lines, logs, UI mailboxes, OBS and chat HTML.

**로그인 / 연결** creates a verifier/challenge with fixed role, opens the validated `/login/<id>` URL in the system browser and shows the matching confirmation code. That browser authenticates and explicitly confirms the channel and requested role. Polling requires the original verifier; the landing URL cannot collect credentials. There is no separate role-picker path, device enrollment or manual display key. Management content never runs in the private chat WebView.

## Control Center and display status

Control Center separates **ChatView service connection** from **External chat page URL (optional)**. Opening the connection panel does not save/apply a URL, start login, change approval or navigate chat. It targets the current HUD using bounded local messages and validates its current state again on click. The HUD independently checks suppression, capture risk and its exclusion affinity, including the existing panel's affinity. Refusal or timeout is not a successful open or server approval.

The native panel is an opaque `WS_EX_LAYERED` window with `LWA_ALPHA` set to 255. Before first showing it, creation must set and read back `WDA_EXCLUDEFROMCAPTURE`. Hidden-panel reopening still checks the existing setting and refuses a lost setting rather than silently resetting it. This follows the documented layered-window prerequisite of [GetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowdisplayaffinity), not a guarantee of physical output exclusion. The main DirectComposition HUD is not converted or replaced by this panel change.

`native-chat-control.hpp` defines `ChatViewOBS.OpenNativeChat.v1` and **`ChatViewOBS.QueryNativeChat.v2`**. Both require zero WPARAM/LPARAM; no pointers, URLs, credentials, account/session IDs, role changes, output commands or accounting authority cross this channel. Open returns 1 only for a visible existing protected panel; otherwise 0. Query returns a bounded enum and validates the full reply width. The old v1 query is removed, not retained as a fallback. Matching native/config builds are required.

Existing 40 ms query and 1500 ms open bounds remain. Foreground permission is directed only to that HUD process, never a visibility override. Same-thread SetWindowSubclass keeps the handler with NativeChatConnection rather than replacing the main HUD procedure. Close and WM_NCDESTROY detach it. Local window messages are UI coordination, not authentication or attestation of arbitrary local processes.

Both Control Center and the panel derive their presentation from the same enum. It distinguishes idle, awaiting approval, connecting, receiving, reconnecting, signing out, local protection, worker stopping, stopped with a return request available, external page selected, and external page selected with a return request available. Unknown replies clear the prior indicator. Only an active client, validated metadata, ready own document and acknowledged subscribed frame yield Receiving. Pending consent never inherits an old external page's Ready state.

Inactive selection comes from WebViewHost's current selected document, not the URL editor or the saved external config. **Selected does not mean loaded successfully.** Empty URL Apply chooses setup, not an external page. A retained in-run approval and completed worker make return locally requestable; this is not a promise of current server validity. Protection, draining work and logout disable return. The same enum controls the existing return button and read-only panel status, without a second approval store. Status text changes only when the phase changes.

## Explicit external switch and same-approval return

**Apply external page** uses the existing configuration action. The native handler stops display/report delivery and detaches its document before HudWindow navigates, including when the first approval/frame is still pending. It cancels automatic native reconnect intent. Ordinary switch/stop does not revoke the server approval. The panel refreshes selected-display status after the host processes that action.

**현재 승인으로 자체 채팅 복귀** waits for prior display/report workers to finish, then renews the exact current client's private origin, role and pinned membership through the existing endpoint. It ignores edited origin/remember controls and unrelated saved accounts. It does not open fresh browser consent or write a memory-only approval to DPAPI. Duplicate requests, missing approval and protection are rejected. A fresh own document and new connection must be ready before rendering; stale external content does not count as a native acknowledgement.

Renewal must preserve the role, broadcast-session ID and connection ID. Server denial removes current authority; a substituted membership never displays private chat. Explicit logout intent forbids return even after logout failure, leaving only explicit logout retry. Switching stops the report worker but does not issue an OBS stop command; remote output observations expire according to their existing lifetimes.

## Existing management pages in the system browser

**계정·시험 광고·활동 관리** opens the original validated service origin plus the fixed `/account` route. Existing account links lead to `/campaigns` and `/campaigns/activity`. It is an explicit navigation request, not a new account, native approval, cookie exchange or SSO mechanism. The original origin is retained only after `DisplayClient.start()` accepts it; no editable service field, external URL or different DPAPI account is used for this action. The target has no native token, account/connection ID, query, fragment or caller-chosen redirect path.

The button is available for the existing receiving connection or a locally resumable stopped/external connection. It rechecks current protection and panel affinity at the click, rejects reentry and does not act during approval, draining, protection or logout. A failed browser launch reports failure without cancelling private chat, resetting membership or changing the displayed WebView document. Retrying requires another user action.

**The browser's cookie determines the management account.** Opening the address does not guarantee that it matches the native approval. A persistent panel notice asks the user to check the channel in the opened page and warns that the system browser is outside the HUD's capture protection. Native credentials do not authenticate these management routes. Another browser account stays independent. Without a browser session, the existing account page shows its sign-in guidance and protected campaign/activity routes remain denied; no native authority is silently promoted to restore that browser session.

Browser logout and native logout remain distinct. A successfully opened browser is not confirmation that the page loaded, that it is signed in or that an account change succeeded. The public address conveys no management capability to the private renderer, Control Center IPC or another process.

## Renewal, persistence and logout

Every approved connection receives in-run `chat:renew`. **이 PC에서 연결 유지** only selects user-scoped Windows DPAPI storage for app restart. Login/renewal require one `X-ChatView-Role`. Wrong-role renewal returns 409 before replacing a valid lease; native RoleMismatch keeps the saved approval for its original mode or explicit logout/reconsent. Gaming is never silently promoted.

Normal server restart can restore the same approval/membership through protected provider storage. Provider grants and native approvals remain separate scopes. Confirmed 401/403 during renewal deletes the matching remembered/current approval. Temporary failure uses bounded one-to-thirty-second retry without replaying a one-use ticket.

**연결 중지** and normal process exit clear delivery, not server authority. **로그아웃** calls `DisplayClient::sign_out()` for the exact current approval, remembered or not. It clears chat/metadata and automatic reconnect immediately; the worker performs matching local cleanup and server signout after display/report I/O ends. Worker completion and logout intent share a mutex so a request cannot be lost at worker exit. Origin remains bound to that approval.

The worker holds the DPAPI file while comparing/deleting only matching credentials. Another saved account remains intact. Locked/corrupt/unreadable storage is an error, not successful cleanup. A client that has never started can explicitly sign out its dormant saved approval; a current context never substitutes an unrelated account when its approval is missing.

Only `signedOut: true` from the authenticated endpoint yields SignedOut. Failed cleanup/signout retains the exact credential only in memory for explicit retry without recreating storage or restarting chat. New login/process exit discards retry authority. Repeated logout clicks do not duplicate requests, and ordinary stop cannot cancel active logout. Shutdown may cancel bounded server I/O; cancellation/lost response does not establish whether revocation happened. Account connection management remains the recovery path when an approval was lost or the process ended before confirmation.

## Connection and video boundaries

Authenticated frames contain only the recipient's membership, same-session distinct open display counts, `captureState: unverified`, and scoped recent output reports. These are neither remote OBS attestation nor audience/exposure measurements. Half-open sockets may remain counted until retired. Streaming/recording reports expire and do not inspect receiver pixels.

WinHTTP validates canonical UUIDs, exact membership, bounded integer counts (four total, one streaming, including itself), and unverified capture state. It rejects invalid metadata before publication, removes `connection` from the renderer envelope and updates native controls separately. Stop/error/retry/expiry clears old connection metadata and chat. The existing fifteen-second local liveness window and lease expiry remain in force.

The persistent **수신 영상의 HUD 제외: 미검증** notice is independent of display phase, temporary notices and connection metadata. Control Center retains **LOCAL HUD READY / HUD NOT READY**, a scoped **OBS capture check**, and **Audience video: NOT VERIFIED**. Neither return eligibility nor a ready local document extends capture support or broadcast authority.

## Renderer and asynchronous transport

The immutable ChatView document uses bounded text rendering, at most 100 rows, no arbitrary-page injection and nonce-bound native readiness. External callbacks cannot obtain credentials or authorize privileged actions. A render acknowledgement confirms local rendering only.

UTF-8 frames are bounded to 2 MiB; binary, malformed and oversized frames end delivery. HTTP/handshake lifetimes and in-flight work are bounded. Worker-owned asynchronous WinHTTP retains callback buffers through handle closing and keeps network work out of UI/OBS callbacks. UI, receipt and renderer acknowledgement remain separate; no synchronous network wait is added to window messages.

## Regression boundaries

Portable tests cover phase decoding, full-width invalid replies, selection/worker/return combinations, bounded English/Korean text and prevention of readiness inheritance. Existing config/IPC tests exercise the actual config binary with synthetic HUD replies. Native display tests exercise the real HUD subclass, WinHTTP/WebView2, panel reuse/protection, message validation, revocation and independent warnings.

The switch fixture retains six cases for memory/remembered/unrelated approvals, revoke, invalid membership and failed logout/retry. Its integrated case launches **actual chat-view-config.exe** as a separate process connected to production HudWindow/NativeChatConnection hosted inside the native test executable. It compares both windows across actual button open, editor-only change, Apply/empty Apply, same-panel reopening, return, stop and logout while external content remains selected. It measures native status text bounds and retains zero-private-frame assertions for external HTML.

The same native fixture checks opaque layered panel properties, hidden affinity and unchanged reopening, plus fixed management targets, launcher failure/reentry, unchanged display/approval and independent browser-warning bounds. The browser-launch callback is a test boundary, not an actual system-browser session. Separate actual HTTP/SQLite tests verify cookie-only management, no native-token elevation, different browser identities and independent browser logout.

Provider responses, external HTML and OBS status mapping are synthetic. This integration is not a run of the full OBS plugin plus packaged HUD executable, real platform browser pages, physical two-PC capture or live CHZZK. Existing DPAPI/logout/output/companion tests remain separate. Exact source hashes, executed checks and unresolved defects belong only in the development plan; routine success is not qualification.