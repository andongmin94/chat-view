# Q1-Q3 lifecycle fixes — 2026-09-12

Integrated as a focused follow-up to `OBS @ ba7176a7ddbcf897bfa168fc866f9af924a144bc`.
This is the previously prepared lifecycle patch, not platform implementation or a rewrite.
The five goals, CHZZK-first selection, no-OBS-on-gaming-PC requirement and single OBS branch remain unchanged.

## Changes

- Q1: record the successful HUD-ready timestamp once per launch. Every running-process loop computes the remaining stable interval from monotonic age. At the boundary, verify that the process is still running before clearing history. Publish events cannot restart the interval. Once cleared, wait for real events rather than repeatedly waking on a stability timer. Existing backoff, failure persistence, explicit restart and circuit limit are unchanged.
- Q2: introduce a separate, idempotent open-interaction request with an explicit accepted/refused result. Control Center no longer reads style bits to infer a toggle or directly shows/focuses the remote HUD. The HUD owns the change, checks its current local capture/system/resume/terminal state, and uses its existing affinity-before-show/after-show path. Failed placement/mode changes cannot be followed by showing a fatal HUD. Shutdown is latched and terminal state cannot be restored through the visibility policy. A request timeout is reported as unconfirmed, not as success or rollback.
- Q3: remove the allocating Windows message formatter from the controller error path. Keep the operation and numeric Win32 error in a noexcept logger; contain exceptions from an OBS logging callback and use a literal debug message if that logger fails. This removes allocations in ChatView's helper, not a claim that OBS or Windows internally never allocates. It does not certify every other noexcept function in the codebase.

The new open command does not replace the legitimate toggle hotkey/Move-Resize action. There is no old-implementation fallback. Private-HUD protection is retained; network/remote capture notifications still have propagation latency. Receiver-local checks do not prove atomic exclusion across every OBS transition or physical capture path.

## Verification before integration

The existing Windows build #273 for **the baseline** completed successfully, including official OBS installation/runtime qualification and package upload:
https://github.com/andongmin94/chat-view/actions/runs/34680162067

That run does not validate this change. Inspect Windows build attached to the integration commit before treating the new native package as validated.

The prepared regression harness was rerun before integration. It uses edited source bodies in Linux with mocked Win32/OBS/WebView calls, C++20, warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer:

- Original running-wait block fails under repeated publish events; patched block passes six timing scenarios (100 ms events, 1 ms events, quiet execution, short runtime, exit at the boundary and a fresh runtime timestamp).
- Original sender forces a hidden HUD visible even after receiver refusal in a stale-snapshot scenario; patched sender/receiver pass seventeen request/state scenarios, including repeated open, refusal, timeout, pause/resume, terminal state and failures during mode/placement/capture handling.
- Original allocating formatter terminates a noexcept test caller under allocation denial; patched logger passes allocation denial and an exception-throwing log sink. The intentional baseline termination test is not sanitizer-backed.
- The actual new runtime-stability test passes with the verified unchanged telemetry header. It is registered in the existing CMake/CTest setup.
- Real-HUD smoke assertions cover accepted/repeated open and refusal under capture suppression, suspend, pending resume and session lock. These execute in the existing Windows test workflow, not in the portable harness.

The portable harness is not a Windows execution test, a real race reproduction, live-provider verification or capture-card qualification. The initial patch was checked on exact source-context fixtures. During integration, full modified files were assembled from the pinned repository contents; compare the resulting GitHub diff with the prepared patch before advancing OBS. No tests or source files outside the intended patch should be removed. No one-shot patch application workflow or new dependency is used.

## Handoff

The earlier authoring session produced a local patch without remote write capability. This follow-up publishes that patch through normal Git blob/tree/commit operations on OBS, preserving its parent history. Do not ask the owner to apply the local patch again after checking that this change is in the current branch. Verify the actual integration commit and its Windows CI; a successful ref update is not a passing build.

Q4 (Control Center cohesion) and Q5 (capture-qualification cancellation/unload ordering and real-use coverage) remain outside this patch. Resolve further failures only when they block the next concrete product acceptance test. Do not reopen general telemetry/preflight expansion in place of first-party CHZZK and OBS-free gaming-PC feasibility work.
