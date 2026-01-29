import { ipcMain } from "electron";

import { updateFixedMode } from "./func.js";
import { store } from "./main.js";
import { createOverlayWindow, mainWindow, overlayWindow } from "./window.js";

export function setupIpcHandlers() {
  ipcMain.on("hidden", () => {
    mainWindow.hide();
  });

  ipcMain.on("minimize", () => {
    mainWindow.minimize();
  });

  // 여기에 다른 IPC 핸들러 추가 가능
  ipcMain.handle("get-value", (_event, key) => {
    const value = (store() as any).get(key);
    if (key === "chatUrl" && value) createOverlayWindow(value);
    return value;
  });

  ipcMain.on("chatUrl", (_event, url) => {
    if (overlayWindow && !overlayWindow.isDestroyed()) overlayWindow.close();

    createOverlayWindow(url);
    (store() as any).set("chatUrl", url);
  });

  ipcMain.on("reInput", () => {
    if (overlayWindow && !overlayWindow.isDestroyed()) overlayWindow.destroy();
  });

  ipcMain.handle("overlay-get-bounds", () => {
    if (!overlayWindow || overlayWindow.isDestroyed()) return null;
    return overlayWindow.getBounds();
  });

  ipcMain.on("overlay-set-bounds", (_event, nextBounds) => {
    if (!overlayWindow || overlayWindow.isDestroyed() || !nextBounds) return;

    const { x, y, width, height } = nextBounds as any;

    if (
      typeof x !== "number" ||
      typeof y !== "number" ||
      typeof width !== "number" ||
      typeof height !== "number"
    ) {
      return;
    }

    if (
      !Number.isFinite(x) ||
      !Number.isFinite(y) ||
      !Number.isFinite(width) ||
      !Number.isFinite(height)
    ) {
      return;
    }

    overlayWindow.setBounds({
      x: Math.round(x),
      y: Math.round(y),
      width: Math.max(Math.round(width), 140),
      height: Math.max(Math.round(height), 120),
    });
  });

  // 오버레이 고정 모드 설정
  ipcMain.on("set-fixed-mode", (_event, isFixed) => {
    (store() as any).set("overlayFixed", isFixed);
    updateFixedMode(isFixed);
    mainWindow.webContents.send("fixedMode", isFixed);
  });

  // 리셋 기능
  ipcMain.on("reset", async () => {
    (store() as any).clear();
    if (overlayWindow && !overlayWindow.isDestroyed()) overlayWindow.destroy();
    mainWindow.webContents.send("fixedMode", false);
  });
}
