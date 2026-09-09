# Architecture

## Product boundary

ChatView OBS is not an OBS dock that happens to contain chat. Its primary surface is an operating-system-level HUD displayed over the streamer's game, browser, or desktop.

The OBS plugin is a controller. The external HUD runtime owns windows, local presentation state, and desktop rendering.

```text
OBS Studio
└── chat-view-obs.dll
    ├── reads frontend output and scene state
    ├── owns the local state transport
    └── owns the HUD process lifecycle
             │
             │ versioned shared state
             ▼
      chat-view-hud.exe
      ├── owns the transparent window
      ├── owns rendering and placement
      └── exits with its OBS parent
```

## Why the HUD is out of process

The plugin runs inside OBS. A null dereference, rendering-driver fault, or blocking operation inside the plugin can affect the broadcast process itself.

The HUD therefore runs out of process from the first functional version. This is the permanent fault boundary for desktop rendering and is also the natural boundary for later dual-PC operation.

The plugin remains deliberately small:

- subscribe to OBS frontend events;
- serialize bounded product state;
- launch, monitor, and stop the HUD runtime;
- never perform network, browser, or rendering work on an OBS callback thread.

## Local controller transport

The local transport uses a named Windows file mapping plus an auto-reset event.

The mapped structure contains:

- a fixed magic value;
- an explicit protocol version;
- a sequence lock;
- state flags;
- a monotonically increasing generation;
- a bounded UTF-8 current-scene field.

The sequence lock prevents the HUD from accepting a partially written state. The event avoids polling. The mapping and event are scoped to the current Windows session and named with the OBS process ID.

Protocol version 2 carries:

- streaming active;
- recording active;
- shutdown requested;
- current OBS scene name.

A layout change requires a protocol-version increment. No compatibility layer is retained between incompatible layouts because the plugin and HUD are shipped as one package.

## Desktop HUD properties

The runtime creates a top-level Win32 layered window with per-pixel alpha. Locked mode paints no opaque full-window background.

The locked window is:

- borderless;
- absent from the taskbar;
- always on top;
- non-activating;
- click-through;
- hidden from supported Windows capture paths where possible.

`Ctrl + Alt + Shift + H` toggles a HUD-local edit mode. Edit mode temporarily removes click-through behavior and renders a drag target. Matching Up and Down hotkeys adjust a bounded scale. Locking saves the result and restores the private-HUD defaults. These controls remain inside the HUD process and do not enlarge the OBS transport.

`WDA_EXCLUDEFROMCAPTURE` is a Windows capture hint, not DRM and not an HDMI-path guarantee. A future show-on-broadcast feature must use a distinct OBS source rather than weakening the private-HUD safety default.

## Local placement storage

Placement is local presentation state, not OBS product state. The HUD stores it in `%LOCALAPPDATA%\ChatView\hud.ini`.

The persisted contract contains:

- the Win32 monitor device name;
- horizontal and vertical offsets from that monitor's work-area origin;
- a HUD scale percentage from 50% to 200% in 10% steps.

Work-area-relative offsets preserve placement when a monitor moves within the virtual desktop. If the saved monitor is absent, the HUD falls back to the primary monitor. Every resolved position is clamped into the selected monitor's visible work area. DPI scaling and the user-selected scale are composed at render time.

## Lifecycle guarantees

- The runtime path is resolved beside the loaded plugin DLL.
- The runtime receives its OBS parent process ID.
- Normal plugin unload publishes a shutdown flag and wake event.
- The runtime also waits on the OBS process handle, so it exits after abnormal OBS termination.
- The plugin restarts a crashed runtime on the next relevant frontend-state update.
- Plugin unload waits only for a bounded interval and cannot indefinitely block OBS shutdown.

## Testing boundary

The Windows workflow builds against pinned OBS Studio 32.2.2 development libraries and then runs:

- placement persistence and malformed-input tests;
- a real HUD process smoke test covering shared memory, scene updates, locked/edit modes, scale persistence, and shutdown;
- package layout validation;
- installer and uninstaller tests against an isolated OBS directory tree.

These tests do not replace an interactive test on a real broadcaster workstation, but they prevent the package from being published when the native controller-to-HUD path is broken.

## Next product layer

The next complete vertical slice is live chat:

1. one provider with an explicit configuration and credential boundary;
2. provider work outside OBS callback threads;
3. a bounded, timestamped message model;
4. native text layout and expiry in the HUD;
5. reconnect and rate-limit behavior covered by deterministic tests.

Provider messages and credentials do not belong in the fixed OBS status mapping. They require a separate runtime-owned subsystem. Dual-PC transport and advertising are deferred until the free single-PC chat path is stable.
