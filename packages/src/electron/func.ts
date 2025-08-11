import { getOverlayWindow } from "./window.js";
import { getStore } from "./main.js";

// const overlayWindow = getOverlayWindow(); // 이 줄을 제거합니다.

export function updateFixedMode(isFixed:any) {
  const overlayWindow = getOverlayWindow(); // 함수 내부에서 직접 호출합니다.
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    overlayWindow.setAlwaysOnTop(isFixed);
    overlayWindow.setIgnoreMouseEvents(isFixed, {forward: true});
    overlayWindow.webContents.send('update-style', isFixed);
  }
}

export function saveBounds() {
  const overlayWindow = getOverlayWindow(); // 함수 내부에서 직접 호출합니다.
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    const bounds = overlayWindow.getBounds();
    (getStore() as any).set('overlayWindowBounds', bounds);
  }
}