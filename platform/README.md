# CHZZK connection slice — P1-01a

This is an executable developer probe plus the reusable TypeScript CHZZK HTTP client. It is **not** the public platform server, a completed chat integration, or a native HUD replacement. No runtime npm packages, database or new desktop framework are required. The public Express/React/PostgreSQL architecture remains a proposal; the loopback test tool is not its authentication architecture.

## Run locally

Use a supported Node.js 22 or 24 release. Register an application in the CHZZK developer center with `유저 정보 조회` and, for the next chat step, `채팅 메시지 조회`. The redirect URI must match exactly:

```text
http://127.0.0.1:47831/callback
```

Copy `platform/.env.example` to `platform/.env` on your own machine and enter the application credentials there. It is ignored by Git. Never send the file or its values in chat, commits, screenshots, logs, or diagnostics. These are developer application credentials, not something ordinary streamers should configure.

From the repository root:

```sh
node --experimental-strip-types --env-file=platform/.env platform/tools/chzzk-connect.mts
```

Open the printed local address in your system browser, follow the local link, and choose **치지직 로그인**. After consent the page shows only your channel ID/name. The other explicit actions check session-URL issuance, rotate the access/refresh tokens, and revoke the application's tokens. Session URLs are validated but deliberately never sent to the browser or connected by this slice.

**권한 철회 affects all tokens for the same application/user, potentially disconnecting other devices.** It is an explicit action, never an automatic exit cleanup. Ctrl+C closes the probe and drops its in-memory token references; it does not claim secure memory erasure or server-side revocation. Do not deploy this tool, proxy it onto the internet, bind it to a LAN address, or package the developer client secret in the gaming-PC companion.

## What actually works in this slice

Authorization URL construction -> one-use, browser-bound callback -> JSON token exchange -> authenticated user lookup -> explicit session ticket issuance/refresh/revoke. The code uses the documented endpoints, camelCase fields, Bearer header and common response envelope. Refresh requests are serialized by the probe and never retried automatically: CHZZK refresh tokens are one-use. An ambiguous refresh failure discards local credentials and requires reauthorization.

The API client has a fixed HTTPS origin, no redirect following, response size/time bounds and safe error categories. It has no logger or credential getters. The probe binds only to 127.0.0.1; it validates Host, Origin, an action nonce, same-browser cookie and expiring OAuth state. Returned channel text is HTML-escaped. Pages are no-store, use no third-party assets/scripts, and explicitly distinguish session issuance from actual chat reception. Tests use synthetic secrets only.

## Checks

```sh
node --experimental-strip-types --test platform/tests/*.test.mts
```

The `CHZZK contract` workflow runs the tests on Windows/Linux and Node 22/24 and performs strict TypeScript checking. Its compiler/type tools are installed at pinned top-level versions in an isolated CI directory, not as product runtime dependencies. Node type stripping is execution, not a substitute for the separate type check.

## Next boundary: socket and messages

Official Session documentation checked on 2026-09-19 specifies Socket.IO-client 1.0.0 through 2.0.3, WebSocket transport, `SYSTEM connected` with a sessionKey, then POST chat subscription. Do not assume a current 4.x client works, implement Socket.IO framing yourself, or install an old client into the public service without dependency/security review and a real authorized-channel test. No Socket.IO dependency is installed by this commit.

P1-01b must select and qualify a concrete transport, then connect the issued URL, subscribe using the received sessionKey, receive CHAT events, and handle unsubscribe/revoke/disconnect. That work, actual message rendering, and native HUD integration are **not complete** here. No synthetic event is presented as a real provider message.

Official Live API currently supplies paginated live listings (up to 20 per page) and concurrentUserCount; it does not document individual ad watch time or a per-channel viewer-time billing endpoint. Do not use this probe as proof of HP/ad accounting feasibility. Permissions, sampling and the reward rules remain a separate acceptance gate.

## Sources and evidence

Primary documentation rechecked 2026-09-19:

- https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- https://chzzk.gitbook.io/chzzk/chzzk-api/tips
- https://chzzk.gitbook.io/chzzk/chzzk-api/user
- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://chzzk.gitbook.io/chzzk/chzzk-api/live
- https://socket.io/docs/v2/client-api/
- https://nodejs.org/docs/latest-v22.x/api/typescript.html

Local evidence: 48 tests passed with Node 22.16.0; strict type checking passed with TypeScript 5.8.3 and Node types 25.1.0. The tests execute real local HTTP requests but **simulate the upstream CHZZK service**. No application credentials were available, so live authorization, real session issuance, live chat and hardware testing were not performed. See `docs/development-plan.md` for the latest remote CI status and next session handoff.
