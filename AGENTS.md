# ChatView development guide

## Start every development session here

Read in this order before selecting or changing work:

1. [PRODUCT.md](PRODUCT.md): user-confirmed product goals, not a description of today's implementation.
2. [docs/architecture.md](docs/architecture.md): current boundaries and proposed target architecture.
3. [docs/development-plan.md](docs/development-plan.md): evidence, incomplete work, open decisions, and the next product milestone.
4. [README.md](README.md): instructions for the implementation that actually exists.

Fetch current branches, commits, and relevant CI results. The status document is a dated snapshot, not permission to assume that refs or tests are unchanged.

`main` is the original Electron product and the user-experience reference. Do not merge it into the native OBS implementation or dismiss its product goals. As of the 2026-09-12 handoff, active implementation work is on `obs-preflight-work`; `OBS` is the intended integration branch. Verify before writing. Do not force-push, bypass protection, or merge unverified work.

## Product guardrails

Preserve all five goals in PRODUCT.md. Only an explicit user decision changes them. A technical proposal, current limitation, failing CI test, or long session is not a scope change.

Private chat HUD, public advertising output, and server-side accounting are separate responsibilities. Hiding the HUD does not prove that a supported broadcasting workflow works. Pairing two PCs does not prove clean video delivery. A heartbeat does not prove actual viewer exposure. An estimated reward is not a confirmed payout.

Do not spend successive milestones exclusively on diagnostics, retries, preflight states, or CI tuning while dual-PC, first-party chat, and the advertising loop remain indefinitely deferred. Fix failures that block the next explicit product acceptance test; retain safety checks.

## Implementation rules

Preserve a working end-to-end product while adding each next layer. Remove superseded paths in the change that replaces them; do not add backward-compatibility layers, fallback architectures, or migrations for obsolete interfaces. Reuse existing dependencies and inspect their documentation/types before adding alternatives. No speculative microservices, plugin frameworks, empty modules, or one-shot workflows that rewrite source files.

Keep OBS callbacks bounded; do not put cloud requests, platform authentication, or accounting in them. Never put provider secrets, access tokens, private chat messages, or payout credentials in commits, diagnostics, or public documentation. Use synthetic fixtures for tests, clearly separated from real advertising evidence.

## End every development session with a durable handoff

Update docs/development-plan.md with the implemented change, exact tested commit/run, tests actually executed, known failures, next acceptance test, and unresolved user decisions. Mark proposals and unimplemented capabilities explicitly. For architecture changes, update docs/architecture.md and explain their effect on the five product goals in the commit or PR. Documentation and code must not claim different guarantees.
