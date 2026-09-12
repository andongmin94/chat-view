# ChatView — OBS-based broadcasting platform

챗뷰는 한 화면에서 게임·작업과 채팅을 함께 보는 개인용 투명 HUD를 출발점으로, 원컴·투컴 방송 지원, 자체 채팅 경험, 스트리머 선택 광고와 시청시간 기반 HP·수익을 연결하는 플랫폼을 목표로 합니다.

**Start here:** [Product goals](PRODUCT.md) · [Architecture](docs/architecture.md) · [Development plan / handoff](docs/development-plan.md) · [Contributor instructions](AGENTS.md)

**Active development: OBS.** CHZZK is the first first-party integration. In dual-PC operation, the gaming PC must not need OBS installed or running; OBS belongs on the streaming PC. The existing native HUD is retained and extended rather than rewritten.

main is the original Electron product and UX reference, not a native-code integration target. Old preflight/telemetry/probe branches are no longer separate development lines after consolidation; their implementation history is retained in OBS. Do not mistake the current single-PC implementation for the final product scope.

## Current implementation status

The source contains a Windows OBS frontend plugin, an out-of-process Win32/WebView2 transparent HUD, native settings/diagnostics, URL normalization for Weflab/CHZZK/SOOP/YouTube, click-through/edit modes, placement persistence, bounded recovery, persisted restart telemetry and native tests.

**Not implemented yet:** platform accounts/backend, own CHZZK message ingestion/UI, an OBS-independent gaming-PC session, a verified OBS-free dual-PC clean feed, public campaigns connected to an ad renderer, HP accounting and payouts. The architecture describes targets, not working features.

Consolidation preserves the latest work branch's runtime source, tests, build configuration and maintained scripts/CI. It updates requirements and excludes obsolete one-shot patch tooling. The last historically reviewed Windows run at native baseline c82fce0 passed 22 native tests and repeats but failed official OBS capture qualification. Read [the handoff](docs/development-plan.md) and inspect the current OBS workflow before choosing a package. Integration is not release approval.

## Install a successfully validated package

1. Obtain chat-view-obs-windows-x64.zip from a successful Windows workflow run and extract it once.
2. Close OBS Studio.
3. Run install.cmd and approve the administrator prompt.
4. Start OBS and open **Tools → ChatView Settings...**.

The installer validates the manifest, checks WebView2 Runtime, runs a local capture-exclusion probe and checks plugin loading before installation. The default target is `C:\Program Files\obs-studio`. Installer success does not prove every game, OBS capture method, GPU or physical capture-card path.

For another OBS root, run PowerShell in the extracted package:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

Use uninstall.cmd, or `uninstall.ps1 -ObsPath "D:\Apps\obs-studio"`, to remove installation files. Settings in `%LOCALAPPDATA%\ChatView` remain.

## Configure the current external-page HUD

In **Tools → ChatView Settings...**, enter a supported URL. Normal broadcast links are normalized to chat documents:

```text
https://chzzk.naver.com/live/<channel-id>
https://www.sooplive.com/station/<channel-id>
https://play.sooplive.com/<channel-id>/<broadcast-number>
https://www.youtube.com/watch?v=<video-id>
https://youtu.be/<video-id>
```

Direct chat-page examples:

```text
https://weflab.com/page/...
https://chzzk.naver.com/chat/<channel-id>
https://play.sooplive.com/<channel-id>?vtype=chat
https://www.youtube.com/live_chat?is_popout=1&v=<video-id>
```

Saving notifies the HUD without restarting OBS. HTTPS/host/path/identifier validation applies. Top-level navigation stays bound to the configured chat document and new windows are suppressed. This viewer is the existing functional layer, not the final first-party platform.

The current **READY TO STREAM / BLOCKED** UI evaluates ChatView-local conditions, not audio, encoding, service delivery or viewer exposure. Its login action does not prove a provider's full external authentication flow works in the restricted viewer. Readability must be distinguished from permission to post in future fixes.

## Move, resize and lock

Use **Ctrl + Alt + Shift + H** to toggle edit mode. Drag the header or edges/corners, then toggle again to save and restore always-on-top, non-activating, click-through behavior. Monitor-relative DIP bounds are stored in `%LOCALAPPDATA%\ChatView\hud.ini` and clamped to an available display after monitor changes.

## Existing runtime and capture limits

```text
OBS Studio
  chat-view-obs.dll        controller and local HUD supervisor
    chat-view-hud.exe      external transparent WebView2 desktop window
    chat-view-config.exe   native control center
```

This local implementation currently exits with its OBS parent. That is a single-PC behavior to adapt for the gaming companion, not permission to require gaming-PC OBS. Reuse rendering/placement/recovery when changing session ownership. Keep cloud/browser work off OBS callbacks and retain bounded crash-loop/shutdown behavior.

WDA_EXCLUDEFROMCAPTURE is a Windows capture hint, not a means to remove pixels already composited into HDMI video. The current Display Capture interlock can hide the HUD during streaming/recording/replay/virtual-camera risk. It is a protective limitation, not fulfillment of the requirement that the streamer keep reading the HUD.

Do not bypass protection to claim support. Qualify simultaneous real-HUD visibility and stream exclusion. Single-PC and dual-PC tests are separate, and dual-PC tests must have OBS absent from the gaming PC. Pairing alone cannot establish that the video is clean. The current compositor fixture test is not evidence of every game, physical capture card or actual viewer exposure.

## Diagnostics

Use **Export diagnostics** in Control Center or **Tools → Export ChatView Diagnostics...**. Exports include integrity/status/restart summaries and ChatView-tagged log data, omit configured URLs/chat text, and redact profile paths/IPC names. The local feature uploads nothing automatically; review before sharing. Planned account/chat networking requires separate consent/data handling.

## Build the current native implementation

Requirements: Windows 10 version 2004 or newer, Visual Studio 2026 C++ tools, Windows SDK 10.0.26100, CMake 3.28+, OBS development prefix, restored WebView2 SDK. The workflow currently pins OBS Studio 32.2.2 Windows x64; inspect it before changing dependencies.

```powershell
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"
$env:WEBVIEW2_SDK_DIR = ./scripts/restore-webview2.ps1

cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --output-on-failure
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is dist/. `.github/workflows/windows-build.yml` defines package, installer and official OBS validation. CTest alone is not a complete package workflow. Follow the checks attached to the actual OBS commit, not an old work-branch result.

## Existing source layout

```text
src/common/       Configuration and shared contracts
src/plugin/       OBS controller, status bridge, HUD lifecycle
src/hud/          WebView2 HUD, interaction and placement
src/config/       Native settings application
src/diagnostics/  Local diagnostic export
src/preflight/    Native installation/runtime checks
data/locale/      OBS locale resources
scripts/          Build dependencies, packaging and installation
tests/            Native, script and OBS integration checks
docs/             Architecture and development handoff
```

No empty platform scaffold is introduced by consolidation. Add working layers on these existing assets, following [PRODUCT.md](PRODUCT.md).
