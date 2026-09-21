# ChatView architecture

Updated: 2026-09-21. Authority: [PRODUCT.md](../PRODUCT.md). Cadence: [development-workflow.md](development-workflow.md). Current evidence and next work: [development-plan.md](development-plan.md). This document defines boundaries, not another task queue.

## Product structure

The HUD is the entry benefit, OBS integration and dual-PC operation are the broadcasting environment, first-party chat is the platform experience, and creator-selected ads/HP/rewards are the business loop. None is an optional replacement for another. CHZZK is first; the gaming PC must not require OBS, including portable/headless/projector variants or another full broadcasting application. main remains the original single-screen UX reference.

| Boundary | Responsibility | Must not own |
| --- | --- | --- |
| OBS plugin on the streaming PC | Menus, bounded OBS output/scene observation, local runtime coordination, future public ad-source integration | Remote requests or financial calculations that block OBS callbacks |
| Existing native HUD/companion | Private rendering, input, placement, local lifecycle, authenticated display connection | Provider secrets, authoritative rewards, a universal local-OBS dependency |
| First-party UI | Private chat, creator management and a separate public ad renderer | Arbitrary-page privileged native access or payout credentials in the display |
| Platform application | Accounts/devices, authorized provider subscriptions, shared chat delivery, broadcast sessions, campaigns/evidence/rewards | Game video transport through the chat service or blind trust in client exposure reports |

The existing C++/Win32/WebView2/DirectComposition runtime and the separate OBS/HUD fault boundary are retained. CHZZK API/session modules, shared JavaScript text rendering, scoped delivery and WinHTTP receiving are implemented development components, **not unimplemented proposals**. The HTTPS creator service is also implemented as a development slice; neither is deployed multi-user or live-channel certification.

## One runtime, two lifecycle roles

OBS-controlled launch validates its parent and local shared memory/events and exits with that parent. Explicit `--companion` skips local OBS transport, shows a development/privacy warning, opens the existing connection panel after readiness and provides local quit/single-instance handling. Invalid OBS arguments do not fall back to companion. Both roles reuse the same HUD, input, placement, recovery and authenticated display code. Browser-approved server roles do not yet connect the native runtime's launch role or remote OBS capture state.

The service extends the existing browser login and renewable ChatView approval with `gaming`/`streaming` membership. Independent browser logins identified as the same CHZZK creator join one logical session and one authorized upstream; other creators have isolated access and gateways. SQLite commits the renewable approval and role atomically, permits one streaming role, and supports owner-scoped revocation. This is not hardware registration. Short display bearers still confer only private chat. The authenticated membership query reports capture state as `unverified`; it does not implement broadcast start/end, prove a runtime is OBS, or report clean video. Only the assigned streaming side may gain the future scoped OBS observation authority; extra HUDs are not audience or reward sources.

## Control data is not game video

```
Gaming PC: game + ChatView companion, NO OBS
  private HUD -> streamer's monitor
  separately qualified HUD-free video -> streaming PC
  authorized chat/device session <-> ChatView service

Streaming PC: OBS + ChatView plugin
  game video + creator-selected public ad -> audience
  authorized output/session evidence <-> ChatView service
```

The OBS-free clean-video mechanism is still an engineering/hardware feasibility requirement. Do not claim a selected or proven implementation. A composited HDMI clone already containing HUD pixels cannot be cleaned by receiving-side window metadata. Windows capture affinity is not physical-HDMI exclusion. Investigate maintained native OS capture/output facilities before introducing capture hooks, drivers, custom encoders or another broadcasting suite.

Support requires simultaneously readable local chat and HUD-free recorded output on a documented topology with no gaming-PC OBS. Record GPU, wiring/capture device, window mode, DPI/HDR/refresh rate, latency and resource use. Creator/session and two-PC role work may proceed while hardware is unavailable; that does not close this requirement. Current protective HUD suppression remains until a verified supported path replaces it, and is never counted as success of the readable-HUD goal.

