import path from "path";
import { app, BrowserWindow } from "electron";

import { saveBounds, updateFixedMode } from "./func.js";
import { __dirname, isDev, store } from "./main.js";
import { closeSplash } from "./splash.js";

export let mainWindow: any;
export let overlayWindow: any;

export function createWindow(port: number) {
  mainWindow = new BrowserWindow({
    show: false,
    width: 416,
    height: 282 + 121,
    frame: false,
    resizable: isDev,
    icon: path.join(__dirname, "../../public/icon.ico"),
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, "preload.js"), // preload 사용 시 주석 해제
    },
  });

  mainWindow.loadURL(`http://localhost:${port}`);

  mainWindow.webContents.on("did-finish-load", () => {
    closeSplash(); // 스플래시 닫기
    mainWindow.show();
  });

  // --- 플랫폼별 우클릭 메뉴 비활성화 시도 ---
  if (process.platform === "win32") {
    mainWindow.on("system-context-menu", (event: any) => {
      event.preventDefault();
    });
  } else {
    mainWindow.webContents.on("context-menu", (event: any) => {
      console.log("Main process context-menu event triggered on macOS/Linux");
      event.preventDefault();
    });
  }

  // 종료 설정
  mainWindow.on("close", (e: any) => {
    if (process.platform === "darwin") {
      // macOS: 사용자가 명시적으로 종료(Cmd+Q 등)하지 않으면 숨김
      e.preventDefault();
      mainWindow.hide();
      app.dock?.hide(); // Dock 에서도 숨김
    }
    // 다른 OS 에서는 window-all-closed 에서 앱 종료 처리
    else app.quit();
  });

  mainWindow.on("closed", () => {
    mainWindow = null; // 창 참조 제거
  });
}

