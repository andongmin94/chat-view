# CHZZK integration and private-display service

The implemented development path is authorization -> provider session -> chat subscription -> ChatView-owned text renderer, shared through the scoped gateway and native WinHTTP client. The HTTPS service adds isolated creator accounts, gaming/streaming roles, protected provider persistence and a separate test-campaign/public-banner flow. **This is development code, not a deployed service, clean two-PC video or an advertising meter.** [Current status](../docs/development-plan.md) is the work queue; [PRODUCT.md](../PRODUCT.md) and [workflow](../docs/development-workflow.md) govern scope and verification.

## Private developer probe

Use the Node version range in package.json (Node 22.16+ or 24). Register a private CHZZK app with required user-info/chat-read permissions and the exact local callback:

```text
http://127.0.0.1:47831/callback
```

Copy `.env.example` to the ignored `.env` and configure application credentials privately. Ordinary streamers never supply developer secrets or expose the probe as a server. Leave DEBUG unset; upstream diagnostics can expose confidential data and the socket factory rejects it.

```sh
cd platform
npm ci --ignore-scripts
npm run connect
```

Open the printed loopback address in the system browser, authorize, choose **채팅 수신 시작 / 재연결**, and open **챗뷰 자체 채팅 화면**. Send a real message through the ordinary CHZZK client to verify the actual provider; a quiet subscribed channel is not first-message evidence. Without app/channel approval, live-provider acceptance remains open.

Stopping unsubscribes this chat session; reconnect uses a fresh provider session URL. It does not globally revoke the user's provider tokens. Explicit provider revocation is a separate action. The local probe remains a developer adapter, not the deployed multi-user product.

## HTTPS service configuration

`server/main.mts` reuses the existing provider/chat/display modules. Set the server-only values from `.env.example`: canonical `CHATVIEW_ORIGIN` (HTTPS, no trailing slash), TLS key/certificate files, absolute `CHATVIEW_SESSION_DATABASE`, `CHATVIEW_PROVIDER_KEY_FILE` and protected CHZZK credentials. Register exactly the HTTPS origin plus `/callback`. The listener uses that origin's port (443 by default) and `CHATVIEW_BIND_ADDRESS`.

The provider encryption key is **32 raw random bytes**, not a password, hex string, display key or streamer setup step. Provision it once through host secret management. This operator-only example refuses to overwrite an existing file:

```sh
node --input-type=module -e 'import {randomBytes} from "node:crypto"; import {writeFileSync} from "node:fs"; writeFileSync(process.argv[1], randomBytes(32), {flag:"wx", mode:0o600});' /secure/path/chatview-provider.key
```

Use a private parent directory and point `CHATVIEW_PROVIDER_KEY_FILE` at it. POSIX group/world key permissions are rejected; Windows needs a service-account-only ACL. Keep the same key across restarts, separate from the SQLite volume/backups. Missing, malformed or wrong keys stop startup without replacing keys or deleting data. Lost keys cannot decrypt stored grants. Key rotation and old-backup recovery need an operator procedure and fresh consent where refresh state is uncertain; a stale database is not a safe token rollback.

```sh
cd platform
npm ci --ignore-scripts
npm run serve
```

Run **one service instance per database**. Transactions fence stale refresh results and local revocation, not distributed live gateways. TLS terminates directly; forwarded headers cannot authorize plaintext. Use a certificate trusted by the native client; never disable certificate checks. Hosting, certificates, ingress controls, backup/recovery and live acceptance are not provisioned by this command. Never log callback queries, authorization/cookie headers, provider responses, private chat or keys.

## Login, launch roles and connection status

In the existing **Ctrl+Alt+Shift+C** connection panel, enter the HTTPS origin, leave local HTTP permission off and choose **로그인 / 연결**. For the private local probe, use its loopback address and explicitly allow local HTTP.

The native launch fixes the role before login: companion requests **게임 PC · 개인 HUD**; validated OBS-controlled execution requests **송출 PC · OBS 관리 런타임**. The panel shows it read-only. The HTTPS browser page displays the requested role and channel with one confirmation action; there is no separate browser role picker. OBS-managed one-PC usage also has the streaming role: this is a process role, not a claim of two physical PCs.

After approval, the panel shows its approved role, shared session ID and server-side display connection counts. Independent browser logins for the same CHZZK creator join the same logical session/upstream; other creators remain isolated. No hardware ID, enrollment, copied session ID or manual display key is needed. Counts represent authorized open display sockets, not remote OBS health, verified video, audience or rewards. Half-open connections can remain counted until retired. **영상 제외 미검증** remains explicit.

The client validates the approved role/session/connection and bounded metadata before accepting a frame; it removes session metadata before publishing chat to WebView2. Stop, reconnect, expiry and failure clear that native status. Mismatched-role renewal returns 409 without evicting the valid lease; the native client retains the saved approval and directs the user to the original mode or explicit logout/reconsent. Gaming approval is never silently promoted.

