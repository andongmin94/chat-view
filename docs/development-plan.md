# Current development state and next flow

Updated: 2026-09-20 (Asia/Seoul). Read [AGENTS.md](../AGENTS.md), [PRODUCT.md](../PRODUCT.md), [development-workflow.md](development-workflow.md) and [architecture.md](architecture.md). This is the only current work queue; technical contracts and past run reports do not override it.

## Latest implementation

**f0543603b4f6b8cb5154d45b044679ca55238e25**, on OBS, applies the previously local cadence/companion patch. Same native executable, explicit `--companion`, no local OBS parent/mapping, development consent, existing connection panel, single-instance guard and quit. Existing OBS-controlled behavior and privacy safeguards remain. Test registration is in existing cmake/native-chat.cmake, not a second application or rewritten root build.

Routine Windows CI now scopes native changes, runs relevant native tests once and does not package. Full repeated/resource/official-OBS/install/package qualification remains explicit. No limit or old assertion was weakened. New launch parser test passed 40 assertions locally with C++20/Werror/ASan/UBSan. Windows development **#295 / 35467657607 at this exact SHA completed successfully**: native compilation/linking and **26/26 routine CTests passed**, including actual companion executable launch/consent/connection-panel/no-OBS-library/duplicate/exit checks, native launch-option parsing, the existing native chat surface and HTTP/WebSocket-to-HUD delivery. The resource test was excluded by its qualification label, and full repeats, official OBS/install/package checks and upload were intentionally not run. This routine pass does not diagnose the historical intermittent first-render/resource failures. No new full qualification, live CHZZK or physical two-PC result is claimed.

Run: https://github.com/andongmin94/chat-view/actions/runs/35467657607

## Product progress, not a percentage

| Goal | Existing implementation | Still required |
| --- | --- | --- |
| G1: one-screen private chat | Native transparent HUD, own text renderer, click-through/edit/placement | Reliable actual-channel use and target-game UX |
| G2: OBS integration and private exclusion | Controller/HUD separation, bounded recovery, capture interlock | Readable local HUD and excluded output together on declared supported paths; hiding it is not completion |
| G3: two PCs, no gaming-PC OBS | Explicit independent developer launch on the same native implementation | Creator/device session with streaming side; real OBS-free clean-video mechanism/topology and qualification |
| G4: CHZZK-first own platform | Auth/API/session, own renderer, scoped gateway, WinHTTP consumer and native controls | Revocable enrollment, secure persistence/renewal, usable multi-user service and actual authorized channel acceptance |
| G5: chosen ads, audience-time HP/rewards | Product boundaries only | Public ad selection/output, permitted evidence, server HP/reward records; paid pilot rules later |

## Next user-visible flow

**One creator approval -> registered display device -> automatically renewed chat-read access -> reconnect -> revoke that device**, usable by the same OBS-controlled HUD and companion. Extend the existing display protocol/service/WinHTTP/renderer; do not recreate them, expose provider secrets, or promote the developer loopback tool directly into a public backend. Registration, persistence, native secure storage and expiry/revocation must work together before claiming durable device access. Do not add empty modules or spend another milestone writing a generic authentication framework.

Address the known first-render defect within the affected connection flow before calling it reliable. Independent service/account implementation need not wait for resource-soak or hardware access. The separate no-OBS gaming-PC clean-video feasibility remains an early engineering task, not something pairing magically solves. When private CHZZK app/channel authorization is available, exercise real consent/subscription/message/stop/reconnect/revoke; never substitute synthetic data for that evidence.

After the usable creator/device flow, connect one approved campaign end to end: creator selection -> separate public OBS banner -> eligible evidence -> server HP -> non-payable reward record. Operator-entered campaigns are enough initially. Do not wait for a full advertiser portal; do not invent real payout terms. Paid operation requires agreed units/rates/budgets, permitted metrics, idempotency, fraud/disclosure/privacy/payout handling.

## Known defects and scope

| Item | Evidence/status | What it blocks |
| --- | --- | --- |
| Intermittent native first render | 82f36a6 / #294 repeat 3: gateway frame not observed; source of failure open | Reliable-display acceptance and affected distribution; not independent account work |
| Resource growth variability | #292 late +104 > 64; #294 initial +284 > 256 but late +8 <= 64; no diagnosed cause | Full distribution qualification; not all feature implementation |
| Misleading readiness wording | READY TO STREAM evaluates local ChatView conditions | Broad broadcast-readiness claim; correct in next relevant UI work |
| Control Center cohesion and capture-test unload ordering | Remaining Q4/Q5 audit concerns, not demonstrated general corruption | Review when changing those responsibilities; not a blanket rewrite gate |

Detailed unresolved samples are retained once in [resource-growth-investigation.md](resource-growth-investigation.md). Do not rerun unchanged until green, relax budgets, or label historical failures resolved by a new routine pass.

## Evidence worth retaining

- a7ea2e7 / Windows #274: Q1-Q3 timing/visibility-owner/error-logging changes were integrated and passed. Do not reapply old downloadable patches or treat those findings as open.
- 436e8ec / CHZZK #13 / 35462872875: all four Windows/Linux x Node22/24 jobs, 135 tests, audit/types/manifests passed using synthetic providers. Dependencies/platform source unchanged by this handoff.
- f065857 / Windows #293 / 35464221001: earlier unchanged-runtime instrumentation passed full qualification; it does not validate later code.
- 82f36a6 / Windows #294 / 35464979632: initial 25/25 and setup-surface repeats passed; repeated suite 23/25 failed. This remains historical failure evidence.

Tests, synthetic gateways, actual native DOM checks, live-provider access, physical capture and paid accounting are distinct. A later documentation commit is not newly tested native code. Record only results actually observed for the correct SHA.

## Documentation cleanup in this handoff

Reviewed all 13 Markdown files in the starting OBS tree, plus the pending cadence/companion text. Removed obsolete closed-audit/repair narratives; retained Q4/Q5 above, the options-only Socket.IO dependency constraint in platform/README, and merged the host-owned document contract into native-display-client.md. Corrected already-implemented chat/worker/companion items mislabelled as future work. Removed full-CI-first global stops and duplicate task queues. PRODUCT.md's five owner-confirmed goals are unchanged. No extra per-session audit file is needed.

Outstanding owner inputs remain private live-provider authorization, representative two-PC hardware, HP/rate/payout semantics and hosting assets. Do not ask again about CHZZK-first, gaming-PC OBS prohibition or the five goals. Update this page in the same coherent implementation change; keep older detail in Git history.
