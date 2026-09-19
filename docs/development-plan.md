# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; fetch current OBS refs/checks before writing.

## Confirmed direction

OBS is the sole active branch; main is the original UX reference. Preserve single-screen private chat, OBS/private-HUD exclusion, dual-PC with NO gaming-PC OBS (including hidden/portable/projector), first-party CHZZK/platform and creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; chat counts/render acknowledgements are not audience exposure; estimates are not payouts. Do not reinterview these decisions or reapply old patches.

## Current result: P2c native transport/display passes; full package gate remains failed

Starting remote: f5390988e4692dbd005fb8abf65f7930e57ddefd, documentation over P2b implementation 97875661581bcdd038759f1cd1bac9c8dbabaafb. New feature: bcd55d73bd0a791b5a2ade62c431608b199280c7. **Tested corrected code: 436e8ec41226b8ffe93d275d2a0ad2cd867f8832.** The following handoff/README commit changes documentation only; it does not fix or erase a failed runtime check.

**CHZZK contract #13 / 35462872875 at 436e8ec passed all four Windows/Linux x Node22/24 jobs.** Every job passed locked installation, full dependency audit, 135 tests, strict TypeScript checking and unchanged manifests. The inspected Linux/Node24 log reports zero vulnerabilities, 135/135 tests and no skipped tests. This verifies platform contracts and fixture types, not real NAVER access.

https://github.com/andongmin94/chat-view/actions/runs/35462872875

**Windows #292 / 35462872896 at 436e8ec completed with FAILURE, not in progress.** Compilation/linking succeeded and the initial CTest run passed **25/25**. The new native-display-client test, containing all nine actual transport/UI scenarios, also passed **all five additional repeats**. The existing native-chat-surface test passed its five repeats. However, the fifth repeat of the existing hud-resource-soak test failed; the repeat suite finished 24/25. Official OBS qualification, package validation and upload were skipped. **No new validated distribution package was produced by this run.**

https://github.com/andongmin94/chat-view/actions/runs/35462872896

The bounded native delivery/display integration gate is closed by those observed passes. The separate full native/package gate remains open because of the resource failure below. Do not reconstruct the working worker/renderer or report the whole build as green.

### Exact remaining resource failure

The failed repeat recorded aggregate HUD/WebView process-tree resources:

| Sample | Processes | Handles | Private bytes | GDI | USER |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 7 | 2909 | 107929600 | 25 | 66 |
| First settled window | 7 | 2961 | 109129728 | 25 | 66 |
| Final | 7 | 3065 | 118251520 | 25 | 69 |

The specific failing comparison in tests/hud-resource-soak-test.cpp is **late handle growth: 3065 - 2961 = 104, exceeding the unchanged limit of 64**. Initial handle growth is 52 (limit 256); private-byte growth is 10321920 (limit 128 MiB). Process and GUI-object changes are within their limits. Do not describe this as a failed memory-byte budget or a proven memory leak.

This test exercises repeated capture visibility, edit mode, configuration notifications and lock/suspend/resume on a fresh profile. It does not activate the new first-party connection panel. The aggregate samples do not identify which process or handle type grew, and do not establish whether the new code is causally involved. Next investigation must localize growth by process and exercise phase, then fix the responsible allocation/lifetime or a demonstrably incorrect measurement. Do not raise budgets, delete assertions, or keep rerunning until an unrelated passing run hides the problem.

The similar #288 observation predates this native worker, and #290 later passed; neither establishes a root cause. Preserve both the resource evidence and capture checks without turning this into another open-ended generic diagnostics project.

## Implemented P2c product path

DisplayClient exchanges P2b's one-use display ticket and receives its existing envelope with asynchronous WinHTTP on a dedicated worker. It uses OS JSON/UTF-8 handling, explicit HTTPS or opt-in literal loopback, bounded receive buffers and a latest-frame mailbox. Stop signals cancellation and clears pending content without waiting for network operations. Callback context/buffers remain alive until HANDLE_CLOSING and hold no HWND/WebView/OBS pointer. Local lease and frame deadlines are enforced independently of server disconnect. Redirects, cookies and implicit HTTP authentication are disabled; provider credentials and automatic exchange replay are absent.

NativeChatConnection attaches the worker to the existing HudWindow/NativeChatSurface. **Ctrl+Alt+Shift+C** opens native origin/key/connect/disconnect controls. The key is masked, cleared on use/close, and not persisted or logged. One WebView message in flight plus one coalesced newest pending frame bounds UI delivery. Transport/lease/system/document loss ends delivery and clears the owned surface. Existing URL settings, OBS parent supervision, placement/input and capture safeguards remain. External-page DOM recovery does not supervise the owned first-party document.

