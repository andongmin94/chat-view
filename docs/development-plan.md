# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; fetch actual OBS refs and checks before writing. Distinguish tested source from a later documentation-only HEAD.

## Confirmed direction

OBS is the sole active development branch; main is the original Electron UX reference, not a native merge target. Preserve all five goals: single-screen private chat; OBS integration and personal HUD exclusion; dual-PC without OBS on the gaming PC (including hidden/portable/projector); first-party CHZZK platform; creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video. Chat counts and render acknowledgements are not viewer exposure. Estimates are not payouts. Do not reinterview these decisions or reapply old patches.

## Current change: remove redundant setup navigation

Starting remote was 6a123919bdb53021f2c8104f44a76660dd242b43 over P2c code 436e8ec. f06585705b46f82af2ef36c04619416dc9b04d9c added resource localization without changing runtime code. The production correction is **82f36a60d727c71110cb1a0180c71fb00a48951d**.

The static setup page retains its exact current document when duplicate empty-URL settings notifications or Windows resume request it again. Explicit reload still makes a fresh document for recovery; actual first-party chat openings retain fresh private URLs. External-page privileges, display cancellation, capture interlocks and package checks are unchanged.

The actual WebView2 regression asserts one navigation for 12 pending and 12 loaded requests, a persistent DOM canary and source/history identity, plus a fresh document on explicit recovery. No new dependency or compatibility path was introduced.

## Latest verification: two unresolved repeat failures

**Windows #294 / 35464979632 at 82f36a6 completed with FAILURE.** Source compilation/linking and initial CTest **25/25** passed. The extended NativeChatSurface test, including the setup-idempotence regression, passed initially and in all five repeats. The full repeat suite finished **23/25** because two other tests failed. Official OBS qualification, package validation and upload were skipped. No validated package was produced by this run.

https://github.com/andongmin94/chat-view/actions/runs/35464979632

- Resource-soak repeat 1 passed. Repeat 2 failed **initial** aggregate handle growth: baseline 2733 -> first settled 3017, **284 > 256**. Final 3025 means late growth **8 <= 64**. Private-byte, process and GUI bounds passed. This differs from #292's late-growth failure; do not call it reproduction of that specific 104-handle spike.
- Native-display-client passed initially and in its first two repeats. Repeat 3 failed the gateway scenario at **WinHTTP gateway frame rendered**. The unchanged message-render deadline and actual DOM assertions remained. The log does not distinguish transport failure, document ownership/handshake loss or delayed rendering. The cause is OPEN. Do not label it merely runner slowness or claim it was proved to be caused by the setup correction.

The setup-navigation correction is validated, but the **full package and repeatable delivery gates remain OPEN**. Historical P2c successes do not supersede this intermittent display failure. No new platform-matrix result exists for this resource-only change; the most recent inspected matrix is #13 at 436e8ec, with platform code/dependencies unchanged.

Local authoring verified Git blob baselines and git diff --check. A C++ harness executing old/new setup and reload method bodies failed before and passed 10 lifecycle assertions after, with simulated WebView calls, warnings-as-errors, ASan and UBSan. This is not local Windows execution or resource measurement. Resource constants and the three capture/edit/lifecycle exercise function bodies were checked unchanged.

## Resource evidence and remaining uncertainty

[resource-growth-investigation.md](resource-growth-investigation.md) records #292's aggregate failure, #293's unchanged-runtime process samples, the setup correction and #294's new per-process data.

#293 at f065857 ran unchanged production with process/phase tracing. Initial 25 tests, all five repeats, official OBS checks and package upload passed. Across its five repeats, native HUD handles stayed 217 at baseline, first and final samples; aggregate late deltas were +14, +20, +9, -8 and +5. Variation was in WebView2 children. The earlier failure did not reproduce, and instrumentation was not a leak fix.

#294 shows most initial-window growth in WebView2 children and a smaller native HUD increase (337 -> 352 -> 352). The native increase occurred after the sampled exercise and before the first settled sample. A fixed 2500 ms delay does not prove completed asynchronous startup. These observations neither prove a leak nor justify moving the measured baseline until a test passes. Investigate actual phases/readiness before changing the measurement contract.

The redundant setup navigation is directly demonstrated and corrected. It is not a proven cause of every earlier resource failure. Keep initial/late handle budgets 256/64, private-byte limit 128 MiB, repetitions and capture assertions. No new transport, renderer or generic diagnostics framework is needed to investigate these specific failures.

