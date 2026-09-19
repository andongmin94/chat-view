# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and docs/architecture.md; re-fetch OBS refs/checks before writing.

## Confirmed direction

OBS is the only active branch; main is the original product/UX reference. Preserve all five goals: one-screen private chat HUD; OBS integration with stream exclusion; dual-PC with NO OBS on the gaming PC; first-party CHZZK/platform; creator-selected public advertising with audience-time-related HP and rewards. Pairing is not clean video, render/message counts are not audience exposure, and estimates are not payouts. The native product and its capture safeguards stay functional while replacements are built.

## Current slice: P2a native display, not complete account/HUD integration

Starting commit: 6598edc1feeda288ecb6c5063e72c87f94957698. Its Windows build #282 completed successfully (re-fetched this session), including official OBS installation/runtime/capture qualification and package upload:
https://github.com/andongmin94/chat-view/actions/runs/35452295314

P2a adds NativeChatSurface on the existing WebViewHost/DirectComposition/WebView2 engine. The browser and native document use one shared chat-renderer.js; browser SSE setup stays separate. CMake embeds the shared renderer and native bridge into a nonce-bound local document. No hosted URL, arbitrary external-page privilege, bundled developer secret, new desktop framework or npm dependency is added. Existing native settings, capture interlock, input/placement and OBS-parent lifecycle are unchanged.

The native display API opens only on a ready host, waits for document load plus its correlated readiness message, and accepts bounded versioned JSON with PostWebMessageAsJson. It rejects wrong document/pre-handshake/closed-host/invalid JSON/oversized input. The receiver accepts trusted bridge events, validates text/count/time bounds, renders text nodes and clears on invalid data, revoke, delivery loss or unload. Host-state messages do not refresh chat liveness. Navigation and process loss invalidate the binding; recreation needs a fresh nonce. Ready/rendered counts are diagnostics, never device authentication or ad evidence.

**Exact scope:** display implementation plus a real-WebView2 test harness. The class is compiled with the HUD, but no user-facing first-party mode is enabled and the CHZZK socket is NOT yet connected to the native process. Do not call this real CHZZK-to-HUD acceptance or OBS-free two-PC support. The developer probe is not promoted to a production server and is not bundled in the native installer. See [native-chat-display.md](native-chat-display.md).

## Tests for this slice

Local Node 22.16: 20 new shared-renderer/native-receiver tests passed; generated module syntax checked. Existing source text used for edits was verified against Git blob hashes before changing it. Local network/package downloads and Windows execution are unavailable, so local results are DOM/bridge simulations, not complete repository or native execution.

The existing CHZZK workflow must run its full suite (including the 20 new tests), strict types, locked installation/audit and manifest stability. Existing preview tests now import the shared renderer and verify its new asset is same-browser protected; there is no compatibility re-export at the old path. The preview asset map owns routing instead of duplicating its allowlist in the probe.

Windows CTest gains chat-view-native-chat-surface using the actual production WebViewHost with synthetic Unicode and HTML-looking strings. It checks actual document readiness/render-count acknowledgements, schema failure, revoke clearing, input bounds, setup/external navigation isolation, reopen and host destruction/recreation. It runs with the existing full Windows workflow and repeat tests, not a new ad-hoc workflow. This authoring record precedes that commit's CI; verify the exact run before calling it passed. No actual channel, GPU/gameplay/capture card, paid exposure or production identity was tested.

## Previously closed gates — do not restart these

- Q1-Q3 native quality fixes at a7ea2e7: Windows #274 passed 23 tests plus repeats and official OBS qualification. Old downloadable patches are obsolete.
- CHZZK P1-01a/b: reusable authorization/API/socket/session/parser, bounded authenticated loopback SSE and first-party browser renderer are implemented. Duplicate starts/reconnect/revoke/expiry behavior is covered. Real NAVER account/channel acceptance remains open.
- 3d801f2 CHZZK contract #7: Windows/Linux x Node 22/24 all passed npm ci, full audit (zero reported findings at the time), 82 tests, strict types and manifest stability. Locked graph and standard tsconfig are retained:
  https://github.com/andongmin94/chat-view/actions/runs/35452154580
- #281 was cancelled by the later documentation update; #282 at 6598edc succeeded. Do not describe #281 as passed or #282 as still running.

Dependency failure/recovery details remain in dependency-validation.md. The unavailable parseuri 2.0.0 experiment and temporary registry-inspection step are not current work. The structured Manager/Engine.IO options route and exact locked graph passed #7. Do not loosen audit/type checks or launch another parser selection project. Historical native #271 capture intermittency was not diagnosed by later passes; investigate only if it recurs and blocks a product test.

## Next concrete product boundary

After this slice's full CI, implement creator/device authorization and an authenticated bounded chat-delivery worker that feeds NativeChatSurface. Provider app secrets/tokens must stay on the service, and the native receiver must have revocable, scoped session access. Do not ask ordinary streamers for developer credentials, promote the loopback probe to production, introduce an unauthenticated local message port, or allow its HTTP URL through external-page settings. Avoid more generic rendering diagnostics; connect the implemented display layer to the product.

Real-provider test remains separate: authorized CHZZK login -> actual connected/subscribed -> Unicode message -> stop/reconnect/revoke. Owner-provided app credentials belong only in private configuration. Lack of them does not prevent independent product code, nor permit a simulated pass.

Then extend native lifecycle ownership for the gaming-PC companion without local OBS, and qualify a separate HUD-free video feed on real hardware. Record wiring/OS/GPU/game mode/HDR/refresh/latency/resources plus simultaneous local readability and recorded exclusion. Keep public ad rendering separate from private HUD data; follow with selection -> evidence -> server HP -> reward records before a bounded paid pilot.

Remaining owner input: real developer authorization, representative dual-PC hardware, HP/rate/payout rules and hosting assets. The goals, CHZZK priority, sole OBS branch and gaming-PC OBS prohibition are already settled.
