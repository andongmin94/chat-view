# P2b — scoped private-display delivery

Updated: 2026-09-20 (Asia/Seoul). Starting code: b0e72a766c451036a62bcb188e4e00dd4e9f056c.

## Working layer and its boundary

`platform/server/chat/display-access.mts` and `display-gateway.mts` add a reusable, short-lived chat-read authorization and delivery boundary. The existing developer probe invokes them after the existing CHZZK creator authorization. They share the existing upstream chat session rather than opening a provider subscription for each display. A real HTTP/WebSocket test exercises the complete probe-to-display path with a simulated CHZZK provider.

This is **not** production account enrollment, persistent machine identity, a public deployment, or the native network worker. A display bearer is a transferable short-lived capability, not proof of physical-device identity. The existing Windows `NativeChatSurface` can consume this exact display envelope, but connecting the native executable to the gateway and presenting a user-facing connect flow remains next work. Do not enter this probe URL into native external-page settings or ship the developer secret/probe to ordinary streamers.

## Creator action and display protocol

After developer authorization, the local management page provides two explicit actions: issue a one-use display key, and revoke display connections only. These remain cookie-authenticated and CSRF-checked. They do not broaden the private chat renderer's authority.

- `POST /display/ticket?csrf=...` is a **probe-management** action. It returns a random 256-bit ticket once, valid for at most 60 seconds. The server stores only its digest. It never enters the regular page notice, chat frame, URL, or logger.
- `POST /display/exchange` with `Authorization: ChatView-Ticket <ticket>` and an empty body consumes the ticket synchronously. It returns `{id, token, expiresInMs, scope:"chat:read"}`. The distinct display token lasts at most five minutes and never outlives the creator's current authorization.
- Upgrade `/display/events` with `Authorization: Bearer <token>` to receive standard WebSocket text messages. No token in query strings, cookies or WebSocket subprotocols; no automatic alternate transport. One simultaneous connection per grant; four pending/active grants maximum per creator context.
- `POST /display/revoke?csrf=...` is another authenticated **probe-management** action. It invalidates display grants and closes display peers without calling CHZZK global token revocation. The access module also supports individual grant revocation.

The short lifetimes bound this integration layer; they are not a decision to make streamers manually reconnect every five minutes in the eventual product. Automatic renewal requires the forthcoming creator/device account flow, not indefinite bearer validity or replay of a consumed ticket. A lost exchange response requires a new creator-issued ticket.

TLS is required outside the explicit `http://127.0.0.1` developer adapter. The gateway checks actual TLS on the request, not an untrusted forwarding header. A public reverse-proxy deployment needs its own explicit, reviewed trust configuration; none is added here. The existing probe still listens only on loopback. No LAN pairing or public listener is introduced.

## Same display contract, separate authority

The gateway reuses `nativeChatSnapshot` from `platform/web/native-chat.js` to whitelist the exact native display fields. It sends:

```json
{"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"Synthetic example","content":"안녕 😀","messageTime":1700000000000}]}}
```

Only state/count and bounded nickname/content/timestamp values are transmitted. Provider tokens, session tickets, sender identifiers, unknown fields, HTML execution, window commands, ad evidence and payouts are not part of the display protocol. Non-subscribed frames have no retained chat. This is private text delivery, not proof of viewer exposure or successful capture exclusion.

WebSocket framing, upgrade validation and incoming-frame bounds are handled by the already locked `ws` library. It is promoted to an explicit runtime dependency at its existing 8.21.3 version; its already locked types become an explicit development dependency. No resolved package version, tarball integrity, transitive URI override or CHZZK wire protocol is changed.

Incoming application data closes a read-only peer. Frames are capped at 2 MiB UTF-8; at most 100 messages are accepted by the shared DTO validation. Updates coalesce over 50 ms. A peer with an outstanding buffered frame is disconnected instead of growing an unbounded queue. Five-second status frames maintain display liveness; a lease deadline and checks before every send stop delivery independently of new chat events.

Account expiry, refresh, reauthorization, upstream CHAT revocation and shutdown clear grants. The probe notifies the gateway immediately; explicit display revocation affects neither the upstream subscription nor other provider tokens. The native consumer must clear the surface on transport loss and enforce its own monotonic deadline even if the server becomes unreachable. Native window/capture decisions remain owned by the HUD, not this service.

## Verification

Local authoring executed 15 access-policy tests and syntax checks for the new source, integration and tests. Full installed-library tests and strict checking must be read from this change's CHZZK contract CI; local npm downloads are unavailable. Do not report these checks as a live CHZZK or Windows native-worker test.

New actual HTTP/WebSocket tests cover ticket exchange/replay, credential-class separation, Host/Origin/CSRF constraints, duplicate readers, unknown fields, the existing native JavaScript receiver, Unicode, account/grant expiry, revoke, incoming commands/oversized input and the probe's shared CHZZK session. One controlled backpressure test substitutes the socket's buffered-byte observation; ordinary delivery tests use the actual library and local sockets. The existing native display CTest is separate from this service transport suite.

## Next product acceptance

Add the bounded native delivery worker and user-facing scoped connection flow, feeding `NativeChatSurface` only while both grant and document identity are valid. Do not put provider auth in OBS callbacks, proxy arbitrary pages into privileged native content, or create another desktop framework. Keep the gaming-PC companion free of OBS dependencies, and qualify a separate HUD-free hardware feed. Live CHZZK validation and public ad/HP/reward requirements remain open.

Primary implementation references reviewed 2026-09-20:
- https://github.com/websockets/ws/blob/8.21.3/README.md#client-authentication
- https://github.com/websockets/ws/blob/8.21.3/doc/ws.md
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security
