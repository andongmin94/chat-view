import { ipcMain } from 'electron';
import { updateFixedMode } from './func.js'
import { getStore } from './main.js';
import { adWindow } from './window.js';
/**
 * 개발 환경용 메뉴 설정
 * @param {Function} getMainWindow - 메인 윈도우 객체를 반환하는 함수
 */
interface MainWindowGetter {
  (): Electron.BrowserWindow | null; // 메인 윈도우 객체를 반환하는 함수
}
export function setupIpcHandlers(getMainWindow: MainWindowGetter, getOverlayWindow: () => Electron.BrowserWindow, createOverlayWindow:any) {
  ipcMain.on('hidden', () => {
    if (adWindow) adWindow.hide();
    getMainWindow()?.hide();
  });

  ipcMain.on('minimize', () => {
    getMainWindow()?.minimize();
  });

  ipcMain.on('maximize', () => {
    const mw = getMainWindow();
    if (mw && mw.isMinimized()) {
      mw.restore();
    } else {
      mw?.maximize();
    }
  });

  // 여기에 다른 IPC 핸들러 추가 가능
  ipcMain.handle('get-value', (event, key) => {
    const value = (getStore() as any).get(key);
    if (key === 'chatUrl' && value) {
      createOverlayWindow(value);
    }
    return value;
  });

  ipcMain.on('chatUrl', (event, url) => {
      if (getOverlayWindow() && !getOverlayWindow().isDestroyed()) {
        getOverlayWindow().close();
      }
      createOverlayWindow(url);
    (getStore() as any).set('chatUrl', url);
  });

  ipcMain.on('reInput', () => {
    if (getOverlayWindow() && !getOverlayWindow().isDestroyed()) {
      getOverlayWindow().destroy();
    }
  });

  // 오버레이 고정 모드 설정
  ipcMain.on('set-fixed-mode', (event, isFixed) => {
    (getStore() as any).set('overlayFixed', isFixed);
    updateFixedMode(isFixed);
    getMainWindow()?.webContents.send('fixedMode', isFixed);
  });

  // 리셋 기능
  ipcMain.on('reset', async () => {
      (getStore() as any).clear();
      if (getOverlayWindow() && !getOverlayWindow().isDestroyed()) {
        getOverlayWindow().destroy();
      }
      getMainWindow()?.webContents.send('fixedMode', false);
  });
}