This is an implemented **developer native connection path**, not public account enrollment, automatic renewal or a finished standalone gaming companion. Provider secrets stay in private developer/service configuration; ordinary streamers must not receive them or run the probe as a public server. The short display lease is not a final requirement to reconnect manually every five minutes. See [native-display-client.md](native-display-client.md) and the root/platform READMEs.

## What the native test actually executed

The main scenario uses actual DisplayAccess/DisplayGateway -> native connection controls -> WinHTTP -> HudWindow/DirectComposition/WebView2 -> actual Unicode/inert-markup DOM assertions -> server revocation -> actual DOM clearing. Eight further real HTTP/WebSocket scenarios cover pending-exchange cancellation, local expiry despite continuing server traffic, redirect rejection, wrong scope, malformed JSON, binary input, oversized frames and idle delivery. All nine run inside the new CTest and passed its initial execution and five repeats in #292.

Identities, credentials and messages are synthetic and credentials travel over inherited stdin, not process arguments. The tests do not contact NAVER or physical capture hardware. They are not a full live CHZZK authorization-to-viewer/capture certification or a test of durable machine identity.

The existing Windows workflow adds Node24 and a locked npm clean install solely for test fixtures, then retains all native tests/five repeats and existing package/official-OBS checks. Node is not shipped as a desktop runtime. No capture/resource budget, assertion, dependency version or security gate was relaxed. Local authoring performed source/blob and fixture syntax/LF-CRLF checks; Windows execution is remote CI evidence, not a local run.

## Failures fixed during P2c

At bcd55d7, Windows #291 compiled the entire feature and passed all 24 pre-existing initial CTests. Its new gateway scenario passed actual WinHTTP and Unicode/inert-markup DOM assertions, then waited for a revoke the fixture never sent: the fixture expected LF stdout, while Windows emitted CRLF. 436e8ec recognizes either complete line ending without changing DOM/revoke assertions or timeouts. #292 verifies the corrected scenario.

https://github.com/andongmin94/chat-view/actions/runs/35462521499

CHZZK #12 exposed an existing fractional-timer boundary: closing the socket at expiry could briefly leave its grant usable. 436e8ec rounds the delay upward and revokes the grant in the same callback as the expiry closure. The unchanged immediate-authentication-rejection regression passes in #13. This is a service lifetime fix, not an extended lease or weakened assertion.

https://github.com/andongmin94/chat-view/actions/runs/35462521476

## Previously validated layers

P1-01a/b: reusable CHZZK auth/API/session/socket/parser and developer preview. P2a: actual WebView2 NativeChatSurface/shared immutable text renderer. P2b: one-use ticket, scoped read-only lease and bounded WebSocket gateway sharing the authorized upstream session. Their contracts remain in native-chat-display.md, display-delivery.md and dependency-validation.md. Do not recreate them or reopen the closed parser/type-check investigation.

- 9787566 / CHZZK #11: four jobs, 135 tests, audit/types/manifests passed. https://github.com/andongmin94/chat-view/actions/runs/35460704182
- 9787566 / Windows #290: rechecked this session; native tests/repeats, official OBS/package validation and upload passed. This is the earlier baseline, not validation of P2c. https://github.com/andongmin94/chat-view/actions/runs/35460704187
- Q1-Q3 lifecycle fixes at a7ea2e7 / Windows #274 remain closed. The old downloadable patch is obsolete.

## Next work, in order

1. Localize the #292 late-handle growth above and resolve that specific full-build blocker without discarding the validated native delivery. Preserve the original budgets and report the next exact code/run result. A passing repeat alone does not diagnose the prior growth.
2. Real authorized CHZZK channel: consent -> real connected/subscribed -> actual Unicode chat in native HUD -> stop/reconnect/revoke. Credentials stay private; a synthetic gateway is not NAVER acceptance. This external test can run alongside independent implementation.
3. Add revocable creator/device enrollment and renewal instead of manual developer keys, and give the same native HUD a gaming-PC lifecycle without local OBS. Keep provider secrets on the service; do not promote the developer probe to an unreviewed public backend.
4. Qualify an OBS-free gaming-PC clean-video path with readable local chat. Record wiring, OS/GPU/game mode/HDR/refresh/latency/resources. Neither an independent process nor pairing proves clean video.
5. One selected public ad -> permitted exposure evidence -> server HP -> reward record, then a bounded paid pilot after metric rights/rates/budgets/idempotency/fraud/payout rules. Synthetic/private chat traffic never becomes payable exposure.

Open owner inputs remain real provider authorization, representative dual-PC hardware, HP/rate/payout semantics and hosting assets. They do not block unrelated code work or authorize changing the five goals. End sessions with exact changed/tested commits and honest failures; never cancel code verification just to publish a handoff.
