# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; fetch current OBS refs and CI before changing code.

## Non-negotiable direction

OBS is the sole active development branch; main is the original UX reference. CHZZK first. No OBS installation/execution, portable/hidden OBS, or OBS projector on the gaming PC. Preserve all five goals: single-screen private HUD, OBS integration with private-HUD exclusion, dual-PC use, first-party platform, creator-selected public advertising with audience-time-related HP/rewards. Pairing is not clean video; private chat counts are not viewer exposure; an estimate is not a payout.

## Current result: P2b delivery contract verified

Implementation commit: **97875661581bcdd038759f1cd1bac9c8dbabaafb**. The preceding feature commit is 265d876c92e801dda7e480dc27060114e9b44216; 9787566 fixes only the test request used to forge Host.

**CHZZK contract #11 / 35460704182 passed all four Windows/Linux x Node22/24 jobs.** Each job passed locked installation, full dependency audit, all 135 tests, strict TypeScript checking and manifest-unchanged checks. The inspected Linux/Node24 log reports zero vulnerabilities and 135/135 passing tests with no skipped tests. The suite adds 33 access/gateway tests to the prior 102; it includes actual HTTP/WebSocket requests and the creator-authenticated probe sharing its simulated CHZZK upstream.

https://github.com/andongmin94/chat-view/actions/runs/35460704182

