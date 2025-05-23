// 일렉트론 모듈
const path = require("path");
const { app, BrowserWindow, ipcMain, Tray, Menu, nativeImage, shell } = require("electron");

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
  server.listen(PORT, 'localhost', () => {}).on('error', (err) => {
    // 포트가 이미 사용 중인 경우 다른 포트로 재시도
    if (err.code === 'EADDRINUSE') {
      PORT += 1; // 포트 번호 증가
      setTimeout(() => {
        server.listen(PORT, 'localhost');
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
    width: 416,
    height: 282,
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

  // 메인 윈도우가 로드된 후 고정 활성화 상태 전송
  mainWindow.webContents.on('did-finish-load', () => {
    const isFixed = store.get('overlayFixed', false);
    mainWindow.webContents.send('fixedMode', isFixed);
  });

  // 예시 조건: 오버레이 윈도우가 항상 위에 떠 있는 상태일 때만 포커스
  mainWindow.on('focus', () => {
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.focus();
    }
  });

  // 우클릭 메뉴 비활성화
  mainWindow.hookWindowMessage(278, function(e) {
    mainWindow.setEnabled(false);
    setTimeout(() => mainWindow.setEnabled(true), 100);
    return true;
  });

  // 종료 설정
  if (process.platform === 'darwin') {
    mainWindow.on('close', (e) => {
      if (!app.isQuiting) {
        e.preventDefault();
        mainWindow.hide();
        app.dock.hide();
      }
      return false;
    });
  } else {
    mainWindow.on('close', () => {
      app.quit();
    });
  }
  
  // 광고용 윈도우 생성
  const adWindowWidth = mainWindow.getSize()[0];
  const adWindowHeight = 121;
  adWindow = new BrowserWindow({
    width: adWindowWidth,
    height: adWindowHeight,
    frame: false,
    resizable: false,
    skipTaskbar: true,
    show: false,
    parent: mainWindow, // 메인 윈도우를 부모로 설정
    webPreferences: {
      webSecurity: false,
    },
  });

  adWindow.loadURL('https://andongmin.com/ad/chat-view');

  const updateAdWindowPosition = () => {
    const mainBounds = mainWindow.getBounds();
    adWindow.setBounds({
      x: mainBounds.x,
      y: mainBounds.y + mainBounds.height,
      width: mainBounds.width,
      height: adWindowHeight,
    });
  };

  // 메인 윈도우가 로드된 후 광고용 윈도우를 보여줌
  mainWindow.webContents.on('did-finish-load', () => {
    updateAdWindowPosition();
    adWindow.show();
  });

  // 메인 윈도우가 이동할 때 광고용 윈도우의 위치를 업데이트
  mainWindow.on('move', updateAdWindowPosition);
  mainWindow.on('resize', updateAdWindowPosition);
  mainWindow.on('show', () => {
    adWindow.show();
  })

  adWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });
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
  } else {
    // 모든 플랫폼에 적용되는 activate 이벤트 핸들러 (macOS 제외)
    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
  }

  // 타이틀 바 옵션
  ipcMain.on("minimize", () => mainWindow.minimize());
  ipcMain.on("hidden", () => {
      if (adWindow) adWindow.hide();
      mainWindow.hide();
    }
  );

  // 트레이 세팅
  const tray = new Tray(nativeImage.createFromPath(path.join(__dirname, "../../public/icon.png")));
  tray.setToolTip('챗뷰');
  tray.on("double-click", () => mainWindow.show());
  tray.setContextMenu(
    Menu.buildFromTemplate([
      { label: "열기", type: "normal", click: () => mainWindow.show() },
      { label: "종료", type: "normal", click: () => mainWindow.close()},
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
    x: mainWindowBounds.x + mainWindowBounds.width + 20,
    y: mainWindowBounds.y,
    width: 400,
    height: 270,
  };
  const storedBounds = store.get('overlayWindowBounds', defaultBounds);
  const isFixed = store.get('overlayFixed', false);

  overlayWindow = new BrowserWindow({
    ...storedBounds,
    frame: false,
    resizable: true,
    alwaysOnTop: isFixed,
    transparent: true,
    skipTaskbar: true,
    icon: path.join(__dirname, "../../public/icon.png"),
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      webSecurity: false,
      nodeIntegration: true,
      webviewTag: true
    },
  });

  const htmlContent = `
    <html>
      <head>
        <style>
          body {
            margin: 0;
            overflow: hidden;
            background-color: ${isFixed ? 'rgba(0,0,0,0)' : 'rgba(70, 130, 180, 0.7)'};
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
            pointer-events: ${isFixed ? 'none' : 'auto'};
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

  overlayWindow.loadURL(`data:text/html;charset=utf-8,${encodeURIComponent(htmlContent)}`);
  
  overlayWindow.webContents.on('did-finish-load', () => {
    overlayWindow.webContents.send('fixedMode', isFixed);
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

  updateFixedMode(isFixed);
};

function updateFixedMode(isFixed) {
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    overlayWindow.setAlwaysOnTop(isFixed);
    overlayWindow.setIgnoreMouseEvents(isFixed, {forward: true});
    overlayWindow.webContents.send('update-style', isFixed);
  }
}

function saveBounds() {
  if (!overlayWindow.isDestroyed()) {
    const bounds = overlayWindow.getBounds();
    store.set('overlayWindowBounds', bounds);
  }
}

ipcMain.handle('get-value', (event, key) => {
  const value = store.get(key);
  if (key === 'chatUrl' && value) {
    createOverlayWindow(value);
  }
  return value;
});

ipcMain.on('chatUrl', (event, url) => {
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.close();
    }
    createOverlayWindow(url);
  store.set('chatUrl', url);
});

ipcMain.on('reInput', () => {
  if (overlayWindow && !overlayWindow.isDestroyed()) {
    overlayWindow.destroy();
  }
});

// 오버레이 고정 모드 설정
ipcMain.on('set-fixed-mode', (event, isFixed) => {
  store.set('overlayFixed', isFixed);
  updateFixedMode(isFixed);
  mainWindow.webContents.send('fixedMode', isFixed);
});

// 리셋 기능
ipcMain.on('reset', async () => {
    store.clear();
    if (overlayWindow && !overlayWindow.isDestroyed()) {
      overlayWindow.destroy();
    }
    mainWindow.webContents.send('fixedMode', false);
});
