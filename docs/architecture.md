# ChatView architecture

Updated: 2026-09-28. Authority: [PRODUCT.md](../PRODUCT.md). Cadence: [development-workflow.md](development-workflow.md). Evidence and next work: [development-plan.md](development-plan.md). This document defines boundaries, not another task queue.

## Product structure

The HUD is the entry benefit, OBS integration and dual-PC operation are the broadcasting environment, first-party chat is the platform experience, and creator-selected ads/HP/rewards are the business loop. None replaces another. CHZZK is first; the gaming PC must not require OBS, including portable/headless/projector variants or another full broadcasting application. main remains the original single-screen UX reference.

| Boundary | Responsibility | Must not own |
| --- | --- | --- |
| OBS plugin on streaming PC | Menus, bounded output/scene observation, local runtime coordination, future public ad-source integration | Remote requests or financial calculations blocking OBS callbacks |
| Native HUD/companion | Private rendering, input, placement, local lifecycle, authenticated display connection | Provider secrets, authoritative rewards, universal local-OBS dependency |
| First-party UI | Private chat, creator management and separate public ad renderer | Arbitrary-page privileged native access or payout credentials in display |
| Platform application | Accounts/devices, provider subscriptions, shared chat, sessions, campaigns/evidence/rewards | Game-video transport or blind trust in client exposure reports |

The C++/Win32/WebView2/DirectComposition runtime and OBS/HUD fault boundary are retained. Provider/session/gateway modules, shared text rendering, WinHTTP and the HTTPS creator service are implemented development components, not a deployed service or live-channel certification.

## One runtime, two lifecycle roles

OBS-controlled launch validates its parent and local shared memory/events and exits with that parent. Explicit `--companion` skips local OBS transport, shows the development/privacy warning, opens the connection panel after readiness and provides local quit/single-instance handling. Invalid OBS arguments do not fall back to companion. Both reuse the same HUD, input, placement, recovery and authenticated display code.

The validated launch selects `gaming` for companion and `streaming` for OBS-managed runtime. Native login sends this intent in a mandatory role header; the public service fixes it before opening the browser. Browser authentication and explicit channel/role consent create authority, not the header alone. The browser cannot replace the requested role through a role-selection URL. Renewal with a different role is rejected before replacing its lease; the native client retains the existing saved approval and asks for the original mode or explicit logout/reconsent.

Independent logins of the same CHZZK creator share one logical session and upstream; other creators have isolated access/gateways. SQLite commits approval and role atomically, allows one streaming role and supports owner-scoped revocation. This is not hardware registration. Short display bearers confer only private chat. The existing gateway includes each recipient's own membership and counts distinct authorized open display connections in that same session. The native connection panel shows the approved role, session ID and those counts; it clears them on loss, retry, cancellation or expiry. Metadata is removed before publishing to the chat renderer.

Open server-side sockets are not remote OBS attestation, output start/end, verified video exclusion or viewers. Half-open remote connections may remain counted until their transport/lease is retired. Capture state stays `unverified`. Only the assigned streaming role may gain future scoped OBS observation authority. Extra HUDs never become audience/reward sources. Role lifecycle and the non-remembered logout gap are tracked in the plan, not hidden by the connection indicator.

## Control data is not game video

```
Gaming PC: game + ChatView companion, NO OBS
  private HUD -> streamer's monitor
  separately qualified HUD-free video -> streaming PC
  authorized chat/session <-> ChatView service

Streaming PC: OBS + ChatView plugin
  game video + creator-selected public ad -> audience
  authorized output/session evidence <-> ChatView service
```

The OBS-free clean-video mechanism remains an engineering/hardware feasibility requirement. Do not claim a selected or proven implementation. A composited HDMI clone already containing HUD pixels cannot be cleaned by receiving-side metadata. Windows capture affinity is not physical-HDMI exclusion. Investigate maintained native OS capture/output facilities before capture hooks, drivers, custom encoders or another broadcasting suite.

Support requires simultaneously readable local chat and HUD-free recorded output on a documented topology without gaming-PC OBS. Record GPU, wiring/capture device, window mode, DPI/HDR/refresh rate, latency and resources. Session/role work may proceed without hardware; it does not close this requirement. Protective HUD suppression remains until a verified path replaces it and never counts as readable-HUD success.

