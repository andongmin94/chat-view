# Code-quality audit — 2026-09-12

Baseline: `12ce12bd040368112db70d6c0880c4ec55aa4779` on `OBS`.
This is a targeted source/build/lifecycle review, not a certification of every source line, Windows race condition, provider page, or broadcaster workstation. Authorship/model labels are not evidence of code quality.

## Verdict

Keep the native product. Its process split, explicit contracts, scoped handle ownership, bounded restart policy, input/placement and maintained installer/tests are useful. Do not interpret the amount of Win32/COM lifecycle code as proof of overengineering. The main liabilities found are leftover unused implementation, mixed responsibilities, and policy/lifecycle edge cases, not a reason for a framework rewrite.

The baseline's Windows build #272 completed successfully, including build/tests, package validation, official OBS installation/runtime qualification and artifact upload:
https://github.com/andongmin94/chat-view/actions/runs/34677416613

Earlier #271 failed with unchanged native sources. A subsequent pass does not diagnose or fix the earlier failure; retain the failure history and investigate repeatability without weakening assertions. Baseline CI is not evidence that this cleanup commit's CI passed.

## Removed in this cleanup

`src/diagnostics/report.cpp`, `src/diagnostics/report.hpp`, and `tests/diagnostics-report-test.cpp` formed an obsolete report/sanitizer path (240 source/test lines). None is a source of a CMake target or a registered CTest. The active diagnostics target builds `diagnostics-exporter.cpp` and uses `common/diagnostic-redaction.hpp`; both the active implementation and its registered tests remain unchanged. Do not revive the old formatter as a compatibility layer.

The one-shot patch workflows/payloads had already been removed by consolidation; this change does not count their deletion again. Current remote refs were checked: only `OBS` and `main` remained before this cleanup. No branch deletion is attributed to this audit.

## Reproduced and fixed: read versus posting permission

Baseline `src/hud/page-health-message.cpp`, `collectStatusText` / `evaluate`: the YouTube input renderer's sign-in text was checked before message-list readiness. A visible message list plus a posting-only sign-in prompt produced `CVH2|4|7|5` (LoginRequired), not Ready. Existing tests did not combine these conditions.

The fix ignores notices inside the posting input only while a concrete YouTube message-list element is visibly present. It handles input descendants matched by generic status selectors too. An outer chat shell alone does not qualify for this exception. Reader-level login alerts, connection loss, ended streams, offline state and the existing recovery protections remain in force. No protocol, dependency, timeout or native capture policy changed.

Eleven added regression combinations cover two concrete list selectors, generic status aliasing, real reader-level login/offline/reconnect alerts, and missing/hidden/undersized lists. These are deterministic DOM fixtures, not a claim that a current live YouTube/CHZZK page or full login flow was exercised.

## Open findings, not silently declared fixed

| ID | Evidence | Required treatment |
| --- | --- | --- |
| Q1 — recovery timing | `RuntimeController::supervisor_loop` passes a fresh 30000 ms timeout on each wait and clears failures only on WAIT_TIMEOUT. Repeated state-publish events postpone reset even when the HUD has survived longer than the intended stable interval. | Use elapsed monotonic runtime age, not event quiescence. Add a timed controller regression before extending session lifecycle; retain crash-loop protection. This is source-derived, not a newly reproduced Windows crash. |
| Q2 — visibility ownership | `ControlCenterWindow::open_hud_interaction` in `src/config/main.cpp` checks a snapshot then calls `ShowWindow(hud, SW_SHOW)` from outside the HUD. HUD-local pause/resume/affinity rules are not the single owner of this transition. | Route an explicit interaction request to the HUD and revalidate there. Test suppression/session-state changes between request and execution. Potential race/ownership gap; no actual privacy leak was reproduced here. |
| Q3 — exception boundary | `RuntimeController::update`, `restart_hud`, and early `stop` paths are noexcept but can call `log_windows_error`, whose formatter allocates a `std::wstring` without a local catch. | Make error reporting non-throwing and allocation-independent or catch within the boundary. Test allocation/error paths. This is an exceptional low-memory/error-path risk, not evidence of routine crashes. |
| Q4 — cohesion | `src/config/main.cpp` combines window construction/layout, state presentation, URL persistence, diagnostics and cross-process actions. It also resolves baseline Windows DPI APIs dynamically despite the declared Windows 10 2004 minimum. | Separate presentation from session/actions when touching this area; remove unsupported-old-OS fallbacks rather than adding another generic UI framework. File length alone is not a defect. |
| Q5 — test boundary | Page-health tests use a fake DOM; capture qualification uses probe windows. The CI qualification worker also makes synchronous UI tasks and is joined on unload. | Keep existing assertions. Add cancellation/unload ordering coverage and real-provider/actual-HUD acceptance evidence. Do not label mocks or a subsequent successful run proof of all capture paths. |

Q1–Q3 require focused tests/fixes before relying on the expanded cross-PC/session lifecycle. Do not use this audit as permission to spend indefinite milestones redesigning diagnostics. Separate the necessary cleanup gate from the CHZZK/platform milestones in the development plan.

## Tests actually executed for the change

- Reconstructed the two relevant source/test files from connector reads and verified their Git blob IDs against the baseline before editing: source `7ff34fb919a428dd1fe27d818a660cfd28482520`, script test `5de27619bbaacea9e1d3c21293224aa5bef9dec5`.
- Ran the original embedded-script test suite with Node.js 22.16.0: passed.
- Added the regression and ran it against original production code: failed at the expected read/posting assertion, demonstrating missing coverage.
- Applied the fix and ran the existing script suite plus all eleven new combinations: passed; `node --check` passed.
- Built and ran the repository's actual `tests/page-health-message-test.cpp` with GCC 14.2, C++20, warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer: passed. The unchanged two headers and C++ test were also checked against their baseline blob IDs. This tests the portable parser/embedded source, not Win32/WebView2 execution.
- New production-source blob: `36f1a13285546ad88cd76bd9421b49543349ba37`. New script-test blob: `4d4a1588814162c85106b1c7ea815f22b4c58149`.

Full Windows/OBS qualification of the resulting commit is performed by the existing workflows after the OBS update. Inspect that exact commit's result; do not reuse #272 as the cleanup result. No physical dual-PC, live account, long-duration streamer-PC or paid-ad test was executed in this audit.

## Primary API semantics

- C++ noexcept and terminate: https://learn.microsoft.com/en-us/cpp/cpp/noexcept-cpp
- Windows GetDpiForSystem minimum platform: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforsystem

These references establish language/API behavior, not evidence that an observed CI failure had the causes above.
