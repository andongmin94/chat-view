# Open native resource and first-render evidence

Updated: 2026-09-20. **Evidence only, not a development queue.** [development-plan.md](development-plan.md) owns current status; [development-workflow.md](development-workflow.md) determines which failures block which claims. Retain the evidence without pausing unrelated product work or growing a general diagnostics project.

## Recorded runs

| Source/run | Observation | Meaning |
| --- | --- | --- |
| 436e8ec / Windows #292 / 35462872896 | Initial 25/25 passed; fifth resource repeat: 2961 -> 3065 handles, late +104 > 64. Initial +52 <= 256. Display/surface five repeats passed; packaging skipped. | Intermittent aggregate late-growth failure, no identified process/type. |
| f065857 / #293 / 35464221001 | Runtime unchanged; per-process tracing added. Full tests/repeats/OBS/package succeeded. Native HUD handles 217 throughout five repeats; aggregate late deltas +14,+20,+9,-8,+5. | The prior failure did not reproduce; instrumentation is not a fix. |
| 82f36a6 / #294 / 35464979632 | Initial 25/25 passed; repeats 23/25. Resource initial growth and a first-render failure; official OBS/package skipped. | Current historical unresolved failures; not release approval. |

## Confirmed correction, distinct from those failures

82f36a6 removes repeated Stop/new-GUID/Navigate work for an unchanged selected/loading setup page. Explicit reload still replaces it, and private-chat openings keep fresh isolated identities. Actual WebView2 checks for 12 pending and 12 loaded requests, persistent DOM/source/history and explicit reload passed initially and five repeats in #294. This establishes the setup correction, not the cause of every handle spike.

## #294 resource repeat 2

Aggregate baseline / first-settled / final handles: **2733 / 3017 / 3025**. Initial +284 exceeds 256; late +8 is within 64. Private bytes: 98521088 / 110669824 / 111427584; process count 7, GDI 25 throughout, USER 63 / 69 / 69. Byte/process/GUI limits passed. This differs from #292's late +104 failure.

| Same process identity in that run | Baseline | First | Final |
| --- | ---: | ---: | ---: |
| chat-view-hud.exe / PID 8904 | 337 | 352 | 352 |
| msedgewebview2.exe / 6844 | 1184 | 1347 | 1352 |
| msedgewebview2.exe / 7012 | 149 | 149 | 149 |
| msedgewebview2.exe / 1376 | 380 | 378 | 380 |
| msedgewebview2.exe / 5940 | 267 | 314 | 314 |
| msedgewebview2.exe / 5360 | 164 | 177 | 177 |
| msedgewebview2.exe / 5880 | 252 | 300 | 301 |

Ticks were 637390 / 646343 / 651578. Native handles rose between the last sampled exercise and the first settled sample, then plateaued. Most increase was in WebView2 children; executable names alone do not establish browser/GPU/renderer roles. Neither a leak nor premature baseline timing is proven. A fixed 2500 ms delay is not proof of completed asynchronous startup. Preserve limits 256/64 and 128 MiB, and existing exercise counts; change measurement only with demonstrated evidence, not to obtain a pass.

## #294 native display repeat 3

Initial execution and two repeats passed; repeat 3 failed `WinHTTP gateway frame rendered`. That iteration did not establish successful DOM rendering/revocation. The log does not distinguish transport failure, owned-document handshake loss or delayed render. Do not replace the transport or inflate its timeout based on this label alone. When working on this flow, use bounded phase evidence without keys, origins, tokens or chat text.

A new companion launch or routine CI success does not resolve either historical defect. Preserve affected release/reliability restrictions, but continue independently testable enrollment, renewal and platform implementation. Full run logs and completed investigations remain in Git/Actions history; do not copy them into every handoff.
