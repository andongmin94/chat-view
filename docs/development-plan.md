# Development plan and session handoff

Updated: 2026-09-19 (UTC). Read AGENTS.md and PRODUCT.md first; re-fetch OBS refs and checks before writing.

## Non-negotiable direction

Develop on OBS only; preserve main as original UX reference. CHZZK first. The gaming PC must not require OBS, portable/hidden OBS or a projector. Preserve all five goals: single-screen private HUD, OBS integration and stream exclusion, dual-PC, own platform, creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; messages/heartbeats are not viewer exposure; an estimate is not a payout.

## Current product path: P1-01b

The interrupted session had already committed 0ac467b on top of 6d65177. This session resumed it, not reconstructed it. Reusable API/session/socket/parser and developer preview code connect authorization -> API-issued ticket -> SYSTEM connected/sessionKey -> POST subscription + matching acknowledgement -> CHAT -> authenticated SSE -> ChatView-owned text UI.

Duplicate starts do not duplicate subscriptions; reconnect obtains a new ticket. Stop/disconnect/unsubscribe/CHAT revocation/token expiry stop delivery and clear history. Refresh stops the old chat before rotation; global revoke stays explicit. The renderer has no provider secrets, native IPC or remote media. Bounded history/preview clients are not a pre-decoding WebSocket frame limit or ad measurement. The native external-page HUD still works as before and is not replaced yet.

## Dependency and CI recovery

Starting remote: 0ac467b1ed47ff06a30249adaa50c35d3c045250. Its native Windows #276 passed; contract #2 failed audit before actual-library tests. The attempted parseuri 2.0.0 version did not exist (#3/#4); registry metadata showed 3.0.2 has a new ES-module API. That diagnostic step is removed. See dependency-validation.md for the primary sources and original failed runs.

12470eb uses Node URL and upstream public Manager/Engine.IO object options, never their old URI constructors. It pins parser 3.3.6 and replaces the old parseuri package with 3.0.2 without adapting or invoking its different API. The real-library test preserves encoded ticket data through EIO3/WebSocket. Contract #5 passed audit and all 82 tests, then caught a test-only captured-address narrowing issue, fixed by capturing the validated port.

9c636e0 commits the actual 54-package resolver graph with published integrity values, uses npm ci, audits development dependencies too, and checks manifest stability. Contract #6 passed clean installation, audit (zero findings) and **82 tests on all four OS/Node jobs**. Both Linux jobs also passed strict types and manifest checks. Windows type checking exposed a separate original npm-script defect: cmd.exe passed literal *.mts arguments to tsc. This change replaces the shell-glob command with the same compiler options and file scope in a standard tsconfig.json, used on every OS. No shell-specific fallback, extra package, suppressed diagnostic or test exclusion is introduced. Scripts do not change the locked dependency graph.

**Next check:** the exact commit containing tsconfig.json must pass the complete matrix. At authoring its new run is pending. Do not report #6 as all-green or redo the dependency investigation; its remaining failure was the Windows typecheck invocation, not transport or package installation.

## Verified evidence

- a7ea2e7 / Windows #274: 23 tests plus five repeats, package/official OBS qualification passed. Q1-Q3 gate closed; never reapply the old patch.
- 6d65177 / Windows #275 and CHZZK #1: passed. Native run: https://github.com/andongmin94/chat-view/actions/runs/35447087516
- 0ac467b / Windows #276: passed. https://github.com/andongmin94/chat-view/actions/runs/35449471047
- 0ac467b / CHZZK #2: failed dependency audit. https://github.com/andongmin94/chat-view/actions/runs/35449471052
- 12470eb / CHZZK #5: audit + 82 tests passed; fixture type failure. https://github.com/andongmin94/chat-view/actions/runs/35451316782
- 9c636e0 / CHZZK #6: all four clean installs/audits/82 tests passed; Linux fully green, Windows shell-glob typecheck failure. https://github.com/andongmin94/chat-view/actions/runs/35451913655

Native src/, tests/, CMake, packaging/installers, capture policy and Windows workflow are unchanged throughout P1-01a/b and this recovery. Recheck the final commit's native run separately. Historical #271 capture intermittency was not diagnosed by subsequent successes; do not weaken tests or reopen unlimited preflight work.

No real CHZZK credentials/events or two-PC hardware are available. Genuine Socket.IO client/server/transport libraries run against a simulated upstream, and renderer logic is DOM-tested. This does not prove NAVER endpoint/TLS approval, a full real-browser/native-HUD flow, clean two-PC video or billable ad exposure. Local package downloads are unavailable; remote CI evidence must not be represented as a local Windows test.

## Next product acceptance tests

After the current matrix passes, the bounded dependency/code-quality recovery is finished. Record its exact run; proceed with product integration rather than more generic diagnostics.

1. Real authorized CHZZK app/channel: login -> real connected/subscribed -> Unicode message in own UI -> stop/reconnect/revoke. Credentials only in private environment configuration.
2. P2: scoped account/device authorization and reuse this session/renderer in the existing native HUD. Do not package the developer secret or widen arbitrary external-page privileges. Replace old provider/DOM paths only with a working end-to-end successor.
3. P1-02/P3: game-PC companion without OBS and a separately qualified HUD-free feed. Record hardware/OS/GPU/capture wiring/game mode/HDR/refresh/latency/resources and simultaneous local readability/recorded exclusion. Pairing alone is insufficient.
4. P3/P4: one selected public ad -> evidence -> server HP -> reward record, then a bounded paid pilot after metric rights, rates/budgets/idempotency/fraud and payout rules. Chat counts or synthetic events never become payable exposure.

Remaining owner inputs are real developer authorization, representative dual-PC hardware, HP/rate/payout rules and hosting assets. The five goals, CHZZK-first and no-gaming-PC-OBS are already confirmed. Missing external credentials do not permit claiming live verification or dropping the platform/ad goals.
