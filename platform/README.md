# CHZZK chat slice — P1-01b

The reusable official API client and developer-only local preview connect authorization -> session ticket -> Socket.IO -> SYSTEM connected -> POST subscription -> matching SYSTEM subscribed -> CHAT -> ChatView-owned text UI.

**Not a deployed multi-user platform, native HUD integration, dual-PC support or an advertising meter.** The old native external-page HUD is unchanged. Do not enter the probe URL in native settings: that viewer does not authorize loopback pages.

## Run

Use a supported Node 22.16+ or Node 24 release. Register a CHZZK developer application with `유저 정보 조회` and `채팅 메시지 조회` and the exact redirect:

```text
http://127.0.0.1:47831/callback
```

Copy .env.example to .env in this directory and configure application credentials privately. These are development credentials, not something ordinary streamers will supply. Never commit, paste or screenshot secrets. Leave DEBUG unset: upstream diagnostics can disclose tickets/messages, and the production factory rejects that configuration.

```sh
cd platform
npm ci --ignore-scripts
npm run connect
```

Open the printed loopback address in the system browser, log in and grant access, choose **채팅 수신 시작 / 재연결**, then **챗뷰 자체 채팅 화면**. Send a message through the normal CHZZK client for your authorized channel. Subscription confirmation and first-message receipt are different states; a quiet channel is not automatically broken.

Stop unsubscribes only this CHAT session and closes its socket. Reconnect obtains a fresh ticket. Duplicate start does not duplicate the subscription. Refresh stops chat before rotating the one-use token. Token expiry and upstream CHAT revocation stop delivery. **권한 철회** explicitly revokes all app/user tokens and may disconnect other devices; exiting does not perform that global action.

## Current limits

The preview and SSE stream require the same authenticated loopback browser. Host, Origin, cookie, OAuth state, single-use callbacks and CSRF checks are enforced. Tokens, session keys and tickets are not sent to the renderer. This tool must not be exposed to the LAN/internet or packaged with a client secret in the companion.

The renderer uses text nodes, not HTML insertion or scraped provider DOM. It makes no remote media requests. Nickname, message, native channel identifiers, role, verified flag and timestamp are parsed; badges/emojis are not rendered yet. CHAT does not document a unique message ID, so identical legitimate messages are not incorrectly deduplicated or assigned invented provider IDs.

History is capped at 100 messages in memory with field/payload bounds. This is not a pre-decoding WebSocket frame-size guarantee. Nothing enters a chat log, database or reward ledger. Stop, disconnect, unsubscribe and revoke clear history; the browser clears on delivery loss. SSE delivery is coalesced and capped at four readers, with slow-reader disconnection. It is not the target public multi-user delivery system.

## Reproducible verification

```sh
npm ci --ignore-scripts
npm audit --audit-level=low
npm test
npm run typecheck
```

The committed lock contains the actual npm-resolved graph. CI uses npm ci with lifecycle scripts disabled, audits runtime AND development dependencies without exemptions, runs tests/strict types on Windows/Linux with Node 22/24, and checks that manifests remain unchanged.

CHZZK's documented client range requires Socket.IO-client 2.0.3 here. Its Engine.IO 3 transport and patched Socket.IO parser are pinned. We use the public Manager object-options API with Node's standard URL parsing, never the legacy URI constructor. **parseuri 3.0.2 is not a callable API replacement**: the installed module is not invoked by the selected connection path. No compatibility shim or custom protocol implementation is included. Details and the history of failed dependency experiments are in [dependency-validation.md](../docs/dependency-validation.md).

The actual-library test checks EIO3/WebSocket, structured options, encoded ticket delivery, matching subscription acknowledgement, Unicode CHAT and unsubscribe. Only the fixture destination is changed in the test; the production URL allowlist and TLS requirements remain intact. This is not certification of the substitutions by NAVER.

At commit 12470eb, the resolved graph passed audit with zero findings and all 82 tests; strict types caught a test-only captured-address narrowing error. This change fixes it and locks that graph. Inspect the new commit's clean-install/type-check results before calling the gate passed. Prior local mock-only evidence must not be confused with installed-library CI.

**No actual CHZZK developer credentials or dual-PC hardware were available. Real authorization/events and native first-party HUD display are still unverified.** The existing OBS/C++ code remains unchanged.

## Next product path

Qualify this exact graph with a real authorized channel, then connect the reusable session/renderer to platform account/device authorization and the existing native HUD. Do not ship the developer probe as the public backend or broaden arbitrary-page native privileges. Preserve the no-gaming-PC-OBS requirement; the clean-video hardware path needs separate proof. Ad selection, audience measurement, HP and rewards remain core goals but receive no credit from this preview.

## Primary references, checked 2026-09-19

- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- https://socket.io/docs/v2/client-api/
- https://github.com/socketio/socket.io-client/blob/2.0.3/lib/manager.js
- https://github.com/socketio/engine.io-client/blob/3.5.6/lib/socket.js
- https://github.com/slevithan/parseuri/blob/main/src/index.js
- https://github.com/socketio/socket.io/security/advisories/GHSA-2m8v-j782-fhvr