## Existing implemented layers: reuse, do not recreate

P1-01a/b provides CHZZK authorization/API/socket subscription and an own browser renderer. P2a embeds the shared renderer in NativeChatSurface with exact host-owned memory-document identity. P2b issues expiring, hash-stored one-use display tickets and separate read-only leases on a bounded WebSocket sharing one creator upstream. P2c connects the gateway to WinHTTP and the existing transparent HUD.

DisplayClient owns asynchronous networking on a worker, bounded receive/latest-frame buffers, local lease/idle expiry and cancellation-safe callback lifetime. Redirects, cookies and implicit authentication are disabled. No provider credentials enter the renderer. NativeChatConnection uses Ctrl+Alt+Shift+C for native origin/masked-key/connect/disconnect controls; keys are not persisted. Transport/lease/system/document loss clears private chat. OBS-parent lifecycle, placement/input and capture guards remain.

This is a developer native connection path, not public account enrollment, automatic device renewal, a standalone gaming companion or a deployed multi-user backend. The five-minute lease is not a final manual reconnection requirement. Do not ship developer secrets or expose the loopback probe as a public service.

P2c's nine actual HTTP/WebSocket/WinHTTP/HudWindow/WebView2 scenarios cover Unicode/inert-markup rendering and revoke clearing, pending cancellation, local expiry despite continued traffic, redirect/wrong scope/malformed JSON/binary/oversized frames and idle connections. Synthetic identities/messages do not establish NAVER access or hardware capture qualification. Node is used for fixtures, not shipped as a new HUD runtime.

Contracts: native-chat-display.md, display-delivery.md, native-display-client.md and dependency-validation.md.

## Historical verification, not current release approval

- 436e8ec / CHZZK #13 / 35462872875: four Windows/Linux x Node22/24 jobs passed all 135 tests, full audit (zero reported findings), strict types and unchanged manifests. No dependency/protocol change in this resource work.
- 436e8ec / Windows #292 / 35462872896: build and initial 25/25 passed; surface/display five repeats passed; resource fifth repeat failed and package stages were skipped.
- 9787566 / Windows #290 / 35460704187: full package succeeded before P2c. #288 showed earlier intermittent resource growth; neither establishes the later cause.
- Q1-Q3 at a7ea2e7 / Windows #274 remain closed. The old downloadable patch is obsolete. Socket dependency/type selection is closed; do not restart it.
- P2c #291 fixture expected LF instead of Windows CRLF; 436e8ec fixed recognition without changing DOM/revoke assertions. The same commit atomically retires grants at timer expiry; immediate authentication-rejection checks passed in #13.

## Next bounded work, then return to product development

1. Address the two exact #294 failures before claiming a current package. For native delivery, add narrowly scoped test failure evidence for exchange/socket progress, connection state and owned-document handshake/render acknowledgement; never log keys/tokens/origins/chat or increase the deadline. For resource growth, test whether baseline collection precedes asynchronous startup work using retained process/phase data. Do not assume a longer warm-up is a fix, silently rebase the measurement, or rerun unchanged until green. Preserve all useful code and limits.
2. Replace manual developer keys with revocable creator/device enrollment and renewal on the current display contract, keeping provider secrets on the service. Give the same native HUD a gaming-PC lifecycle without local OBS, preserving the working single-PC path; do not start a second generic authentication framework.
3. When private app/channel authorization is available: actual CHZZK consent -> subscription -> real Unicode message in native HUD -> stop/reconnect/revoke. Synthetic tests do not close this gate; unavailable credentials do not block unrelated implementation.
4. Separately qualify an OBS-free gaming-PC clean-video path while local chat stays readable. Record wiring, OS/GPU/game mode/HDR/refresh/latency/resources. A companion or pairing does not establish HUD-free HDMI.
5. One selected public ad -> permitted exposure evidence -> server HP -> reward record. A paid pilot additionally needs metric rights, rates, budgets, idempotency, fraud review and payout rules. Synthetic/private chat never becomes payable exposure.

Open owner inputs are private provider authorization, representative two-PC hardware, HP/rate/payout semantics and hosting assets. Confirmed goals are not questions to repeat. End sessions with actual changed/tested commits, failures and one next acceptance path. Never cancel code verification merely to publish a handoff.