Every approval is renewable during its current run. **이 PC에서 연결 유지** only chooses Windows user-scoped persistence across app restart. **연결 중지** and ordinary program exit are not server revocation. **로그아웃** uses the current approval regardless of persistence: it clears local chat/status immediately, removes only matching saved information and requests server revocation on the existing worker. A different saved account is preserved. Only a confirmed server response produces a success notice; storage/server failure is unconfirmed and can be retried explicitly within the same client process. It does not silently reconnect or recreate storage. Lost approval responses or interrupted logout may require creator connection management to confirm/revoke the remaining approval.

The account page lists only its creator's connections. **이 연결 해제** revokes one approval. **모든 PC 연결 및 치지직 권한 철회** invalidates local authority first, then reports whether provider revocation succeeded. **이 브라우저만 로그아웃** leaves native approvals intact. One streaming role is permitted; explicit native logout or owner revocation releases that role without revoking the gaming approval.

The streaming runtime separately reports fresh OBS streaming/recording activity using a short `broadcast:report` grant. The existing local IPC and an independent cancellable WinHTTP worker keep networking out of OBS callbacks and private-chat receipt. Samples must be less than three seconds old and accepted reports last at most fifteen seconds; loss/revocation returns the shared state to unknown. These are client-reported activity flags, not proof of viewer reception, capture safety or advertising exposure.

## Provider restart continuity

Provider channel/token/absolute-expiry records use AES-256-GCM with fresh nonces and authenticated owner/revision binding. Chat messages and browser cookies are not persisted. Valid native renewal after restart restores only its creator, checks provider identity/authorization and shares one upstream. Expired provider access refreshes once for concurrent requests; loading never extends its lifetime. Native approval expiry still applies.

Before refreshing, the service commits unavailable/in-flight state. Only a matching successful revision installs a replacement grant. Crash or ambiguous refresh response never replays the old token after restart; that creator must reauthorize, after which still-valid PC roles can resume. Confirmed provider denial/revocation atomically removes grants and dependent approvals. Temporary identity lookup/chat transport failure alone does not delete valid authority. This is not uninterrupted operation under every failure mode.

[Native display](../docs/native-display-client.md) owns controls/document isolation and [delivery](../docs/display-delivery.md) the protocol. Management pages never run in private chat. Role headers/metadata require matching service/native builds; no obsolete-interface migration or fallback is provided. Actual Windows service restart, provider and hardware acceptance remain separate from fixtures.

## Test campaign and separate public banner

Use the HTTPS service, not the developer probe. In the existing authenticated account page choose **시험 캠페인 선택 · OBS 공개 배너**. `/campaigns` lists one operator-defined ChatView campaign and its selected/stopped state. Selecting requires the account's valid provider authorization and an approved streaming connection. The browser's existing cookie and CSRF protect selection/stop; native chat, renewal and output capabilities cannot perform these management actions.

