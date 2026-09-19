# ChatView — OBS-based broadcasting platform

챗뷰는 하나의 화면에서 게임·작업과 채팅을 함께 보는 개인용 투명 HUD를 출발점으로, 원컴·투컴 방송 지원, 자체 채팅 경험, 스트리머가 선택하는 광고와 시청시간 기반 HP·수익을 연결하는 플랫폼을 목표로 합니다.

**Start here:** [Product goals](PRODUCT.md) · [Architecture](docs/architecture.md) · [Development handoff](docs/development-plan.md) · [Contributor instructions](AGENTS.md)

**Active development: OBS.** main remains the original Electron product and UX reference, not a native merge target. CHZZK is the first own integration. Dual-PC operation must not require OBS on the gaming PC. Do not mistake today's single-PC implementation for the final product scope.

## Current implementation

The native Windows branch provides an OBS controller, separate Win32/WebView2 transparent HUD, configuration/diagnostics, external chat URL normalization, click-through/edit modes, DPI/placement persistence, bounded recovery and tests. Inspect the actual code commit's CI before selecting a package; [the handoff](docs/development-plan.md) separates exact validation results, documentation-only commits and unresolved hardware checks.

The [CHZZK integration](platform/README.md) authenticates a creator, subscribes to CHAT events and produces bounded, validated messages. A [scoped display gateway](docs/display-delivery.md) shares that upstream session with read-only displays. The [native display client](docs/native-display-client.md) connects the gateway to the existing transparent HUD through WinHTTP and the shared first-party text renderer. The developer browser preview remains available, but its web page is not loaded into the native viewer and no provider secret is sent to the HUD.

**This is a developer connection path, not a deployed multi-user service.** Real CHZZK authorization/messages, production accounts and device renewal, the OBS-independent gaming companion, demonstrated OBS-free dual-PC clean video, public ad campaigns, HP accounting and payouts remain separate open gates. Synthetic tests and CI passes do not certify those capabilities.

## Install a validated native package

Get `chat-view-obs-windows-x64.zip` from a successful Windows workflow, extract it, close OBS and run `install.cmd` with administrator approval. Start OBS and open **Tools → ChatView Settings...**. The default target is `C:\Program Files\obs-studio`.

For another root, run PowerShell in the package:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

Use `uninstall.cmd` or `uninstall.ps1 -ObsPath "D:\Apps\obs-studio"` to remove installation files. `%LOCALAPPDATA%\ChatView` settings remain. The installer checks the manifest, WebView2 Runtime, local capture exclusion and plugin loading. These checks do not establish support for every OBS source, game, GPU or HDMI/capture-card path.

## Connect first-party chat — developer validation

With the OBS-started HUD running, press **Ctrl + Alt + Shift + C** to open its native connection panel. Enter the service origin and a fresh one-use display key from the creator-authorized management page, then choose **연결**. HTTPS is required by default. The existing loopback developer probe requires explicit local-server opt-in and the literal `http://127.0.0.1:47831` origin; localhost aliases and LAN HTTP are not accepted.

The masked key is cleared on use/close and is not saved. Closing the panel keeps the connection; **연결 종료** clears the private display. Server revocation, expired authorization or transport loss also end delivery. These short display leases are not a requirement for final users to reconnect manually every five minutes: durable account/device enrollment and renewal are not implemented yet.

See [the native connection instructions and test scope](docs/native-display-client.md) and [private developer setup](platform/README.md). Ordinary streamers must not receive developer client secrets or be asked to run this probe as a public server. Never paste provider credentials into the connection panel, chat, commits or diagnostics. Its key is a limited chat-read capability, not a CHZZK password or access token.

## Configure the existing external-page HUD

Enter a supported broadcast or chat URL in **Tools → ChatView Settings...**:

```text
https://chzzk.naver.com/live/<channel-id>
https://chzzk.naver.com/chat/<channel-id>
https://www.sooplive.com/station/<channel-id>
https://play.sooplive.com/<channel-id>/<broadcast-number>
https://play.sooplive.com/<channel-id>?vtype=chat
https://www.youtube.com/watch?v=<video-id>
https://youtu.be/<video-id>
https://www.youtube.com/live_chat?is_popout=1&v=<video-id>
https://weflab.com/page/...
```

