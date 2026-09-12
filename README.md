# ChatView — OBS-based broadcasting platform

챗뷰는 한 화면에서 게임·작업과 채팅을 함께 보는 개인용 투명 HUD를 출발점으로, 원컴·투컴 방송 지원, 자체 채팅 경험, 스트리머 선택 광고와 시청시간 기반 HP·수익을 연결하는 플랫폼을 목표로 합니다.

**Start here:** [Product goals](PRODUCT.md) · [Architecture](docs/architecture.md) · [Development plan / handoff](docs/development-plan.md) · [Contributor instructions](AGENTS.md)

`main` is the original Electron product and the user-experience reference. The native OBS implementation does not preserve its Electron runtime or obsolete interfaces, but must preserve its product purpose. Do not mistake the current single-PC implementation for the final platform scope.

## Current implementation status

This branch contains a Windows OBS frontend plugin, an out-of-process Win32/WebView2 transparent HUD, native settings and diagnostics, URL normalization for Weflab/CHZZK/SOOP/YouTube, click-through/edit modes, placement persistence, lifecycle recovery and native tests.

**Not implemented yet:** platform accounts/backend, authenticated dual-PC operation, first-party chat ingestion, a public advertising source connected to campaigns, HP accounting and payouts. The architecture describes these as planned work, not existing features.

The implementation baseline recorded on 2026-09-12 is `obs-preflight-work @ c82fce0`. Its last reviewed Windows run passed 22 native tests and their repeated runs, but failed official OBS capture qualification; it did not publish a package in that run. Read [the handoff](docs/development-plan.md) and recheck current CI before choosing an artifact. Do not treat this working branch as a verified public release.

## Install a successfully validated package

1. Obtain `chat-view-obs-windows-x64.zip` from a successful Windows workflow run and extract it once.
2. Close OBS Studio.
3. Run `install.cmd` and approve the administrator prompt.
4. Start OBS and open **Tools → ChatView Settings...**.

The installer validates the package manifest, checks Microsoft Edge WebView2 Runtime, runs a calibrated local capture-exclusion probe and checks plugin loading before installation. It installs into `C:\Program Files\obs-studio` by default. A successful local installer probe is not a guarantee about every OBS source, game, GPU, encoder or physical capture-card path.

For another OBS root, run PowerShell in the extracted package:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

Use `uninstall.cmd`, or `uninstall.ps1 -ObsPath "D:\Apps\obs-studio"`, to remove installation files. User settings in `%LOCALAPPDATA%\ChatView` are retained.

## Configure the current external-page HUD

Open **Tools → ChatView Settings...** and enter a supported URL. Normal broadcast links are normalized to a supported chat document:

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

Saving notifies the running HUD without restarting OBS. URLs must pass the implementation's HTTPS, host, path and identifier validation. Top-level navigation stays bound to the configured chat document and new windows are suppressed. The external-page viewer is the current implementation, not the final first-party platform.

The current UI contains a **READY TO STREAM / BLOCKED** label. This describes local ChatView checks; it does not verify audio, encoding, service delivery, or actual viewer exposure. A login recovery button also does not prove that a provider's complete external authentication flow works in the restricted viewer. These limitations are recorded in the development plan.

## Move, resize and lock

Press **Ctrl + Alt + Shift + H** to toggle edit mode. Drag the header to move the HUD and an edge/corner to resize it. Toggle again to save and restore always-on-top, non-activating, click-through behavior. Placement is stored as monitor-relative device-independent bounds in `%LOCALAPPDATA%\ChatView\hud.ini`. Missing monitors are handled by clamping placement to an available display.

## Current runtime and capture limits

```text
OBS Studio
  chat-view-obs.dll       controller and local HUD supervisor
    chat-view-hud.exe     external transparent WebView2 desktop window
    chat-view-config.exe native control center
```

The current local runtime exits with its OBS parent; this is an implementation fact to revise for the planned game-PC companion role, not a permanent single-PC product restriction. Recovery is bounded, including repeated crashes, page-health loss, connectivity changes and Windows lock/suspend/resume. Keep browser/network work outside OBS callbacks.

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint and does not remove HUD pixels from a composited physical HDMI signal. As a protective interlock, the current code hides the HUD when active/showing Display Capture is risky during streaming, recording, replay buffering or virtual-camera output, with additional conservative handling around startup and scene changes. That protects against some leakage but **does not fulfill the requirement to keep the HUD usable during such a broadcast**.

Do not bypass protection merely to claim support. Qualify a clean video path while the real HUD remains visible. Single-PC and dual-PC tests must be separate. OBS capture CI uses a qualification fixture and the OBS compositor; it is not proof of actual chat reception, all games, a physical capture card, or what a remote viewer saw. See [the architecture](docs/architecture.md) for the target clean-feed and public-ad separation.

## Diagnostics

Use **Export diagnostics** in the Control Center or **Tools → Export ChatView Diagnostics...**. Current exports include integrity/status/restart summaries and ChatView-tagged log data; they omit configured URLs/chat text and redact profile paths and IPC names. Nothing is automatically uploaded by this diagnostics feature. Review exports before sharing. Planned cloud chat/account connections are separate from this existing local diagnostics behavior and require their own consent and data handling.

## Build the current native implementation

Requirements: Windows 10 version 2004 or newer, Visual Studio 2026 C++ tools, Windows SDK 10.0.26100, CMake 3.28+, an OBS development prefix, and the restored WebView2 SDK. The workflow currently pins OBS Studio 32.2.2 on Windows x64; inspect the workflow before changing versions.

```powershell
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"
$env:WEBVIEW2_SDK_DIR = ./scripts/restore-webview2.ps1

cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --output-on-failure
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is `dist/`. Packaging, manifest verification, installer checks and official OBS runtime qualification are defined by `.github/workflows/windows-build.yml`. A passing CTest run alone is not a passing package workflow. Work-branch pushes are not automatically covered by every workflow; inspect trigger definitions or the PR checks.

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

No platform server/UI scaffold is claimed to exist. Add each next component with a working product path, following the five goals in [PRODUCT.md](PRODUCT.md).
