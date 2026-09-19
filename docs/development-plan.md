# Development plan and session handoff

Updated: 2026-09-20 (Asia/Seoul). Read AGENTS.md, PRODUCT.md and architecture.md; fetch current OBS refs/checks before writing.

## Confirmed direction

OBS is the sole active branch; main is the original UX reference. Preserve one-screen private chat, OBS/private-HUD exclusion, dual-PC with NO gaming-PC OBS (including hidden/portable/projector), first-party CHZZK/platform and creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; chat counts/render acknowledgements are not audience exposure; estimates are not payouts. Do not reinterview these decisions or reapply old patches.

## Starting evidence: P2b complete

Starting remote: **f5390988e4692dbd005fb8abf65f7930e57ddefd**, documentation only over implementation **97875661581bcdd038759f1cd1bac9c8dbabaafb**.

- **CHZZK #11 / 35460704182** passed all four Windows/Linux x Node22/24 jobs: locked install, full audit, 135 tests, strict types and unchanged manifests. https://github.com/andongmin94/chat-view/actions/runs/35460704182
- **Windows #290 / 35460704187** was re-fetched at this session's start and is **fully successful**, including native build/tests/repeats, official OBS qualification, package verification and upload. This supersedes the prior handoff's in-progress status, not the separate undiagnosed #288 resource observation. https://github.com/andongmin94/chat-view/actions/runs/35460704187

P1-01a/b: reusable CHZZK auth/API/session/socket/parser plus developer preview. P2a: actual WebView2 NativeChatSurface and shared immutable text renderer. P2b: one-use display ticket, scoped read-only lease and bounded standard WebSocket gateway sharing the authorized upstream session. Their contracts and prior repair evidence remain in native-chat-display.md, display-delivery.md and dependency-validation.md. Do not redo parser selection or build another authorization framework.

## This change: P2c native consumer and connection panel

The new DisplayClient exchanges P2b's ticket and receives its existing display envelope using asynchronous WinHTTP on a dedicated worker. It uses Windows.Data.Json, bounded UTF-8 frames and a single latest-frame mailbox. The owner thread enforces expiry and consumes updates without networking. Cancellation does not free an outstanding callback's buffer: the context lives until HANDLE_CLOSING. Redirects/cookies/automatic HTTP authentication are disabled; tickets/tokens do not enter logs, URLs, settings or process arguments.

NativeChatConnection connects the client to the existing HudWindow/NativeChatSurface. Ctrl+Alt+Shift+C opens native origin/key/connect/disconnect controls. HTTPS is default; literal 127.0.0.1 HTTP requires explicit developer opt-in, not a wider external-page URL rule. The masked key is cleared on use/close. One in-flight WebView message plus one coalesced newest pending frame bounds slow-renderer delivery. Transport/lease/system/document loss ends delivery and clears the owned surface without changing capture visibility decisions. Existing URL settings, parent supervision, placement/input and safeguards remain; no second desktop runtime is added.

**This is a developer end-to-end native connection, not production account enrollment or a finished standalone gaming companion.** Provider secrets remain in the existing private developer/service environment; do not ship the probe to streamers. The short lease does not mean final users must reconnect manually every five minutes. See [native-display-client.md](native-display-client.md) for usage, exact boundaries and tests.

## Verification for this new code: pending at initial commit

Local review verified edited baseline files against their Git blob hashes and checked the new Node fixture's syntax. This environment cannot download packages or compile/run Windows. No native execution, real provider event or physical workstation test is claimed locally.

The new Windows CTest runs actual gateway -> native connection controls -> WinHTTP -> HudWindow/WebView2 DOM -> server revoke -> DOM clear. Additional real transport scenarios cover pending-exchange cancellation, local expiry despite continuing server traffic, redirect rejection, wrong scope, malformed/binary/oversized data and idle delivery. All fixture credentials/content are synthetic, passed over stdin rather than process arguments. It uses existing locked ws dependencies. The existing workflow now sets up Node and clean-installs that locked graph before all native tests/repeats and unchanged official-OBS/package gates. The CHZZK matrix continues full audit/types/tests; no assertion or resource budget was relaxed.

Inspect **this exact feature commit's** Windows and CHZZK runs before marking P2c validated. Prior #290 does not validate these new native changes. Record any genuine failure/fix and final tested SHA here; do not report an in-progress build as successful or cancel a running code verification merely to update its handoff.

## Next product acceptance

1. Finish P2c's actual Windows integration/negative scenarios and CHZZK strict-type matrix. Fix real blockers only; no generic diagnostics milestone.
2. Real authorized CHZZK app/channel: consent -> actual connected/subscribed -> native Unicode chat -> stop/reconnect/revoke. Private credentials only. A simulated gateway is not NAVER verification.
3. Replace manual developer pairing with revocable account/device enrollment and renewal, and give the same native HUD a gaming-PC lifecycle without local OBS. Keep provider/OAuth secrets server-side. Do not convert the developer probe into an unreviewed public server.
4. Qualify an OBS-free gaming-PC clean-video route while local chat stays readable. Record actual wiring, OS/GPU/game mode/HDR/refresh/latency/resources; neither a standalone process nor pairing proves clean video.
5. One selected public ad -> supported evidence -> server HP -> reward record, followed by a bounded paid pilot only after metric rights/rates/budgets/idempotency/fraud/payout rules are agreed. No synthetic/private chat event is payable exposure.

Open owner inputs remain real developer authorization, representative two-PC hardware, HP/rate/payout semantics and hosting assets. They do not block unrelated code work or permit goal changes.

## Historical constraints

Q1-Q3 lifecycle corrections at a7ea2e7 / Windows #274 are closed; never reapply the old ZIP. CHZZK #7 closed earlier dependency/type-check recovery. #288's resource-soak repeat exceeded its handle/private-byte growth budget, although its native display tests passed. #290's later success is not a diagnosis of that variability. Earlier capture intermittency likewise requires actual evidence if it recurs. Preserve these checks instead of increasing budgets or treating all failures as product regressions.
