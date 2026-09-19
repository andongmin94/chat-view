# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; fetch current OBS refs and CI before changing code.

## Non-negotiable direction

OBS is the sole active development branch; main is the original UX reference. CHZZK first. No OBS installation/execution, portable/hidden OBS, or OBS projector on the gaming PC. Preserve all five goals: single-screen private HUD, OBS integration with private-HUD exclusion, dual-PC use, first-party platform, creator-selected public advertising with audience-time-related HP/rewards. Pairing is not clean video; private chat counts are not viewer exposure; an estimate is not a payout.

## Recovered state at this session's start

Starting remote: **b0e72a766c451036a62bcb188e4e00dd4e9f056c**. Interrupted sessions had already added P2a, so do not recreate it.

P1-01a/b authenticates a CHZZK creator, obtains a ticket, negotiates the documented Socket.IO/Engine.IO path, confirms CHAT subscription and renders bounded messages in ChatView's own browser UI. The dependency/type-check recovery is closed: 3d801f2 / CHZZK #7 passed all four Windows/Linux x Node22/24 jobs. Its locked graph and strict checks are retained. 6598edc / native Windows #282 subsequently passed.

P2a adds `NativeChatSurface`, compiled with the existing WebView2/DirectComposition host, and shared native/browser renderer code. It is not a selectable first-party HUD mode. The initial NavigateToString/data-URI loading defect was repaired by exact, fresh host-owned in-memory document URLs, not by loosening external URL policy. b0e72a7 also repaired the native bridge event check while preserving exact document/source/nonce binding. See native-chat-display.md.

## What the recovered CI actually says

- **Windows #288 / 35458181889 at b0e72a7:** build succeeded; initial CTest 24/24 passed. The new native-chat-surface test also passed all five repeats. An existing hud-resource-soak repeat then failed its growth budget: baseline 2725 handles / 97,935,360 private bytes; final 2936 handles / 114,798,592 private bytes. Official-OBS/package stages did not run. This does not invalidate the observed display-test results, but the full build is not green and the resource cause remains undiagnosed.
  https://github.com/andongmin94/chat-view/actions/runs/35458181889
- **CHZZK #9 / 35458181887 at b0e72a7:** npm ci succeeded; the audit endpoint returned HTTP503 maintenance. Tests/types were consequently skipped. This was not a new vulnerability report; do not change package versions or disable the audit on that basis.
  https://github.com/andongmin94/chat-view/actions/runs/35458181887

These are the starting runs, not results for the new P2b change. Inspect the exact new commit's runs. A later successful resource test alone would not prove a diagnosed fix for #288; keep that distinction. Do not increase budgets, remove assertions, or start another indefinite preflight project merely to hide it.

## This change: P2b scoped display delivery

`platform/server/chat/display-access.mts` owns expiring, hash-stored, single-use display tickets and separate chat-read leases for one verified creator context. `display-gateway.mts` exchanges the ticket over HTTP and sends the existing native display envelope over a read-only standard WebSocket. The developer probe invokes these modules after its existing creator authentication; display capabilities do not authorize probe management, provider calls, advertising or payouts.

A creator may issue a one-use key and revoke display connections without revoking CHZZK tokens. Expiry, account loss, token rotation and upstream revocation clear display authorization. Multiple authorized display readers share the existing CHZZK session; no duplicate upstream subscription or audience credit. The gateway strips non-display data using the same JavaScript validator as NativeChatSurface and bounds peers, messages, frame size, buffering and lifetime. No secret in URL/log/chat data and no arbitrary-page native privilege.

This is an executable service-to-display protocol and integration layer, **not** public account enrollment, durable machine identity, a native WinHTTP consumer, or full two-PC support. The probe still binds loopback only and must not ship with developer secrets. Production TLS/account/device enrollment and the native connection UX are outstanding. See [display-delivery.md](display-delivery.md) for exact endpoints and trust boundaries.

Reuse `ws` 8.21.3 and its 8.18.1 types, already present in the lock, as explicit dependencies. No resolved version or integrity is changed; no new package implementation or protocol shim is introduced. Native src/, native tests/, CMake, installers, capture interlocks and Windows workflow are unchanged from b0e72a7.

### Verification at authoring

15 local access-policy tests passed; new TypeScript source/test syntax checks passed. Exact downloaded probe/native JavaScript/lock baselines were checked against Git blob hashes before editing. The local environment cannot download npm dependencies, so actual HTTP/WebSocket/probe tests and strict type checking are delegated to the existing four-job CHZZK CI. Do not call them passed until reading its actual result. No real CHZZK credentials, local Windows execution or dual-PC hardware were available.

The new tests exercise one-use exchange, reader-only authority, rejection of cookie/ticket substitution, expiry/revoke of active sockets, frame validation, native receiver acceptance, Unicode, incoming data rejection, buffering and shared CHZZK delivery. The provider is simulated even when the HTTP/WebSocket libraries and sockets are real. Existing renderer and auth regressions are preserved.

## Next product acceptance tests

1. Inspect this P2b commit's installed dependency audit, complete platform tests/types, and native run. Repair genuine blockers only; do not redo already completed parser selection or Q1-Q3 lifecycle patches.
2. Add a bounded native delivery worker and a user-facing scoped connection flow feeding the existing NativeChatSurface. Clear on transport/lease/document loss; enforce local expiry; keep networking off the OBS/UI callback path. Never use an external page as a privileged bridge or ship a developer secret.
3. Real authorized CHZZK app/channel: actual consent -> connected/subscribed -> real Unicode message in own HUD -> stop/reconnect/revoke. The developer probe and synthetic tests do not close this gate.
4. Gaming-PC companion without local OBS, and a separately tested HUD-free feed. Record OS/GPU/window mode/capture wiring/HDR/refresh/latency/resources and simultaneous readable local HUD / clean recording. Pairing alone does not qualify video.
5. One selected public ad -> supported exposure evidence -> server HP -> reward record, then a bounded paid pilot with metric rights, campaign budgets/idempotency/fraud and payout rules. No synthetic or private chat traffic becomes payable exposure.

Remaining owner inputs: private provider authorization, representative dual-PC hardware, HP/rate/payout semantics and hosting assets. The five goals, CHZZK-first and gaming-PC OBS prohibition are confirmed, not questions to repeat.

Historical quality record: a7ea2e7 / Windows #274 passed 23 tests plus five repeats and official-OBS/package stages; Q1-Q3 is closed. Later platform work did not reopen those fixes. Earlier capture intermittency and #288 resource variability require evidence, not blanket success claims.
