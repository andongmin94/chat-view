# ChatView architecture

Updated: 2026-09-12. Product authority: [PRODUCT.md](../PRODUCT.md).

**Status:** the five product goals are user-confirmed. The target design below is the proposed implementation direction, not a claim that a backend, dual-PC pairing, first-party chat, advertising, or payouts already work. This replaces the old HUD-only roadmap; it does not require rewriting the functioning native implementation.

## 1. Separate product surfaces and authority

| Component | Responsibility | Must not own |
| --- | --- | --- |
| OBS plugin | OBS menus, bounded output/scene state observation, explicit integration with the public ad source, local runtime coordination | Provider authentication, remote network waits, HP or financial decisions inside OBS callbacks |
| Desktop runtime | Native private HUD, placement/input, local OBS IPC, authenticated device/session connection | Financial authority or a requirement that OBS always run on the game PC |
| ChatView web UI | First-party chat renderer, creator dashboard, separate public ad renderer | Provider secrets or privileged desktop commands in arbitrary web content |
| Platform service | Accounts/devices, provider connections, chat delivery, broadcast sessions, campaigns, evidence evaluation, HP/reward records | Game video capture/encoding or an assumption that client telemetry proves exposure |

These are logical responsibilities, not four new services. Keep the existing plugin/HUD fault boundary. Extend the current native runtime instead of adding a second desktop framework. On a dual-PC setup the game PC may run the HUD companion without local OBS; the streaming PC hosts OBS and its local runtime connection. These are deployment roles of the same native implementation, not separate products or duplicated business logic.

The public advertising renderer is separate from the private chat HUD. Prefer OBS's existing Browser Source for the ad page rather than implementing another embedded browser. This intentionally uses OBS's existing browser-source boundary; the private WebView2 HUD remains outside OBS. Ads have a first-party renderer and reviewed image/media assets, not advertiser-supplied arbitrary JavaScript. Adding or changing a scene source requires explicit user action; do not silently change a broadcast.

## 2. Proposed deployment and stack

Start with a modular monolith: one application deployment, one PostgreSQL database, and object storage for approved banner assets. REST handles configuration; authenticated WebSocket connections deliver chat and session updates. Provider subscriptions and accounting jobs belong to the same codebase. Do not add Redis, a message broker, Kubernetes, a service mesh, or independent microservices without a demonstrated operational need.

Proposed stack:

- Native: retain C++/CMake, OBS APIs, Win32, WebView2 and DirectComposition already in this branch. The present implementation targets Windows x64; broader OS support is not user-confirmed or implemented.
- Web: TypeScript, React and Vite; reuse suitable UI work and familiar dependencies from `main` without carrying over its Electron runtime or obsolete IPC.
- Server: TypeScript on a supported Node.js LTS release, Express, schema validation using Zod, and PostgreSQL. Express/React/TypeScript/Zod already occur in `main/packages/package.json`; this is reuse of project choices, not a claim that a backend already exists on the OBS branch.
- Choose actual versions and websocket/database/auth libraries after checking their current documentation, maintenance, types, and provider protocol requirements. Pin them when adding working code, not in advance as speculative infrastructure.

Within the server, separate accounts/devices, provider connections/chat, broadcast sessions, campaigns/placements, and evidence/rewards. Keep these as modules in one application. Only create directories and interfaces when an end-to-end feature actually uses them.

Suggested future layout (not scaffolding to create now): `src/plugin`, `src/hud`, `src/config` remain native; `platform/server`, `platform/web`, and genuinely shared `platform/contracts` are introduced with the first working server/UI slice. Do not move the entire current source tree just to match a diagram.

## 3. Video and control are different paths

### Single PC

The game and native HUD compose on the streamer's monitor. OBS must capture a verified game/window/display path that excludes the private HUD while leaving it visible locally. The public ad source is then composed in OBS. Recording/stream-start and scene-transition tests must verify the actual HUD, not just a synthetic colored window.

### Dual PC

```
Game PC: game + private ChatView HUD
  game-only video ----------------> capture input on streaming PC
  ChatView device connection -----> platform service

Streaming PC: OBS + ChatView plugin/runtime
  capture input + public ad source -> audience
  OBS/session evidence -----------> platform service

Platform service: chat/session updates -> both authorized device roles
```

