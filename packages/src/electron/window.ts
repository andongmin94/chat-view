import path from "path";
import { app, BrowserWindow } from "electron";

import { saveBounds, updateFixedMode } from "./func.js";
import { __dirname, isDev, store } from "./main.js";
import { closeSplash } from "./splash.js";

export let mainWindow: BrowserWindow | null;
export let overlayWindow: BrowserWindow;

export function createWindow(port: number) {
  mainWindow = new BrowserWindow({
    show: false,
    width: 416,
    height: 282 + 121,
    frame: false,
    resizable: isDev,
    icon: path.join(__dirname, "../../public/icon.png"),
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, "preload.js"), // preload 사용 시 주석 해제
    },
  });

  mainWindow.loadURL(`http://localhost:${port}`);

  mainWindow.webContents.on("did-finish-load", () => {
    closeSplash(); // 스플래시 닫기
    mainWindow?.show();
  });

  // --- 플랫폼별 우클릭 메뉴 비활성화 시도 ---
  if (process.platform === "win32") {
    mainWindow.hookWindowMessage(278, function () {
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.setEnabled(false);
        setTimeout(() => {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.setEnabled(true);
          }
        }, 100);
      }
      return true;
    });
  } else {
    mainWindow.webContents.on("context-menu", (event) => {
      console.log("Main process context-menu event triggered on macOS/Linux");
      event.preventDefault();
    });
  }

  // 종료 설정
  mainWindow.on("close", (e) => {
    if (process.platform === "darwin") {
      // macOS: 사용자가 명시적으로 종료(Cmd+Q 등)하지 않으면 숨김
      e.preventDefault();
      mainWindow?.hide();
      app.dock?.hide(); // Dock 에서도 숨김
    }
    // 다른 OS 에서는 window-all-closed 에서 앱 종료 처리
    else {
      app.quit();
    }
  });

  mainWindow.on("closed", () => {
    mainWindow = null; // 창 참조 제거
  });
}

// overlayWindow 생성 및 설정 복원 함수
export const createOverlayWindow = (url: string) => {
  const mainWindowBounds = mainWindow?.getBounds();
  const defaultBounds = {
    x: (mainWindowBounds?.x ?? 0) + (mainWindowBounds?.width ?? 800) + 20,
    y: mainWindowBounds?.y ?? 0,
    width: 400,
    height: 270,
  };

  const storedBounds = (store() as any).get(
    "overlayWindowBounds",
    defaultBounds,
  );
  const isFixed = (store() as any).get("overlayFixed", false);

  overlayWindow = new BrowserWindow({
    ...storedBounds,
    frame: false,
    resizable: true,
    alwaysOnTop: isFixed,
    transparent: true,
    skipTaskbar: true,
    icon: path.join(__dirname, "../../public/icon.png"),
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      webSecurity: false,
      nodeIntegration: true,
      webviewTag: true,
    },
  });

  const htmlContent = `
    <html>
      <head>
        <style>
          body {
            margin: 0;
            overflow: hidden;
            background-color: ${isFixed ? "rgba(0,0,0,0)" : "rgba(70, 130, 180, 0.7)"};
            transition: background-color 0.3s;
          }
          #dragRegion {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            -webkit-app-region: drag;
          }
          webview {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            pointer-events: ${isFixed ? "none" : "auto"};
          }
        </style>
      </head>
      <body>
        <div id="dragRegion">
        <webview src="${url}"/>
        </div>
      </body>
      <script>
        electron.on('update-style', (isFixed) => {
        document.body.style.backgroundColor = isFixed ? 'rgba(0,0,0,0)' : 'rgba(70, 130, 180, 0.7)';
        document.querySelector('webview').style.pointerEvents = isFixed ? 'none' : 'auto';
      });
      </script>
    </html>
  `;

  overlayWindow.loadURL(
    `data:text/html;charset=utf-8,${encodeURIComponent(htmlContent)}`,
  );

  overlayWindow.webContents.on("did-finish-load", () => {
    overlayWindow?.webContents.send("fixedMode", isFixed);
  });

  // 윈도우 위치 및 크기 변경 감지 및 저장
  overlayWindow.on("moved", saveBounds);
  overlayWindow.on("resized", saveBounds);

  // 우클릭 메뉴 비활성화
  overlayWindow.hookWindowMessage(278, function () {
    overlayWindow?.setEnabled(false);
    setTimeout(() => overlayWindow?.setEnabled(true), 100);
    return true;
  });

  updateFixedMode(isFixed);
};
