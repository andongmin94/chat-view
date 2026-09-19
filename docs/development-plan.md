# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; re-fetch OBS refs/checks before writing.

## Confirmed direction

OBS is the only active branch; main is the original product/UX reference. Preserve the five goals: one-screen private chat HUD; OBS with stream exclusion; dual-PC with NO OBS on the gaming PC; own CHZZK/platform; creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; render/message counts are not audience exposure; estimates are not payouts. Do not reinterview the owner about CHZZK or gaming-PC OBS.

## Current work: finish the interrupted P2a native display

Starting remote: 170a2dddd77b72746dbb314b46fbe31c2fbe4479. The interrupted session had already added NativeChatSurface, shared browser/native text renderer, bounded display DTO receiver and real-WebView2 CTest. It had not connected account/device authorization or a network worker, and did not enable a user-facing mode. Preserve this work rather than rebuilding it.

Its Windows #286 / 35456426730 compiled, passed the existing 23 tests and failed the new native display test. Logs establish the cause: NavigateToString generated a data: navigation URI; the old about:blank guard cancelled it before script execution. Repair replaces the internal loading path with exact host-owned memory documents served through WebView2 WebResourceRequested. Setup/chat/clearing share that path. Normal external chat URL validation and capture policy are not widened. Source ownership now includes the exact fresh document identity as well as host/nonce/navigation correlation.

The repair removes temporary test URI/script tracing and adds real DOM assertions, forbidden/stale URL cancellation, successful setup loading, immediate delivery invalidation and queued clear/reopen checks. No new npm/native package, framework, local port, compatibility shim or workflow is introduced. The existing Windows workflow performs compilation, all CTests/repeats and official OBS/package validation. See [native-chat-display.md](native-chat-display.md) for the implementation contract.

**At authoring this repair has not yet run on Windows.** Check its exact commit's run before claiming success. Local work verified the original host source/header against their Git blob SHA and reviewed the bounded replacement; no local Windows/OBS execution or real-channel verification is claimed. Do not mistake the earlier 20 DOM/bridge tests for this native test.

## Existing validated layers

- Native Q1-Q3 at a7ea2e7: Windows #274 passed 23 tests/repeats and official OBS validation. Old downloadable patches are obsolete.
- CHZZK P1-01a/b: reusable API/auth/socket/session/parser and authenticated bounded loopback preview are implemented. Client secrets stay in developer/service configuration. Real NAVER authorization remains an open external acceptance test.
- 3d801f2 / CHZZK #7: four Windows/Linux x Node22/24 jobs passed locked install, audit, 82 tests, strict types and unchanged manifests. https://github.com/andongmin94/chat-view/actions/runs/35452154580
- Native Windows #282 / 6598edc: all stages passed. https://github.com/andongmin94/chat-view/actions/runs/35452295314
- P2a added 20 shared-renderer/native-receiver tests; consult its contract CI separately. It changed native files, so #282 does not validate P2a.
- 170a2dd / Windows #286: diagnosed native-document loading failure. https://github.com/andongmin94/chat-view/actions/runs/35456426730

Historical dependency failures and the closed parser/type-check recovery are in dependency-validation.md. Do not reopen that investigation. Native #271 capture intermittency was not diagnosed by later successes; investigate only if it recurs and blocks a product test. Never disable privacy assertions to pass CI.

## Product acceptance after P2a passes

1. Feed NativeChatSurface with a bounded authenticated chat-delivery worker and scoped revocable creator/device access. Keep OAuth/provider secrets server-side. Do not promote the loopback probe to public service or allow arbitrary pages/native commands. Reuse the implemented session/renderer; no further generic rendering diagnostics milestone.
2. In parallel, real authorized CHZZK login -> connected/subscribed -> Unicode chat -> stop/reconnect/revoke. Credentials only in private configuration. A mock/server fixture is not a live pass.
3. Extend native lifecycle to gaming-PC companion without local OBS, and separately qualify HUD-free video while HUD stays readable. Record wiring, OS/GPU/game mode/HDR/refresh/latency/resources. No OBS/projector workaround on gaming PC.
4. Selected public ad -> bounded exposure evidence -> server HP -> reward record, then a paid pilot only after metric rights, budget/idempotency/fraud and payout rules. Synthetic events never create payable credit.

Still absent: completed user-facing CHZZK-to-HUD connection, production accounts/backend, authenticated gaming companion, verified OBS-free two-PC clean feed, public campaigns and reward/payout system. The display class is not a substitute for these goals.

Remaining owner inputs: real developer authorization, representative dual-PC hardware, HP/rate/payout semantics and hosting assets. Their absence does not stop unrelated code work or permit inventing completion. End each session with exact changed/tested commit and run evidence.