Use the platform's authenticated outbound connection for chat, pairing and session state first. This avoids a separate unauthenticated LAN server or hand-built discovery/pairing protocol. It does not transport game video and does not promise offline cross-PC operation. Establish bounded session leases and explicit reconnect state so disconnection cannot leave stale accounting active. Do not kill a game-PC companion merely because a local OBS PID is absent.

A pairing code is short-lived, one-use, rate-limited, and approved by the signed-in owner; it is not a permanent device credential. Do not place secrets in URLs intended for copying or logs. Each device has revocable credentials and role-scoped access. Only the assigned streaming-side authority reports a broadcast's output state; a second PC does not create duplicate ad credit.

### Clean-feed qualification is a release requirement

`WDA_EXCLUDEFROMCAPTURE` is a Windows capture hint, not a guarantee for a physical HDMI signal. If the sent video already contains composited HUD pixels, the receiving OBS plugin cannot reconstruct the obscured game from window metadata. Never claim that pairing solves this.

A first dual-PC candidate is an independently captured game-only feed, using existing OBS Game Capture and a dedicated output/projector route to a capture-card display, while the streamer sees the normal game plus HUD. This may require OBS on the game PC for video transport; it is not required merely to run the HUD companion. Hardware cost, latency, exclusive fullscreen, HDR, anti-cheat and refresh-rate behavior must be measured on the selected setup. Do not write a new game capture/encoder stack to avoid choosing a supported topology.

Simple display cloning/passthrough is not automatically safe. A capture setup that cannot meet both local visibility and stream exclusion must be marked unverified/unsupported rather than silently accepted. Keep current protective suppression until a replacement is verified, but do not call suppression successful product support. Windowed/borderless mode is an initial qualification candidate, not a claim that a top-level HUD covers every exclusive-fullscreen game.

## 4. First-party chat and authentication

Provider authorization and message subscriptions belong in the platform service, not inside OBS callbacks or page-DOM scraping. Normalize only the fields needed by ChatView's renderer, retain provider attribution and native message IDs, handle deletion/revocation where supported, and use bounded buffers/backpressure. Do not persist all chat text indefinitely by default.

One authorized provider subscription can feed both PC roles of one creator session; do not open duplicate upstream subscriptions per UI or count reconnects as new audience. Account for provider quotas and session limits before promising scale. Message presence, chat-reader health and permission to send chat are distinct states. Read-only chat must not be rejected solely because posting requires login.

CHZZK is the proposed first provider, pending owner preference and feasibility verification. Its official session API documents chat events and access scopes; its Live list documents current audience counts. The documented Live endpoint is a paginated list, not proof of an efficient per-channel polling mechanism. Verify lookup cost, quotas, consent, commercial use and a suitable supported client implementation. The session guide currently specifies Socket.IO client protocol/version constraints; do not assume the newest client is wire-compatible or vendor a deprecated stack without review.

Use normal provider authorization in the system browser with server-side callback/state validation. Use PKCE where supported; do not assume every provider supports the same grant. Store provider secrets/tokens on the server with protected credentials and revocation/deletion handling, never in the plugin binary or HUD page. Browser-only renderer sessions get short-lived, narrowly scoped authorization. The ad page cannot read private chat or request payouts, and a private HUD session cannot authorize payments. Keep privileged native IPC separate from both remote chat content and public ad content.

The existing external chat-page viewer remains the current working feature until a complete replacement slice is ready. It is not a fallback architecture to expand indefinitely. Remove replaced provider/page-health paths when their replacements ship; do not leave parallel obsolete implementations solely for compatibility.

## 5. Advertising, HP and reward authority

Required product loop: creator selects campaign/banner -> public OBS ad placement -> audience-time-related evidence -> HP progress -> reward record.

Define distinct records when this slice is implemented: campaign and approved creative, creator participation/placement, broadcast session, audience samples with source/time, ad/output evidence intervals, accounting rule version, and reward entries. Money uses integer minor units or exact decimal, never floating point. HP is a display of campaign progress; it is not itself a bank balance or proof of payment. HP ownership, display placement, conversion rate and payout eligibility remain open user decisions.

