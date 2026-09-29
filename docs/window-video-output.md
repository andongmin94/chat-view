# Companion window-video output

This is the first experimental G3-03 path in the existing companion, not a qualified capture-card or dual-PC broadcasting product. [PRODUCT.md](../PRODUCT.md) owns the goal; [development-plan.md](development-plan.md) owns current results and remaining work.

## Local user flow

Run the existing HUD with `--companion`. Its initial privacy warning remains. **Ctrl+Alt+Shift+C** still opens chat; **Ctrl+Alt+Shift+V** opens **게임 창 별도 출력 (실험)**. The capture controls do not run in OBS-managed mode and require the existing HUD to be ready, visible and not system/protection-suppressed.

Select a visible top-level game window and a separate non-primary SDR extended display, then choose **선택 창 출력** and explicitly confirm. Neither choice is preselected or persisted. The list excludes this process's windows, desktop/shell, hidden/minimized/cloaked windows; the capture engine checks source process/thread identity again. Failed list insertion clears the choices rather than letting a displayed label select a different indexed target. No hardware enrollment or provider credentials are needed for this local video path.

The output window fills the selected extended display without taking focus. The intended experimental topology is:

```
gaming PC primary display: game window + readable private ChatView HUD
gaming PC separate extended SDR output: opaque game-window video only
  -> capture card input on streaming PC -> OBS
```

The user must arrange the OS displays and capture-card cabling. ChatView does not reconfigure monitors, clone the primary display, run a projector or install another broadcasting program. Selecting an output is not proof of the physical cabling. The game window, HUD and selection panel must not overlap the output display. Active display-path queries reject a clone or an advanced-color/HDR source/output and fail closed when the topology cannot be confirmed. Display/power/settings changes and a source-monitor change stop output; recovery requires explicit selection/start.

**중지 · 검은 화면** cancels capture and keeps the opaque output window black; private chat/HUD is not hidden by the video control. A separate owned top-level black cover is shown immediately above the GPU presentation window while the worker releases its frame/session. It does not depend on GDI painting inside the flip-model output HWND. Closing the selection panel also stops output and keeps the black output. **출력 창 닫기** separately releases that window after worker teardown. Closing the output or exiting/crashing the app can expose that screen's desktop again. Stop the receiver's live scene before releasing the output; this experiment is not a fail-safe physical-video switch.

## Windows and GPU boundary

The engine uses Microsoft's [IGraphicsCaptureItemInterop::CreateForWindow](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) (Windows 10 version 1903 or later) and checks `GraphicsCaptureSession::IsSupported`. The source is exactly the selected window. There is no desktop/screen-capture fallback, hook, driver, hidden OBS, encoder, audio capture or network-video service. The system capture border is not disabled.

A worker owns the hardware D3D11 device, free-threaded Windows Graphics Capture session, two-buffer frame pool, DXGI output swap chain and D2D GPU scaling. WGC callbacks only signal the worker; neither capture nor presentation runs in an OBS callback. At most two capture buffers are retained and the newest is preferred; frames are not queued indefinitely or written to disk. No screenshots, window titles or raw errors enter diagnostics.

The first pixel path is bounded SDR BGRA with each dimension at most 4096. Aspect ratio is retained with black letterboxing. On resize the frame pool is recreated and the transition frame is discarded. Only `ContentSize` is copied: Microsoft's [screen-capture guidance](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture) notes that pool padding outside that region can be undefined. HDR/advanced-color output requires a different explicit format/tone-mapping path and is not supported here.

Source minimization, closing, hiding, cloaking or replacement terminates capture. An old frame is not used to substitute a different window or desktop. Frames older than two seconds relative to QPC are rejected; a two-second no-frame interval blanks the output until a fresh frame arrives. This conservative freshness rule can blank a static window that generates no new capture frames. A source-loss/error restart is manual. Device removal or capture/presentation failure clears output where the OS/GPU permits and reports failure, not a fabricated successful frame.

Normal Stop is signal-only and nonblocking for the UI. Final shutdown joins the capture worker before destroying its output HWND; OS/driver teardown is not a guaranteed hard real-time operation. Local protection is observed from the existing HUD, not relaxed to make video start. This control does not attest remote OBS, set `captureState` to verified or alter ad/audience accounting.

## Verification boundary

The portable geometry test checks dimension limits, aspect fit, screen/clone/HUD separation and negative display coordinates. The actual companion executable test checks the new controls, empty selection rejection, visible HUD preservation and absence of OBS modules.

The Windows capture test starts a separate synthetic moving source, overlaps it with a deliberately non-capture-excluded HUD-like window, and reads known synthetic output pixels. It tests window-only isolation, local overlay visibility, input/output resize and letterboxing, minimize, explicit restart, cancellation and source close. It also checks the owned top-level black-cover pattern over active GPU presentation and fresh video after uncovering. It fails rather than skips when the required interactive capture environment is unavailable. This is a single-desktop synthetic pixel test, not two physical displays or a capture card.

Physical cabling, game/window modes and anti-cheat/protected surfaces, cursor/system overlays on the extended desktop, multi-GPU behavior, latency, audio, resource endurance, lock/resume/hotplug behavior on representative hardware and the streaming PC's encoded recording still require verification. An opaque topmost desktop window is not exclusive ownership of physical HDMI. No universal game support or clean-HDMI guarantee follows from a WGC test. Current exact commit/CI results are maintained only in the development plan.
