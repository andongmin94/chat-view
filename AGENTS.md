# ChatView development guide

## Start here

Read [PRODUCT.md](PRODUCT.md), [docs/development-workflow.md](docs/development-workflow.md), [docs/architecture.md](docs/architecture.md), then [docs/development-plan.md](docs/development-plan.md). Read usage and specific contracts only when relevant. Fetch OBS HEAD and relevant checks once before changing code; do not rerun old audits or continuously poll unrelated CI.

## Owner-confirmed constraints

Preserve the five product goals: single-screen private chat; OBS integration with readable local HUD excluded from audience video; dual-PC without OBS on the gaming PC; first-party platform starting with CHZZK; creator-selected public ads with audience-time-related HP and rewards. Only the owner can change these goals. Do not reinterview them.

OBS is the only active development branch. main is the original Electron product/UX reference, not a native merge target. No new long-lived work/probe branches, force pushes, protection bypasses or silent changes to main. Re-read the target ref before updating it.

No installed, portable, hidden or headless OBS/projector on the gaming PC, and no replacement full broadcasting-suite requirement. Reuse the native HUD companion. Independent launch or device pairing is not a clean-video solution. Hiding the HUD is protective degradation, not successful support of a readable private HUD. A render acknowledgement is not viewer exposure, and estimated credit is not a payout.

## Implementation first; proportional verification

The owner approved the cadence in development-workflow.md on 2026-09-20. Implement coherent user flows and run their relevant checks. Full repeated/resource/installer/capture/package qualification belongs to a distribution candidate, not every small edit. Documentation-only changes must not start the heavyweight Windows job or cancel code verification.

Known release failures stay visible and block the affected release claims, not independent implementation. Privacy/credential/broadcast-crash/financial-integrity issues take priority in their affected paths. Do not loosen test budgets, swallow failures or report skipped checks as passed. Do not consume consecutive milestones expanding diagnostics or waiting for unrelated CI while user flows are missing.

Preserve the working native rendering, input, placement, recovery and OBS/HUD fault boundary. Keep cloud/network/auth/accounting out of bounded OBS callbacks. Extend the existing program, not a second desktop framework. Use existing maintained dependencies and inspect documentation/types before adding alternatives. No speculative abstractions, empty scaffolds, compatibility layers or migrations for obsolete interfaces. Remove replaced paths with a working replacement.

Never commit or log provider secrets, display credentials, private messages or payout data. Synthetic fixtures are not real-provider or paid-exposure evidence.

## Durable, short handoff

Update development-plan.md in the same coherent change: changed user behavior, exact source/CI evidence, known failures with affected scope, and one next user flow. Put product authority only in PRODUCT.md, cadence only in development-workflow.md, architecture only in architecture.md, and current status only in development-plan.md. Technical contracts may explain current mechanisms but must not redefine goals, priority or release policy.

Do not create a new Markdown report per session or duplicate CI transcripts in several documents. Completed investigations belong in Git history; retain only still-needed contracts and unresolved evidence. Never claim a remote update when only a local patch exists. Developer builds, routine passes, full qualification and live/hardware acceptance are different states.
