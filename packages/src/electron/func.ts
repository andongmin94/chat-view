import { store } from "./main.js";
import { overlayWindow } from "./window.js";

export function updateFixedMode(isFixed: any) {
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    overlayWindow.setAlwaysOnTop(isFixed);
    overlayWindow.setIgnoreMouseEvents(isFixed, { forward: true });
    overlayWindow.webContents.send("update-style", isFixed);
  }
}

export function saveBounds() {
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    const bounds = overlayWindow.getBounds();
    (store() as any).set("overlayWindowBounds", bounds);
  }
}
