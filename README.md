# ChatView OBS

ChatView OBS is a Windows-first OBS Studio plugin that drives a private, fully transparent streamer HUD outside the OBS window.

This branch is an independent implementation. It does not preserve or embed the Electron application from `main`.

## Current milestone: v0.1 controller-to-HUD path

The current source implements the first end-to-end slice:

- OBS frontend events are observed by a native plugin.
- The plugin launches `chat-view-hud.exe` as a separate process.
- OBS and the HUD exchange a small versioned state through Windows shared memory and an event.
- The HUD is a borderless, per-pixel-alpha, always-on-top Windows overlay.
- The overlay is non-activating and click-through.
- Windows capture exclusion is requested with `WDA_EXCLUDEFROMCAPTURE`.
- `LIVE`, `REC`, `LIVE • REC`, startup, and shutdown transitions are rendered.
- The HUD exits when OBS exits, even if the plugin cannot send a normal shutdown event.

Keeping the renderer out of the OBS process is intentional: an overlay crash must not take the broadcaster down with it.

## Repository layout

```text
src/
├── common/   Shared state contract and Win32 ownership helpers
├── plugin/   Thin OBS controller and HUD process lifecycle
└── hud/      External transparent Windows HUD runtime

data/locale/  OBS plugin locale resources
docs/         Architecture and product constraints
scripts/      Package installation and removal
```

## Install a CI package

1. Download and extract `chat-view-obs-windows-x64.zip` from the latest successful Windows build.
2. Close OBS Studio.
3. Open an elevated PowerShell terminal in the extracted package directory.
4. Run:

```powershell
./install.ps1
```

The default OBS root is `C:\Program Files\obs-studio`. Pass a different root explicitly when needed:

```powershell
./install.ps1 -ObsPath "D:\Apps\obs-studio"
```

The installer validates the OBS executable and copies only the ChatView plugin, HUD runtime, and locale files. Remove the same known files with:

```powershell
./uninstall.ps1
```

## Build

### Requirements

- Windows 10 version 2004 or newer
- Visual Studio 2026 with Desktop development with C++ and Windows 11 SDK 10.0.26100
- CMake 3.28 or newer
- An OBS Studio development build or install prefix exposing:
  - `libobsConfig.cmake`
  - `obs-frontend-apiConfig.cmake`

The active development and CI target is OBS Studio 32.2.2 on Windows x64. Older OBS versions have not been qualified.

Set the OBS CMake package prefix, then configure and build:

```powershell
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-build-or-install-prefix"

cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
cmake --install build/windows-x64 --config RelWithDebInfo
```

The install tree is produced in `dist/`:

```text
dist/
├── install.ps1
├── uninstall.ps1
├── README.md
├── LICENSE
├── obs-plugins/64bit/
│   ├── chat-view-obs.dll
│   └── chat-view-hud.exe
└── data/obs-plugins/chat-view-obs/
    └── locale/
```

Every push to `OBS` runs a pinned Windows build, exercises the installer and uninstaller against a temporary OBS tree, and uploads the same package layout as a CI artifact.

## Expected behavior

1. Start OBS Studio.
2. A transparent `CHATVIEW READY` indicator appears briefly on the primary monitor.
3. Starting a stream shows `LIVE`.
4. Starting a recording shows `REC`, or `LIVE • REC` when both are active.
5. Stopping the last active output shows `OFFLINE` briefly and then hides the HUD.
6. Closing OBS terminates the HUD runtime.

The HUD is designed to remain outside capture. Physical HDMI capture cards still receive whatever pixels the game computer outputs and are not affected by Windows capture exclusion.

## Development order

1. Prove plugin load, process lifecycle, transparent rendering, and OBS state propagation.
2. Add an edit mode, monitor selection, bounds persistence, and a global lock hotkey.
3. Add the first native chat provider and message rendering.
4. Add game-PC/stream-PC pairing using the same state contract.
5. Add the creator advertising layer only after the free HUD is stable.

See [`docs/architecture.md`](docs/architecture.md) for the decisions that constrain implementation.