The server computes progress and rewards. Neither a browser timer nor an OBS client may submit authoritative HP deductions or balances. Use database transactions and uniqueness constraints for accepted intervals and reward entries; retries/restarts must not double-credit a placement. Cap credits against campaign budget atomically across concurrent creators. Distinguish estimated, held/rejected, confirmed, and paid states. Corrections are auditable records, not silent balance edits.

Potential accounting input is audience samples combined with eligible exposure intervals. This is only a proposal: it estimates audience time, not individual ad attention, and requires provider permission and an agreed campaign rule before becoming billable. A chat-reader count is not viewer count. A missing/stale audience sample is unknown, not zero or unlimited continuation. Multi-platform audience deduplication and per-viewer tracking must not be assumed.

OBS activity/showing flags, source readiness, transform checks, and public-renderer heartbeats provide operational evidence, not independent proof that the ad was visible to viewers. Preview/studio mode, hidden/occluded/cropped/transparent sources, transitions, output delays, client modification and duplicate devices require explicit tests. Server receipt or signed client reports do not make an untrusted client truthful. Initial real-money pilots require bounded exposure rules, fraud review and agreed evidence; do not advertise guaranteed impression verification.

On missing output/ad evidence or expired server authorization, stop accruing credit and make the ad renderer expire to transparent at its authorized deadline. Do not disrupt the game or OBS. Keep chat availability, ad delivery, and accounting status separate so an accounting outage cannot intentionally stop a broadcast. Synthetic demo audience/HP data must be visibly labeled and isolated from any payout ledger.

Before paid campaigns, validate provider API/advertising policies, available rights to audience data and derived metrics, campaign terms, disclosure, privacy and payout obligations. API availability does not mean unrestricted commercial accounting permission. In particular, YouTube documents restrictions on derived metrics and permissions; do not apply a cross-platform watch-time formula without review. This is a release gate, not a decision to discard G5.

## 6. Existing runtime baseline to preserve

Code baseline: `c82fce0e06baafd6cef6d8a1a5db9752e422ce23` on `obs-preflight-work` before this documentation change.

The implemented branch has a native OBS controller, an external WebView2 HUD, native configuration/diagnostics, a versioned local status transport, page URL normalization, placement persistence, recovery policies, and Windows tests. It does not implement the platform design above.

Retain these useful constraints:

- Keep cloud/network/browser work off OBS callback threads and preserve bounded unload/restart behavior and crash-loop protection.
- Local plugin/HUD package contracts are versioned and released together. Do not retain obsolete layouts. Current status uses named shared memory/events scoped to session/PID/random token; re-evaluate access control when adding privileged commands.
- Preserve transparent DirectComposition rendering, click-through/non-activating locked mode, edit-mode move/resize, and monitor-relative DIP placement.
- Preserve capture verification around show/resume and bounded suspend/lock recovery. Never weaken privacy tests merely to turn CI green.
- Treat external pages as untrusted. Current code accepts bounded page-health reports; it does not expose general ChatView control or payout functions. A closed Shadow DOM is UI isolation, not a security boundary.
- Current UI says READY TO STREAM, but it evaluates ChatView-local conditions, not all broadcast health or viewer delivery. Narrow the claim in the next relevant UI change.

## 7. Documentation and implementation changes

Use [development-plan.md](development-plan.md) for milestones, test evidence and handoffs. Each architectural change must say which product goal it serves, what working behavior it preserves, and how completion is observed. Do not add speculative abstractions to prebuild all future modules. Do not equate a dated branch name or a successful unit suite with release approval.

## Primary references checked 2026-09-12

These establish API behavior, not support claims for ChatView. Recheck before implementation and paid release.

- OBS sources: https://obsproject.com/kb/sources-guide
- OBS Game Capture: https://obsproject.com/kb/game-capture-source
- OBS Browser Source: https://obsproject.com/kb/browser-source
- Windows capture affinity: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity
- CHZZK sessions: https://chzzk.gitbook.io/chzzk/chzzk-api/session
- CHZZK Live: https://chzzk.gitbook.io/chzzk/chzzk-api/live
- YouTube policies: https://developers.google.com/youtube/terms/developer-policies
- YouTube policy guidance: https://developers.google.com/youtube/terms/developer-policies-guide
- PostgreSQL transactions: https://www.postgresql.org/docs/current/tutorial-transactions.html