## First-party chat and service growth

Provider authorization/subscriptions remain on the service side, outside OBS callbacks. One creator's authorized upstream feeds its permitted devices. Preserve available attribution/event semantics, bound buffers and avoid indefinite chat persistence. Reading and posting permissions are different. External-page support remains usable until its replacement works; remove replaced obsolete paths together rather than maintaining a permanent fallback architecture.

Current server modules use TypeScript, Node HTTP/HTTPS, the locked Socket.IO/Engine.IO client and ws; current renderer uses shared JavaScript. `platform/server/main.mts` composes the existing provider/session/gateway with one creator registry and browser/account routes. Creator identity comes from the server-side provider user endpoint, not a native-submitted channel. Provider refresh is single-flight; ambiguous one-use refresh failures require reauthorization rather than replay. Epoch checks prevent late authorization from restoring a revoked context. Provider tokens remain process-local in this slice; only opaque app-approval hashes and role membership persist. Protected provider persistence and operational hosting are not supplied by this mechanism.

Do not rewrite working modules just to match an earlier React/Express diagram. Keep one application, internally separated accounts/devices, provider/chat, sessions, campaigns and evidence/reward modules. PostgreSQL and asset storage remain deployment proposals to introduce with real persistence/assets, not evidence of an existing backend. React/Vite/Express/Zod are available choices from main's ecosystem, not mandatory new dependencies for every layer. No speculative microservices, queues, Redis, Kubernetes or empty modules.

Provider credentials belong in protected server configuration; system-browser authorization validates callback state and provider-supported security mechanisms. The HTTPS entry point terminates TLS directly and does not trust forwarded headers to authorize plaintext. Per-browser cookies and CSRF are separate from native challenge/renewal/bearer capabilities; management pages never run inside the private display. The local developer probe remains private and separate. Public renderer, private display and account management have distinct scopes. [Platform usage](../platform/README.md), [display-delivery.md](display-delivery.md) and [native-display-client.md](native-display-client.md) describe current mechanisms; current limits are not permanent product UX requirements.

## Advertising is a separate public output and server authority

Required flow: creator selects an approved banner/campaign -> public OBS ad source -> eligible audience-time-related evidence -> HP progress -> reward record. Prefer the existing OBS Browser Source with first-party rendering and approved assets. Do not embed arbitrary advertiser JavaScript, combine public ads with the private HUD, or silently change the creator's scene.

The server owns campaign budgets, accepted evidence intervals, versioned accounting rules and exact monetary records. A client reports observations, never authoritative HP deductions or balances. Use transactions, idempotency and atomic budget caps when implementing this flow; separate estimates, held/rejected evidence, confirmed rewards and paid amounts, with auditable corrections. Synthetic demos cannot enter payable accounting.

Audience counts integrated over eligible intervals are at most a candidate estimate, not measured individual attention. Unknown/stale samples are not verified exposure. OBS showing/activity flags, transforms and render heartbeats do not independently prove visibility; preview, hiding, occlusion, cropping, source changes, duplicate devices and modified clients require explicit treatment. Provider data rights, agreed campaign rules and fraud/disclosure/privacy/payout checks are prerequisites to real money, not reasons to omit the business loop from development. HP unit, shared/per-creator ownership, placement and payout semantics remain owner decisions; do not invent a zero-HP payout rule.

Account/ad outages stop unsupported accrual and expire unauthorized ads without stopping OBS or intentionally breaking private chat. Keep gameplay, display availability and accounting outcomes separate.

## Maintained principles

Keep OBS callbacks bounded and preserve bounded restart/crash-loop and shutdown behavior. Privileged window visibility stays owned by the HUD. Local IPC remains local, not cross-PC networking. Renderer acknowledgements are not authentication or advertising evidence. Closed Shadow DOM is not a security boundary. Routine checks and release qualification follow development-workflow.md; this architecture does not introduce another global validation gate.
