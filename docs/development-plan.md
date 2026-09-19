# Development plan and session handoff

Updated: 2026-09-19 (UTC). Read AGENTS.md and PRODUCT.md first; re-fetch OBS refs and checks before writing.

## Non-negotiable direction

Develop on OBS only; preserve main as original UX reference. CHZZK first. The gaming PC must not require OBS, portable/hidden OBS or a projector. Preserve the five goals: single-screen private HUD, OBS integration with stream exclusion, dual-PC, own platform, creator-selected public ads with audience-time-related HP/rewards. Pairing is not clean video; a message/heartbeat is not viewer exposure; an estimate is not a payout.

## Current work: recover and validate P1-01b

This session started from remote `0ac467b1ed47ff06a30249adaa50c35d3c045250`, which an interrupted session had already committed on top of 6d65177. Do not reconstruct or reapply that implementation.

Implemented path: developer browser authorization -> API-issued ticket -> Socket.IO SYSTEM connected/sessionKey -> POST subscription and matching SYSTEM subscribed -> CHAT validation -> authenticated SSE -> ChatView-owned text renderer. API/session/socket/renderer/preview concerns are separate. The issuance-only /session action was removed. There is no new desktop framework or empty public backend scaffold.

Duplicate starts do not duplicate subscriptions; reconnect requests a new ticket. Stop, disconnect, unsubscribe, CHAT revocation and token expiry stop delivery and clear history. Refresh stops chat before rotating tokens; global revoke remains explicit. The text renderer has no provider secrets/native IPC/remote media. History is capped at 100 messages and the developer SSE preview at four clients with coalesced delivery. This is not a pre-decoding frame limit or a proof of ad exposure.

### Dependency recovery in this session

Original contract #2 failed audit before any installed-library test ran. e7ad608 tried parseuri 2.0.0 based on an advisory boundary, but that version does not exist. A temporary read-only registry inspection (b4a8a4d / contract #4) established actual versions and the different 3.x API; that inspection step has been removed.

12470eb replaced the vulnerable graph with published parseuri 3.0.2 and socket.io-parser 3.3.6, kept CHZZK's Socket.IO-client 2.0.3/Engine.IO 3 protocol, and removed the legacy string-URI constructor path. Node URL now supplies structured Manager/Engine.IO options. No compatibility shim, vendor source edit or custom Socket.IO framing is introduced. The actual wire test preserves a special-character/Unicode auth query instead of dropping the ticket.

Contract #5 at 12470eb installed successfully, reported zero audit findings and passed **82 tests**, including actual Socket.IO/WebSocket and authenticated local preview. Its remaining strict-type errors were solely the captured HTTP address in the fixture constructor. This change captures the validated numeric port, adds the real resolver's 54-package lock, switches to npm ci, and checks manifest stability. Details: [dependency-validation.md](dependency-validation.md).

**At the time of this commit, the new locked clean-install/type-check matrix is pending.** Inspect the checks on its exact commit; do not report #5 as an overall success because it failed strict types. No local installation of npm packages was possible in this container; remote installed-library execution and local syntax/JSON checks are separate evidence.

## Evidence and source scope

- Windows #274 / a7ea2e7: passed 23 native tests plus five repeats, official OBS qualification and artifact upload. Q1-Q3 quality gate closed; never reapply the old patch.
- Windows #275 / 6d65177: passed all stages. https://github.com/andongmin94/chat-view/actions/runs/35447087516
- CHZZK contract #1 / 6d65177: four jobs passed. https://github.com/andongmin94/chat-view/actions/runs/35447087630
- Windows #276 / 0ac467b: passed all stages, rechecked in this session. https://github.com/andongmin94/chat-view/actions/runs/35449471047
- CHZZK contract #2 / 0ac467b: failed dependency audit. https://github.com/andongmin94/chat-view/actions/runs/35449471052
- CHZZK contract #4 / b4a8a4d: registry metadata succeeded; installation still failed for the nonexistent version. https://github.com/andongmin94/chat-view/actions/runs/35450955155
- CHZZK contract #5 / 12470eb: audit and 82 tests passed; strict type check failed as described above. https://github.com/andongmin94/chat-view/actions/runs/35451316782

Native src/, tests/, CMake, packaging/installers, capture policy and Windows workflow are unchanged throughout P1-01a/b and this recovery. The native source remains the validated a7ea2e7 tree. Check the final commit's Windows workflow separately. Historical #271 capture intermittency was not diagnosed by later successful runs; do not weaken checks or restart an unlimited preflight project.

No real developer credentials or CHZZK events were available. The local network fixture uses genuine client/server/transport libraries but simulates NAVER and does not prove endpoint/TLS/commercial approval. A provider simulator and browser-DOM tests are not native HUD, real browser/device, two-PC video, or paid-ad certification.

## Next acceptance gates

1. Finish the current commit's locked clean installs, full audit, 82 tests, strict types and native CI; record exact results. Do not restart the dependency investigation if those pass.
2. With a real authorized CHZZK app/channel: login -> real connected/subscribed -> Unicode message in own UI -> unsubscribe/reconnect/revoke. Keep credentials in private environment configuration, not chat or commits.
3. P2: scoped account/device authorization and first-party chat into the existing native HUD. Do not package developer client secrets or widen arbitrary external-page privileges. Keep the working native feature until a complete replacement works; remove superseded provider/DOM paths with that replacement.
4. P1-02/P3: OBS-independent gaming companion and a qualified HUD-free video feed. No game-PC OBS workaround. Record hardware/OS/GPU/capture wiring/window mode/HDR/refresh/latency/resources and simultaneous local readability/recorded exclusion.
5. P3/P4: one selected public ad -> evidence -> server HP -> reward record, then a bounded paid pilot after metric rights, rates/budgets/idempotency/fraud and payout rules. Never count chats as viewers or synthetic events as payable exposure.

Remaining owner input: real developer authorization, representative dual-PC hardware, HP/rate/payout details and hosting assets. The five goals, first provider and prohibition of gaming-PC OBS are already decided. Missing external credentials do not permit pretending live verification or discarding the platform/ad goals.
