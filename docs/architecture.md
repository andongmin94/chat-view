# Architecture

## Product boundary

ChatView OBS is not an OBS dock that happens to contain chat. Its primary surface is an operating-system-level HUD displayed over the streamer's game, browser, or desktop.

The OBS plugin remains a controller. The external HUD runtime owns windows and rendering.

```text
OBS Studio
└── chat-view-obs.dll
    ├── reads OBS output state
    ├── owns the local state transport
    └── owns the HUD process lifecycle
             │
             │ versioned state
             ▼
      chat-view-hud.exe
      ├── owns the transparent window
      ├── owns desktop rendering
      └── exits with its OBS parent
```

## Why the HUD is out of process

The plugin runs inside OBS. A null dereference, rendering-driver fault, or blocking UI operation inside the plugin can affect the broadcast process itself.

The HUD therefore runs out of process from the first functional version. This is not a temporary compatibility layer. It is the permanent fault boundary for desktop rendering and later also enables dual-PC operation.

The plugin must remain small:

- subscribe to OBS frontend events;
- serialize product state;
- launch, monitor, and stop the runtime;
- never perform network, browser, or rendering work on an OBS callback thread.

## v1 local transport

The first transport uses a named Windows file mapping plus an auto-reset event.

The mapped structure is:

- fixed magic value;
- explicit protocol version;
- sequence lock;
- state flags;
- monotonically increasing generation.

The sequence lock prevents the HUD from accepting a partially written state. The event avoids polling. The mapping and event are scoped to the current Windows session and named using the OBS process ID.

The transport currently carries only:

- streaming active;
- recording active;
- shutdown requested.

New state must be added by incrementing the protocol version when the layout is no longer compatible.

## Desktop HUD properties

The runtime creates a top-level Win32 layered window with per-pixel alpha. No opaque background is painted.

The window is:

- borderless;
- absent from the taskbar;
- always on top;
- non-activating;
- click-through;
- hidden from supported Windows capture paths where possible.

`WDA_EXCLUDEFROMCAPTURE` is a Windows capture hint, not DRM and not an HDMI-path guarantee. A future “show on broadcast” feature must use a distinct OBS source instead of disabling the private-HUD safety default.

## Lifecycle guarantees

- The runtime path is resolved beside the loaded plugin DLL.
- The runtime receives its OBS parent process ID.
- Normal plugin unload sends a shutdown flag and wake event.
- The runtime also waits on the OBS process handle, so it exits after an abnormal OBS termination.
- The plugin may restart a crashed runtime on the next relevant frontend state update.
- Plugin unload waits only for a bounded interval and must not hang OBS shutdown.

## Near-term state model

The next state additions will be limited to what the next working feature needs:

- selected monitor and persisted HUD bounds;
- edit/locked mode;
- microphone mute state;
- current scene display.

Chat messages, account data, ad campaigns, and audience measurement do not belong in this protocol until the free HUD path is stable.