// overlayWindow 생성 및 설정 복원 함수
export const createOverlayWindow = (url: string) => {
  const mainWindowBounds = mainWindow.getBounds();
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
    thickFrame: process.platform === "win32",
    skipTaskbar: true,
    icon: path.join(__dirname, "../../public/icon.ico"),
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
          html,
          body {
            width: 100%;
            height: 100%;
          }
          body {
            --resize-border: 12px;
            margin: 0;
            position: relative;
            overflow: hidden;
            background-color: ${isFixed ? "rgba(0,0,0,0)" : "rgba(70, 130, 180, 0.7)"};
            transition: background-color 0.3s;
          }
          #dragRegion {
            position: absolute;
            top: var(--resize-border);
            left: var(--resize-border);
            right: var(--resize-border);
            bottom: var(--resize-border);
            overflow: hidden;
            -webkit-app-region: drag;
          }
          .resizeHandle {
            position: absolute;
            z-index: 20;
            display: ${process.platform === "win32" ? "block" : "none"};
            -webkit-app-region: no-drag;
          }
          .resizeHandle[data-direction="n"] {
            top: 0;
            left: var(--resize-border);
            right: var(--resize-border);
            height: var(--resize-border);
            cursor: ns-resize;
          }
          .resizeHandle[data-direction="s"] {
            bottom: 0;
            left: var(--resize-border);
            right: var(--resize-border);
            height: var(--resize-border);
            cursor: ns-resize;
          }
          .resizeHandle[data-direction="w"] {
            top: var(--resize-border);
            left: 0;
            bottom: var(--resize-border);
            width: var(--resize-border);
            cursor: ew-resize;
          }
          .resizeHandle[data-direction="e"] {
            top: var(--resize-border);
            right: 0;
            bottom: var(--resize-border);
            width: var(--resize-border);
            cursor: ew-resize;
          }
          .resizeHandle[data-direction="nw"] {
            top: 0;
            left: 0;
            width: var(--resize-border);
            height: var(--resize-border);
            cursor: nwse-resize;
          }
          .resizeHandle[data-direction="ne"] {
            top: 0;
            right: 0;
            width: var(--resize-border);
            height: var(--resize-border);
            cursor: nesw-resize;
          }
          .resizeHandle[data-direction="sw"] {
            bottom: 0;
            left: 0;
            width: var(--resize-border);
            height: var(--resize-border);
            cursor: nesw-resize;
          }
          .resizeHandle[data-direction="se"] {
            right: 0;
            bottom: 0;
            width: var(--resize-border);
            height: var(--resize-border);
            cursor: nwse-resize;
          }
          webview {
            position: absolute;
            top: 0;
            left: 0;
            width: ${Math.max(storedBounds?.width ?? defaultBounds.width, 100000)}px;
            height: 100%;
            pointer-events: ${isFixed ? "none" : "auto"};
          }
        </style>
      </head>
      <body>
        <div id="dragRegion">
          <webview src="${url}"/>
        </div>
        <div class="resizeHandle" data-direction="n"></div>
        <div class="resizeHandle" data-direction="e"></div>
        <div class="resizeHandle" data-direction="s"></div>
        <div class="resizeHandle" data-direction="w"></div>
        <div class="resizeHandle" data-direction="ne"></div>
        <div class="resizeHandle" data-direction="nw"></div>
        <div class="resizeHandle" data-direction="se"></div>
        <div class="resizeHandle" data-direction="sw"></div>
        <script>
        const MANUAL_RESIZE_ENABLED = ${process.platform === "win32"};
        const MIN_WIDTH = 140;
        const MIN_HEIGHT = 120;
        const webview = document.querySelector('webview');

        electron.on('update-style', (isFixed) => {
          document.body.style.backgroundColor = isFixed ? 'rgba(0,0,0,0)' : 'rgba(70, 130, 180, 0.7)';
          if (webview) webview.style.pointerEvents = isFixed ? 'none' : 'auto';
        });

        if (MANUAL_RESIZE_ENABLED) {
          document.querySelectorAll('.resizeHandle').forEach((handle) => {
            handle.addEventListener('pointerdown', async (event) => {
              if (event.button !== 0) return;

              const direction = handle.dataset.direction;
              if (!direction) return;

              event.preventDefault();
              handle.setPointerCapture(event.pointerId);

              let startBounds = null;
              try {
                startBounds = await electron.invoke('overlay-get-bounds');
              } catch {
                handle.releasePointerCapture(event.pointerId);
                return;
              }

              if (!startBounds) {
                handle.releasePointerCapture(event.pointerId);
                return;
              }

              const startX = event.screenX;
              const startY = event.screenY;

              const onPointerMove = (moveEvent) => {
                const deltaX = moveEvent.screenX - startX;
                const deltaY = moveEvent.screenY - startY;

                let x = startBounds.x;
                let y = startBounds.y;
                let width = startBounds.width;
                let height = startBounds.height;

                if (direction.includes('e')) {
                  width = Math.max(MIN_WIDTH, startBounds.width + deltaX);
                }
                if (direction.includes('s')) {
                  height = Math.max(MIN_HEIGHT, startBounds.height + deltaY);
                }
                if (direction.includes('w')) {
                  const nextWidth = Math.max(MIN_WIDTH, startBounds.width - deltaX);
                  x = startBounds.x + (startBounds.width - nextWidth);
                  width = nextWidth;
                }
                if (direction.includes('n')) {
                  const nextHeight = Math.max(MIN_HEIGHT, startBounds.height - deltaY);
                  y = startBounds.y + (startBounds.height - nextHeight);
                  height = nextHeight;
                }

                electron.send('overlay-set-bounds', { x, y, width, height });
              };

              const onPointerUp = (endEvent) => {
                if (handle.hasPointerCapture(endEvent.pointerId)) {
                  handle.releasePointerCapture(endEvent.pointerId);
                }
                handle.removeEventListener('pointermove', onPointerMove);
                handle.removeEventListener('pointerup', onPointerUp);
                handle.removeEventListener('pointercancel', onPointerUp);
              };

              handle.addEventListener('pointermove', onPointerMove);
              handle.addEventListener('pointerup', onPointerUp);
              handle.addEventListener('pointercancel', onPointerUp);
            });
          });
        }
      </script>
      </body>
    </html>
  `;

  overlayWindow.loadURL(
    `data:text/html;charset=utf-8,${encodeURIComponent(htmlContent)}`,
  );

  overlayWindow.webContents.on("did-finish-load", () => {
    overlayWindow.webContents.send("fixedMode", isFixed);
  });

  // 윈도우 위치 및 크기 변경 감지 및 저장
  overlayWindow.on("moved", saveBounds);
  overlayWindow.on("resized", saveBounds);

  // 우클릭 메뉴 비활성화
  overlayWindow.on("system-context-menu", (event: any) => {
    event.preventDefault();
  });

  updateFixedMode(isFixed);
};
