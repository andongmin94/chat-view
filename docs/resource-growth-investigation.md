# Bounded HUD resource investigation

Updated: 2026-09-20 (Asia/Seoul). Evidence for the native package gate, not a new diagnostics product or universal leak-freedom claim.

## Starting failure

Windows #292 / 35462872896 at 436e8ec compiled and passed its initial 25 CTests. The fifth resource-soak repeat failed the unchanged late-handle bound: first settled 2961, final 3065, growth 104 > 64. Memory-byte, process and GUI bounds passed. Native display/surface tests passed their five repeats; packaging was skipped.

https://github.com/andongmin94/chat-view/actions/runs/35462872896

The aggregate failure does not identify a process, handle type or operation. A similar #288 observation predates the network worker; #290 passed. Do not call this a diagnosed native memory leak or hide it by selecting a successful retry.

## Instrumented unchanged runtime

f06585705b46f82af2ef36c04619416dc9b04d9c changes only the soak test and log reporting. Production remains identical to 436e8ec. Samples record PID plus creation time, executable basename, handles and private bytes, not command lines, user paths, chat or credentials. All resource limits, actions, delays and repeats remain unchanged.

Windows #293 / 35464221001 passed initial 25 tests, all five repeats, official OBS checks and package upload. The earlier failure did not reproduce.

https://github.com/andongmin94/chat-view/actions/runs/35464221001

| Repeat | Aggregate baseline | First settled | Final settled | Late delta | Native HUD handles (baseline / first / final) |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 2785 | 2757 | 2771 | +14 | 217 / 217 / 217 |
| 2 | 2780 | 2777 | 2797 | +20 | 217 / 217 / 217 |
| 3 | 2774 | 2770 | 2779 | +9 | 217 / 217 / 217 |
| 4 | 2749 | 2714 | 2706 | -8 | 217 / 217 / 217 |
| 5 | 2772 | 2725 | 2730 | +5 | 217 / 217 / 217 |

Observed handle variation belongs to WebView2 children in this successful run; native HUD handles are stable. This does not retrospectively locate #292's growth or mean instrumentation fixed anything.

## Confirmed redundant work and targeted correction

Every unchanged empty-URL settings notification and resume requested a fresh setup document: GUID URL, Stop and Navigate. The resource exercise sends those notifications repeatedly. The static setup page need not be replaced when already selected or loading.

WebViewHost now preserves the exact currently selected setup document on show_setup_page. Explicit reload still makes a fresh setup document for recovery. Actual NativeChatSurface opens retain fresh isolated URLs; external navigation invalidates private ownership; close clears the setup identity. No external-page privilege or capture policy changes.

The real WebView2 regression checks 12 requests before load and 12 after load: one navigation, preserved document canary, source and history length. It also checks explicit recovery replaces the document and subsequent ordinary requests retain it. Existing Unicode, inert markup, navigation isolation and teardown assertions remain.

This removes demonstrated unnecessary navigation/history churn. Source inspection alone does not prove it caused the previous intermittent aggregate handle failure.

## Correction verification at authoring

Baseline blobs and git diff --check passed. A local C++ harness executing original/changed setup/reload bodies with simulated WebView calls failed before and passed 10 lifecycle assertions after, under warnings-as-errors, ASan and UBSan. This is not Windows execution or resource measurement.

The correction requires its own Windows build, actual WebView2 regression, display tests, resource repeats and official OBS/package gates. Do not substitute #293's unchanged-runtime result for that check. Record the exact new result in development-plan.md before closing the session. No real CHZZK credentials or physical dual-PC hardware were available.

If growth recurs, use retained process/phase evidence. Do not raise the 64-handle late limit, suppress repeats or rewrite working chat transport. Product work remains device authorization/renewal and an OBS-independent gaming companion, with clean video qualification separate.
