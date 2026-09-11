# ChatView OBS

ChatView OBS is a Windows OBS Studio plugin that displays a private, transparent chat HUD on the streamer's desktop while keeping the renderer outside the OBS process.

The `OBS` branch is an independent native implementation. It does not embed or preserve the Electron application from `main`.

## Status

This branch currently provides an installable single-PC alpha:

- native OBS frontend plugin;
- out-of-process Win32 HUD runtime;
- transparent WebView2 composition rendering;
- Weflab, CHZZK, SOOP, and YouTube chat pages;
- automatic conversion of ordinary CHZZK, SOOP, and YouTube broadcast URLs;
- browser connectivity and provider connection-loss recovery;
- OBS **Tools → ChatView Settings...** configuration;
- click-through locked mode;
- draggable and resizable edit mode;
- persisted monitor and DPI-independent bounds;
- LIVE and REC status indicators;
- Windows capture-exclusion request through `WDA_EXCLUDEFROMCAPTURE`;
- fail-closed Display Capture interlock while streaming, recording, replay buffering, or virtual-camera output is active;
- bounded shutdown and an automatic restart circuit that stops crash loops;
- explicit **Tools → Restart ChatView HUD** recovery without restarting OBS;
- actual OBS Display Capture → scene → main-texture pixel qualification in official OBS Studio;
- pinned Windows CI, native tests, installer test, and packaged artifact.

The renderer remains out of process deliberately. A browser or desktop-rendering failure must not take down OBS Studio. The provider page cannot call ChatView control functions: native status is delivered one way into a closed Shadow DOM layer.

## Install

1. Download `chat-view-obs-windows-x64.zip` from a successful Windows workflow run and extract it once.
2. Close OBS Studio.
3. Double-click `install.cmd` and approve the Windows administrator prompt.
4. Start OBS Studio and open **Tools → ChatView Settings...**.

The installer verifies the package manifest and the Microsoft Edge WebView2 Runtime, then runs a calibrated local pixel probe before copying any files. The probe must observe an ordinary foreground window and then observe the measured background after Windows capture exclusion is enabled. It then copies the plugin, HUD, settings application, and locale files into the default OBS directory:

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

For CHZZK, SOOP, and YouTube, paste the normal channel or broadcast URL that is already in the browser address bar:

```text
https://chzzk.naver.com/live/<channel-id>
https://www.sooplive.com/station/<channel-id>
https://play.sooplive.com/<channel-id>/<broadcast-number>
https://www.youtube.com/watch?v=<video-id>
https://youtu.be/<video-id>
```

ChatView converts those links into their chat-page form automatically. Direct chat URLs are also accepted:

```text
https://weflab.com/page/...
https://chzzk.naver.com/chat/<channel-id>
https://play.sooplive.com/<channel-id>?vtype=chat
https://www.youtube.com/live_chat?is_popout=1&v=<video-id>
```

Saving broadcasts a local configuration-change message, so the running HUD reloads without restarting OBS.

The URL normalizer requires HTTPS, the default HTTPS port, no embedded credentials, a supported host and path, a valid CHZZK channel ID, a valid SOOP channel path, and a non-empty YouTube video ID. SOOP broadcast-number routes are reduced to a channel-scoped `vtype=chat` URL so a live redirect remains bound to the configured streamer. It stores only the normalized chat URL. New top-level WebView navigation outside the allowlist is cancelled.

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
      ├── loads the normalized chat URL
      └── exits when OBS exits
```

The platform page probe sends a compact health heartbeat every two seconds, even when the detected state has not changed. It also reports browser offline/online transitions and provider reconnecting or disconnected banners from bounded structural status regions rather than ordinary chat messages. The native HUD watchdog reloads a configured chat page after twelve seconds without a valid heartbeat. A separately bounded connection-recovery policy gives a reported disconnect ten seconds to recover, reloads the page once, and restarts the HUD if the disconnect survives another fifteen seconds. Browser-offline time and Display Capture suppression time do not count toward either recovery deadline.

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint. It does not remove the HUD from a physical HDMI signal sent to a capture card. As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active or showing Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. At output start it temporarily treats any configured Display Capture source as risky until OBS source activation settles. Failed starts expire automatically, and the HUD returns only after the risk condition clears. While hidden by that policy, the HUD pauses the periodic hidden-window affinity query. It explicitly reapplies and verifies capture exclusion before showing again, then verifies once more after restoration.

The Windows workflow also loads a CI-only qualification module into the official OBS portable build. That module creates a real `monitor_capture` source, routes it through the current scene, reads back `obs_render_main_texture()`, and performs a calibrated foreground/background pixel comparison. The required assertion is that hiding the protected top-level window removes its pixels from the OBS program compositor. Whether Windows capture affinity is honored by the runner's selected DXGI/WGC path is recorded separately and is not confused with the fail-closed hide guarantee. This test covers Display Capture → scene → OBS main texture; it does not claim to validate encoder, muxer, streaming-service, or physical capture-card paths.

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

This is not the finished product. Before a public release it still needs interactive qualification on real streamer PCs across game capture, display capture, multi-monitor DPI layouts, and common anti-cheat environments. Dual-PC pairing, first-party multi-platform aggregation, account/backend services, and the creator advertising system remain later layers. Weflab pages can be used for broader platform aggregation during this alpha.

See [`docs/architecture.md`](docs/architecture.md) for the decisions that constrain implementation.
