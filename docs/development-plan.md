# Development plan and session handoff

Updated: 2026-09-12. Read [PRODUCT.md](../PRODUCT.md) before using this plan.

## Current evidence, not target capabilities

Before this documentation-only change, the active implementation was `obs-preflight-work` at `c82fce0e06baafd6cef6d8a1a5db9752e422ce23`. This source baseline was rechecked for this handoff. No runtime code, package dependencies, CI workflows, or financial behavior are changed by this documentation update.

Branch snapshot: `main` = `496d474` (original product reference); `OBS` = `f9ed23e` (integration branch); `obs-runtime-telemetry-work` = `251f278`; `obs-preflight-work` = `c82fce0`; `telemetry-staging-probe` = `63610b2`; `__schema_probe__` = `65b9392`. Re-read refs before any next change. Do not merge all branches blindly. The preflight branch already contains the telemetry work; `OBS` has separate one-shot patch tooling not to resurrect.

Existing implementation: Windows OBS plugin, external private web-page HUD, configuration, diagnostics, status/recovery policies and native tests. Not yet implemented: platform service/accounts, authenticated dual-PC use, first-party chat ingestion/rendering, public advertising placements, HP accounting or real payouts.

Prior handoff inspected Windows build run #271 at this source SHA: CTest 22/22 and five-repeat runs passed, but the official OBS capture qualification failed with `Display Capture did not produce a frame before timeout`, followed by failure to shut down OBS. That is historical CI evidence, not a test executed by this documentation change. Do not claim release readiness. Re-fetch the run and logs before diagnosing it:
https://github.com/andongmin94/chat-view/actions/runs/34626547210

## Milestones with visible completion

### P0. Preserve the product direction

Record the five user-confirmed goals, distinguish architecture proposals from current implementation, and establish the session-entry/handoff documents. This documentation change performs P0. It is not completion of any runtime feature.

### P1. Resolve the two feasibility questions that determine the product

P1-01: inspect official CHZZK authorization, real chat subscription, audience data access, session limits/protocol support, and policy constraints for the proposed HP model. Produce an executable smallest-path adapter test and record what a real authorized account can actually receive. Do not substitute DOM presence or fake messages for live API verification. Without account access, fixture tests can run, but real verification remains explicitly blocked.

P1-02: choose a representative single-PC and dual-PC clean-video topology. Prove simultaneously that the native HUD remains visible on the gaming screen and absent from an OBS recording. Document hardware/OS, window mode, capture source, output path, start/scene-switch behavior and evidence. Device pairing alone does not pass P1-02. Do not require zero possible bugs in the free HUD before investigating these product-defining questions.

Repair the known capture CI failure and confirmed read-vs-login/readiness misclassifications insofar as they block these acceptance tests. Do not remove assertions or expand a generic diagnostics subsystem instead of completing the tests.

### P2. One first-party platform path that works

Introduce the minimum real server and UI, using the architecture's proposed stack unless a documented dependency review changes it. One authorized channel -> provider event -> ChatView service -> ChatView-owned renderer -> existing native transparent HUD. Preserve names/badges/deletion behavior that the provider supports. Test disconnect/reconnect, revocation, bounded buffers, attribution and no credentials in diagnostics. No empty multi-provider framework or unused infrastructure.

The completion demo includes a game/working screen, actual chat and a recording proving personal HUD exclusion for the chosen single-PC setup. Keep the existing product usable until the replacement is functional; remove replaced obsolete paths together, not via a permanent fallback.

### P3. The same session on two PCs plus one advertising loop

Extend the same native runtime and authenticated service to game-PC and streaming-PC roles. Test pairing approval/revocation, stale state, duplicate devices and reconnects without duplicate audience or credit. Re-run clean-feed qualification on the selected actual dual-PC setup.

Connect one approved campaign end to end: creator selection -> separate public OBS banner -> evidence interval -> server-calculated progress -> HP and reward record in the dashboard. Campaign registration may be an operator action rather than a fully built advertiser portal. Use clearly identified synthetic or non-payable data for early demos; these do not prove paid exposure. Chat remains private while the banner is public. Do not prematurely decide that HP must appear in the banner, that HP belongs to one specific entity, or that a zero HP threshold is required for payment.

### P4. Bounded paid pilot, then expansion

A paid pilot requires agreed HP/rate/budget rules, provider permissions, validated audience sampling, exposure/fraud review, idempotent accounting, campaign budget caps, and payout/disclosure/privacy checks. Tests must cover expired evidence, scene/source hiding/cropping, preview-only activity, concurrent creators and repeated deliveries. Distinguish estimates, accepted rewards and actual payments.

Expand additional platforms and scale only from the working path. Each platform needs its own capability/permission review; neither a universal viewer-time API nor universal capture safety is assumed.

## Decisions the owner can supply

These are not reasons to repeat the five product goals or stop all development.

| Question | Proposed working assumption | When a decision/evidence is required |
| --- | --- | --- |
| First platform/channel | CHZZK first; additional providers remain in product scope | Before real provider integration; application registration and channel authorization needed |
| Representative dual-PC wiring | Investigate a clean game-only feed, not assume mirrored HDMI is private | Before claiming two-PC support; capture card/output/OS/window-mode details needed |
| HP and reward semantics | Rule-based server records; no money promised by prototype | Before billable implementation: global/per-creator HP, unit, rate, completion and payout rules, HP display audience |
| Existing backend/deployment assets | No existing platform server confirmed; modular monolith proposed | Before deployment: any existing repo/domain/hosting/accounts and budget; secrets through secure configuration only |

Implementation may use fixtures and draft interfaces while related external facts are unavailable, but must label those limitations. Do not invent commercial terms, silently replace first-party goals with URL viewing, or treat every unanswered detail as permission for speculative infrastructure.

## Definition of done for each implementation change

State the goal G1–G5, starting source SHA, changed behavior and user-visible result. Record tests actually run, CI run/commit, failures and real-workstation evidence separately. A source/API schema check is not a device test. A heartbeat is not verified ad attention. Keep previous functionality working or replace it end to end in the same change. Update this file before ending a development session.

## Next session entry

Re-fetch branches and CI. Start P1-01 with official provider feasibility and a smallest executable path, while preparing P1-02's representative capture matrix. Do not return to open-ended telemetry/preflight work. The next implementation is not a new advertising microservice or a wholesale rewrite of the existing native runtime.
