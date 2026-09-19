# Development workflow

Owner-approved: 2026-09-20 (Asia/Seoul). Product scope: [PRODUCT.md](../PRODUCT.md). This is the single authority for development cadence and supersedes old instructions that make every full-CI failure a global development stop.

## Build useful flows, not a sequence of reports

Work on OBS; leave main as the original UX reference. Choose one visible flow, implement it on the existing working layers, run relevant checks and commit code/tests/a short handoff together. Do not spend a session recreating closed fixes or merely adding another diagnostics document. No force pushes or protection bypasses.

## Three verification levels

| Level | When | Required result |
| --- | --- | --- |
| Change checks | During implementation | Relevant compiler/type checks, focused regression tests and diff review. Do not wait for unrelated full CI. |
| Flow checks | A connected user flow changes | Exercise the actual affected path, including stop/revoke where applicable. A fixture is explicitly not live CHZZK or real hardware. |
| Distribution qualification | Preparing a package for users/testers | Full native suite, five additional repeats, resource limits, official OBS/capture/install checks, manifest/dependency checks, and package upload only after success. |

The Windows workflow automatically detects native-impacting changes. Pure Markdown and unrelated server changes get a small scope job, not the Windows build. Native changes compile and run the non-qualification CTests once, including actual display integration and companion launch. The resource-soak test retains its implementation/limits and is labelled qualification; the full suite runs it unfiltered. The separate CHZZK workflow keeps its own contract/type/dependency checks.

Full Windows qualification runs for an explicit `obs-verify/<candidate>` tag on OBS history, or workflow dispatch with `qualify=true` where GitHub exposes that workflow. main is unrelated and is not changed to expose a dispatch button. A validation tag is not a GitHub Release or authorization for public distribution. Do not create validation tags for every edit. Routine checks never upload the distribution package.

## Defects block their affected claim, not all progress

A first-message/render failure blocks calling that chat flow reliable; it does not prevent independently tested account/device work. A resource failure remains a distribution blocker, not a reason to indefinitely postpone the platform. Failures involving private-HUD exposure, secrets, broadcast crashes or incorrect financial records take priority in the affected path. Do not call a known-broken flow complete.

Never increase limits to obtain green checks, disable assertions, count a skip as a pass, retry unchanged work until a failure disappears, or use a different commit's successful run as proof. Report intermittent failures even after a routine pass. Full qualification and actual provider/hardware acceptance remain mandatory before their corresponding release/support claims.

## Session handoff

Read the current ref and relevant CI at entry. Poll only when the result changes the next decision; no idle polling loop. End with implemented behavior, code commit, checks actually executed, unresolved limitations and the next user flow in [development-plan.md](development-plan.md). Do not write a standalone audit/handoff file per session. Documentation-only updates must not cancel the code run. A CI job can be running at handoff; that is not a passed check or a promise of background work.
