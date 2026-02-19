use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{collections::HashMap, fs, path::PathBuf, sync::Mutex};
use tauri::{
  menu::{Menu, MenuItem},
  tray::{MouseButton, MouseButtonState, TrayIcon, TrayIconBuilder, TrayIconEvent},
  utils::config::Color,
  AppHandle, Manager, PhysicalPosition, PhysicalSize, State, WebviewUrl, WebviewWindow,
  WebviewWindowBuilder, Window, WindowEvent,
};

const MAIN_LABEL: &str = "main";
const OVERLAY_LABEL: &str = "overlay";
const STORE_FILE_NAME: &str = "store.json";
const DEFAULT_OVERLAY_WIDTH: u32 = 400;
const DEFAULT_OVERLAY_HEIGHT: u32 = 270;
const MIN_OVERLAY_WIDTH: u32 = 140;
const MIN_OVERLAY_HEIGHT: u32 = 120;
const TRAY_SHOW_MENU_ID: &str = "tray_show";
const TRAY_QUIT_MENU_ID: &str = "tray_quit";

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct WindowBounds {
  x: i32,
  y: i32,
  width: u32,
  height: u32,
}

impl Default for WindowBounds {
  fn default() -> Self {
    Self {
      x: 100,
      y: 100,
      width: DEFAULT_OVERLAY_WIDTH,
      height: DEFAULT_OVERLAY_HEIGHT,
    }
  }
}

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
struct PersistedStore {
  values: HashMap<String, Value>,
}

