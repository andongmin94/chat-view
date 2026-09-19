# Private-display delivery contract

Updated: 2026-09-20. This documents the implemented service boundary, not project priorities. See [development-plan.md](development-plan.md) for current acceptance, [native-display-client.md](native-display-client.md) for the implemented Windows consumer, and [PRODUCT.md](../PRODUCT.md) for scope.

## Implemented path and authority

`platform/server/chat/display-access.mts` and `display-gateway.mts` share one creator-authorized upstream session with bounded read-only displays. The local developer probe invokes these modules; the native WinHTTP consumer and connection panel are already connected to them. Do not implement those layers again. This is not persistent machine identity, account/device enrollment, public deployment or verified audience exposure.

## Existing protocol

| Operation | Authentication and effect |
| --- | --- |
| Probe `POST /display/ticket?csrf=...` | Cookie-authenticated/CSRF-checked management action; returns a random 256-bit, one-use ticket valid at most 60 seconds; only the digest is retained. |
| `POST /display/exchange` | Empty body; `Authorization: ChatView-Ticket <ticket>`. Consumes once and returns `{id, token, expiresInMs, scope:"chat:read"}`. Lost response needs new approval, not ticket replay. |
| Upgrade `/display/events` | `Authorization: Bearer <token>`; standard WebSocket text snapshots. One connection per grant, four pending/active grants per creator context. |
| Probe `POST /display/revoke?csrf=...` | Authenticated management action; revokes displays without globally revoking CHZZK tokens. The module also supports an individual grant. |

A lease lasts at most five minutes and never exceeds the creator authorization. It is a confidential transferable capability, not proof of a physical device. The short development lease must evolve into safely renewed device access, not final-user manual five-minute reconnection. Keep credentials out of query strings, WebSocket subprotocols, diagnostics, renderer messages and process arguments.

TLS is required outside explicit literal-loopback development. The gateway checks actual request TLS rather than trusting arbitrary forwarding headers. Public deployment needs an explicit trust configuration; the loopback probe must not simply be exposed to the internet.

## Display data and bounds

The gateway reuses `nativeChatSnapshot` from `platform/web/native-chat.js` to whitelist the same envelope used by the native renderer:

```json
{"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"Synthetic example","content":"안녕 😀","messageTime":1700000000000}]}}
```

Only bounded state/count, nickname/content/time reach the display; provider secrets, sender identifiers, unknown fields, window commands and financial actions do not. Non-subscribed/invalid snapshots clear old text. Incoming application commands close the read-only peer.

The existing ws dependency owns framing/upgrades. Frames are bounded to 2 MiB UTF-8 and the shared validator accepts at most 100 text rows. Updates coalesce over 50 ms; a peer with an outstanding buffered frame is disconnected instead of queued indefinitely. Five-second status frames maintain delivery liveness independently of new chat. The server enforces expiry before sends and at deadlines; expiry invalidates the grant as well as closing the socket.

Authorization expiry/change, upstream revocation and shutdown clear grants. Explicit display revocation leaves the creator's provider subscription intact. The native consumer also clears content and enforces its own deadline on transport/authorization loss. Window/capture policy belongs to the HUD, not this protocol.

## Verification boundary

Contract tests use actual local HTTP/WebSocket/library traffic, shared-renderer validation and synthetic creator/provider data. They cover credential separation, replay, CSRF/Host/Origin, Unicode, duplicates, expiry, revocation and invalid input. A backpressure case controls buffered-byte observation. Actual Windows transport/DOM tests are separate. Neither suite proves live NAVER approval, deployed account security, physical two-PC capture or payable advertising exposure. Exact results and open defects live only in development-plan.md.
