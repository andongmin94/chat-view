# CHZZK integration and private-display service modules

The development path is authorization -> API session URL -> Socket.IO connected -> subscription acknowledgement -> CHAT -> ChatView-owned renderer. The gateway shares one creator's upstream with the native WinHTTP consumers. The HTTPS service adds isolated accounts, gaming/streaming roles and protected provider persistence across restarts. **This is development code, not deployed multi-user, live CHZZK, clean two-PC video or advertising certification.** [Current status](../docs/development-plan.md) is the only work queue; [PRODUCT.md](../PRODUCT.md) and [development-workflow.md](../docs/development-workflow.md) govern goals/cadence.

## Private developer setup

Use the Node version range in package.json (Node 22.16+ or 24). Register a CHZZK application with user-info/chat-read scopes. Keep credentials in the Git-ignored .env copied from .env.example. Ordinary streamers never supply developer secrets or run this probe publicly. Leave DEBUG unset: upstream diagnostics may expose secrets/messages and the production socket factory rejects it.

For the local probe, register this exact callback:

```text
http://127.0.0.1:47831/callback
```

```sh
cd platform
npm ci --ignore-scripts
npm run connect
```

Open the printed loopback address, authorize the channel, choose **채팅 수신 시작 / 재연결**, and open **챗뷰 자체 채팅 화면**. Send a real message using the normal CHZZK client. Subscription readiness is not first-message receipt; a quiet channel need not be broken. Without actual app/channel authorization the real-provider check remains unverified.

Stop removes this CHAT subscription, not the user's provider authorization. Reconnect obtains a fresh session URL. Duplicate start does not duplicate subscriptions. Probe refresh stops chat before rotation; expiry/revocation stops delivery. **권한 철회** is an explicit global app/user action and may affect other devices. The probe remains loopback-only and keeps provider tokens in memory; it is not the HTTPS service or its persistence path.

## HTTPS creator service

`server/main.mts` reuses the provider/chat/display modules. Set the server-only values in `.env.example`: canonical HTTPS `CHATVIEW_ORIGIN` without a trailing slash, TLS key/certificate files, absolute `CHATVIEW_SESSION_DATABASE`, absolute `CHATVIEW_PROVIDER_KEY_FILE`, and CHZZK application credentials. Register exactly `CHATVIEW_ORIGIN` plus `/callback`. Restrict files and their parent directories to the service account and keep them outside repository/distribution assets. The listener uses the origin's port (443 by default) and `CHATVIEW_BIND_ADDRESS`.

The provider encryption key is **32 raw random bytes**, not a password, hex string, display key or streamer setup step. Provision it once through the host's secret-management process. For a local operator-controlled deployment, this command creates a new file and refuses to overwrite an existing key:

```sh
node --input-type=module -e 'import {randomBytes} from "node:crypto"; import {writeFileSync} from "node:fs"; writeFileSync(process.argv[1], randomBytes(32), {flag:"wx", mode:0o600});' /secure/path/chatview-provider.key
```

Use an existing private parent directory and point `CHATVIEW_PROVIDER_KEY_FILE` at that file. POSIX group/world permissions are rejected; on Windows enforce a service-account-only ACL separately. Keep the same key across restarts, separate from the SQLite volume and its backups. A missing, malformed or wrong key stops startup; it never creates a replacement key or discards the database. Key loss prevents decryption. Key rotation and restoring old backups require an operator procedure and fresh authorization where refresh state is uncertain; do not restart against a stale database snapshot and assume its one-use tokens remain valid.

```sh
cd platform
npm ci --ignore-scripts
npm run serve
```

Run **one service instance per database**. This is not a replicated backend. SQLite transactions fence stale refresh results and atomic revocation within this service, not distributed live gateway state. The listener terminates TLS directly; `X-Forwarded-Proto` cannot authorize a plaintext backend. Use a certificate trusted by the native client and never disable certificate checks. Hosting, certificates, ingress abuse controls, backup/recovery and live acceptance are not provisioned by this command. Do not log callback queries, authorization/cookie headers, provider responses, keys or private chat.

## Login, roles and restart continuity

