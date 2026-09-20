# CHZZK integration and private-display service modules

The implemented development path is authorization -> API session URL -> Socket.IO connected -> subscription acknowledgement -> CHAT -> ChatView-owned renderer. The scoped gateway shares that session with the implemented native WinHTTP consumer. **It is not a deployed multi-user service, durable device enrollment, clean two-PC video or an advertising meter.** [Current status](../docs/development-plan.md) is the only work queue; [PRODUCT.md](../PRODUCT.md) and [development-workflow.md](../docs/development-workflow.md) govern goals/cadence.

## Private developer setup

Use the Node version range declared in package.json (currently Node 22.16+ or 24). Register a CHZZK application with user-info/chat-read scopes and the exact callback below. Keep credentials in the Git-ignored .env copied from .env.example. Ordinary streamers must never supply developer secrets or run this probe as a public server. Leave DEBUG unset because upstream diagnostics may expose secrets/messages and the production socket factory rejects it.

```text
http://127.0.0.1:47831/callback
```

```sh
cd platform
npm ci --ignore-scripts
npm run connect
```

Open the printed loopback URL in the system browser, authorize the channel, choose **채팅 수신 시작 / 재연결**, and open **챗뷰 자체 채팅 화면** for browser preview. Use the normal CHZZK client to send a real test message. Subscription readiness and first-message receipt differ; a quiet channel need not be broken. Without actual app/channel authorization this real-provider check remains unverified, not simulated success.

Stop unsubscribes this CHAT session; reconnect obtains a fresh session URL. Duplicate start does not duplicate subscriptions. Refresh stops chat before rotating the token. Expiry/upstream revocation stop delivery. **권한 철회** globally revokes the app/user tokens and may affect other devices; simply exiting does not perform global revocation.

## Native display

Start an OBS-managed HUD, or use the explicit developer `--companion` entry point without local OBS. In **Ctrl+Alt+Shift+C**, enter `http://127.0.0.1:47831`, explicitly allow the local server and choose **로그인 / 연결**. ChatView creates a request-bound verifier, opens the system browser, and delivers chat only after the creator explicitly approves the displayed channel. No display key needs to be copied into the HUD.

Every approved connection receives a renewable ChatView read session so a long broadcast does not require a new approval every five minutes. **이 PC에서 연결 유지** controls only persistence across app restarts: when selected, the renewal credential is stored with Windows user-scoped protection; when not selected it remains memory-only. **로그아웃** removes the local remembered approval and asks the service to revoke that ChatView session. These are ChatView display credentials, never CHZZK developer secrets or provider access tokens.

[Native display](../docs/native-display-client.md) owns controls, Windows protected persistence and document isolation; [delivery contract](../docs/display-delivery.md) owns login/session/display authorization limits. The private surface never loads the privileged management page. Do not paste the loopback management URL into external chat settings or put credentials in URLs, logs or command lines. Production still needs a deployed multi-user account/channel service and reviewed TLS/reverse-proxy trust; the loopback probe is not that service.

## Current data and dependency contract

The browser preview retains same-browser Host/Origin/cookie/state/one-use-callback/CSRF protections. Display consumers use separate ticket/bearer credentials with no management or financial authority. The shared renderer creates text nodes and makes no remote media requests. It retains at most 100 bounded messages in memory; no chat database or reward ledger exists. Stop/disconnect/revoke clear text. Provider graphical badges/emoticons are not yet rendered, and the adapter does not invent unique provider message IDs or deduplicate legitimate repeated text.

The locked Socket.IO client 2.0.3/Engine.IO3 path uses a fresh Manager with structured options parsed by Node URL. **Do not change it to `io(url)`, `new Manager(url)` or the legacy host option:** the pinned parseuri 3.0.2 override is not a callable replacement for the legacy URI API and the selected options-only path does not invoke it. Socket framing remains the library's job; do not introduce a parser shim. Socket.IO parser and ws versions/integrities remain in package-lock.json. Version changes need protocol/installed-library verification, not assumptions about a newer client.

The gateway and UI bound their queues and frames; these downstream bounds do not imply a pre-decoding frame-size limit on the separate CHZZK upstream transport. Device/preview counts and chat messages are not viewers or billable ad exposure.

## Relevant checks

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

The existing contract workflow checks Windows/Linux and Node22/24 with locked install, full runtime/development audit, strict types and unchanged manifests. Tests exercise actual local HTTP/WebSocket/library traffic and synthetic provider responses. Windows native checks separately exercise WinHTTP and WebView2; Node is a fixture tool, not a desktop runtime dependency. Run relevant checks during development and full qualification for a distribution candidate according to the workflow document. Do not reuse historical audit/CI success as current provider, deployment or hardware evidence.

For implementation, extend these modules into the multi-user creator/channel service and same-user two-PC session described by the current plan; browser login and in-run/remembered display renewal are already development code and should not be rebuilt. Live authorization and clean-video testing proceed when their external prerequisites exist; missing credentials do not prohibit independent implementation.
