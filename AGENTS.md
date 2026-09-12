# ChatView development guide

## Start every session here

Read [PRODUCT.md](PRODUCT.md), [docs/architecture.md](docs/architecture.md), [docs/development-plan.md](docs/development-plan.md), then [README.md](README.md). Fetch current refs and relevant CI before writing. A dated handoff is evidence, not proof that refs or tests are unchanged.

## Owner-confirmed constraints (2026-09-12)

- Preserve all five product goals in PRODUCT.md. Only an explicit owner decision changes them.
- CHZZK is the first first-party integration. Do not ask which platform to start with again.
- In dual-PC operation, OBS is allowed on the streaming PC only. The gaming PC must not need OBS installed or running, including portable/headless OBS or an OBS projector. Do not hide that dependency inside the companion or replace it with another full broadcasting application requirement.
- The gaming PC may run ChatView's native HUD companion. A clean video path with that companion but without gaming-PC OBS must be demonstrated separately from pairing; it is not implemented yet.
- OBS is the sole active integration/development branch. Do not start another long-lived preflight, telemetry or probe branch. Use a short-lived PR branch only if a real protection/review requirement needs it, then integrate it promptly. Never bypass protection or force-push.
- main remains the original Electron product and UX reference, not a native-code merge target. Preserve its single-screen use case without retaining obsolete Electron runtime/IPC compatibility.

## Preserve the implemented product

The existing native HUD, OBS controller, placement/input, recovery, telemetry, diagnostics, packaging and useful tests are assets to extend, not a failed rewrite to discard. The branch consolidation preserves the latest work-branch source/test/build trees. Keep current safeguards until a verified replacement works. Fix assumptions and user-facing claims rather than throwing away their whole modules.

Private chat HUD, public advertising output and server-side accounting are separate responsibilities. Hiding the HUD is protective degradation, not successful support of a workflow that needs the HUD visible. Pairing is not clean video delivery. A heartbeat is not proof of viewer exposure; an estimate is not a paid reward.

Do not spend successive milestones exclusively on diagnostics/retries/preflight/CI tuning while dual-PC, first-party chat and advertising remain indefinitely deferred. Fix failures that block explicit product acceptance tests, and retain honest failure reporting.

## Implementation rules

Keep a working end-to-end product while adding each layer. Remove superseded paths when their replacements work; do not retain obsolete interfaces through compatibility layers, fallback architectures or migrations. Check existing dependencies and their documentation/types before adding alternatives. No speculative microservices, empty modules, generic frameworks or one-shot workflows that rewrite product source.

Keep OBS callbacks bounded; browser/cloud/auth/accounting work must not block them. Never commit secrets, provider credentials, private chat or payout information. Use scoped revocable authorization and synthetic fixtures explicitly isolated from real ad evidence and rewards.

## Integration and handoff

Consolidation of known development work is not approval for public release. Report source preservation, tests actually run and known CI failures separately. Before deleting any old work branch, verify its current head is an ancestor of OBS; keep main and OBS. Never delete a branch just because its name looks temporary.

End each session by updating docs/development-plan.md with changed behavior, starting/resulting commits, actual test evidence, failures and the next acceptance test. Architecture changes must explain their effect on G1-G5. Keep confirmed requirements, proposals and implementation facts distinct.
