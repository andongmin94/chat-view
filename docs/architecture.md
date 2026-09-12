# ChatView architecture

Updated: 2026-09-12. Product authority: [PRODUCT.md](../PRODUCT.md). Work state: [development-plan.md](development-plan.md).

## Confirmed requirements and implementation status

The five product goals, CHZZK-first integration, and **no OBS on the gaming PC in dual-PC operation** are owner-confirmed. OBS is the active development/integration branch; main remains the original UX reference. Other target choices below are proposals, not claims of implemented platform capabilities.

The native controller/HUD/configuration/recovery/diagnostics/installer/test implementation is retained. Platform accounts, first-party ingestion, dual-PC companion operation, public campaigns and reward accounting have not yet been implemented. Do not restart the native project to add them.

## 1. Boundaries and ownership

| Component | Responsibility | Exclusions |
| --- | --- | --- |
| OBS plugin, on the streaming PC | OBS menus; bounded output/scene/ad-source state; local runtime coordination | Blocking cloud/auth requests and financial calculations in OBS callbacks |
| ChatView native companion | Transparent private HUD, input/placement, desktop/session connection; local OBS IPC where OBS exists | Requirement for local OBS in the gaming-PC role; authoritative HP or balances |
| ChatView web UI | Own chat renderer, creator dashboard, separate public ad renderer | Provider secrets or arbitrary-content access to privileged native commands |
| Platform application | Accounts/devices, provider connections/chat delivery, broadcast sessions, campaigns/evidence/rewards | Game-video transport through the chat service; treating client reports as independent proof |

These are responsibility boundaries, not four microservices. Reuse the existing native HUD process rather than adding Electron or a second native renderer. The same implementation acquires explicit single-PC, gaming-PC and streaming-side roles; avoid duplicated product/business logic.

Keep private chat outside OBS's browser/rendering process. Public ads use a separate OBS source, preferably OBS's existing Browser Source rather than a new browser engine. The ad page is first-party code with approved image/media assets, not arbitrary advertiser scripts. Source addition/scene edits require the creator's explicit action; never silently rearrange a broadcast.

## 2. Proposed stack and deployment

Keep C++/CMake/OBS APIs/Win32/WebView2/DirectComposition already present. Current implementation targets Windows x64, not every desktop OS. Web UI: TypeScript/React/Vite. Server: supported Node.js LTS, Express/Zod, PostgreSQL, and object storage for approved assets. These web choices build on main's existing ecosystem; do not reintroduce its obsolete Electron runtime/IPC. Check current dependency documentation/types and protocol requirements before adding pinned versions.

Start with one deployable application and database. Separate accounts/devices, provider/chat, broadcast sessions, campaigns/placements and evidence/rewards in code. REST is proposed for configuration and authenticated WebSocket for delivery/session updates. No speculative Redis, broker, Kubernetes, service mesh or empty plugin framework.

Keep current src/plugin, src/hud, src/config and tests in place. Introduce platform/server, platform/web and truly shared contracts only with a working first-party slice. Do not reorganize the whole native tree for a diagram.

## 3. Single-PC and dual-PC operation

Single PC: game plus private HUD on the local monitor; a verified OBS capture path excludes the HUD; a separate public ad source is composed for the audience. Check actual HUD visibility/exclusion during output start and scene transitions, not only a colored fixture.

Dual PC target:

```
Gaming PC: game + ChatView companion (NO OBS)
  private HUD -> streamer's screen only
  independently qualified clean video -> streaming-PC capture input
  authenticated ChatView connection -> platform service

Streaming PC: OBS + ChatView plugin/local runtime
  capture input + selected public ad source -> audience
  bounded OBS/session evidence -> platform service

Platform service: authorized chat/session data -> both device roles
```

OBS installed/running on the gaming PC is prohibited, including portable/headless OBS and OBS Game Capture/projector routing. The previous game-PC OBS proposal is withdrawn. Do not silently require a different full broadcasting application instead. No such dependency may be buried in the companion installer.

The existing HUD currently requires a local OBS parent and local shared-state mapping. Preserve that working single-PC behavior until a tested role-aware lifecycle replaces the universal assumption. Gaming-PC mode must have its own authorized session lifecycle without a local OBS PID. Reuse rendering, input, placement and recovery modules; do not treat local shared memory itself as network transport.

Use authenticated outbound platform connections first for pairing, chat and session state; avoid a hand-built unauthenticated LAN listener. Pairing codes are short-lived, one-use, rate-limited and owner-approved; devices receive revocable scoped credentials. Bounded leases prevent stale session state or continued ad credit after disconnect. Only the assigned streaming role reports authoritative session output state; a second HUD/device is not additional audience.

### Clean video without gaming-PC OBS: open feasibility gate

Pairing does not remove pixels. Windows WDA_EXCLUDEFROMCAPTURE addresses certain OS capture APIs, not every output path. If HDMI duplication already includes the HUD, receiving-side metadata cannot reconstruct the hidden game. Do not claim privacy for that topology merely because local affinity checks pass.

Evaluate a supported native OS capture/exclusion path and separate clean output that can operate within ChatView's companion without OBS. Verify existing maintained components and Windows capabilities before choosing a library or adding code. This is a candidate direction, not a selected/proven capture mechanism or a commitment to implement game hooks, drivers, a custom encoder or another streaming suite. Existing code contains no demonstrated OBS-free dual-PC clean-feed solution.

