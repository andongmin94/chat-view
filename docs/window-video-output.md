# Companion window-video output

The experimental G3-03 path extends the existing companion, not a qualified capture-card or dual-PC broadcasting product. [PRODUCT.md](../PRODUCT.md) owns the goal; [development-plan.md](development-plan.md) owns results and remaining work.

## Local user flow

Run the HUD with `--companion`. Its privacy warning remains. **Ctrl+Alt+Shift+C** opens chat; **Ctrl+Alt+Shift+V** opens **게임 창 별도 출력 (실험)**. Video controls are companion-only and require the existing HUD to be ready, visible and not system/protection-suppressed. No account, enrollment, display credential or gaming-PC OBS is added.

Select a visible external top-level game window and a separate non-primary SDR extended display, then explicitly confirm **선택 창 출력**. Neither choice is preselected or persisted. The list excludes this process's windows, desktop/shell and hidden/minimized/cloaked windows. Source process/thread identity is checked again before capture.

```
gaming PC primary display: game window + readable private ChatView HUD
gaming PC separate extended SDR output: opaque window video
  -> capture card input on streaming PC -> OBS
```

The user configures OS displays and physical cabling. ChatView does not change monitor configuration, clone the primary display, run an OBS projector or install a broadcasting suite. Selecting a screen is not proof of cabling. Game, HUD and controls must not overlap the output. Active display-path queries reject clones, advanced-color/HDR and unknown topology. While capturing, geometry is checked on each UI tick and active SDR paths at most once per second. Output/cover HWND geometry and the selected GDI display name must still match; failure stops rather than choosing another screen.

## Interruption and explicit release

**중지 · 검은 화면** signals capture cancellation and raises/repaints the independent owned black cover without waiting for GPU shutdown. It does not hide the private HUD. The same output and cover HWNDs are reused on restart, remaining black until a new capture reports a frame. Selecting a different output cannot implicitly destroy the old black window; the old output must first be explicitly released.

The panel registers its own current-session [WTS notifications](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification). If registration fails, video controls do not open. Lock/disconnect, unlock/connect, suspend/resume and display/settings/device-list changes stop capture in the notification handler and invalidate both target lists. Recovery alone never restarts capture. Refresh the lists, select targets and confirm again. Device-list notifications lack a specific device identity, so unrelated hardware changes can conservatively interrupt video too. Existing HUD lock/protection behavior is unchanged.

A change while a confirmation dialog is open invalidates that confirmation, even if a subsequent unlock restores ordinary conditions before the user answers. The same generation check cancels pending output release on a context change. Closing the controls or sending a close request to the output stops to black without releasing the output.

**출력 창 닫기** first stops to black, then asks whether the receiver's use of that input has been stopped. The default is **아니요**. Declining keeps black output, not a silently resumed capture. Confirming releases HWNDs only after the worker finishes. ChatView cannot verify the receiver was actually stopped. This is a warning/explicit-consent boundary, not physical-video attestation.

When the app exits or crashes, or the user confirms output release, that screen's desktop may become visible. An opaque topmost window does not control other programs, cursor/system overlays, the secure desktop or physical HDMI after process exit. Failure to raise/paint the black cover is reported as unconfirmed protection with an instruction to stop the receiver's scene, not success.

## Windows/GPU boundary

The engine uses [IGraphicsCaptureItemInterop::CreateForWindow](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) and checks `GraphicsCaptureSession::IsSupported`. The source is exactly the selected window; there is no desktop fallback, hook, driver, hidden OBS, encoder, audio or network-video service. The system capture border stays enabled.

A worker owns hardware D3D11, free-threaded Windows Graphics Capture, a two-buffer pool, DXGI swap chain and D2D scaling. WGC callbacks only signal the worker. The newest bounded frame is preferred; frames and window titles are not persisted or logged. SDR BGRA dimensions are at most 4096 each, with black aspect-preserving letterboxing. Resize recreates the pool and discards the transition frame. Only `ContentSize` is copied because [pool padding can be undefined](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture).

Minimization, hiding, closing, cloaking or source identity loss terminates capture. Frames older than two seconds are rejected; two seconds without a frame blanks output until a fresh frame arrives. This can blank a static window. Source-loss/error restarts are manual. Device/presentation errors clear output where the OS/GPU allows; they do not fabricate a successful frame.

Final close keeps the HWNDs alive until GPU teardown finishes. [DXGI may synchronously send messages to the owner thread](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices), so the shutdown wait services sent messages rather than blocking the window owner on a plain join. Reentrant starts/control commands are fenced. Posted input, timers and commands are not dispatched and `WM_QUIT` is not consumed in that wait. OS/driver teardown is still not guaranteed hard real-time; there is no timeout-driven worker detachment or use-after-free fallback.

## Verification boundary

The existing geometry and companion tests remain. The existing WGC test checks real GPU pixels with a separate synthetic source and a deliberately non-capture-excluded HUD-like overlay, including resize, minimize, restart, cancellation and source close. It is not physical capture-card evidence.

The lifecycle fixture uses the real HUD, controls, WGC engine, output-window construction and black-cover handler. Test friendship mounts a synthetic rectangle on one desktop because no second physical output is available; production selection has no such override. Injected session/power/display/device messages exercise immediate stop, stale-selection invalidation, recovery without restart, same-HWND reuse, retarget rejection, declined/confirmed/invalidated release and active final close. The separate shutdown-wait test deterministically exercises a synchronous cross-thread message and preserves queued commands/quit. Unavailable interactive environments fail these tests rather than being silently skipped.

Real two-display topology, capture-card and encoded receiver video, game/anti-cheat/protected surfaces, multi-GPU, latency, audio, prolonged resource use, actual lock/resume/hotplug and physical overlay exclusion still need hardware acceptance. No capture/ad-accounting authority is granted by these fixtures. Exact results are recorded only in the development plan.
