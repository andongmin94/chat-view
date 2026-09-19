# Development plan and session handoff

Updated: 2026-09-19. Read AGENTS.md and PRODUCT.md first.

## Non-negotiable direction

Develop on OBS only; preserve main as original UX reference. CHZZK first. The gaming PC must not require OBS, portable/hidden OBS or an OBS projector. Preserve the five goals: single-screen private HUD, OBS integration with stream exclusion, dual-PC, own platform, creator-selected public ads with viewer-time-related HP/rewards. Pairing is not clean video; a heartbeat/message count is not viewer exposure; an estimate is not a payout.

## Current work: P1-01b implementation, not live certification

Starting remote commit: `6d65177acfcc336e67801236ac8f4f31b9dc095d`.

The prior P1-01a authorization client is extended, not replaced. New code connects the issued ticket using Socket.IO, handles SYSTEM connected/sessionKey, POST chat subscription and matching subscribed acknowledgement, receives/validates CHAT and displays it in ChatView's own text renderer. The existing developer loopback tool now starts/stops that session and serves authenticated SSE/preview assets. The obsolete issuance-only `/session` action is removed. API/renderer/session/preview concerns are separate, without a multi-provider framework, database or new desktop runtime.

User-visible result after developer authorization: start chat -> subscription confirmed -> first message distinguished -> own chat list. Duplicate starts do not duplicate upstream; reconnect requests a fresh ticket. Stop, disconnect, unsubscribe, CHAT revocation and token expiry stop delivery/clear memory. Refresh stops the old chat before rotating tokens. Global token revoke remains explicit. The renderer treats content as text, has no provider secrets/native IPC and renders no remote media. History is capped at 100 messages; at most four preview clients and bounded/coalesced delivery prevent unbounded consumer queues.

The concrete client is Socket.IO-client 2.0.3 with explicit patched transitive dependencies, as explained in platform/README.md. CI must audit and exercise the actual installed graph against a real EIO3-enabled Socket.IO test server. A simulator is not NAVER: there are still no real credentials or actual CHZZK messages in the available evidence. Do not claim P1-01 is closed or the public backend/native HUD now supports first-party chat.

## Verification and source scope

Local: 41 authentication/session/real-loopback-preview tests passed; strict TypeScript checks and renderer syntax check passed. Old authentication scenarios were retained while the obsolete session-only action assertions became start-chat assertions. Runtime/API exceptions and raw tickets are not returned to the renderer. New tests cover ordering, duplicate events, wrong channel, late callbacks, handshake deadlines, stop/revoke/expiry, payload bounds, SSE authorization and text-node rendering.

The local environment cannot resolve external hosts or install the new npm graph. Actual-library transport tests and the unchanged API suite must be checked in CHZZK contract CI (Windows/Linux x Node 22/24); do not confuse local mocks with these checks. Dependency manifest generation/audit is part of that run. No new code has been Windows-desktop-tested locally.

This slice leaves `src/`, native `tests/`, CMake, installation/packaging, capture policy and Windows build workflow unchanged. It adds platform implementation/tests and updates documents/CHZZK CI only. The native source is still the validated a7ea2e7 tree. Neither synthetic chat nor native CI establishes hardware privacy.

## CI evidence at session start

- Windows #274 / a7ea2e7: all stages passed, including 23 tests + five repeats, official OBS qualification and package upload. Q1-Q3 quality gate is CLOSED; never reapply the old downloadable patch.
- Windows #275 / 6d65177: all stages passed, re-fetched at this session's start. https://github.com/andongmin94/chat-view/actions/runs/35447087516
- CHZZK contract #1 / 6d65177: four Windows/Linux and Node 22/24 jobs passed. https://github.com/andongmin94/chat-view/actions/runs/35447087630

These validate the baseline, not this new commit. Inspect and record the actual new commit's runs before declaring its CI passed. Older #271 failed intermittent capture qualification; later successes did not diagnose it. Do not weaken assertions or reopen unlimited preflight work.

## Next acceptance gates

1. Inspect the P1-01b commit's npm audit, actual Socket.IO test, all HTTP/UI tests, strict types and native CI. Resolve genuine failures and lock the tested dependency graph before further feature changes.
2. Real authorized CHZZK app/channel: login -> actual connected/subscribed events -> actual Unicode message in own UI -> unsubscribe/reconnect/revoke. Confirm the exact dependency substitutions with NAVER's endpoint. Only private environment configuration may contain credentials.
3. P2: production account/device authorization and first-party chat into the existing native HUD, without widening arbitrary external-page privileges or bundling client secrets. Keep the current native feature working until its replacement works end to end; remove superseded page/DOM paths together.
4. P1-02/P3: OBS-independent gaming companion plus an actually qualified HUD-free feed. No gaming-PC OBS workaround. Record hardware/OS/GPU/capture wiring/window mode/HDR/refresh/latency/resources and simultaneous local readability/recorded exclusion.
5. P3/P4: one selected public ad -> evidence -> server HP -> reward record, then a bounded paid pilot after metric permissions, budget/idempotency/fraud and payout rules are agreed. Do not use chat counts as audience or synthetic events as payable exposure.

Remaining owner input is real developer authorization, representative dual-PC hardware, HP/rate/payout details and hosting resources. The five goals, first provider and gaming-PC OBS prohibition are not open questions. This slice does not authorize real-money operations or universal capture guarantees.