In the existing HUD connection panel (**Ctrl+Alt+Shift+C**), use the HTTPS service origin, leave local HTTP permission off and choose **로그인 / 연결**. The system browser authenticates the creator and asks for channel confirmation and **게임 PC · 개인 HUD로 연결** or **송출 PC · OBS 역할로 연결**. Independent browser logins of the same channel join one logical session/upstream. A submitted channel ID cannot join another creator. No hardware identifier, pairing product or manual display key is required. For the local probe use its loopback origin and explicitly allow local HTTP instead.

Every approved connection is renewable during its current run. **이 PC에서 연결 유지** only controls Windows user-scoped persistence across app restarts. Native **로그아웃** clears that local credential and revokes its ChatView approval; these are not provider tokens. Short display bearers remain chat-read-only and are reissued after service restart through the existing renewal route.

The service stores CHZZK channel/token/absolute-expiry records using AES-256-GCM, with a fresh nonce and authenticated owner/revision binding. It does not persist chat messages or browser login cookies. On a valid native renewal after restart, it loads only that creator, checks provider identity/authorization, and shares one upstream among its approved PCs. Expired provider access is refreshed once for concurrent requests, not given a new lifetime by loading it. Native app-approval expiry still applies.

Before refreshing, the service commits an unavailable/in-flight record; only the matching successful result can install a new encrypted grant. A crash or ambiguous refresh response does **not** replay the previous token after restart. That creator must reauthorize, after which still-valid native roles can resume. Confirmed provider denial/revocation removes the provider grant and all dependent PC approvals atomically, so those revoked approvals cannot return after reconsent. A temporary identity lookup or chat transport outage does not by itself delete a valid provider grant.

The account page lists only that creator's connections. **이 연결 해제** revokes one approval; **모든 PC 연결 및 치지직 권한 철회** first invalidates local authority, then reports provider-revoke success separately. **이 브라우저만 로그아웃** leaves native approvals intact. One streaming role is permitted; revoke its old connection before approving a replacement. Native launch-role detection, role-status UI and OBS output observation are still separate work. `/broadcast/session` returns the calling approval's membership and `captureState: unverified`, not a measured broadcast interval or clean-video proof.

[Native display](../docs/native-display-client.md) owns controls and document isolation; [delivery contract](../docs/display-delivery.md) owns base authorization limits. Management pages never run inside the private HUD. Do not put management URLs or secrets into external-chat settings, display URLs, logs or command lines. Actual service-hosting and Windows restart acceptance remain distinct from the synthetic tests.

## Data and dependency contract

The renderer creates text nodes, makes no remote media requests and keeps at most 100 bounded messages in memory. Stop/disconnect/revoke clears text. No chat database or reward ledger exists. Graphical badges/emoticons are not yet rendered; the adapter does not invent unique provider message IDs or deduplicate legitimate repeated text. Display acknowledgements, devices and preview counts are not viewers or billable exposure.

The locked Socket.IO 2.0.3/Engine.IO3 client uses a fresh Manager with structured options parsed by Node URL. **Do not change it to `io(url)`, `new Manager(url)` or the legacy host option:** the parseuri 3.0.2 override is not the legacy callable URI API. Framing remains the library's job; no parser shim. Dependency changes require installed-library/protocol verification. Downstream gateway bounds do not imply pre-decoding limits on the separate CHZZK upstream.

## Relevant checks

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

Focused persistence/HTTP checks using synthetic provider/chat/gateway boundaries:

```sh
node --experimental-strip-types --test tests/provider-grants.test.mts tests/platform-service.test.mts tests/provider-restart.test.mts
```

`provider-restart-wire.test.mts` additionally uses the actual locked ws library and display gateway. These tests cover SQLite reopen, a child-process exit during refresh, account/role isolation, renewal and revocation. They are not live CHZZK, trusted-certificate, Windows HUD or physical dual-PC acceptance. The contract CI uses locked installs and strict types on Windows/Linux and Node22/24; native and distribution checks remain separate. Exact executed evidence and the next user flow belong only in the current plan, not in assumptions about earlier CI passes.
