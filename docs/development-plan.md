# Development plan and session handoff

Updated: 2026-09-19 (UTC). Read AGENTS.md and PRODUCT.md first; re-fetch OBS refs and checks before writing.

## Non-negotiable direction

Develop on OBS only; preserve main as original UX reference. CHZZK first. The gaming PC must not require OBS, portable/hidden OBS or a projector. Preserve all five goals: single-screen private HUD, OBS integration and stream exclusion, dual-PC, own platform, creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; messages/heartbeats are not viewer exposure; an estimate is not a payout.

## Current result: P1-01b repository verification passed; live acceptance remains open

Tested implementation commit: **3d801f20a5480701300ec2d3aba9a8820794a288**. CHZZK contract **#7 / 35452154580** completed successfully in all four jobs: Windows/Linux x Node 22/24. Each job passed a locked clean install, full dependency audit, 82 tests, strict TypeScript checking and manifest-unchanged verification. The inspected Linux/Node24 log reports zero audit findings and 82/82 passing tests, including actual Socket.IO/WebSocket delivery.

https://github.com/andongmin94/chat-view/actions/runs/35452154580

The bounded P1-01b dependency/type-check gate is **CLOSED**. This handoff update changes documentation only. Do not rerun the entire parser selection investigation or reapply old downloadable patches. Native Windows **#281 / 35452154579** for the tested implementation was still in progress at this record; inspect its final result before treating that package as validated. A later documentation-only commit's own CI is separate from these exact run results.

https://github.com/andongmin94/chat-view/actions/runs/35452154579

## Implemented product path

The interrupted session had committed 0ac467b on top of 6d65177; this session recovered it rather than reconstructing it. Reusable API/session/socket/parser and developer preview code connect authorization -> API-issued ticket -> SYSTEM connected/sessionKey -> POST subscription + matching acknowledgement -> CHAT -> authenticated SSE -> ChatView-owned text UI.

Duplicate starts do not duplicate subscriptions; reconnect obtains a new ticket. Stop/disconnect/unsubscribe/CHAT revocation/token expiry stop delivery and clear history. Refresh stops the old chat before rotation; global revoke stays explicit. The renderer has no provider secrets, native IPC or remote media. Bounded history/preview clients are not a pre-decoding WebSocket frame limit or ad measurement. The native external-page HUD remains unchanged and is not replaced yet.

This is a developer-only loopback preview, not a deployed multi-user platform. Real CHZZK credentials/events and dual-PC hardware were not available. Genuine Socket.IO client/server libraries run against a simulated upstream; renderer logic is DOM-tested. Neither proves NAVER endpoint/TLS approval, a full real-browser/native-HUD flow, clean two-PC video or billable exposure. Do not ship the developer probe/client secret to streamers or allow its loopback URL through the native external-page settings.

## Dependency recovery and reproducibility

Starting remote was 0ac467b1ed47ff06a30249adaa50c35d3c045250. Native Windows #276 passed; contract #2 failed audit before installed-library tests. The attempted parseuri 2.0.0 did not exist (#3/#4); registry metadata established published 3.0.2 and its different ES-module API. The temporary diagnostic workflow step was removed. See [dependency-validation.md](dependency-validation.md) for historical failures and primary sources; its pending-at-authoring notes are superseded by the #7 result above.

12470eb uses Node URL and the public Manager/Engine.IO object-options constructors, never their old URI constructors. Parser 3.3.6 and parseuri 3.0.2 replace the vulnerable versions. The new parseuri API is not adapted or invoked by this connection path; no compatibility shim, vendor source patch or custom wire framing is added. The actual wire test preserves an encoded ticket through EIO3/WebSocket.

9c636e0 commits the real 54-package resolver graph with original published integrity values, uses npm ci, audits development dependencies too and checks manifest stability. A captured-address test type error was fixed by retaining its validated numeric port. Windows npm's literal-glob typecheck failure was then fixed in 3d801f2 using a standard tsconfig with the same strict settings and file scope. No diagnostic, security gate or test was disabled. The final clean-install matrix validates the locked graph, not merely top-level version claims.

## Evidence history and native preservation

- a7ea2e7 / Windows #274: 23 tests plus five repeats, package/official OBS qualification passed. Q1-Q3 quality gate closed; never reapply its old patch.
- 6d65177 / Windows #275 and CHZZK #1: passed. https://github.com/andongmin94/chat-view/actions/runs/35447087516
- 0ac467b / Windows #276: passed. https://github.com/andongmin94/chat-view/actions/runs/35449471047
- 0ac467b / CHZZK #2: dependency audit failed. https://github.com/andongmin94/chat-view/actions/runs/35449471052
- 12470eb / CHZZK #5: audit and 82 tests passed; fixture type check failed. https://github.com/andongmin94/chat-view/actions/runs/35451316782
- 9c636e0 / CHZZK #6: all four installs/audits/82 tests passed; Windows typecheck command failed. https://github.com/andongmin94/chat-view/actions/runs/35451913655
- 3d801f2 / CHZZK #7: **all four jobs and all steps passed**, as recorded above.

Comparison from 6d65177 through 3d801f2 confirms no changes to native src/, tests/, CMake, installers/packaging, capture policy or Windows workflow. Native source remains a7ea2e7. Historical #271 capture intermittency was not diagnosed by subsequent success; do not weaken checks or reopen unlimited preflight work. Local package downloads were unavailable; remote CI must not be described as local Windows execution.

## Next product acceptance tests

The next work is product integration, not another generic diagnostics or dependency project. Keep real-provider acceptance visible while progressing independent work that does not require secrets.

1. Real authorized CHZZK app/channel: login -> real connected/subscribed -> Unicode message in own UI -> stop/reconnect/revoke. Credentials stay in private environment configuration. Without authorization, record this gate as pending, never simulate a pass.
2. P2: scoped account/device authorization and reuse this session/renderer in the existing native HUD. Do not package the developer secret or widen arbitrary external-page privileges. Replace old provider/DOM paths only with a working end-to-end successor. Keep OAuth, chat delivery and native window/lifecycle ownership separate.
3. P1-02/P3: game-PC companion without OBS and a separately qualified HUD-free feed. Record hardware/OS/GPU/capture wiring/game mode/HDR/refresh/latency/resources and simultaneous local readability/recorded exclusion. Pairing alone is insufficient.
4. P3/P4: one selected public ad -> evidence -> server HP -> reward record, followed by a bounded paid pilot after metric rights, rates/budgets/idempotency/fraud and payout rules. Chat counts or synthetic events never become payable exposure.

Remaining owner inputs: real developer authorization, representative dual-PC hardware, HP/rate/payout rules and hosting assets. The five goals, CHZZK-first and no-gaming-PC-OBS are confirmed, not questions for the next session.
