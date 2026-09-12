# Development plan and session handoff

Updated: 2026-09-12. Product authority: [PRODUCT.md](../PRODUCT.md).

## Active work and owner decisions

**OBS is the sole active development/integration branch.** main remains the original Electron product/UX reference and is not merged into the native tree. Do not keep using old work/probe branches as separate development lines.

The owner confirmed CHZZK first and prohibited OBS installation/execution on the gaming PC in a dual-PC setup. The previously proposed gaming-PC OBS/projector video route is withdrawn. These are requirements, not questions for another session.

## Code-quality gate requested before feature expansion

Review baseline: `12ce12bd040368112db70d6c0880c4ec55aa4779`. See [code-quality-audit.md](code-quality-audit.md) for evidence, limitations and open findings.

This cleanup removes the unused diagnostics/report implementation and its unregistered test (three files), and fixes the reproduced posting-login-versus-readable-chat classification with eleven regression combinations. The active diagnostics exporter/redaction, registered native tests, IPC, capture safeguards, packaging and dependencies are preserved. No new branch or framework is introduced.

Actual local checks: original script suite passed; new regression failed original code as expected; corrected script suite/regressions and syntax check passed; repository C++ page-health tests passed under GCC 14.2 with warnings-as-errors and ASan/UBSan. Relevant baseline and modified source/test blob identities were verified. This is not a local Windows build or a real provider-page test.

Before extending the cross-PC/session lifecycle, address audit Q1 (stable-time reset based on event silence), Q2 (configuration UI directly showing the HUD) and Q3 (allocating error formatter reachable from noexcept boundaries) with focused regressions. Then resume CHZZK/platform work, rather than open-ended cleanup. Q1–Q3 are recorded source findings, not fixes included in this commit.

## Consolidation record

Merge `12ce12b` preserves the latest work tree at cfe9421084190b6fa09495d6b546ad7ccdd47e7f and existing OBS head f9ed23ea36744571fd2d495abb28c82202d20b7b as two histories. No force replacement or ancestry-erasing squash was used.

| Former branch/head | Disposition |
| --- | --- |
| obs-preflight-work / cfe9421 | Latest implementation plus product documents integrated into OBS |
| obs-runtime-telemetry-work / 251f278 | Ancestor already included in latest work |
| telemetry-staging-probe / 63610b2 | Common ancestor; no separate work lost |
| __schema_probe__ / 65b9392 | Ancestor; no independent work lost |
| OBS / f9ed23e | Obsolete one-shot telemetry patch tooling excluded; history retained |
| main / 496d474 | Original product preserved unchanged |

At the start of this quality audit the remote branch list contained only OBS and main. Branch deletion was not performed by this audit. Always re-read refs before writing.

## Evidence and known failures

Native baseline c82fce0 was preserved through consolidation. Earlier Windows build #271 failed official OBS capture qualification after passing its native tests:
https://github.com/andongmin94/chat-view/actions/runs/34626547210

Windows build #272 at `12ce12b` subsequently **passed all workflow stages**, including native tests, package checks, official OBS installation/runtime qualification and artifact upload. This was re-read during this audit:
https://github.com/andongmin94/chat-view/actions/runs/34677416613

The subsequent pass with unchanged native sources is not a diagnosed fix for #271. Keep the history and investigate repeatability if it recurs; do not weaken assertions or report #271 as the latest run. Likewise #272 is baseline evidence, not the result for this cleanup. Inspect both Windows build and Page health script checks attached to the new commit.

Implemented assets: native plugin and private HUD, rendering/input/placement, native configuration, local transport, recovery/telemetry, active redacted diagnostics, installers/package checks and tests. Not implemented: platform service/accounts, own CHZZK ingestion/UI, OBS-independent gaming-PC session, demonstrated OBS-free dual-PC clean feed, campaign/public-ad/HP/reward system. Missing future modules do not make existing modules unusable.

## Milestones and acceptance

### P0. Product direction and branch consolidation

Five goals and confirmed constraints are in PRODUCT.md; existing development is integrated in OBS with ancestry preserved. Source/test/build preservation was verified at merge. The current cleanup is a separate quality change, not another architecture rewrite.

### P1. Resolve product-defining feasibility while retaining the native product

P1-01: implement the smallest official CHZZK authorization/subscription path and verify one real authorized channel. Check protocol/client dependencies, limits, audience-data access and permitted use for the HP proposal. Fixture tests can proceed without credentials but are not live verification.

P1-02: qualify a single-PC capture path and investigate a dual-PC clean feed with OBS absent from the gaming PC. Pairing and private-HUD rendering alone do not pass. Do not return to the rejected gaming-PC OBS proposal or silently require another broadcasting suite. Record hardware, OS, capture/HDMI path, game/window mode, latency/resources, local visibility and recorded exclusion.

After the bounded code-quality gate above, fix only further blockers to explicit acceptance tests. Do not replace all diagnostics/recovery or spend successive milestones extending them alone. The UI's broad READY TO STREAM claim still needs narrowing when its presentation is changed.

### P2. First-party CHZZK chat on the existing HUD

One authorized channel -> provider event -> ChatView service -> ChatView-owned renderer -> existing native transparent HUD. Add the minimum real server/UI, not empty scaffolding. Test reconnect/revocation, bounded buffers, attribution and credential privacy. Keep the current product working until replacement is complete, then remove superseded paths together.

The demo includes actual chat on a game/work screen and a recording showing private HUD exclusion in the selected single-PC configuration.

### P3. OBS-independent gaming companion and one ad loop

Extend the native runtime with a gaming-PC role without local OBS. Pair with the streaming PC through the authenticated session; test revocation, duplicates, expiration and reconnect. Prove clean video on the selected OBS-free gaming-PC path before claiming dual-PC completion.

Connect one approved campaign: selection -> separate public OBS banner -> evidence interval -> server progress -> HP/reward record. Campaign registration can initially be an operator action. Label demo/non-payable data and exclude it from real accounting. Public ad placement and private chat remain separate.

### P4. Bounded paid pilot and expansion

Before real money, agree HP/unit/rate/budget and payout rules; validate provider permissions, evidence quality, fraud review, idempotency and budget caps. Cover hiding/cropping/occlusion, preview-only state, stale evidence, simultaneous creators and duplicate deliveries. Separate estimates, confirmed rewards and actual payments. Expand providers from a working CHZZK path.

## Remaining owner input, not repeated questions

| Item | State |
| --- | --- |
| First provider | Decided: CHZZK |
| Gaming-PC OBS | Decided: prohibited, including OBS video/projector workarounds |
| Representative OBS-free two-PC hardware | Capture card/ports/display routing, game/window mode and measured resource budget remain to select/test |
| HP/reward specifics | Global/per-creator HP, unit/rate, display audience, earning and payout conditions remain unspecified |
| Hosting/backend assets | No separate platform repo/deployment confirmed; modular monolith remains a proposal |

Provider registration and channel authorization are needed for real integration; secrets belong in secure configuration, never chat or commits. Label missing evidence without redefining the product.

## Next session

Read AGENTS.md, this audit and current OBS refs/CI. Complete focused Q1–Q3 fixes with regression tests before session expansion, then resume CHZZK and OBS-free gaming-PC feasibility. Preserve native assets and avoid another indefinite telemetry/preflight project. End with exact changed/tested commits, honest failures and the next product acceptance test.
