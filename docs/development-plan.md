# Development plan and session handoff

Updated: 2026-09-12. Product authority: [PRODUCT.md](../PRODUCT.md).

## Active work and owner decisions

**OBS is the sole active development/integration branch.** main remains the original Electron product/UX reference and is not merged into the native tree. Do not keep using old work/probe branches as separate development lines.

The owner confirmed CHZZK first and prohibited OBS installation/execution on the gaming PC in a dual-PC setup. The previously proposed gaming-PC OBS/projector video route is withdrawn. These are requirements, not questions for another session.

## Consolidation record

This integration uses the latest work tree at cfe9421084190b6fa09495d6b546ad7ccdd47e7f and the existing OBS head f9ed23ea36744571fd2d495abb28c82202d20b7b as the two merge histories. Keep both histories rather than force-replacing OBS or squashing away ancestry.

Verified before integration:

| Branch/head | Relation and disposition |
| --- | --- |
| obs-preflight-work / cfe9421 | Latest implementation plus product documents; primary tree to preserve |
| obs-runtime-telemetry-work / 251f278 | Fully ancestral to latest work; its product changes are already included |
| telemetry-staging-probe / 63610b2 | Common ancestor of OBS/latest work; no separate work to recover |
| __schema_probe__ / 65b9392 | Ancestor of latest work; no independent changes to recover |
| OBS / f9ed23e | Three unique commits add only obsolete apply-runtime-history-telemetry workflow/script and six payload fragments; retain history, exclude that tooling |
| main / 496d474 | Original product preserved unchanged, outside native consolidation |

The consolidation tree changes product/architecture/handoff documents but preserves the latest work branch's entire src, tests, scripts, data, maintained CI and CMake content. The old OBS-only patch workflow/script/payloads do not return. This is integration of existing development, not a runtime rewrite or a release claim.

Branch-name cleanup is a separate operation from code/history integration. Delete a work/probe ref only after rechecking that its current head is an ancestor of OBS and that it has not advanced. Do not claim deletion from ancestry alone. Fetch remote refs to see whether cleanup was actually performed. No new branch is needed for this consolidation.

## Evidence and known failures

Native source baseline remains c82fce0e06baafd6cef6d8a1a5db9752e422ce23 (cfe9421 changed documents only). Prior handoff inspected Windows build #271 at c82fce0: 22/22 CTest tests and additional five-repeat tests passed, but official OBS capture qualification failed with `Display Capture did not produce a frame before timeout`, followed by failure to shut down OBS. This is historical evidence, not a fresh Windows test of this integration.

Run reference: https://github.com/andongmin94/chat-view/actions/runs/34626547210

Before release, inspect the new OBS commit's workflow. A successful ref update or identical source tree is not a successful CI run. The consolidation does not fix the known capture failure. Do not weaken checks, disable protection, fabricate success, or label an in-progress run as passed.

Implemented assets: native plugin and private HUD, rendering/input/placement, native configuration, local transport, recovery and telemetry, redacted diagnostics, installers/package checks, native/script/OBS tests. Not implemented: platform service/accounts, own CHZZK ingestion/UI, OBS-independent gaming-PC session, demonstrated OBS-free dual-PC clean feed, campaign/public-ad/HP/reward system. Missing future modules do not make the existing modules unusable.

## Milestones and acceptance

### P0. Product direction and branch consolidation

Keep five goals and confirmed constraints in PRODUCT.md; distinguish implemented behavior from proposed architecture; integrate existing work into OBS with ancestry preserved. Confirm source/test/build preservation by tree/diff comparison. Retire old development lines without deleting unmerged work.

### P1. Resolve product-defining feasibility while retaining the native product

P1-01: implement the smallest official CHZZK authorization/subscription path and verify one real authorized channel. Check supported protocol/client dependencies, limits, audience-data access and permitted use for the HP proposal. Fixture tests can proceed without credentials, but must not be represented as live verification.

P1-02: qualify a single-PC capture path and investigate a dual-PC clean-feed route with OBS absent from the gaming PC. Pairing and private-HUD rendering alone do not pass this test. Do not return to the rejected game-PC OBS proposal or silently require another broadcasting suite. Record actual hardware, OS, capture/HDMI path, game/window mode, latency/resource usage, local visibility and recorded exclusion.

Fix the known capture CI failure and read-vs-posting login/readiness errors as targeted work that enables these tests. Do not replace the entire diagnostics/recovery subsystems or spend successive milestones extending them alone.

### P2. First-party CHZZK chat on the existing HUD

One authorized channel -> provider event -> ChatView service -> ChatView-owned renderer -> current native transparent HUD. Add the minimum real server/UI, not empty platform scaffolding. Test reconnect/revocation, bounded buffers, attribution and credential privacy. Keep the existing product working until replacement is complete and remove superseded paths with their replacements.

The demo includes actual chat on a game/work screen and a recording showing private HUD exclusion for the selected single-PC configuration.

### P3. OBS-independent gaming companion and one ad loop

Extend the existing native runtime with a gaming-PC role that has no local OBS dependency. Pair it with the streaming PC through the authenticated session; test revocation, duplicate devices, expiration and reconnects. Prove clean video on the selected OBS-free gaming-PC hardware path before claiming dual-PC completion.

Connect one approved campaign: selection -> separate public OBS banner -> evidence interval -> server progress -> HP/reward record. Campaign registration can be an operator action before a full advertiser portal. Label demo/non-payable data and keep it out of real accounting. Public ad placement and private chat remain separate.

### P4. Bounded paid pilot and expansion

Before real money, agree HP/unit/rate/budget and payout rules; validate provider permissions, evidence quality, fraud review, idempotency and budget caps. Cover source hiding/cropping/occlusion, preview-only state, stale evidence, concurrent creators and duplicate delivery. Separate estimates, confirmed rewards and actual payments. Expand providers only from a working CHZZK path.

## Remaining owner input, not repeated questions

| Item | State |
| --- | --- |
| First provider | Decided: CHZZK |
| Gaming-PC OBS | Decided: prohibited, including OBS-based video/projector workarounds |
| Representative OBS-free two-PC hardware | Capture card/ports/display routing, game/window mode and measured resource budget remain to select and test |
| HP/reward specifics | Global/per-creator HP, unit/rate, display audience, earning and payout conditions remain unspecified |
| Existing hosting/backend assets | No separate platform repo/deployment confirmed; modular monolith remains a proposal |

Provider application registration and channel authorization will be needed for real integration; secrets belong in secure configuration, never chat or commits. Missing external evidence must be labeled without halting unrelated work or redefining the product.

## Next session

Read AGENTS.md and current OBS refs/CI. Work on CHZZK's smallest real path and the OBS-free gaming-PC companion/clean-feed feasibility. Preserve the verified native assets; do not restart in Electron or return to endless telemetry/preflight work. End with exact changed/tested commits, honest failures and the next user-visible acceptance test.
