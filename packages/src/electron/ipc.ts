import { ipcMain } from "electron";

import { updateFixedMode } from "./func.js";
import { store } from "./main.js";
import { createOverlayWindow, mainWindow, overlayWindow } from "./window.js";

export function setupIpcHandlers() {
  ipcMain.on("hidden", () => {
    mainWindow?.hide();
  });

  ipcMain.on("minimize", () => {
    mainWindow?.minimize();
  });

  // 여기에 다른 IPC 핸들러 추가 가능
  ipcMain.handle("get-value", (event, key) => {
    const value = (store() as any).get(key);
    if (key === "chatUrl" && value) {
      createOverlayWindow(value);
    }
    return value;
  });

  ipcMain.on("chatUrl", (event, url) => {
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.close();
    }
    createOverlayWindow(url);
    (store() as any).set("chatUrl", url);
  });

  ipcMain.on("reInput", () => {
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.destroy();
    }
  });

  // 오버레이 고정 모드 설정
  ipcMain.on("set-fixed-mode", (event, isFixed) => {
    (store() as any).set("overlayFixed", isFixed);
    updateFixedMode(isFixed);
    mainWindow?.webContents.send("fixedMode", isFixed);
  });

  // 리셋 기능
  ipcMain.on("reset", async () => {
    (store() as any).clear();
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.destroy();
    }
    mainWindow?.webContents.send("fixedMode", false);
  });
}
