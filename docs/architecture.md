# Architecture

## Product boundary

ChatView OBS is not an OBS dock. Its primary surface is an operating-system-level HUD displayed over the streamer's game, browser, or desktop.

The OBS plugin is a controller. The external HUD runtime owns browser composition, the desktop window, and local presentation state.

```text
OBS Studio
└── chat-view-obs.dll
    ├── observes stream and recording state
    ├── owns the local status transport
    ├── launches configuration UI
    └── owns the HUD process lifecycle
             │
             │ versioned shared state
             ▼
      chat-view-hud.exe
      ├── owns the transparent top-level window
      ├── hosts WebView2 with DirectComposition
      ├── owns interaction and placement
      └── exits with its OBS parent
```

## Fault boundary

The plugin runs inside OBS. A null dereference, blocked callback, or browser/rendering failure inside the plugin could affect the broadcast process.

Browser and desktop rendering therefore run out of process from the first useful version. This is a permanent fault boundary, not a compatibility layer. It also leaves a clean process boundary for later game-PC/stream-PC operation.

The plugin remains deliberately small:

- subscribe to OBS frontend events;
- serialize bounded status state;
- launch, monitor, and stop the HUD runtime;
- launch the settings application;
- never perform browser, network, or rendering work on an OBS callback thread.

## Local status transport

The single-PC transport uses a named Windows file mapping and an auto-reset event. Protocol version 3 carries only:

- streaming active;
- recording active;
- shutdown requested;
- a monotonically increasing generation.

The mapped structure has a fixed magic value, explicit protocol version, and sequence lock. The sequence lock prevents the HUD from accepting a partial write, while the event avoids polling. Names are scoped to the current Windows session, the OBS process ID, and a per-launch CNG random token.

The plugin and HUD ship as one package. Incompatible layouts increment the protocol version; obsolete layouts are not retained.

## Chat content boundary

The current alpha renders an explicitly configured web chat page rather than implementing platform protocols inside OBS.

The settings application accepts either direct chat pages or normal CHZZK, SOOP, and YouTube channel/broadcast links. The configuration layer converts supported links into one canonical chat URL before persistence:

```text
CHZZK /live/<channel-id> or /<channel-id>
    → https://chzzk.naver.com/chat/<channel-id>

SOOP /station/<channel-id> or play/<channel-id>[/<broadcast-number>]
    → https://play.sooplive.com/<channel-id>?vtype=chat

YouTube /watch?v=<video-id>, /live/<video-id>, youtu.be/<video-id>
    → https://www.youtube.com/live_chat?is_popout=1&v=<video-id>
```

Weflab `/page/...` URLs remain direct. The normalizer requires HTTPS, the default HTTPS port, no URL credentials, supported paths, a 32-character hexadecimal CHZZK channel ID, an ASCII SOOP channel ID with an optional numeric broadcast route, and an ASCII YouTube video identifier. SOOP canonicalization intentionally binds to the streamer channel rather than one transient broadcast number. Unsupported or ambiguous links are rejected instead of being guessed.

Configuration is stored by the separate native settings application in `%LOCALAPPDATA%\ChatView\config.ini`. Saving broadcasts a registered local Windows message so the HUD reloads immediately.

Top-level WebView navigation is restricted to the supported hosts and new windows are suppressed. Page subresources continue to load normally.

This web-content boundary is the smallest complete path that preserves the existing ChatView use case. First-party platform aggregation is a later backend/runtime layer and must not be half-integrated as unused provider code.

## Transparent WebView2 composition

The HUD uses a WebView2 composition controller hosted by a DirectComposition visual tree. This avoids an opaque child HWND and permits transparent web content in the top-level overlay.

The runtime:

- creates a D3D11 BGRA device, with WARP fallback;
- creates a DirectComposition device, target, and root visual;
- creates an `ICoreWebView2CompositionController`;
- sets the WebView default background to transparent;
- injects a small isolated Shadow DOM control layer for edit bounds and OBS status;
- forces document and body backgrounds transparent without rewriting the provider UI.

The WebView2 Evergreen Runtime is checked by the package installer and installed from Microsoft's signed bootstrapper when absent. The SDK used at build time is pinned separately in CI.

## Desktop window modes

Locked mode is the broadcasting default. The top-level window is:

- borderless;
- absent from the taskbar;
- always on top;
- non-activating;
- click-through;
- requested to be excluded from supported Windows capture paths;
- created with `WS_EX_NOREDIRECTIONBITMAP` for DirectComposition.

`Ctrl + Alt + Shift + H` toggles edit mode. Edit mode temporarily enables activation and native move/resize hit testing. The injected control layer shows a visible frame, while the webpage remains the content surface. Locking persists the final bounds and restores the private HUD defaults.

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint, not DRM and not an HDMI-path guarantee. A future broadcast-visible overlay must be a separate OBS source rather than weakening the private-HUD default.

## Placement storage

Placement is local presentation state and belongs to the HUD runtime. It is stored in `%LOCALAPPDATA%\ChatView\hud.ini` as:

- Win32 monitor device name;
- horizontal and vertical offsets from the monitor work-area origin in device-independent pixels;
- width and height in device-independent pixels.

Using monitor-relative DIPs preserves useful placement across virtual-desktop reordering and DPI changes. Missing monitors fall back to the primary display. Restored dimensions and positions are clamped into the current work area.

## Lifecycle guarantees

- Runtime and settings executable paths are resolved beside the loaded plugin DLL.
- The HUD receives its OBS parent process ID.
- Normal unload publishes a shutdown state and wakes the HUD.
- The HUD also waits on the OBS process handle and exits after abnormal OBS termination.
- Unexpected HUD exits use bounded exponential backoff and open an automatic-restart circuit after six consecutive failures.
- **Tools → Restart ChatView HUD** explicitly resets the circuit and replaces a running HUD without restarting OBS.
- Plugin unload uses a bounded wait and does not indefinitely block OBS shutdown.
- WebView2 asynchronous callbacks are serviced by an alertable, input-available Win32 message loop.

## Testing boundary

The Windows workflow builds against pinned OBS Studio 32.2.2 development libraries and a pinned WebView2 SDK. It then runs:

- provider URL normalization, rejection, and configuration persistence tests;
- placement validation, monitor fallback, and malformed-input tests;
- a calibrated pixel-level Windows capture-exclusion capability probe followed by a real WebView2 HUD process smoke test covering initialization readiness, locked/edit modes, native resize, persistence, capture-exclusion request, shared-state shutdown, and delayed WebView profile cleanup;
- deterministic restart-policy tests covering backoff, circuit opening, saturation, and explicit reset;
- package layout validation;
- installer and uninstaller tests against an isolated OBS directory tree.

The uploaded artifact contains the install tree directly, so users extract it once and run `install.cmd`.

These checks prevent publishing a package with a broken controller-to-HUD path. They do not replace interactive qualification on a real broadcaster workstation, GPU driver stack, game, and capture configuration.

## Layering order

Development proceeds only from a working product layer:

1. installable single-PC OBS-controlled transparent web chat HUD;
2. direct URL coverage for the major target platforms and real-workstation qualification;
3. authenticated dual-PC pairing with a separate transport implementation;
4. first-party multi-platform chat aggregation and backend services;
5. creator advertising and verified campaign accounting.

Advertising does not enter the codebase until the free HUD is stable enough to earn installation on its own.
