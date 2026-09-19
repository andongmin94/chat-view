# HUD resource and repeatability investigation

Updated: 2026-09-20 (Asia/Seoul). Product authority: PRODUCT.md. This is a bounded investigation of the native package gate, not a new telemetry product or a declaration of leak freedom.

## Starting failure: late aggregate handle growth

Windows #292 / 35462872896 at 436e8ec compiled and passed its initial 25 CTests. Its fifth resource-soak repeat failed late handle growth: first settled 2961 -> final 3065, **104 > 64**. Initial growth was 52 <= 256. Memory-byte, process and GUI bounds passed. Native display and surface tests passed their five repeats. Packaging was skipped.

https://github.com/andongmin94/chat-view/actions/runs/35462872896

Those aggregate samples do not identify a process, handle type or operation. A similar #288 observation predates the network worker; #290 passed. Neither makes this a diagnosed native memory leak.

## Instrumented unchanged runtime

Commit f06585705b46f82af2ef36c04619416dc9b04d9c changes only the soak test and log reporting. Production remains identical to 436e8ec. Samples record PID plus creation time, executable basename, handles and private bytes, not command lines, user paths, chat or credentials. All resource limits, existing actions, settle delays and repetitions are preserved. Sampling was added within the verification window.

Windows #293 / 35464221001 passed initial 25 tests, all five repeats, official OBS checks and package upload. The earlier failure did not reproduce.

https://github.com/andongmin94/chat-view/actions/runs/35464221001

| Repeat | Aggregate baseline | First settled | Final settled | Late delta | Native HUD handles (baseline / first / final) |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 2785 | 2757 | 2771 | +14 | 217 / 217 / 217 |
| 2 | 2780 | 2777 | 2797 | +20 | 217 / 217 / 217 |
| 3 | 2774 | 2770 | 2779 | +9 | 217 / 217 / 217 |
| 4 | 2749 | 2714 | 2706 | -8 | 217 / 217 / 217 |
| 5 | 2772 | 2725 | 2730 | +5 | 217 / 217 / 217 |

The observed variation belongs to WebView2 children in this successful run. This does not retrospectively locate #292's spike or mean instrumentation fixed it.

## Confirmed unnecessary work: corrected and regression-tested

Every unchanged empty-URL settings notification and resume requested a fresh setup document: GUID URL, Stop and Navigate. The soak exercise sends those notifications repeatedly. The static setup page need not be replaced when already selected or loading.

Correction **82f36a60d727c71110cb1a0180c71fb00a48951d** preserves the exact currently selected setup document on show_setup_page. Explicit reload still creates a fresh setup document for recovery. Actual NativeChatSurface opens retain fresh isolated URLs; external navigation invalidates private ownership; close clears the setup identity. No external-page privilege or capture policy changes.

The actual WebView2 regression checks 12 requests before load and 12 after load: one navigation, preserved document canary, source and history length. Explicit recovery must replace the document, and subsequent ordinary requests must retain it. Existing Unicode, inert markup, navigation isolation and teardown assertions remain.

This regression passed initially and in all five additional repeats in Windows #294. Local before/after C++ method-body tests with simulated WebView calls failed before and passed 10 lifecycle assertions after, with warnings-as-errors, ASan and UBSan. The local harness is not a Windows resource measurement.

## Latest full run: still failed, for two precise reasons

**Windows #294 / 35464979632 at 82f36a6 finished with FAILURE.** Compile/link and initial CTest 25/25 passed. The repeat suite finished 23/25. Official OBS qualification, package validation and upload were skipped. Do not present #293's earlier package as a validated build of this correction.

https://github.com/andongmin94/chat-view/actions/runs/35464979632

### Resource repeat 2: initial growth, not late growth

Repeat 1 passed with aggregate handles 2926 -> 2927 -> 2927 and native HUD 352 throughout. Repeat 2 recorded:

| Sample | Processes | Handles | Private bytes | GDI | USER |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 7 | 2733 | 98521088 | 25 | 63 |
| First settled | 7 | 3017 | 110669824 | 25 | 69 |
| Final settled | 7 | 3025 | 111427584 | 25 | 69 |

The failing comparison is **initial handles: 3017 - 2733 = 284 > 256**. Late growth is only **3025 - 3017 = 8 <= 64**. Private-byte growth is 12906496 bytes, below 128 MiB. Process and GUI limits passed. This does not reproduce #292's specific late-growth failure.

The same process identities were present at each boundary:

| Executable / PID in this run | Baseline handles | First settled | Final settled |
| --- | ---: | ---: | ---: |
| chat-view-hud.exe / 8904 | 337 | 352 | 352 |
| msedgewebview2.exe / 6844 | 1184 | 1347 | 1352 |
| msedgewebview2.exe / 7012 | 149 | 149 | 149 |
| msedgewebview2.exe / 1376 | 380 | 378 | 380 |
| msedgewebview2.exe / 5940 | 267 | 314 | 314 |
| msedgewebview2.exe / 5360 | 164 | 177 | 177 |
| msedgewebview2.exe / 5880 | 252 | 300 | 301 |

Do not infer renderer/GPU/browser roles solely from executable names or handle counts. The native process remained at 337 through the sampled exercise and reached 352 between its last exercise sample and the first settled sample. Aggregate baseline/first/final ticks were 637390 / 646343 / 651578. This localizes most observed initial growth to WebView2 children and shows a later plateau; it does not identify a handle type or prove causation. A fixed 2500 ms settle delay is not evidence of completed asynchronous startup. That is a measurement question to investigate, not permission to move the baseline until a test passes.

### Native delivery repeat 3: first render not observed

The native-display-client test passed initially and in its first two repeats. The third repeat failed the gateway scenario with `WinHTTP gateway frame rendered`. No actual DOM success or revocation success can be claimed for that failed iteration. The test's one-use keys, timeouts, actual DOM assertions and server-revocation checks were unchanged.

The failure message alone does not show whether the worker failed, the private document lost ownership/handshake, or the frame did not complete rendering. Slower CI timings do not establish a cause. The next useful change is targeted, non-secret test failure evidence for those phases, not an unqualified retry, a longer timeout or a new transport implementation.

## Resume rules

The setup-navigation defect is corrected and verified. The intermittent resource-growth cause and native first-render failure remain OPEN, and the latest package gate remains failed. Never conflate these three statements.

Keep initial/late handle limits 256/64, the 128 MiB private-byte limit, exercise and repeat counts, and capture/privacy assertions. Change measurement only when a reproducible observation establishes its defect; retain old failure evidence. Do not remove the external-page feature before a working first-party replacement or require OBS on the gaming PC.

After this bounded gate, return to creator/device authorization and renewal on the existing display contract, the OBS-independent gaming companion, separately qualified clean video, and the public ad/HP/reward loop. No real CHZZK credentials or physical two-PC capture hardware were available in this investigation.
