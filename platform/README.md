# CHZZK chat and private-display delivery

The reusable official API client and developer-only local preview connect authorization -> session ticket -> Socket.IO -> SYSTEM connected -> POST subscription -> matching SYSTEM subscribed -> CHAT -> ChatView-owned text UI. The scoped display gateway additionally delivers the existing native display envelope through a separate read-only WebSocket. The native WinHTTP consumer and connection panel now connect that gateway to the existing HUD; [native-display-client.md](../docs/native-display-client.md) describes the implementation and its exact verification boundary.

**Not a deployed multi-user platform, durable device enrollment, dual-PC support or an advertising meter.** The current external-page HUD remains available. The new native path does not load the probe web page or receive provider secrets. Do not enter the developer probe URL in external-chat settings or package its secret with the companion.

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

Open the printed loopback address in the system browser, log in and grant access, choose **채팅 수신 시작 / 재연결**, then **챗뷰 자체 채팅 화면** to inspect the browser preview. Send a message through the normal CHZZK client for your authorized channel. Subscription confirmation and first-message receipt are distinct; a quiet channel is not automatically broken. Actual provider acceptance still requires a real authorized account; fixtures do not establish it.

Stop unsubscribes only this CHAT session. Reconnect obtains a fresh ticket; duplicate start does not duplicate subscription. Refresh stops chat before rotating the one-use token. Expiry and upstream CHAT revocation stop delivery. **권한 철회** explicitly revokes all app/user tokens and may disconnect other devices; exiting never performs global revocation.

## Connect the existing native HUD

After creator authorization, **표시 연결용 1회 키 발급** returns a one-use, at-most-60-second display ticket to the authenticated management browser. It is not a CHZZK credential. A display consumer exchanges it for an at-most-five-minute `chat:read` lease. **표시 기기 연결만 모두 해제** cancels display access without revoking provider tokens or stopping the creator's upstream chat subscription.

Start the OBS-managed HUD from a validated native build and press **Ctrl + Alt + Shift + C**. In the native panel, enter `http://127.0.0.1:47831`, explicitly enable the developer-only local-server option, paste the fresh display key in the masked field and connect. Do not paste a provider access token or client secret. Normal service origins require HTTPS. The key is cleared on use/close and never saved; closing the panel leaves the connection running, while **연결 종료** stops it and clears the owned display.

The access/gateway modules share the creator's existing CHZZK session and emit the same versioned, text-only DTO consumed by NativeChatSurface. Exact header-based endpoints and limits are described in [display-delivery.md](../docs/display-delivery.md). The display key is confidential; never put it in URLs, command-line arguments, logs or native chat-URL settings.

This remains a local developer integration, not secure multi-user account enrollment or proof of machine identity. Its short lease is not the eventual streamer's manual reconnect workflow. No LAN/internet listener or public account proxy is added. Production deployment still needs TLS, revocable account/device enrollment and renewal. The native client enforces its own monotonic expiry/idle limits and clears on delivery loss, independently of the gateway.

## Current limits

The browser preview/SSE requires the same authenticated loopback browser and retains Host, Origin, cookie, OAuth state, one-use callback and CSRF checks. The separate display exchange/socket require their own ticket/bearer; neither grants browser management or financial authority. Provider tokens, session keys and provider tickets never enter either renderer.

The shared renderer uses text nodes, not HTML insertion or scraped provider DOM, and makes no remote media requests. CHAT does not document a unique message ID, so repeated legitimate messages are not incorrectly deduplicated or assigned invented provider IDs. Provider-specific graphical badges/emoticons are not rendered yet; ordinary Unicode text is supported.

History is capped at 100 bounded messages in memory. Nothing enters a chat log, database or reward ledger. Stop/disconnect/unsubscribe/revoke clear chat. Preview readers and display grants are separately bounded; slow display consumers are disconnected rather than queued indefinitely. The native UI also limits in-flight renderer delivery. Display framing limits do not imply a pre-decoding frame-size bound on CHZZK's separate upstream Socket.IO transport.

## Reproducible verification

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

The committed lock contains the actual npm-resolved graph. CI uses npm ci without lifecycle scripts, audits runtime AND development dependencies without exemptions, runs tests/strict types on Windows/Linux and Node22/24, and checks that manifests remain unchanged. Gateway usage reuses already locked ws8.21.3 and @types/ws8.18.1 as explicit dependencies. P2c does not change resolved versions, tarball integrity values or the provider wire protocol.

CHZZK's documented range requires Socket.IO-client2.0.3 here. Its Engine.IO3 transport and patched Socket.IO parser are pinned. The public Manager object-options API uses Node URL rather than legacy URI constructors. **parseuri3.0.2 is not a callable API replacement**; it is not invoked by this connection path. No compatibility shim or custom wire protocol is added. See [dependency-validation.md](../docs/dependency-validation.md) for the completed recovery and historical failures.

The platform suite includes actual HTTP/WebSocket requests, the CHZZK library transport against a simulated upstream, scoped display authorization and shared-renderer validation. The separate Windows CTest adds the actual native panel, WinHTTP and HudWindow/WebView2 DOM, plus cancellation/expiry and invalid-transport scenarios. All test identities, credentials and chat are synthetic. The test runner uses inherited stdin, not secret command-line arguments.

[The handoff](../docs/development-plan.md) records exact code commits and each CI run. Earlier passes do not validate new native changes, and an in-progress run is not a passing package. Real CHZZK credentials, TLS deployment and dual-PC hardware have not been verified by these fixtures. Historical resource/capture variability is not considered diagnosed merely because a later run passes.

## Next product path

Validate a real authorized CHZZK channel through the native path. Add revocable creator/device enrollment and renewal instead of requiring manual developer keys, and give the same native runtime an independent gaming-PC lifecycle. Do not ship the probe as the public backend or broaden arbitrary-page native privileges. Preserve the no-gaming-PC-OBS condition and qualify clean video separately. Ad selection, audience measurement, HP and rewards remain core goals but receive no credit from preview or private-display traffic.

Primary references:
- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- https://socket.io/docs/v2/client-api/
- https://github.com/websockets/ws/blob/8.21.3/README.md#client-authentication
- https://github.com/websockets/ws/blob/8.21.3/doc/ws.md
