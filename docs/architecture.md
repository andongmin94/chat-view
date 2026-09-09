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
      ├── owns desktop rendering and placement
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

The runtime creates a top-level Win32 layered window with per-pixel alpha. No opaque background is painted in locked mode.

The locked window is:

- borderless;
- absent from the taskbar;
- always on top;
- non-activating;
- click-through;
- hidden from supported Windows capture paths where possible.

`Ctrl + Alt + Shift + H` toggles a HUD-local edit mode. Edit mode temporarily removes click-through behavior and renders a drag target. The matching Up and Down hotkeys adjust HUD scale in bounded 10% steps. Locking persists the result and restores the private HUD defaults. These controls remain in the HUD process and do not enlarge the OBS-to-HUD transport.

`WDA_EXCLUDEFROMCAPTURE` is a Windows capture hint, not DRM and not an HDMI-path guarantee. A future “show on broadcast” feature must use a distinct OBS source instead of disabling the private-HUD safety default.

## Local placement storage

Placement is local presentation state, not OBS product state. It is stored in `%LOCALAPPDATA%\ChatView\hud.ini` by the HUD runtime.

The persisted contract contains:

- the Win32 monitor device name;
- horizontal offset from that monitor's work-area origin;
- vertical offset from that monitor's work-area origin;
- HUD scale percentage.

Using work-area-relative offsets preserves placement when a monitor moves within the virtual desktop. If the saved monitor is absent, the HUD falls back to the primary monitor. Every resolved position is clamped into the selected monitor's visible work area. Scale is restricted to 50–200% in 10% steps and is applied on top of the monitor DPI scale.

## Lifecycle guarantees

- The runtime path is resolved beside the loaded plugin DLL.
- The runtime receives its OBS parent process ID.
- Normal plugin unload sends a shutdown flag and wake event.
- The runtime also waits on the OBS process handle, so it exits after an abnormal OBS termination.
- The plugin may restart a crashed runtime on the next relevant frontend state update.
- Plugin unload waits only for a bounded interval and must not hang OBS shutdown.

## Near-term state model

The next transport additions will be limited to what the next working status feature needs:

- microphone mute state;
- current scene display.

Chat messages, account data, ad campaigns, and audience measurement do not belong in this protocol until the free HUD path is stable.