## First-party chat and service growth

Provider authorization/subscriptions stay server-side, outside OBS callbacks. One creator's upstream feeds its permitted devices. Preserve available attribution/event semantics, bound buffers and avoid indefinite chat persistence. Reading/posting permissions differ. External-page support remains usable until replacement works; remove replaced obsolete paths instead of maintaining permanent fallbacks.

The service uses TypeScript, Node HTTP/HTTPS, locked Socket.IO/Engine.IO and ws; the renderer uses shared JavaScript. `platform/server/main.mts` composes existing modules with creator and browser/account routes. Identity comes from the provider user endpoint, never a submitted channel. No new desktop/server framework is required.

`ProviderGrants` owns encrypted records on the existing `SessionStore` SQLite connection. Opaque native approval hashes/membership remain separate from provider capabilities. AES-256-GCM binds encrypted channel/token/absolute-expiry records to owner and revision; the operator key lives outside the DB volume. Startup validates it without replacement. Native renewal restores only its owner, checks provider identity and shares one upstream. Loading does not extend expiry. Browser cookies, pending approvals and short bearers are process-local; chat is not persisted.

Provider refresh is single-flight and commits an unavailable/claimed state before network I/O. Replacement uses a revision-checked write; late responses cannot overwrite reauthorization or revive revoked authority. Crash/ambiguous responses require reauthorization, never replay of possibly spent tokens. Confirmed provider denial/revocation atomically removes encrypted grants and dependent approvals. Temporary chat failure after rotation preserves the durable replacement. This is single-instance: DB compare-and-set is not distributed gateway revocation or stale-backup rollback protection.

Keep one application with separate account, provider/chat, session, campaign and accounting concerns. PostgreSQL/asset storage remain proposals for real requirements. React/Vite/Express/Zod are choices, not mandatory rewrites. No speculative microservices, queues, Redis, Kubernetes or empty modules.

Provider credentials belong in protected configuration. System-browser authorization validates callback state and supported provider security mechanisms. Direct TLS never trusts forwarded headers to authorize plaintext. Browser cookie/CSRF authority is separate from native challenge/renewal/bearers; management pages never run in the private display. The private developer probe is not the public service. Public rendering, private display and account management remain distinct scopes. [Platform usage](../platform/README.md), [delivery](display-delivery.md) and [native client](native-display-client.md) describe mechanisms, not product/release policy.

## Advertising is separate public output and server authority

Required flow: creator selects approved banner/campaign -> public OBS ad source -> eligible audience-time evidence -> HP -> reward record. Prefer OBS Browser Source with first-party rendering and approved assets. No arbitrary advertiser JavaScript, private-HUD ad accounting or silent scene changes.

The server owns budgets, accepted intervals, versioned rules and exact monetary records. Clients report observations, not HP deductions/balances. Use transactions, idempotency and atomic caps; separate estimates, held/rejected evidence, confirmed rewards and paid amounts with auditable corrections. Synthetic demos cannot enter payable accounting.

Integrated audience counts are at most estimates, not measured individual attention. Unknown/stale samples are not verified exposure. OBS activity/showing flags, transforms and render heartbeats cannot independently prove visibility. Preview, hiding, occlusion, cropping, source changes, duplicate devices and modified clients need explicit treatment. Data rights, campaign rules and fraud/disclosure/privacy/payout checks precede real money without cancelling the business loop. HP units, ownership, placement and payout remain owner decisions; no invented zero-HP payout rule.

Account/ad outages stop unsupported accrual and expire unauthorized ads without stopping OBS or intentionally breaking private chat. Gameplay, display availability and accounting outcomes remain separate.

## Maintained principles

Keep OBS callbacks bounded and preserve bounded restart/crash-loop and shutdown behavior. Privileged visibility belongs to the HUD. Local IPC stays local. Renderer acknowledgements are neither authentication nor advertising evidence; closed Shadow DOM is not a security boundary. Routine checks and release qualification follow development-workflow.md, not another global gate.