Acceptance requires actual gaming-PC hardware with OBS absent, simultaneous readable HUD and HUD-free recorded feed, input behavior, acceptable measured resource overhead/latency, and start/transition/reconnect tests. Record refresh rate, HDR/DPI, window mode, GPU and capture hardware. Do not assume exclusive-fullscreen coverage or all anti-cheat compatibility. If a topology fails, investigate another within the confirmed constraint; do not relax no-gaming-PC-OBS without owner instruction.

Current Display Capture suppression remains a protective limitation until a verified replacement works. Hiding the HUD is not completion of the private-HUD use case. Its policy module is reusable; its blanket behavior and UI claims need revision, not deletion without evidence.

## 4. CHZZK-first own chat

CHZZK is selected, not an unresolved preference. Authenticate and subscribe through official supported APIs; the platform service owns credentials/subscriptions and the renderer displays messages rather than scraping a provider page. Normalize only needed fields and retain attribution, message IDs and deletion/revocation behavior where supplied. Use bounded buffers/backpressure and no indefinite chat retention by default.

One creator subscription should serve the creator's authorized devices; duplicate HUDs must not create duplicate upstream subscriptions or ad credit. Official session docs specify connection limits and Socket.IO client protocol/version constraints. Check a suitable maintained implementation rather than assuming a current client is compatible or copying a deprecated dependency unchecked. Verify real channel authorization, audience-query feasibility and data-use permission separately. Real chat access does not prove billable watch-time measurement.

Use system-browser provider authorization with validated server callback/state, and PKCE where supported. Keep secrets/provider tokens off the plugin and pages. Public ads, private HUDs and dashboard accounts receive distinct scoped authorization. No payout/native control functions exposed to arbitrary web content. Posting permission and ability to read are separate; a sign-in-to-post prompt must not block a readable chat.

Keep the existing external-page feature usable until the first-party path works end to end. Then remove the replaced provider/DOM-health paths instead of indefinitely maintaining two architectures. Reusing WebView2 for ChatView-owned UI does not require retaining external-page DOM heuristics.

## 5. Advertising, HP and accounting

Required loop: creator selects campaign/banner -> public OBS placement -> audience-time-related evidence -> HP progress -> reward record. The business model remains a core goal while technical feasibility is validated early.

Server-side modules will own approved campaigns/creatives, creator placements, broadcast sessions, timestamped audience/ad evidence, accounting rule versions and reward records. HP represents progress, not a bank balance; its ownership, unit, display audience and payout relation remain unspecified. Do not invent a zero-HP payout requirement.

The client reports bounded evidence, not authoritative deductions or money. Use exact monetary units, database transactions, uniqueness/idempotency and atomic campaign-budget caps across simultaneous creators. Distinguish estimates, held/rejected records, confirmed rewards and paid amounts; corrections need audit records. Synthetic demo data cannot enter payable accounting.

Audience samples times eligible intervals are at most a candidate estimate, not proof of individual ad attention or cross-platform unique viewers. Unknown/stale samples remain unknown. Confirm provider permission and campaign terms before billing. Do not automatically apply one platform's metric rules to others.

OBS source showing/activity, renderer heartbeats and transforms are operational evidence, not proof against occlusion, cropping, studio preview, modified clients or fraudulent viewers. Test these cases and define bounded pilot review. Server receipt or signatures do not make client reports true.

On expired ad authorization or missing evidence, stop accrual and let the public renderer expire to transparent. Do not stop the broadcast or intentionally break private chat because accounting is unavailable. Before actual money, confirm data/commercial permissions, campaign rules, anti-fraud review, privacy/disclosure and payout obligations.

## 6. Preserve versus change

| Existing asset | Treatment |
| --- | --- |
| OBS controller/callback and external HUD boundary | Preserve; add only necessary platform/session coordination |
| WebView2/DirectComposition transparency; click-through/edit; DIP placement | Reuse for first-party UI and gaming-PC companion |
| Bounded restart/circuit, persisted telemetry, lock/suspend recovery | Preserve and adapt lifecycle ownership where required |
| Local shared memory/event transport | Keep for local OBS integration; do not pretend it connects two PCs |
| Installer, package integrity, diagnostics redaction and native tests | Preserve; adjust packages/evidence only for real new roles |
| Readiness/login interpretation and blanket capture suppression | Fix specific policy/claim issues; retain useful recovery and safeguards |
| External page/DOM health implementation | Current usable feature; remove only when replaced end to end |
| One-shot patch workflows and staged payloads | Obsolete tooling; exclude from the consolidated tree |

Closed Shadow DOM is UI isolation, not a security boundary. Current code accepts bounded page-health reports; documentation must not claim it registers no page-to-native messages. Current READY TO STREAM text describes local ChatView checks, not audio/encoder/service delivery or independent ad exposure.

## References

Behavioral references, not evidence of completed ChatView support. Recheck before implementation/release.

- Windows capture affinity (checked 2026-09-12): https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity
- CHZZK sessions (checked 2026-09-12): https://chzzk.gitbook.io/chzzk/chzzk-api/session
- OBS source APIs: https://docs.obsproject.com/reference-sources
- OBS Browser Source: https://obsproject.com/kb/browser-source
- CHZZK Live API: https://chzzk.gitbook.io/chzzk/chzzk-api/live
- YouTube policies: https://developers.google.com/youtube/terms/developer-policies
- PostgreSQL transactions: https://www.postgresql.org/docs/current/tutorial-transactions.html
