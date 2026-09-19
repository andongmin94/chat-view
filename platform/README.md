# CHZZK chat slice — P1-01b

A reusable official API client and a developer-only local chat preview now connect authorization -> session ticket -> Socket.IO -> SYSTEM connected -> POST subscription -> matching SYSTEM subscribed -> CHAT -> ChatView-owned text UI.

**This is not a deployed multi-user platform, native HUD integration, dual-PC support or an advertising meter.** The old native external-page HUD remains unchanged. Do not enter the probe URL into the native settings; that viewer does not authorize loopback pages.

## Run

Use Node 22.16+ or Node 24. Register a CHZZK application with `유저 정보 조회` and `채팅 메시지 조회`, with this exact redirect:

```text
http://127.0.0.1:47831/callback
```

Copy `.env.example` to `.env` in this directory and configure developer credentials privately. Ordinary streamers will not need client secrets. Never commit, paste or screenshot secrets. Do not set `DEBUG`: the upstream library can log sensitive tickets/messages, and the socket factory rejects that configuration.

```sh
cd platform
npm install --ignore-scripts
npm run connect
```

Open the printed loopback address in the system browser. Log in and grant access, choose **채팅 수신 시작 / 재연결**, then open **챗뷰 자체 채팅 화면**. Keep your authorized channel available and send a message using the normal CHZZK client. The preview separately reports subscription confirmation and the first received message; a quiet channel is not automatically a broken connection.

Stopping unsubscribes only this CHAT session and closes its socket. Reconnect uses a new API-issued URL; duplicate start does not create a duplicate subscription. Refresh stops chat before rotating the one-use token. Token expiration or upstream CHAT revocation stops delivery. **권한 철회** is different: it explicitly revokes all this application's tokens for the user and may disconnect other devices. Exiting never performs global revocation.

## Boundaries and limits

- Only the same authenticated loopback browser can load the preview, assets and event stream. Host, Origin, OAuth state, one-use callbacks, CSRF and cookie checks remain enforced. No access token, session key or ticket is returned to the renderer.
- The text-only renderer uses text nodes, not HTML insertion or provider-page DOM scraping. It makes no remote media requests. Nickname, content, native channel identifiers, role, verified flag and timestamp are parsed; badges/emojis are not rendered yet. The documented CHAT payload does not provide a unique message ID, so repeated identical messages are not incorrectly deduplicated and no provider ID is invented.
- At most 100 messages are retained in process memory, with field/serialized-payload bounds. This is not a pre-decoding WebSocket frame-size guarantee. Nothing is written to chat logs, a database or an advertising ledger. Disconnect, unsubscribe, revoke and stop clear the history; the browser also clears on delivery loss.
- Browser delivery uses same-origin Server-Sent Events, at most four readers, coalesced bursts and slow-reader disconnection. It is a developer preview, not the target public delivery architecture. No LAN binding, proxy exposure, pairing, payout or native privileged IPC is added.

## Tests and dependency decision

```sh
npm test
npm run typecheck
npm audit --omit=dev --audit-level=low
```

CHZZK documents Socket.IO-client **1.0.0 through 2.0.3**, WebSocket transport and no automatic reconnect. We select 2.0.3 with explicit upstream security-maintained dependency substitutions: engine.io-client 3.5.6, socket.io-parser 3.3.4, parseqs/parseuri 0.0.6 and xmlhttprequest-ssl 1.6.3. The Engine.IO package brings patched ws 7.5.10. These are library dependencies, not a home-grown framing implementation or a fallback to a newer incompatible Socket.IO protocol.

This is a customized dependency graph, **not a claim that NAVER has certified these substitutions**. CI audits it and runs the actual installed client against an actual Socket.IO test server with EIO3 enabled, on Windows/Linux and Node 22/24. It checks the negotiated protocol, subscription, Unicode message delivery and cleanup. The destination is substituted only inside the test's module cache; production has no arbitrary-endpoint switch. Confirm this exact graph with a real authorized CHZZK channel before release.

Local verification at authoring: 41 tests (existing authentication tests, new session/preview tests), strict type check and renderer syntax check passed. The local runtime cannot download npm packages; actual-library transport and remaining unchanged API tests are delegated to the repository CI. Inspect the latest commit's results in `docs/development-plan.md`. **No real developer credentials or hardware were available; live authorization, real CHZZK events and native-HUD display are not verified.**

## Next

Use a real authorized channel to verify the exact client/dependency graph. Then connect the reusable session and renderer to the platform/native HUD with narrowly scoped authorization, not by shipping this developer loopback tool or its client secret. Keep the no-gaming-PC-OBS constraint and separate clean-video qualification. Ads, audience measurement, HP and rewards remain separate goals and receive no credit from this preview.

## Primary references, checked 2026-09-19

- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- https://socket.io/docs/v2/client-api/
- https://socket.io/docs/v4/client-installation/#version-compatibility
- https://github.com/socketio/socket.io-client/blob/2.0.3/package.json
- https://github.com/socketio/engine.io-client/blob/3.5.6/package.json
- https://github.com/socketio/socket.io-parser/blob/3.3.4/package.json
- https://github.com/websockets/ws/security/advisories/GHSA-3h5v-q93c-6h6q
