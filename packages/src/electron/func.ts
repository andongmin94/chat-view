import { store } from "./main.js";
import { getOverlayWindow } from "./window.js";

export function updateFixedMode(isFixed: any) {
  const overlayWindow = getOverlayWindow(); // 함수 내부에서 직접 호출합니다.
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    overlayWindow.setAlwaysOnTop(isFixed);
    overlayWindow.setIgnoreMouseEvents(isFixed, { forward: true });
    overlayWindow.webContents.send("update-style", isFixed);
  }
}

export function saveBounds() {
  const overlayWindow = getOverlayWindow(); // 함수 내부에서 직접 호출합니다.
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    const bounds = overlayWindow.getBounds();
    (store() as any).set("overlayWindowBounds", bounds);
  }
}