Saving normalizes and applies supported HTTPS/host/path/identifier combinations without restarting OBS. Navigation is bound to the configured chat document and new windows are suppressed. Changing these settings replaces the owned first-party document and ends its old delivery; private chat is never forwarded into the replacement external page. This existing layer is retained until its first-party replacement is accepted end to end.

The current **READY TO STREAM / BLOCKED** label describes local ChatView checks, not audio/encoding/service delivery or actual viewer exposure. Complete provider login inside the restricted external viewer is not guaranteed. Do not enter the developer probe URL here: native first-party delivery uses the separate connection panel, not an exception to external-page URL policy.

## Move, resize and lock

Use **Ctrl + Alt + Shift + H** to toggle edit mode, drag the header or edges/corners, then toggle again to save and restore non-activating click-through behavior. Bounds are monitor-relative DIP values in `%LOCALAPPDATA%\ChatView\hud.ini`, clamped to an available display after monitor changes. Control Center interaction requests are validated by the HUD itself.

## Runtime and capture limitations

OBS loads `chat-view-obs.dll`, which supervises external `chat-view-hud.exe`; `chat-view-config.exe` is the native control center. The current local HUD exits with its OBS parent. Gaming-PC-independent session ownership remains to implement, reusing the existing renderer/input/placement/recovery modules. The new network worker is independent of OBS and WebView objects, but that does not make the shipping executable an independent companion yet.

`WDA_EXCLUDEFROMCAPTURE` is an OS capture hint, not a way to remove pixels already composited into HDMI video. The current Display Capture interlock may hide the HUD during output risk. This is protective degradation, **not** completion of the requirement that the streamer keep reading the HUD while it is absent from the broadcast.

Qualify actual-HUD visibility and stream exclusion together. Single-PC and dual-PC tests are separate; gaming-PC OBS is prohibited for the dual-PC requirement. Pairing, native text delivery and synthetic compositor fixtures are not proof of a clean hardware video feed. Do not bypass protection to claim support.

## Diagnostics

Use **Export diagnostics** in Control Center or **Tools → Export ChatView Diagnostics...**. The active local exporter includes status/restart summaries and ChatView-tagged log data, omits configured URLs/chat text and redacts profile paths/IPC names. It uploads nothing automatically. Review exports before sharing. Native chat connections are separate user-initiated network activity; their tokens, URLs, server error bodies and chat text are not added to product logs. Production account operation requires its own data handling.

## Native build

Windows 10 version 2004+, Visual Studio 2026 C++ tools, Windows SDK 10.0.26100, CMake 3.28+, an OBS development prefix and restored WebView2 SDK are required. The current workflow pins OBS 32.2.2 Windows x64; inspect it before upgrading. With testing enabled, also install a supported Node 22.16+ or Node 24 and the locked platform dependencies for the actual native HTTP/WebSocket fixture. Node is a development/test prerequisite, not a new runtime shipped to streamers.

```powershell
npm --prefix platform ci --ignore-scripts
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"
$env:WEBVIEW2_SDK_DIR = ./scripts/restore-webview2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --output-on-failure
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is `dist/`; `.github/workflows/windows-build.yml` defines packaging/installer/official-OBS checks. Native CTest alone is not a complete package workflow. The separate `CHZZK contract` workflow audits the locked graph and tests the provider/session/gateway/renderer and strict types, not real CHZZK access or capture safety. The native integration test adds actual WinHTTP, native controls and WebView2 to a synthetic gateway fixture; its assertions are not live-provider certification.

Source layout: `src/common`, `src/plugin`, `src/hud`, `src/config`, `src/diagnostics`, `src/preflight`; native tests in `tests/`; dependency/installation scripts in `scripts/`; CHZZK API/session, scoped display delivery, developer tools, shared renderer and tests in `platform/`; product and handoff documents in the root and `docs/`.