The first P2b run (#10) passed 134/135 tests but its forged-Host case used fetch, which replaced the test's Host value with the destination authority. A local HTTP reproduction confirmed that behavior. The correction uses node:http to actually transmit the forged Host, retaining both the 403 assertion and the check that a rejected request does not consume the valid ticket. Production guards were not relaxed.

**Native Windows #290 / 35460704187 for 9787566 was still in progress at the last check**, in Build, test, and install ChatView. Do not claim the package or the earlier resource-soak issue is resolved until reading that run. The final handoff commit changes only this document and skips its redundant push workflow; the implementation commit's native check remains independent and must finish. Do not conflate a documentation HEAD with the tested source commit.

https://github.com/andongmin94/chat-view/actions/runs/35460704187

The bounded P2b server-side authorization/delivery test gate is CLOSED. Next product implementation is the native delivery worker and scoped connection UX, not another generic authorization framework or a repeat of parser selection. Live CHZZK acceptance, native end-to-end connection and dual-PC video remain separate open gates.

## Recovered state at this session's start

Starting remote: **b0e72a766c451036a62bcb188e4e00dd4e9f056c**. Interrupted sessions had already added P2a, so do not recreate it.

P1-01a/b authenticates a CHZZK creator, obtains a ticket, negotiates the documented Socket.IO/Engine.IO path, confirms CHAT subscription and renders bounded messages in ChatView's own browser UI. The dependency/type-check recovery is closed: 3d801f2 / CHZZK #7 passed all four Windows/Linux x Node22/24 jobs. Its locked graph and strict checks are retained. 6598edc / native Windows #282 subsequently passed.

P2a adds NativeChatSurface, compiled with the existing WebView2/DirectComposition host, and shared native/browser renderer code. It is not a selectable first-party HUD mode. The initial NavigateToString/data-URI loading defect was repaired by exact, fresh host-owned in-memory document URLs, not by loosening external URL policy. b0e72a7 also repaired the native bridge event check while preserving exact document/source/nonce binding. See native-chat-display.md.

## Earlier CI evidence, not the current result

- **Windows #288 / 35458181889 at b0e72a7:** build succeeded; initial CTest 24/24 passed. The new native-chat-surface test also passed all five repeats. An existing hud-resource-soak repeat then failed its growth budget: baseline 2725 handles / 97,935,360 private bytes; final 2936 handles / 114,798,592 private bytes. Official-OBS/package stages did not run. This does not invalidate the observed display-test results, but that full run was not green and the resource cause remains undiagnosed.
  https://github.com/andongmin94/chat-view/actions/runs/35458181889
- **CHZZK #9 / 35458181887 at b0e72a7:** npm ci succeeded; the audit endpoint returned HTTP503 maintenance. Tests/types were consequently skipped. This was not a vulnerability finding. Later #10/#11 audits succeeded without repinning the graph to address that outage.
  https://github.com/andongmin94/chat-view/actions/runs/35458181887

A later successful resource test alone would not prove a diagnosed fix for #288. Do not increase budgets, remove assertions, or start an indefinite preflight project merely to hide it.

## Implemented P2b scope and boundaries

platform/server/chat/display-access.mts owns expiring, hash-stored, single-use display tickets and separate chat-read leases for one verified creator context. display-gateway.mts exchanges the ticket over HTTP and sends the existing native display envelope over a read-only standard WebSocket. The developer probe invokes these modules after its existing creator authentication; display capabilities do not authorize probe management, provider calls, advertising or payouts.

A creator may issue a one-use key and revoke display connections without revoking CHZZK tokens. Expiry, account loss, token rotation and upstream revocation clear display authorization. Multiple authorized display readers share the existing CHZZK session; no duplicate upstream subscription or audience credit. The gateway strips non-display data using the same JavaScript validator as NativeChatSurface and bounds peers, messages, frame size, buffering and lifetime. No secret in URL/log/chat data and no arbitrary-page native privilege.

This is an executable service-to-display protocol and integration layer, **not** public account enrollment, durable machine identity, a native WinHTTP consumer, or full two-PC support. The probe still binds loopback only and must not ship with developer secrets. Production TLS/account/device enrollment and the native connection UX are outstanding. See [display-delivery.md](display-delivery.md) for exact endpoints and trust boundaries. Its pending-at-authoring test notes are superseded by the #11 results above.

Reuse ws8.21.3 and its8.18.1 types, already present in the lock, as explicit dependencies. No resolved version or integrity changed; the clean install still resolves 54 packages. No new protocol implementation or compatibility shim was added. Native src/, native tests/, CMake, installers, capture interlocks and Windows workflow are unchanged from b0e72a7.

### Verification scope

Before pushing, 15 local access-policy tests and new TypeScript source/test syntax checks passed. Baseline probe/native JavaScript/lock content was checked against Git blob hashes before editing. Local package downloads were unavailable; the installed-library and strict-type results above were executed in GitHub Actions, not a local Windows workstation.

The new tests exercise one-use exchange, reader-only authority, rejection of cookie/ticket substitution, expiry/revoke of active sockets, frame validation, existing native JavaScript receiver acceptance, Unicode, incoming data rejection, buffering and shared CHZZK delivery. Backpressure uses a controlled buffered-byte observation; ordinary socket tests use the actual library. The provider is simulated. No real CHZZK credentials, physical Windows desktop or dual-PC hardware were available. The native JavaScript receiver test is not a WinHTTP-to-WebView2 end-to-end test.

## Next product acceptance tests

1. Read native Windows #290's final result and inspect any genuine failure. Do not redo completed parser/dependency selection, Q1-Q3 lifecycle patches, or P2b's passing contract tests as a substitute for product work.
2. Add a bounded native delivery worker and a user-facing scoped connection flow feeding the existing NativeChatSurface. Clear on transport/lease/document loss; enforce local expiry; keep networking off the OBS/UI callback path. Never use an external page as a privileged bridge or ship a developer secret. The five-minute developer lease is not a product decision to make streamers reconnect manually every five minutes; durable device authorization/renewal remains to implement.
3. Real authorized CHZZK app/channel: actual consent -> connected/subscribed -> real Unicode message in own HUD -> stop/reconnect/revoke. The developer probe and synthetic tests do not close this gate.
4. Gaming-PC companion without local OBS, and a separately tested HUD-free feed. Record OS/GPU/window mode/capture wiring/HDR/refresh/latency/resources and simultaneous readable local HUD / clean recording. Pairing alone does not qualify video.
5. One selected public ad -> supported exposure evidence -> server HP -> reward record, then a bounded paid pilot with metric rights, campaign budgets/idempotency/fraud and payout rules. No synthetic or private chat traffic becomes payable exposure.

Remaining owner inputs: private provider authorization, representative dual-PC hardware, HP/rate/payout semantics and hosting assets. The five goals, CHZZK-first and gaming-PC OBS prohibition are confirmed, not questions to repeat.

Historical quality record: a7ea2e7 / Windows #274 passed 23 tests plus five repeats and official-OBS/package stages; Q1-Q3 is closed. Later platform work did not reopen those fixes. Earlier capture intermittency and #288 resource variability require evidence, not blanket success claims.
