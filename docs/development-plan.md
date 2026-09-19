# Development plan and session handoff

Updated: 2026-09-19. Product authority: [PRODUCT.md](../PRODUCT.md).

## Confirmed direction

OBS is the sole development/integration branch. main is the original Electron product/UX reference, not a native merge target. CHZZK is first. Dual-PC operation must not require OBS on the gaming PC, including portable/hidden OBS or a projector workaround. Keep all five goals: single-screen private HUD, OBS integration and stream exclusion, dual-PC, own platform, and creator-selected ads with viewer-time-related HP/rewards. Do not ask these decisions again or turn the current implementation's limits into product scope.

## Current work: P1-01a, CHZZK authorization boundary

Starting commit: `a7ea2e7082a84d220db7948371b02893ab7d3ed8`.

This change adds the reusable TypeScript official HTTP client and a working local developer connection probe in [platform/README.md](../platform/README.md). It implements authorization-code exchange, refresh, explicit revoke, authenticated user lookup and socket-session URL issuance. The probe performs the browser callback/user lookup flow and exposes explicit session/refresh/revoke actions. No provider credentials/session URLs are exposed to its UI/logs. Refresh is serialized and an ambiguous one-use refresh is not retried.

**This is not actual chat ingestion or a deployed platform.** P1-01a's implementation and synthetic-provider tests are complete; real-account validation is still outstanding. P1-01 overall remains open until a real authorized channel supplies chat through a qualified socket transport. There are no empty service modules, public backend, account database, new runtime npm dependencies or changes to the native sources/CMake/installer. The loopback tool is developer-only, not an architecture to ship to ordinary streamers. The existing native HUD remains usable while this separate provider boundary is built.

Local verification actually executed: 48 Node tests, including real local HTTP request/callback flows with a simulated upstream, passed on Node 22.16.0. Strict TypeScript checks passed on TypeScript 5.8.3 with Node types 25.1.0. Tests cover replay/expiry, wrong cookie/state, CSRF/Host checks, sensitive error suppression, escaped channel text, rejected session URLs, provider errors, token rotation and duplicate refresh. No live credentials, provider network, Windows desktop or physical capture setup was used locally.

The new `CHZZK contract` CI targets Windows/Linux on Node 22/24 and a separate strict type check. Inspect its actual commit's result after integration. Native Windows build remains unchanged and runs independently. Do not reuse a successful baseline as proof of this commit's CI.

## Next executable milestone — P1-01b

Choose and verify the concrete official chat transport, connect the API-issued URL, receive SYSTEM connected/sessionKey, call chat subscription, receive a real CHAT event and display it in ChatView-owned UI. Test revoked/unsubscribed/disconnected states and cleanup. Reuse `platform/server/chzzk/api.mts`; do not start a second OAuth client or expand the loopback probe into a public account service.

Official docs rechecked 2026-09-19 still specify Socket.IO-client 1.0.0 through 2.0.3. No such dependency was introduced here. Investigate dependency/security and protocol compatibility, including actual authorized-channel evidence, before selecting a client. Do not quietly use an unsupported newer version, write a home-grown wire protocol, or call fixtures live verification. Current session-URL validation accepts only HTTPS NAVER nchat subdomains and must be requalified if an official endpoint changes.

Real-account verification needs the owner's registered developer application and channel consent. Credentials go into private runtime configuration only, never the conversation or repository. Account access is needed to test real behavior, not to repeat the agreed goals.

## Dual-PC and advertising gates remain active

P1-02: investigate an OBS-free native clean-video route and qualify representative hardware. Device pairing is not clean video. Keep the HUD readable on the gaming monitor while excluding it from the recorded feed. Record OS/GPU, capture card/output wiring, window mode, HDR/refresh/DPI, latency/resources and transition/reconnect behavior. No hardware support was demonstrated in this change. Do not return to requiring gaming-PC OBS.

P2: finish one authorized CHZZK channel -> service -> own renderer -> existing transparent HUD, with disconnect/revocation/bounded buffers and a real recording proving private HUD exclusion. Remove replaced external-page/DOM paths only with a working replacement; do not add permanent fallback architectures.

P3: extend the existing native runtime to authenticated gaming/streaming roles without local OBS dependency on the gaming PC. Test pairing revocation/expiry/duplicate devices and the actual clean feed. Connect one approved ad end to end: selection -> separate public OBS banner -> evidence -> server-side HP/reward record. Keep synthetic/non-payable evidence out of financial records.

P4: a bounded paid pilot requires agreed HP/unit/rate/budget/payout rules, provider/data permissions, exposure/fraud review and idempotent accounting/budget caps. Check preview-only sources, cropping/occlusion, expired evidence, simultaneous creators and duplicated reports. A concurrent viewer count is not measured individual ad attention. Public ads and private HUD are separate outputs.

## Closed quality gate and historical CI

`ba7176a` removed the unused diagnostics path and corrected read-versus-posting classification. See [code-quality-audit.md](code-quality-audit.md). `a7ea2e7` implemented Q1-Q3 (elapsed stability, HUD-owned interaction, non-allocating error formatter); see [lifecycle-fixes.md](lifecycle-fixes.md). **Windows build #274 at a7ea2e7 passed the complete workflow**: 23 native tests plus five repeats, package checks, official OBS qualification and artifact upload. This was re-fetched at session start; the Q1-Q3 gate is closed, not waiting for a patch to be applied again.

- #271, c82fce0: failed capture qualification after native tests. https://github.com/andongmin94/chat-view/actions/runs/34626547210
- #272, 12ce12b: all stages passed. https://github.com/andongmin94/chat-view/actions/runs/34677416613
- #273, ba7176a: all stages passed. https://github.com/andongmin94/chat-view/actions/runs/34680162067
- #274, a7ea2e7: all stages passed. https://github.com/andongmin94/chat-view/actions/runs/34683514443

Later passes do not diagnose the earlier intermittent failure. Preserve assertions and investigate repeatability if it recurs. Q4/Q5 are focused follow-up findings, not permission for another indefinite diagnostics/cleanup project. The broad native READY TO STREAM claim remains to narrow when that UI changes.

## Branch history preserved

Merge 12ce12b retained both OBS f9ed23e and latest work cfe9421. Telemetry 251f278, staging 63610b2 and schema probe 65b9392 were ancestors; their product work was not lost. Obsolete one-shot patch tooling was excluded, not restored. Current remote refs checked at session start: OBS a7ea2e7 and main 496d474 only. Never force-push or delete an advanced ref based on this snapshot.

## Outstanding owner details, not blockers to unrelated work

Representative OBS-free two-PC hardware; HP ownership/unit/display/rate/payout conditions; existing hosting/backend resources; developer application/channel authorization. CHZZK-first and no gaming-PC OBS are already decided. No real-money operation or universal capture guarantee is authorized by this handoff.

At session end, record actual changed/tested SHA and CI, unverified parts and the next executable acceptance check. Treat the result as P1-01a, not the final platform or P1-01 completion.
