# Private-display delivery contract

Updated: 2026-09-20. This documents the implemented service boundary, not project priorities. See [development-plan.md](development-plan.md) for current acceptance, [native-display-client.md](native-display-client.md) for the implemented Windows consumer, and [PRODUCT.md](../PRODUCT.md) for scope.

## Implemented path and authority

`DisplayAccess`, `DisplaySessionGateway` and `DisplayGateway` share one creator-authorized upstream session with bounded read-only displays. The session gateway owns browser-login, renewal and logout HTTP operations; the display gateway owns the read-only WebSocket stream. The local developer probe composes both with the CHZZK session; the native WinHTTP consumer and connection panel are connected to them. Do not rebuild these layers as a separate device-registration product.

This is not a deployed multi-user account service, persistent hardware identity, physical two-PC capture proof or verified audience exposure. A remembered ChatView login authorizes private chat only; it is not a provider credential, viewer identity or advertising/reward authority.

## Browser login and session protocol

| Operation | Authentication and effect |
| --- | --- |
| `POST /display/login` | Native sends `Authorization: ChatView-Challenge <sha256(verifier)>`; optional `X-ChatView-Remember: 1` affects consent/persistence intent only. Returns a random request id and exact `/login/<id>` verification path. |
| Browser `/login/<id>` | Same-browser protected CHZZK consent resolves the creator/channel, starts the authorized chat session and requires explicit approval. The URL id cannot collect the resulting credential. |
| `POST /display/login/<id>` | Native polls with `Authorization: ChatView-Login <verifier>`. Approval is one-use and bound to the same owner and authorization generation; revoke/reauthorize invalidates uncollected approval. |
| `POST /display/refresh` | `Authorization: ChatView-Session <renewal>`. Mints a fresh short `chat:read` lease without replaying browser approval. |
| `POST /display/signout` | Revokes that remembered/current ChatView renewal session. It does not globally revoke CHZZK unless the creator separately chooses provider revocation. |
| Upgrade `/display/events` | `Authorization: Bearer <short display token>`; standard WebSocket text snapshots. One connection per grant. |
| Legacy/developer `POST /display/exchange` | One-use `ChatView-Ticket` compatibility path. It is not the normal native UX and must not reappear as a manual-key requirement. |

Every explicit browser approval receives a renewable ChatView session for the current app run, so a long broadcast does not require reapproval every five minutes. The native **remember this PC** choice controls only whether that renewal credential is persisted across restarts. Server persistence stores only a hash; Windows persistence uses user-scoped protection. Provider tokens and chat content are not part of remembered display state.

Short display leases still bound individual WebSocket exposure. The renewal session is separately revocable and cannot be used as a display bearer, provider token, account-management credential or financial credential. Keep all credential classes out of query strings, WebSocket subprotocols, diagnostics, renderer messages and process arguments.

TLS is required outside explicit literal-loopback development. The session/display gateways check actual request TLS rather than trusting arbitrary forwarding headers. Public deployment needs explicit reverse-proxy trust and multi-user account/channel isolation; the loopback probe must not simply be exposed to the internet.

## Display data and bounds

The gateway reuses `nativeChatSnapshot` from `platform/web/native-chat.js` to whitelist the same envelope used by the native renderer:

```json
{"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"Synthetic example","content":"안녕 😀","messageTime":1700000000000}]}}
```

Only bounded state/count, nickname/content/time reach the display; provider secrets, sender identifiers, unknown fields, window commands and financial actions do not. Non-subscribed/invalid snapshots clear old text. Incoming application commands close the read-only peer.

The existing ws dependency owns framing/upgrades. Frames are bounded to 2 MiB UTF-8 and the shared validator accepts at most 100 text rows. Updates coalesce over 50 ms; a peer with an outstanding buffered frame is disconnected instead of queued indefinitely. Status frames maintain delivery liveness independently of new chat. The server and native client enforce lease/session expiry and stop old display authorization before continuing with a replacement.

Authorization expiry/change, upstream revocation and logout clear affected grants. Native transport loss clears old chat and uses bounded retry only while a renewable ChatView session remains valid. Window/capture policy belongs to the HUD, not this protocol.

## Verification boundary

Contract tests use actual local HTTP/WebSocket/library traffic, shared-renderer validation, SQLite session persistence and synthetic creator/provider data. They cover verifier/challenge separation, explicit consent, approval replay, revocation generation, renewal/logout, credential separation, CSRF/Host/Origin, Unicode, expiry and invalid input. Windows native tests separately exercise WinHTTP, protected persistence and actual WebView2 DOM.

Neither suite proves live NAVER approval, deployed multi-user account security, physical two-PC capture or payable advertising exposure. Exact current results and open defects live only in development-plan.md.