After selection, the page supplies a stable `/public/ads/<source-id>` address. The management screen now also separates **not selected**, **selected but waiting for a current streaming-PC output report**, and **current report permits test-banner display**. Refresh the management page to recheck the same server predicate as the public test source; no reading of this page creates a report. An OBS stopped/preview report can still permit the test banner, so even the ready label cannot prove an actual composed OBS frame, broadcast, or audience exposure. In OBS on the streaming PC, add a Browser Source with that URL and a 960 × 180 viewport, then explicitly arrange its position and size in the intended scene. Set Page permissions to None: the artwork has no need to read or modify OBS. Enable Shutdown source when not visible and Refresh browser source when scene becomes active for this setup. These properties are documented in the [official OBS Browser Source guide](https://obsproject.com/kb/browser-source). The current implementation does not insert sources or modify scenes automatically, and browser fixtures do not certify OBS scene transitions or captured video.

The public URL is a read-only artwork address, not a private display key. Its page/state/assets ignore account cookies and do not accept private credentials. Public reads never restore a provider grant or start a chat subscription. Responses contain only the bounded test campaign, visible/hidden state and a short remaining lifetime; they exclude account/session identities, private chat, provider tokens and financial actions. The page displays **시험 광고 · 지급 없음**. It contains no advertiser JavaScript, tracking media, OBS commands, HP deductions or reward accounting.

Selection is tied to the exact streaming approval in SQLite. Logout/expiry/revocation removes or invalidates it; a replacement streaming approval must explicitly select again. **공개 배너 중지** leaves that account's public source address intact and does not revoke private chat. Normal service restart retains a still-valid selection, but the page stays transparent until the existing sender reconnects and sends a fresh report. A recent report permits arranging the banner even when streaming/recording are stopped; preview is not payable exposure.

The page starts transparent. Normal stop appears on the next roughly two-second public poll, while report/approval lifetime and an independent renderer watchdog bound stale display. Failed/malformed/oversized responses hide artwork without affecting private chat. Page exit cancels its reads and clears text; page restoration starts a new read and never reuses an old advertisement deadline. Source IDs need not be copied between PCs for login or pairing. A live provider, trusted hosting and actual OBS/dual-PC capture remain separate acceptance requirements.

## Non-payable activity and optional official audience samples

The existing account and campaign screens link to `/campaigns/activity`. Only the authenticated owner can read that history; neither a public banner URL nor native display/report credentials grant access. Consecutive accepted output reports bracket activity intervals. Missing sequences, expiry, state/connection/clock changes and restart gaps are not filled. Estimates never grow just because an account or public page is read.

Official audience collection is **off by default**. After the operator has checked the application's current permissions, quota and terms for this data use/retention, set `CHATVIEW_AUDIENCE_SAMPLING=1` in protected server configuration and restart the service. Unset or `0` disables collection; other values fail startup. This is not a new streamer key, user scope or device setup. Implementation and API field availability are not confirmation of NAVER's approval for commercial advertising use. No live account or paid use is certified by fixtures.

The [official Live API](https://chzzk.gitbook.io/chzzk/chzzk-api/live) specifies `GET /open/v1/lives`, Client authentication, `size`/opaque `next` pagination and channel-wide `concurrentUserCount` with `liveId`. The [official common API guide](https://chzzk.gitbook.io/chzzk/chzzk-api/tips) specifies Client headers and quota errors. No per-channel filter, measured ad attention or numeric allocation is assumed. Only the existing server-held Client-Id/Client-Secret are used. No unofficial live-detail endpoint, user cookies, scraping or private viewer identifiers are introduced.

One shared on-demand scan serves selected, authorized creators with recent streaming reports. It starts at most once per minute, reads at most twenty pages of twenty rows, and has a fifteen-second total deadline. It ends early once all target channels are found. The ranking/list limit may exclude smaller channels: unscanned or absent channels stay **unmeasured**, never zero. HTTP 401/403/429 impose a five-minute cooldown without affecting private chat or refreshing/revoking user grants. A failed sweep discards samples; revoked targets and service close reject late results. The app's actual allocation must support this bound before enabling it.

Counts are fresh for at most ninety seconds from the server's request start. The provider does not supply a sample timestamp in this contract; request time is labelled accordingly. For each accepted continuous streaming interval, estimation holds the latest count known at its start, provided both ends retain fresh samples for the same live and continuity period. Recording-only/preview time, missing samples and errors are excluded. A new sample does not backfill a previously unknown interval. An explicit observed zero yields zero; no covered interval yields null. The page shows estimated viewer-minutes, covered streaming duration, uncovered streaming duration and current sampling status separately.

These are **channel-wide estimates, not measured advertisement viewing**. The banner may be hidden, cropped, absent or in another scene; viewer attention is not known. Measured ad time, HP and revenue remain uncalculated, and the test flow is never payable. History and estimated totals share one SQLite transaction and the existing retention policy: at most 1,000 retained intervals per owner starting within seven days, with fifty shown. Removing history also removes its estimate rows. Raw provider lists and interpolation/cache state do not persist. A normal restart preserves totals, not samples or missing-time estimates. Storage errors fail the report and break continuity without stopping private chat.

## Data and dependency contract

The private renderer uses text nodes, no remote media requests and at most 100 bounded messages. Stop/disconnect/revoke clears text. No chat database or reward ledger exists. Graphical badges/emoticons remain unimplemented; the adapter does not invent provider IDs or remove legitimate repeated text. Display acknowledgements/connections and public-banner reads are never billable exposure.

The locked Socket.IO 2.0.3/Engine.IO3 client uses a fresh Manager with structured options parsed by Node URL. **Do not change it to `io(url)`, `new Manager(url)` or the legacy host option:** parseuri 3.0.2 is not the legacy callable URI API. Framing stays with maintained dependencies, not a parser shim. Version changes require installed-library/protocol verification. Downstream gateway bounds do not imply pre-decoding bounds on the separate provider transport.

## Relevant checks

```sh
npm ci --ignore-scripts
npm test
npm run typecheck
npm audit --audit-level=low
```

The existing contract workflow runs locked dependencies, full tests, strict types, audit and unchanged-manifest checks on Windows/Linux and Node22/24. Role/campaign tests include actual HTTP/WebSocket owner/session separation, selection, stop, expiry, restart and rejection paths. Audience tests add the real HTTP adapter with synthetic official responses, bounded shared scans, count/unknown distinction, estimation/coverage and owner-page integration. Windows native fixtures exercise WinHTTP, DPAPI and WebView2, including role mismatch, metadata isolation and current-approval logout. The public-ad CTest loads the real public HTTP page in WebView2 and checks artwork, transparency, stop/reselection, synthetic page restoration and outage expiry. They use synthetic provider/OBS inputs, not live-account or OBS capture evidence. Node is a fixture/server dependency, not a desktop runtime requirement. Exact outcomes and next work belong only in the development plan; full qualification is reserved for a distribution candidate.
