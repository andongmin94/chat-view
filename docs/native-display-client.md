# Native private-chat connection

Current implementation: `DisplayClient` (WinHTTP), `NativeChatConnection` (native controls) and `NativeChatSurface` (immutable own renderer). Product authority is [PRODUCT.md](../PRODUCT.md); current evidence and open defects are in [development-plan.md](development-plan.md). This contract does not certify live CHZZK, clean video or distribution.

## Entry and role

The HUD connection panel opens with **Ctrl+Alt+Shift+C**. Existing display/input/placement and capture/lock protections remain in force; opening or reconnecting does not override suppression or failed capture exclusion. Companion is the explicit `--companion` entry without local OBS. OBS-controlled launch first validates its parent and local transport. Invalid OBS arguments never fall back to companion.

The launch determines the request role: companion uses `gaming`; OBS-managed runtime uses `streaming`. The panel shows that role read-only. It cannot change the process into the other role or replace browser authorization. In one-PC use the OBS-managed runtime still has the streaming role; its label does not imply a separate physical computer.

Enter the HTTPS service origin without a path and choose **로그인 / 연결**. Local development HTTP requires the explicit checkbox and accepts only `http://127.0.0.1`. The native consumer disables cookies, automatic authentication and redirects and does not disable certificate validation. Credentials never appear in URLs, command lines, diagnostics or chat HTML.

The client generates a random verifier/challenge, sends its role on `/display/login`, opens the validated `/login/<request>` URL in the system browser and displays the matching confirmation code. Public browser consent confirms the channel and the already-requested role with one approval action. The old browser role-selection URLs are removed. Polling requires the original verifier; knowing the landing URL cannot collect the grant. The management page is never loaded into the private chat WebView.

## Renewal, persistence and mismatch

All approved connections receive an in-run `chat:renew` credential. **이 PC에서 연결 유지** only chooses user-scoped Windows DPAPI persistence for app restart; it is not hardware enrollment. The native client keeps its in-run renewal credential private and rotates short display leases through the existing endpoint.

Login and renewal require exactly one `X-ChatView-Role` header. A saved approval belonging to the other launch role receives 409 before the server replaces any valid lease. The native client stops that attempt with `RoleMismatch`, keeps the saved approval and directs the user to the original launch mode or explicit logout/reconsent. It never silently promotes a gaming approval to streaming or deletes another running connection to make the mismatch disappear.

Renewed membership must match the original role, broadcast-session ID and connection ID. The server retains that membership across its normal restart using the existing protected provider store and approval database. Native credentials and provider grants have separate scopes. Confirmed 401/403 during renewal removes the matching remembered approval; provider/transport unavailability follows bounded retry without reusing a one-use login ticket. The native retry delay is bounded between one and thirty seconds.

**연결 중지** stops local delivery and clears chat/session status without claiming to revoke the stored approval. For remembered approvals, **로그아웃** waits for cancellation, removes DPAPI storage and separately reports server signout success or failure. The current non-remembered in-run logout cannot yet submit its worker-owned renewal credential for server revocation; D5 in the plan tracks this gap. Local disappearance of chat is not proof that its server approval/streaming slot was revoked. Owner connection management can explicitly revoke an approval.

## Connection status is not capture evidence

The existing authenticated gateway sends a bounded `connection` object alongside each chat snapshot. It contains only the recipient's own `role`, `broadcastSessionId`, `connectionId`, the same-session counts `gamingConnections`/`streamingConnections`, and `captureState: "unverified"`. Counts use distinct authorized open display connections on that creator's gateway. They exclude other creators and revoked leases. A half-open transport may remain counted until it is retired; this is not remote runtime attestation.

The connection panel displays the accepted role, full shared session ID and counts with **영상 제외 미검증**. Same IDs mean the server grouped the approvals; no user has to copy an ID to pair machines. The ID is not a password or a billable broadcast interval. Neither a streaming-role socket nor a count proves that OBS is broadcasting, that game video arrived, or that the HUD is excluded.

WinHTTP validates UUID syntax, exact approved membership, integer/bounded counts (at most four total, one streaming, including itself) and the unverified capture state before accepting a frame. Missing/mismatched metadata or a claimed verified state fails closed. The connection object is removed before the JSON envelope is passed to `NativeChatSurface`; status is conveyed separately through the native mailbox. It grants no WebView command capability.

Stop, denial, error, retry and stale connection clear the displayed session state. A frame/lease watchdog clears stale native chat and status after the existing fifteen-second liveness window. Server connection changes are coalesced through the existing gateway and heartbeat; no second polling service or hardware directory is introduced.

## Renderer and transport boundaries

The immutable ChatView document uses shared text rendering and accepts at most 100 bounded messages. It is not injected into arbitrary provider pages. Native readiness is nonce-bound to that document; external-page callbacks cannot authenticate, obtain credentials or drive privileged window actions. A render acknowledgement confirms only local rendering, never account authorization, remote connection authority or advertising evidence.

Frames are UTF-8 text with bounded accumulation (2 MiB); binary/malformed/oversized frames terminate delivery. HTTP responses and handshake lifetimes are bounded. Worker-owned asynchronous WinHTTP keeps the UI/OBS thread nonblocking, preserves callback buffers until handle closing and handles cancellation without waiting on the network in the UI callback. Native delivery and WebView publication remain separate mailboxes/acknowledgements with bounded in-flight work.

## Relevant regression coverage

The portable launch test checks parsing, explicit mode-to-role mapping, identifiers and status wording. The native display fixture exercises actual WinHTTP → HUD/WebView2, the streaming role/status panel, Unicode/inert markup, revocation and metadata removal. Negative scenarios include wrong scope/role/session, missing metadata, invalid counts, claimed capture verification, invalid JSON, binary/oversized frames, redirects, cancellation, expiry and idle timeout.

The remembered-session fixture exercises DPAPI restart, role-mismatch preservation, correct-role recovery, renewal, interrupted delivery and revoke. The companion entry test still launches the actual executable without OBS libraries, checks warning/connection UI, duplicate handling and exit. These fixtures use synthetic provider/chat input, not real CHZZK or dual-PC hardware. Test results are recorded only in the development plan; routine success is not qualification or live-provider acceptance.
