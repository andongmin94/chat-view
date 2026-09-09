# ChatView OBS

ChatView OBS is a Windows OBS Studio plugin that displays a private, transparent chat HUD on the streamer's desktop while keeping the renderer outside the OBS process.

The `OBS` branch is an independent native implementation. It does not embed or preserve the Electron application from `main`.

## Status

This branch currently provides an installable single-PC alpha:

- native OBS frontend plugin;
- out-of-process Win32 HUD runtime;
- transparent WebView2 composition rendering;
- Weflab page, CHZZK chat, and YouTube live-chat URLs;
- OBS **Tools → ChatView Settings...** configuration;
- click-through locked mode;
- draggable and resizable edit mode;
- persisted monitor and DPI-independent bounds;
- LIVE and REC status indicators;
- Windows capture-exclusion request through `WDA_EXCLUDEFROMCAPTURE`;
- bounded shutdown and automatic HUD restart on a later OBS state update;
- pinned Windows CI, native tests, installer test, and packaged artifact.

The renderer remains out of process deliberately. A browser or desktop-rendering failure must not take down OBS Studio.

## Install

1. Download and extract `chat-view-obs-windows-x64.zip` from a successful Windows workflow run.
2. Close OBS Studio.
3. Double-click `install.cmd` and approve the Windows administrator prompt.
4. Start OBS Studio and open **Tools → ChatView Settings...**.

The installer verifies the Microsoft Edge WebView2 Runtime and installs it when missing. It then copies the plugin, HUD, settings application, and locale files into the default OBS directory:

```text
C:\Program Files\obs-studio
```

For a non-default OBS directory, open PowerShell in the extracted package and run:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

Double-click `uninstall.cmd` to remove the default installation, or pass a custom OBS root to `uninstall.ps1`. User settings in `%LOCALAPPDATA%\ChatView` are retained on uninstall.

## Configure chat

Start OBS Studio and open:

```text
Tools → ChatView Settings...
```

Paste one of the currently supported HTTPS URLs:

```text
https://weflab.com/page/...
https://chzzk.naver.com/chat/...
https://www.youtube.com/live_chat?is_popout=1&v=...
```

For YouTube, open a live stream's chat pop-out window and copy its URL. Saving broadcasts a local configuration-change message, so the running HUD reloads without restarting OBS.

The URL validator rejects non-HTTPS URLs, credentials embedded in URLs, non-default ports, unrelated hosts, unsupported paths, and YouTube live-chat URLs without a video ID. New top-level WebView navigation outside the allowlist is cancelled.

## Move, resize, and lock

Press:

```text
Ctrl + Alt + Shift + H
```

to unlock the HUD. In edit mode:

- drag the header area to move the HUD;
- drag an edge or corner to resize it;
- press the same shortcut again to save and lock it.

Locked mode restores always-on-top, non-activating, click-through behavior. Bounds are stored as monitor-relative device-independent pixels in:

```text
%LOCALAPPDATA%\ChatView\hud.ini
```

If the saved monitor disappears, the HUD falls back to the primary monitor and clamps itself into the visible work area.

## Runtime behavior

```text
OBS Studio
└── chat-view-obs.dll
    ├── observes stream and recording state
    ├── owns the local shared-state transport
    ├── launches the settings application
    └── owns the HUD process lifecycle
             │
             │ versioned shared memory + event
             ▼
      chat-view-hud.exe
      ├── owns the transparent desktop window
      ├── hosts WebView2 through DirectComposition
      ├── loads the configured chat URL
      └── exits when OBS exits
```

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint. It does not remove the HUD from a physical HDMI signal sent to a capture card.

## Build

### Requirements

- Windows 10 version 2004 or newer
- Visual Studio 2026 with Desktop development with C++
- Windows 11 SDK 10.0.26100
- CMake 3.28 or newer
- OBS Studio development prefix exposing `libobsConfig.cmake` and `obs-frontend-apiConfig.cmake`
- Microsoft WebView2 SDK restored by `scripts/restore-webview2.ps1`

The active CI target is OBS Studio 32.2.2 on Windows x64.

```powershell
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"
$env:WEBVIEW2_SDK_DIR = ./scripts/restore-webview2.ps1

cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --output-on-failure
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is written to `dist/` and contains:

```text
dist/
├── install.cmd
├── install.ps1
├── uninstall.cmd
├── uninstall.ps1
├── ensure-webview2-runtime.ps1
├── README.md
├── LICENSE
├── obs-plugins/64bit/
│   ├── chat-view-obs.dll
│   ├── chat-view-hud.exe
│   └── chat-view-config.exe
└── data/obs-plugins/chat-view-obs/locale/
```

## Repository layout

```text
src/common/   Configuration and shared-state contracts
src/plugin/   OBS controller and HUD process lifecycle
src/hud/      WebView2 HUD, interaction, and placement persistence
src/config/   Native settings application
data/locale/  OBS locale resources
scripts/      Build dependency, installation, and removal scripts
tests/        Configuration, placement, and real HUD process tests
docs/         Architecture constraints
```

## Remaining product work

This is not the finished product. Before a public release it still needs interactive qualification on real streamer PCs across game capture, display capture, multi-monitor DPI layouts, and common anti-cheat environments. Direct SOOP support, dual-PC pairing, first-party multi-platform aggregation, account/backend services, and the creator advertising system remain later layers. Weflab pages can be used for broader platform aggregation during this alpha.

See [`docs/architecture.md`](docs/architecture.md) for the decisions that constrain implementation.
