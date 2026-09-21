# CHZZK integration and private-display service modules

The implemented development path is authorization -> API session URL -> Socket.IO connected -> subscription acknowledgement -> CHAT -> ChatView-owned renderer. The scoped gateway shares that session with the implemented native WinHTTP consumer. An HTTPS creator service now composes these modules with isolated browser/account contexts and shared gaming/streaming session roles. **This is development code, not a deployed multi-user service, clean two-PC video or an advertising meter.** [Current status](../docs/development-plan.md) is the only work queue; [PRODUCT.md](../PRODUCT.md) and [development-workflow.md](../docs/development-workflow.md) govern goals/cadence.

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

## HTTPS creator service (development)

`server/main.mts` is separate from the local probe and reuses the same provider/chat/display modules. Set the server-only values shown in `.env.example`: a canonical HTTPS `CHATVIEW_ORIGIN` without a trailing slash, TLS key/certificate files, an absolute `CHATVIEW_SESSION_DATABASE` path, and protected CHZZK application credentials. Register exactly `CHATVIEW_ORIGIN` plus `/callback` with CHZZK. Key files and the database must be restricted to the service account and kept outside repository/distribution assets. The server listens on the origin's port (443 by default) and `CHATVIEW_BIND_ADDRESS`.

```sh
cd platform
npm ci --ignore-scripts
npm run serve
```

This listener terminates TLS directly. A reverse proxy must not turn public HTTPS into an implicitly trusted plaintext backend: `X-Forwarded-Proto` is not authorization. Use a valid certificate accepted by the native client; do not disable certificate checking. Certificates, hosting, ingress abuse controls, backup/recovery and live-provider acceptance are not provisioned by this command. Do not log callback query strings, authorization/cookie headers, provider responses or private chat. The loopback-only HTTP route adapter exists for synthetic tests, not public operation.

In the existing HUD connection panel, use the configured HTTPS origin, leave the local-HTTP permission off and choose **로그인 / 연결**. The system browser authenticates the creator and asks for channel confirmation and either **게임 PC · 개인 HUD로 연결** or **송출 PC · OBS 역할로 연결**. A separate browser on the other PC can approve the same channel and join the same logical session; a different creator cannot join by submitting a channel ID. No hardware ID, pairing product, or manual display key is required.

The account page lists that creator's connections. **이 연결 해제** revokes only the selected renewable approval. **모든 PC 연결 및 치지직 권한 철회** first stops that creator's local approvals/delivery, then reports whether the provider revoke succeeded. **이 브라우저만 로그아웃** does not revoke native sessions. One streaming role is allowed; revoke its previous connection before approving a replacement. Native launch-role detection, role-status UI and OBS output observation are not yet wired to these server roles. The session ID groups approved connections; it is not a measured broadcast interval. `/broadcast/session` accepts the existing renewal credential, returns only its own membership and explicitly reports `captureState: unverified`.

Provider credentials stay in server memory in this slice. Server restart requires CHZZK reauthorization; saved app-approval hashes and roles remain in SQLite and can resume after the same creator reauthorizes. Ambiguous provider refresh failure suspends the affected account rather than replaying a one-use token. Bounded upstream reconnect is distinct from provider token refresh. These limits must not be presented as uninterrupted operational service.

## Native display

Start an OBS-managed HUD, or use the explicit developer `--companion` entry point without local OBS. For the local probe, in **Ctrl+Alt+Shift+C**, enter `http://127.0.0.1:47831`, explicitly allow the local server and choose **로그인 / 연결**. For the HTTPS service use its origin as above. ChatView creates a request-bound verifier, opens the system browser, and delivers chat only after the creator explicitly approves the displayed channel. No display key needs to be copied into the HUD.

Every approved connection receives a renewable ChatView read session so a long broadcast does not require a new approval every five minutes. **이 PC에서 연결 유지** controls only persistence across app restarts: when selected, the renewal credential is stored with Windows user-scoped protection; when not selected it remains memory-only. **로그아웃** removes the local remembered approval and asks the service to revoke that ChatView session. These are ChatView display credentials, never CHZZK developer secrets or provider access tokens.

[Native display](../docs/native-display-client.md) owns controls, Windows protected persistence and document isolation; [delivery contract](../docs/display-delivery.md) owns the base login/session/display authorization limits. The private surface never loads the privileged management page. Do not paste the management URL into external chat settings or put credentials in URLs, logs or command lines. The HTTPS entry point is implemented, but actual hosting and the new service's Windows end-to-end acceptance remain unverified in the current plan.

## Current data and dependency contract

The browser preview retains same-browser Host/Origin/cookie/state/one-use-callback/CSRF protections. Display consumers use separate ticket/bearer credentials with no management or financial authority; the HTTPS service does not expose the probe's manual ticket route. The shared renderer creates text nodes and makes no remote media requests. It retains at most 100 bounded messages in memory; no chat database or reward ledger exists. Stop/disconnect/revoke clear text. Provider graphical badges/emoticons are not yet rendered, and the adapter does not invent unique provider message IDs or deduplicate legitimate repeated text.

The locked Socket.IO client 2.0.3/Engine.IO3 path uses a fresh Manager with structured options parsed by Node URL. **Do not change it to `io(url)`, `new Manager(url)` or the legacy host option:** the pinned parseuri 3.0.2 override is not a callable replacement for the legacy URI API and the selected options-only path does not invoke it. Socket framing remains the library's job; do not introduce a parser shim. Socket.IO parser and ws versions/integrities remain in package-lock.json. Version changes need protocol/installed-library verification, not assumptions about a newer client.

The gateway and UI bound their queues and frames; these downstream bounds do not imply a pre-decoding frame-size limit on the separate CHZZK upstream transport. Device/preview counts and chat messages are not viewers or billable ad exposure.

## Relevant checks

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

Focused service checks:

```sh
node --experimental-strip-types --test tests/service-roles.test.mts tests/platform-service.test.mts
```

The new service tests use actual HTTP/SQLite with synthetic provider/chat/gateway boundaries. They do not prove a WebSocket wire exchange, a trusted TLS certificate, a native HUD session, or a real CHZZK account. The existing contract workflow separately checks Windows/Linux and Node22/24 with locked install, full runtime/development audit, strict types and unchanged manifests; its other tests exercise actual local WebSocket/library traffic and synthetic provider responses. Windows native checks separately exercise WinHTTP and WebView2; Node is a fixture tool, not a desktop runtime dependency. Run relevant checks during development and full qualification for a distribution candidate according to the workflow document. Do not reuse historical audit/CI success as current provider, deployment or hardware evidence.

Extend this service rather than rebuilding login or adding device enrollment. The current plan records what was actually executed and which persistence, native, hosting and clean-video work remains. Live authorization and clean-video testing proceed when their external prerequisites exist; missing credentials do not prohibit independent implementation.
