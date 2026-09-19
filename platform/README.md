# CHZZK chat and private-display delivery

The reusable official API client and developer-only local preview connect authorization -> session ticket -> Socket.IO -> SYSTEM connected -> POST subscription -> matching SYSTEM subscribed -> CHAT -> ChatView-owned text UI. The scoped display gateway additionally delivers the existing native display envelope through a separate read-only WebSocket.

**Not a deployed multi-user platform, completed native HUD integration, dual-PC support or an advertising meter.** The current external-page HUD remains usable. NativeChatSurface is an implemented, separately tested display component, but its authenticated network consumer and user-facing connection mode are not yet implemented. Do not enter the developer probe URL in native settings or package its secret with the companion.

## Run the developer integration

Use a supported Node 22.16+ or Node 24 release. Register a CHZZK developer application with `유저 정보 조회` and `채팅 메시지 조회` and the exact redirect:

```text
http://127.0.0.1:47831/callback
```

Copy .env.example to .env in this directory and configure application credentials privately. Ordinary streamers will not supply application secrets. Never commit, paste or screenshot secrets. Leave DEBUG unset: upstream diagnostics can disclose tickets/messages, and the production factory rejects that configuration.

```sh
cd platform
npm ci --ignore-scripts
npm run connect
```

Open the printed loopback address in the system browser, log in and grant access, choose **채팅 수신 시작 / 재연결**, then **챗뷰 자체 채팅 화면**. Send a message through the normal CHZZK client for your authorized channel. Subscription confirmation and first-message receipt are distinct; a quiet channel is not automatically broken.

Stop unsubscribes only this CHAT session. Reconnect obtains a fresh ticket; duplicate start does not duplicate subscription. Refresh stops chat before rotating the one-use token. Expiry and upstream CHAT revocation stop delivery. **권한 철회** explicitly revokes all app/user tokens and may disconnect other devices; exiting never performs global revocation.

## Separate private-display access

After creator authorization, **표시 연결용 1회 키 발급** returns a one-use, at-most-60-second display ticket to the authenticated management browser. It is not a CHZZK credential. A display consumer exchanges it for an at-most-five-minute `chat:read` lease. **표시 기기 연결만 모두 해제** cancels display access without revoking provider tokens or stopping the creator's upstream chat subscription.

The reusable access/gateway modules are actually attached to this probe and covered by a real HTTP/WebSocket integration test, with CHZZK simulated. They emit the same versioned, text-only DTO consumed by NativeChatSurface. Exact header-based endpoints, limits and remaining native-worker work are described in [display-delivery.md](../docs/display-delivery.md). The one-use key is a confidential display capability; do not paste it into URLs, logs or native chat-URL settings.

This remains a local developer integration. It is not secure multi-user account enrollment or proof of machine identity, and its short lease is not the eventual streamer's manual reconnect workflow. No LAN/internet binding or account proxy is added. Public deployment needs TLS and creator/device authorization; the native worker needs its own local expiry and disconnect clearing.

## Current limits

The browser preview/SSE requires the same authenticated loopback browser and retains Host, Origin, cookie, OAuth state, one-use callback and CSRF checks. The separate display exchange/socket require their own ticket/bearer; neither grants browser management or financial authority. Provider tokens, session keys and provider tickets never enter either renderer.

The shared renderer uses text nodes, not HTML insertion or scraped provider DOM, and makes no remote media requests. CHAT does not document a unique message ID, so repeated legitimate messages are not incorrectly deduplicated or assigned invented provider IDs. Badges/emojis are not rendered yet.

History is capped at 100 bounded messages in memory. Nothing enters a chat log, database or reward ledger. Stop/disconnect/unsubscribe/revoke clear chat. Preview readers and display grants are separately bounded; slow display consumers are disconnected rather than queued indefinitely. Display framing limits do not imply a pre-decoding frame-size bound on CHZZK's separate upstream Socket.IO transport.

## Reproducible verification

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

The committed lock contains the actual npm-resolved graph. CI uses npm ci without lifecycle scripts, audits runtime AND development dependencies without exemptions, runs tests/strict types on Windows/Linux and Node22/24, and checks that manifests remain unchanged. New gateway usage promotes already locked ws8.21.3 and @types/ws8.18.1 to explicit dependencies; resolved versions and tarball integrity values are unchanged.

CHZZK's documented range requires Socket.IO-client2.0.3 here. Its Engine.IO3 transport and patched Socket.IO parser are pinned. The public Manager object-options API uses Node URL rather than legacy URI constructors. **parseuri3.0.2 is not a callable API replacement**; it is not invoked by this connection path. No compatibility shim or custom wire protocol is added. See [dependency-validation.md](../docs/dependency-validation.md) for the completed recovery and historical failures.

The actual-library CHZZK test verifies EIO3/WebSocket, structured options, encoded ticket delivery, subscription, Unicode and unsubscribe. CHZZK contract#7 at 3d801f2 passed the original82 tests, audit and types on all four jobs. Native renderer tests were subsequently added. At b0e72a7, contract#9 failed because the audit service returned503, not because it reported a vulnerability. Always check the new commit's actual CI before declaring the new layer passed.

No real CHZZK developer credentials or dual-PC hardware were available. Genuine socket libraries against a simulated provider do not establish live-provider approval, native worker operation, capture exclusion or advertising exposure. [The handoff](../docs/development-plan.md) records exact tests and current native resource-test failure separately.

## Next product path

Connect the reusable gateway/session to the existing native display with scoped authorization and a bounded native worker. Do not ship the probe as the public backend or broaden arbitrary-page native privileges. Preserve the no-gaming-PC-OBS condition and qualify clean video separately. Ad selection, audience measurement, HP and rewards remain core goals but receive no credit from preview traffic.

Primary references:
- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- https://socket.io/docs/v2/client-api/
- https://github.com/websockets/ws/blob/8.21.3/README.md#client-authentication
- https://github.com/websockets/ws/blob/8.21.3/doc/ws.md
