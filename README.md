# ChatView OBS

ChatView OBS is a Windows-first OBS Studio plugin that drives a private, transparent streamer HUD outside the OBS window.

The `OBS` branch is an independent native implementation. It does not embed or preserve the Electron application from `main`.

## Current milestone

The current end-to-end path provides:

- a native OBS frontend plugin;
- an out-of-process Win32 HUD runtime;
- a versioned shared-memory transport with an event wake-up;
- streaming and recording state indicators;
- the current OBS scene name, updated when the scene changes;
- a per-pixel-alpha, always-on-top, non-activating overlay;
- click-through locked mode and a draggable edit mode;
- persisted monitor, work-area-relative position, and 50–200% HUD scale;
- a Windows capture-exclusion request through `WDA_EXCLUDEFROMCAPTURE`;
- bounded shutdown and automatic runtime restart on a later state update;
- a pinned Windows CI build, native tests, installer test, and packaged artifact.

The HUD stays out of the OBS process deliberately. A desktop-rendering failure must not take down the broadcaster.

## Install a CI package

1. Download and extract `chat-view-obs-windows-x64.zip` from the latest successful Windows build.
2. Close OBS Studio.
3. Open an elevated PowerShell terminal in the extracted directory.
4. Run:

```powershell
./install.ps1
```

The default OBS root is `C:\Program Files\obs-studio`. A different installation can be selected explicitly:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

Remove the same known files with:

```powershell
./uninstall.ps1
```

## Move, resize, and lock the HUD

Press `Ctrl + Alt + Shift + H` to enter edit mode. Drag the visible panel to the desired monitor and position.

While edit mode is active:

```text
Ctrl + Alt + Shift + Up      increase scale by 10%
Ctrl + Alt + Shift + Down    decrease scale by 10%
Ctrl + Alt + Shift + H       save and lock
```

Locked mode immediately restores click-through behavior. The monitor device, offset inside its work area, and scale are stored in `%LOCALAPPDATA%\ChatView\hud.ini`. A missing monitor falls back to the primary display, and an invalid position is clamped into the visible work area.

## Expected behavior

1. Start OBS Studio.
2. `CHATVIEW READY` appears briefly on the saved monitor or the primary monitor.
3. Starting a stream shows `LIVE`; starting a recording shows `REC`; both active shows `LIVE • REC`.
4. The second line displays the current OBS scene and updates on a scene change.
5. Stopping the final active output shows `OFFLINE` briefly and hides the HUD.
6. Closing OBS terminates the HUD runtime.

The Windows capture-exclusion API is a best-effort operating-system hint. A physical HDMI capture card still receives the pixels emitted by the game computer.

## Build

### Requirements

- Windows 10 version 2004 or newer
- Visual Studio 2026 with Desktop development with C++
- Windows 11 SDK 10.0.26100
- CMake 3.28 or newer
- an OBS Studio development prefix exposing `libobsConfig.cmake` and `obs-frontend-apiConfig.cmake`

The active CI target is OBS Studio 32.2.2 on Windows x64.

```powershell
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"

cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --output-on-failure
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is written to `dist/` and contains the plugin DLL, HUD executable, locale files, installer scripts, license, and this README.

## Repository layout

```text
src/common/   Shared protocol and Win32 ownership helpers
src/plugin/   OBS controller and HUD process lifecycle
src/hud/      Transparent HUD, placement persistence, and state reader
data/locale/  OBS locale resources
scripts/      Package installation and removal
tests/        Native placement and HUD lifecycle tests
docs/         Architecture constraints
```

## Remaining product work

The native private-HUD foundation is working, but the application is not feature-complete. The next product layer is the first live-chat provider, including credential/configuration storage, message ingestion, bounded message history, and native HUD rendering. Dual-PC transport and the creator advertising layer follow only after the free single-PC chat path is stable.

See [`docs/architecture.md`](docs/architecture.md) for the constraints that govern the implementation.