struct AppState {
  store: Mutex<PersistedStore>,
  is_quitting: Mutex<bool>,
  tray: Mutex<Option<TrayIcon>>,
  suppress_overlay_bounds_persist: Mutex<bool>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct RuntimeInfo {
  install_mode: String,
  platform: String,
  arch: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct OverlayBootstrap {
  url: String,
  is_fixed: bool,
}

fn store_file_path(app: &AppHandle) -> Result<PathBuf, String> {
  let mut dir = app
    .path()
    .app_data_dir()
    .map_err(|err| format!("failed to resolve app data directory: {err}"))?;

  fs::create_dir_all(&dir)
    .map_err(|err| format!("failed to create app data directory: {err}"))?;

  dir.push(STORE_FILE_NAME);
  Ok(dir)
}

fn load_store_from_disk(app: &AppHandle) -> PersistedStore {
  let Ok(path) = store_file_path(app) else {
    return PersistedStore::default();
  };

  let Ok(raw) = fs::read_to_string(path) else {
    return PersistedStore::default();
  };

  serde_json::from_str(&raw).unwrap_or_default()
}

fn save_store_to_disk(app: &AppHandle, store: &PersistedStore) -> Result<(), String> {
  let path = store_file_path(app)?;
  let payload = serde_json::to_string_pretty(store)
    .map_err(|err| format!("failed to serialize store: {err}"))?;

  fs::write(path, payload).map_err(|err| format!("failed to write store: {err}"))
}

fn get_store_value(state: &State<'_, AppState>, key: &str) -> Result<Option<Value>, String> {
  let guard = state
    .store
    .lock()
    .map_err(|_| "failed to lock application store".to_string())?;

  Ok(guard.values.get(key).cloned())
}

fn set_store_value(
  app: &AppHandle,
  state: &State<'_, AppState>,
  key: &str,
  value: Value,
) -> Result<(), String> {
  let mut guard = state
    .store
    .lock()
    .map_err(|_| "failed to lock application store".to_string())?;
  guard.values.insert(key.to_string(), value);
  save_store_to_disk(app, &guard)
}

fn clear_store(app: &AppHandle, state: &State<'_, AppState>) -> Result<(), String> {
  let mut guard = state
    .store
    .lock()
    .map_err(|_| "failed to lock application store".to_string())?;
  guard.values.clear();
  save_store_to_disk(app, &guard)
}

fn overlay_window(app: &AppHandle) -> Option<WebviewWindow> {
  app.get_webview_window(OVERLAY_LABEL)
}

fn set_overlay_bounds_persist_suppressed(state: &State<'_, AppState>, suppressed: bool) {
  if let Ok(mut guard) = state.suppress_overlay_bounds_persist.lock() {
    *guard = suppressed;
  }
}

fn is_overlay_bounds_persist_suppressed(state: &State<'_, AppState>) -> bool {
  state
    .suppress_overlay_bounds_persist
    .lock()
    .map(|guard| *guard)
    .unwrap_or(false)
}

fn close_overlay_window(app: &AppHandle) {
  let state = app.state::<AppState>();
  let _ = save_overlay_bounds(app, &state);
  set_overlay_bounds_persist_suppressed(&state, true);

  if let Some(window) = overlay_window(app) {
    let _ = window.close();
  }
}

fn default_overlay_bounds(app: &AppHandle) -> WindowBounds {
  let mut bounds = WindowBounds::default();

  let Some(main_window) = app.get_webview_window(MAIN_LABEL) else {
    return bounds;
  };

  let Ok(position) = main_window.outer_position() else {
    return bounds;
  };

  let Ok(size) = main_window.outer_size() else {
    return bounds;
  };
  let scale = main_window.scale_factor().unwrap_or(1.0);
  let scaled_width = ((DEFAULT_OVERLAY_WIDTH as f64) * scale).round() as u32;
  let scaled_height = ((DEFAULT_OVERLAY_HEIGHT as f64) * scale).round() as u32;
  bounds.width = scaled_width.max(MIN_OVERLAY_WIDTH);
  bounds.height = scaled_height.max(MIN_OVERLAY_HEIGHT);

  let main_width = i32::try_from(size.width).unwrap_or(0);
  let overlay_width = i32::try_from(bounds.width).unwrap_or(0);
  let overlay_height = i32::try_from(bounds.height).unwrap_or(0);

  bounds.x = position.x + main_width + 20;
  bounds.y = position.y;

  if let Ok(Some(monitor)) = main_window.current_monitor() {
    let monitor_pos = monitor.position();
    let monitor_size = monitor.size();

    let monitor_left = monitor_pos.x;
    let monitor_top = monitor_pos.y;
    let monitor_right = monitor_pos.x + i32::try_from(monitor_size.width).unwrap_or(0);
    let monitor_bottom = monitor_pos.y + i32::try_from(monitor_size.height).unwrap_or(0);

    let bounds_right = bounds.x + overlay_width;
    if bounds_right > monitor_right {
      let left_candidate = position.x - overlay_width - 20;
      if left_candidate >= monitor_left {
        bounds.x = left_candidate;
      } else {
        bounds.x = (monitor_right - overlay_width).max(monitor_left);
      }
    }

    if bounds.x < monitor_left {
      bounds.x = monitor_left;
    }

    let max_y = monitor_bottom - overlay_height;
    if max_y >= monitor_top {
      bounds.y = bounds.y.clamp(monitor_top, max_y);
    } else {
      bounds.y = monitor_top;
    }
  }

  bounds
}

fn normalize_bounds_size(mut bounds: WindowBounds) -> WindowBounds {
  bounds.width = bounds.width.max(MIN_OVERLAY_WIDTH);
  bounds.height = bounds.height.max(MIN_OVERLAY_HEIGHT);
  bounds
}

fn read_overlay_bounds(state: &State<'_, AppState>, app: &AppHandle) -> WindowBounds {
  let stored = get_store_value(state, "overlayWindowBounds")
    .ok()
    .flatten()
    .and_then(|value| serde_json::from_value::<WindowBounds>(value).ok())
    .map(normalize_bounds_size);

  stored.unwrap_or_else(|| default_overlay_bounds(app))
}

fn read_fixed_mode(state: &State<'_, AppState>) -> bool {
  get_store_value(state, "overlayFixed")
    .ok()
    .flatten()
    .and_then(|value| value.as_bool())
    .unwrap_or(false)
}

fn read_chat_url(state: &State<'_, AppState>) -> Option<String> {
  get_store_value(state, "chatUrl")
    .ok()
    .flatten()
    .and_then(|value| value.as_str().map(ToString::to_string))
}

fn should_persist_overlay_bounds(state: &State<'_, AppState>) -> bool {
  read_chat_url(state).is_some()
}

fn show_main_window(app: &AppHandle) {
  if let Some(main_window) = app.get_webview_window(MAIN_LABEL) {
    let _ = main_window.show();
    let _ = main_window.unminimize();
    let _ = main_window.set_focus();
  }
}

fn setup_tray(app: &AppHandle) -> Result<(), String> {
  let show_item = MenuItem::with_id(app, TRAY_SHOW_MENU_ID, "열기", true, None::<&str>)
    .map_err(|err| format!("failed to create tray show menu item: {err}"))?;
  let quit_item = MenuItem::with_id(app, TRAY_QUIT_MENU_ID, "종료", true, None::<&str>)
    .map_err(|err| format!("failed to create tray quit menu item: {err}"))?;
  let menu = Menu::with_items(app, &[&show_item, &quit_item])
    .map_err(|err| format!("failed to create tray menu: {err}"))?;

  let mut builder = TrayIconBuilder::with_id("main")
    .menu(&menu)
    .tooltip("챗뷰")
    .show_menu_on_left_click(false)
    .on_tray_icon_event(|tray: &TrayIcon, event: TrayIconEvent| {
      if let TrayIconEvent::Click {
        button: MouseButton::Left,
        button_state: MouseButtonState::Up,
        ..
      } = event
      {
        show_main_window(&tray.app_handle());
      }
    })
    .on_menu_event(|app: &AppHandle, event: tauri::menu::MenuEvent| {
      if event.id() == TRAY_SHOW_MENU_ID {
        show_main_window(app);
      } else if event.id() == TRAY_QUIT_MENU_ID {
        let state = app.state::<AppState>();
        if let Ok(mut quitting) = state.is_quitting.lock() {
          *quitting = true;
        }
        app.exit(0);
      }
    });

  if let Some(icon) = app.default_window_icon().cloned() {
    builder = builder.icon(icon);
  }

  let tray = builder
    .build(app)
    .map_err(|err| format!("failed to build tray icon: {err}"))?;

  let state = app.state::<AppState>();
  let mut guard = state
    .tray
    .lock()
    .map_err(|_| "failed to lock tray state".to_string())?;
  *guard = Some(tray);

  Ok(())
}

fn setup_main_close_behavior(app: &AppHandle) -> Result<(), String> {
  let main_window = app
    .get_webview_window(MAIN_LABEL)
    .ok_or_else(|| "main window not found".to_string())?;

  let app_handle = app.clone();
  let main_for_hide = main_window.clone();

  main_window.on_window_event(move |event| {
    if let WindowEvent::CloseRequested { api, .. } = event {
      let state = app_handle.state::<AppState>();
      let should_quit = state
        .is_quitting
        .lock()
        .map(|flag| *flag)
        .unwrap_or(false);

      if !should_quit {
        api.prevent_close();
        let _ = main_for_hide.hide();
      }
    }
  });

  Ok(())
}

fn save_overlay_bounds(app: &AppHandle, state: &State<'_, AppState>) -> Result<(), String> {
  if !should_persist_overlay_bounds(state) {
    return Ok(());
  }
  if is_overlay_bounds_persist_suppressed(state) {
    return Ok(());
  }

  let Some(window) = overlay_window(app) else {
    return Ok(());
  };

  let position = window
    .outer_position()
    .map_err(|err| format!("failed to read overlay position: {err}"))?;
  let size = window
    .inner_size()
    .map_err(|err| format!("failed to read overlay inner size: {err}"))?;

  let bounds = WindowBounds {
    x: position.x,
    y: position.y,
    width: size.width,
    height: size.height,
  };
  let bounds = normalize_bounds_size(bounds);

  let payload =
    serde_json::to_value(bounds).map_err(|err| format!("failed to encode overlay bounds: {err}"))?;
  set_store_value(app, state, "overlayWindowBounds", payload)
}

fn apply_fixed_mode(app: &AppHandle, is_fixed: bool) -> Result<(), String> {
  let Some(window) = overlay_window(app) else {
    return Ok(());
  };

  window
    .set_background_color(Some(Color(0, 0, 0, 0)))
    .map_err(|err| format!("failed to set overlay background color: {err}"))?;

  window
    .set_always_on_top(is_fixed)
    .map_err(|err| format!("failed to set always-on-top mode: {err}"))?;

  window
    .set_ignore_cursor_events(is_fixed)
    .map_err(|err| format!("failed to set click-through mode: {err}"))?;

  let _ = window.eval(format!("window.__chatviewSetFixedMode?.({is_fixed});"));

  Ok(())
}

fn create_overlay_window(
  app: &AppHandle,
  state: &State<'_, AppState>,
  _url: &str,
) -> Result<(), String> {
  close_overlay_window(app);

  let bounds = read_overlay_bounds(state, app);
  let is_fixed = read_fixed_mode(state);

  let mut builder = WebviewWindowBuilder::new(
    app,
    OVERLAY_LABEL,
    WebviewUrl::App("overlay.html".into()),
  )
    .title("챗뷰 오버레이")
    .decorations(false)
    .transparent(true)
    .shadow(false)
    .resizable(true)
    .always_on_top(is_fixed)
    .visible(false)
    .skip_taskbar(true);

  if let Some(icon) = app.default_window_icon().cloned() {
    builder = builder
      .icon(icon)
      .map_err(|err| format!("failed to set overlay icon: {err}"))?;
  }

  let window = builder
    .build()
    .map_err(|err| format!("failed to create overlay window: {err}"))?;

  set_overlay_bounds_persist_suppressed(state, false);

  window
    .set_position(PhysicalPosition::new(bounds.x, bounds.y))
    .map_err(|err| format!("failed to set initial overlay position: {err}"))?;
  window
    .set_size(PhysicalSize::new(bounds.width, bounds.height))
    .map_err(|err| format!("failed to set initial overlay size: {err}"))?;
  window
    .show()
    .map_err(|err| format!("failed to show overlay window: {err}"))?;
  // On Windows multi-DPI setups, hidden-window sizing can be scaled on show.
  // Re-apply the target size once the window is visible on its final monitor.
  window
    .set_size(PhysicalSize::new(bounds.width, bounds.height))
    .map_err(|err| format!("failed to normalize overlay size after show: {err}"))?;

  let app_for_events = app.clone();
  window.on_window_event(move |event| {
    if matches!(event, WindowEvent::Moved(_) | WindowEvent::Resized(_)) {
      let state = app_for_events.state::<AppState>();
      let _ = save_overlay_bounds(&app_for_events, &state);
    }
  });

  apply_fixed_mode(app, is_fixed)?;
  Ok(())
}

async fn create_overlay_window_async(app: AppHandle, url: String) -> Result<(), String> {
  tauri::async_runtime::spawn_blocking(move || {
    let state = app.state::<AppState>();
    create_overlay_window(&app, &state, &url)
  })
  .await
  .map_err(|err| format!("overlay worker failed: {err}"))?
}

#[tauri::command]
fn minimize_main(window: Window) -> Result<(), String> {
  window
    .minimize()
    .map_err(|err| format!("failed to minimize window: {err}"))
}

#[tauri::command]
fn hide_main(window: Window) -> Result<(), String> {
  window
    .hide()
    .map_err(|err| format!("failed to hide window: {err}"))
}

#[tauri::command]
async fn get_value(
  app: AppHandle,
  state: State<'_, AppState>,
  key: String,
) -> Result<Option<Value>, String> {
  let value = get_store_value(&state, &key)?;

  if key == "chatUrl" {
    if let Some(Value::String(url)) = value.clone() {
      log::info!("restoring overlay window from persisted chatUrl");
      create_overlay_window_async(app.clone(), url).await?;
    }
  }

  Ok(value)
}

#[tauri::command]
async fn chat_url(
  app: AppHandle,
  state: State<'_, AppState>,
  url: String,
) -> Result<(), String> {
  let normalized = url.trim().to_string();
  log::info!("creating overlay window from chat_url command");

  set_store_value(&app, &state, "chatUrl", Value::String(normalized.clone()))?;
  create_overlay_window_async(app.clone(), normalized).await
}

#[tauri::command]
fn re_input(app: AppHandle) {
  close_overlay_window(&app);
}

#[tauri::command]
fn overlay_get_bounds(app: AppHandle) -> Result<Option<WindowBounds>, String> {
  let Some(window) = overlay_window(&app) else {
    return Ok(None);
  };

  let position = window
    .outer_position()
    .map_err(|err| format!("failed to read overlay position: {err}"))?;
  let size = window
    .inner_size()
    .map_err(|err| format!("failed to read overlay inner size: {err}"))?;

  let bounds = normalize_bounds_size(WindowBounds {
    x: position.x,
    y: position.y,
    width: size.width,
    height: size.height,
  });

  Ok(Some(bounds))
}

#[tauri::command]
fn overlay_set_bounds(
  app: AppHandle,
  state: State<'_, AppState>,
  payload: Value,
) -> Result<(), String> {
  let mut bounds =
    serde_json::from_value::<WindowBounds>(payload).map_err(|_| "invalid bounds payload".to_string())?;

  bounds.width = bounds.width.max(MIN_OVERLAY_WIDTH);
  bounds.height = bounds.height.max(MIN_OVERLAY_HEIGHT);

  if let Some(window) = overlay_window(&app) {
    window
      .set_position(PhysicalPosition::new(bounds.x, bounds.y))
      .map_err(|err| format!("failed to set overlay position: {err}"))?;
    window
      .set_size(PhysicalSize::new(bounds.width, bounds.height))
      .map_err(|err| format!("failed to set overlay size: {err}"))?;
  }

  if !should_persist_overlay_bounds(&state) {
    return Ok(());
  }

  let encoded =
    serde_json::to_value(bounds).map_err(|err| format!("failed to encode bounds: {err}"))?;
  set_store_value(&app, &state, "overlayWindowBounds", encoded)
}

#[tauri::command]
fn set_fixed_mode(
  app: AppHandle,
  state: State<'_, AppState>,
  is_fixed: bool,
) -> Result<(), String> {
  set_store_value(&app, &state, "overlayFixed", Value::Bool(is_fixed))?;
  apply_fixed_mode(&app, is_fixed)
}

#[tauri::command]
fn overlay_bootstrap(state: State<'_, AppState>) -> Result<Option<OverlayBootstrap>, String> {
  let Some(url) = read_chat_url(&state) else {
    return Ok(None);
  };

  Ok(Some(OverlayBootstrap {
    url,
    is_fixed: read_fixed_mode(&state),
  }))
}

#[tauri::command]
fn reset_state(app: AppHandle, state: State<'_, AppState>) -> Result<(), String> {
  close_overlay_window(&app);
  clear_store(&app, &state)?;
  Ok(())
}

#[tauri::command]
fn app_runtime_info() -> RuntimeInfo {
  let has_portable_context = std::env::var_os("PORTABLE_EXECUTABLE_FILE").is_some()
    || std::env::var_os("PORTABLE_EXECUTABLE_DIR").is_some();

  let install_mode = if cfg!(target_os = "windows") && has_portable_context {
    "portable"
  } else if cfg!(target_os = "windows") {
    "msi"
  } else {
    "unknown"
  };

  RuntimeInfo {
    install_mode: install_mode.to_string(),
    platform: std::env::consts::OS.to_string(),
    arch: std::env::consts::ARCH.to_string(),
  }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
  tauri::Builder::default()
    .setup(|app| {
      let loaded_store = load_store_from_disk(app.handle());
      app.manage(AppState {
        store: Mutex::new(loaded_store),
        is_quitting: Mutex::new(false),
        tray: Mutex::new(None),
        suppress_overlay_bounds_persist: Mutex::new(false),
      });

      if cfg!(debug_assertions) {
        app.handle().plugin(
          tauri_plugin_log::Builder::default()
            .level(log::LevelFilter::Info)
            .build(),
        )?;
      }

      if !cfg!(debug_assertions) {
        if let Some(main_window) = app.get_webview_window(MAIN_LABEL) {
          if let Err(err) = main_window.set_resizable(false) {
            log::error!("failed to disable resize on main window: {err}");
          }
          if let Err(err) = main_window.set_maximizable(false) {
            log::error!("failed to disable maximize on main window: {err}");
          }
        } else {
          log::error!("failed to find main window while disabling resize");
        }
      }

      if let Err(err) = setup_main_close_behavior(app.handle()) {
        log::error!("failed to initialize main close behavior: {err}");
      }
      if let Err(err) = setup_tray(app.handle()) {
        log::error!("failed to initialize system tray: {err}");
      }

      Ok(())
    })
    .invoke_handler(tauri::generate_handler![
      minimize_main,
      hide_main,
      get_value,
      chat_url,
      re_input,
      app_runtime_info,
      overlay_bootstrap,
      overlay_get_bounds,
      overlay_set_bounds,
      set_fixed_mode,
      reset_state
    ])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}

