// 일렉트론 모듈
const path = require("path");
const { app, BrowserWindow, ipcMain, Tray, Menu, nativeImage } = require("electron");

// 환경 변수 설정
require("dotenv").config();
let PORT = process.env.NODE_ENV === 'development' ? 3000 : 1994;

// 로컬 웹 서버 모듈
const express = require('express');
const server = express();
const isDev = process.env.NODE_ENV === 'development';

// 개발 모드가 아닐때 빌드 파일 서빙 로직
if (!isDev) {
  // 빌드 파일 서빙
  server.use(express.static(path.join(__dirname, '../../dist')));

  // 루트 경로 요청 처리
  server.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, '../../dist', 'index.html'));
  });

  // 서버 시작
  server.listen(PORT, () => {}).on('error', (err) => {
    // 포트가 이미 사용 중인 경우 다른 포트로 재시도
    if (err.code === 'EADDRINUSE') {
      PORT += 1; // 포트 번호 증가
      setTimeout(() => {
        server.listen(PORT);
      }, 1000); // 1초 후에 다시 시도
    }
  });
}

// 일렉트론 생성 함수
let mainWindow;
let overlayWindow;
let store;

async function createWindow() {
  const { default: Store } = await import('electron-store');
  store = new Store();

  // 브라우저 창 생성
  mainWindow = new BrowserWindow({
    width: 400,
    height: 250,
    frame: false,
    resizable: isDev,
    icon: path.join(__dirname, "../../public/icon.png"),
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      webSecurity: false,
    },
  });

  // 포트 연결
  mainWindow.loadURL(`http://localhost:${PORT}`);
};

// Electron의 초기화가 완료후 브라우저 윈도우 생성
app.whenReady().then(() => {
  createWindow();
  // 기본 생성 세팅
  app.on("window-all-closed", () => {
    if (process.platform !== "darwin") app.quit();
  });
  // macOS-specific settings
  if (process.platform === 'darwin') {
    app.on('before-quit', () => {
      tray.destroy();
    });

    app.on('activate', () => {
      app.dock.show();
      if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });

    mainWindow.on('close', (e) => {
      if (!app.isQuiting) {
        e.preventDefault();
        mainWindow.hide();
        app.dock.hide();
      }
      return false;
    });
  }
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });

  // 타이틀 바 옵션
  ipcMain.on("hidden", () => mainWindow.hide());
  ipcMain.on("minimize", () => mainWindow.minimize());

  // 트레이 세팅
  const tray = new Tray(nativeImage.createFromPath(path.join(__dirname, "../../public/icon.png")));
  tray.setToolTip('챗뷰');
  tray.on("double-click", () => mainWindow.show());
  tray.setContextMenu(
    Menu.buildFromTemplate([
      { label: "리셋", type: "normal", click: () => { store.clear(); app.quit() }},
      { label: "열기", type: "normal", click: () => mainWindow.show() },
      { label: "종료", type: "normal", click: () => app.quit() },
    ])
  );

  // F5 새로고침, F12 개발자 도구 열기
  if (isDev) {
    const menu = Menu.buildFromTemplate([
      {
        label: "File",
        submenu: [
          {
            label: "Reload",
            accelerator: "F5",
            click: () => {
              mainWindow.reload();
            },
          },
          {
            label: "Toggle DevTools",
            accelerator: "F12",
            click: () => {
              mainWindow.webContents.toggleDevTools();
              overlayWindow?.webContents.toggleDevTools();
            },
          },
        ],
      },
    ]);
    Menu.setApplicationMenu(menu);
  }
});

// overlayWindow 생성 및 설정 복원 함수
const createOverlayWindow = (url) => {
  const mainWindowBounds = mainWindow.getBounds();
  const defaultBounds = {
    x: mainWindowBounds.x + mainWindowBounds.width + 20, // 메인 윈도우 오른쪽에 20픽셀 떨어진 위치
    y: mainWindowBounds.y,
    width: 400,
    height: 250,
  };
  const storedBounds = store.get('overlayWindowBounds', defaultBounds);

  overlayWindow = new BrowserWindow({
    ...storedBounds,
    frame: false,
    resizable: isDev,
    // skipTaskbar: true,
    alwaysOnTop: store.get('overlayAlwaysOnTop', false),
    icon: path.join(__dirname, "../../public/icon.png"),
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      webSecurity: false,
      nodeIntegration: true,
      webviewTag: true
    },
  });

  overlayWindow.loadURL(`http://localhost:${PORT}/overlay`);
  overlayWindow.webContents.on('did-finish-load', () => {
    overlayWindow.webContents.send('url', url);
  });
  
  // 윈도우 위치 및 크기 변경 감지 및 저장
  overlayWindow.on('moved', saveBounds);
  overlayWindow.on('resized', saveBounds);

  // 우클릭 메뉴 비활성화
  overlayWindow.hookWindowMessage(278, function(e) {
    overlayWindow.setEnabled(false);
    setTimeout(() => overlayWindow.setEnabled(true), 100);
    return true;
  });

  // 웹 컨텐츠에서 우클릭 메뉴 비활성화
  overlayWindow.webContents.on('context-menu', (e, params) => {
    e.preventDefault();
  });
};

function saveBounds() {
  if (!overlayWindow.isDestroyed()) {
    const bounds = overlayWindow.getBounds();
    store.set('overlayWindowBounds', bounds);
  }
}

ipcMain.handle('get-store-value', (event, key) => {
  const value = store.get(key);
  if (key === 'chatUrl') {
    createOverlayWindow(value);
  }
  return value;
});

ipcMain.handle('set-store-value', (event, key, value) => {
  overlayWindow?.close();
  if (key === 'chatUrl') {
    createOverlayWindow(value);
  }
  store.set(key, value);
});

// 오버레이 윈도우 토글
ipcMain.handle('toggle-overlay', (event, shouldShow) => {
  if (shouldShow) {
    overlayWindow?.show();
  } else {
    overlayWindow?.hide();
  }
});

// 오버레이 윈도우 고정/고정 해제
ipcMain.handle('set-overlay-always-on-top', (event, alwaysOnTop) => {
  overlayWindow?.setAlwaysOnTop(alwaysOnTop);
  store.set('overlayAlwaysOnTop', alwaysOnTop);
});