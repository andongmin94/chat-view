# Private-display delivery contract

Updated: 2026-09-28. Mechanisms only: [development-plan.md](development-plan.md) owns evidence/priorities, [native-display-client.md](native-display-client.md) the Windows consumer and [PRODUCT.md](../PRODUCT.md) the product scope.

## Implemented authority

The HTTPS application composes creator-isolated provider sessions with the existing browser login, display access and gateway. The private local probe uses those same lower-level modules. One creator's upstream serves its approved displays; no separate hardware registration or pairing product is required. This remains development code, not deployed-service, live-provider, clean-video or advertising certification.

Native renewal credentials, short chat bearers, browser-management cookies and provider tokens have separate scopes. A ChatView display approval is not a viewer identity, provider credential or financial authority. Server-side membership groups approved gaming/streaming connections; it is not an observed broadcast interval.

## Login and renewal protocol

| Operation | Authentication and effect |
| --- | --- |
| `POST /display/login` | `Authorization: ChatView-Challenge <sha256(verifier)>`, exactly one `X-ChatView-Role: gaming` or `streaming`, optional `X-ChatView-Remember: 1`. The role is fixed before the browser opens. Returns a random request ID and exact `/login/<id>` path. |
| Browser `/login/<id>` | Protected provider authentication resolves creator/channel. The public page displays the already-requested role and requires explicit confirmation through `/login/<id>/approve`; it cannot change the role. Old `/approve/gaming` and `/approve/streaming` routes are removed. |
| `POST /display/login/<id>` | `Authorization: ChatView-Login <verifier>`. Collects a one-use approval bound to its creator, authorization generation and fixed role. The landing URL alone cannot collect credentials. |
| `POST /display/refresh` | `Authorization: ChatView-Session <renewal>` and mandatory `X-ChatView-Role`. Wrong role returns 409 before replacing a lease. Roleless obsolete display approvals are not silently upgraded. |
| `POST /display/signout` | Revokes the submitted ChatView approval, not all provider tokens. The native non-remembered logout submission gap is D5 in the plan; an API capability does not imply every UI path invokes it. |
| Upgrade `/display/events` | `Authorization: Bearer <short chat token>`, standard receive-only WebSocket. One connection per lease. |
| `POST /broadcast/session` (service) | The caller's native renewal credential returns only its own membership and `captureState: unverified`. No output-writing authority is conferred. |
| Developer-only `POST /display/exchange` | Internal one-use `ChatView-Ticket` exchange in lower-level fixtures/local probe. The public service returns 404. It is not a manual-key UX or compatibility fallback. |

Every explicit approval receives an in-run renewable session. **이 PC에서 연결 유지** only chooses Windows user-scoped persistence across app restarts. Native approval secrets are stored as server hashes; provider grants are separately encrypted by the service. No chat content is persisted. Display lease expiration bounds each WebSocket; renewal and role remain separately revocable.

An incorrect role header cannot change stored consent. A native client also validates returned membership against its actual launch intent, then requires the same role/session/connection through renewal. Server origin, actual TLS, HTTP method/body, credential scheme, duplicate headers, CSRF and callback state checks remain in place. No credentials in query strings, WebSocket subprotocols, logs, renderer messages or process arguments. TLS is required outside explicit literal-loopback development; forwarded headers do not make plaintext trusted.

## Data and bounds

The gateway sanitizes chat with `nativeChatSnapshot` and adds separate recipient-specific native connection metadata:

```json
{
  "type": "chat-snapshot",
  "version": 1,
  "snapshot": {
    "state": "subscribed", "received": 1,
    "messages": [{"nickname": "Synthetic example", "content": "안녕 😀", "messageTime": 1700000000000}]
  },
  "connection": {
    "role": "gaming",
    "broadcastSessionId": "01234567-89ab-cdef-0123-456789abcdef",
    "connectionId": "abcdef01-2345-6789-abcd-ef0123456789",
    "gamingConnections": 1, "streamingConnections": 1,
    "captureState": "unverified"
  }
}
```

The UUIDs above are synthetic identifiers, not credentials. Live frames contain the recipient's own membership. Counts deduplicate authorized open display connections on that creator's gateway and in that same logical session; other creators, expired leases and revoked approvals are excluded. A connection event/close updates the existing coalescer. Half-open sockets can remain counted until retired. Counts do not prove remote OBS health, an active broadcast, clean video or audience exposure.

The native client validates membership equality, canonical identifier syntax, integer counts, at most four total/one streaming and inclusion of its own role. It accepts only the explicit unverified capture state. It removes `connection` before passing the envelope to the chat document and separately updates read-only native controls. The renderer receives only bounded chat state/count and nickname/content/time, never membership, provider secrets, sender IDs, window commands or financial actions.

The ws dependency owns framing/upgrades; no custom WebSocket parser is introduced. Frames remain at most 2 MiB UTF-8 and 100 text rows. Updates coalesce for 50 ms; outstanding buffered output disconnects a slow consumer rather than growing a queue. Existing five-second status frames provide local delivery liveness. Native lease/liveness watchdogs clear chat and session state on expiry/loss. Incoming application commands terminate the receive-only peer. Capture/visibility policy remains owned by the HUD, not this connection protocol.

## Verification boundary

Contract tests use real local HTTP/WebSocket/library traffic and SQLite with synthetic provider/creator input. Coverage includes fixed role consent, missing/duplicate/invalid role headers, obsolete selector rejection, owner/session isolation, role-preserving renewal, visible connection counts and selective/full revocation, alongside prior verifier, CSRF, Unicode, expiry, restart and malformed-input cases.

Windows fixtures separately exercise WinHTTP, protected persistence and the actual HUD/WebView2. They validate native role/status display and reject wrong roles, session IDs, counts and capture claims before publishing chat. Neither suite proves live NAVER approval, deployed-service security, physical dual-PC capture or payable exposure. Exact runs and open defects are recorded only in the development plan.
