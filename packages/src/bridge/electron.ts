import { invoke } from "@tauri-apps/api/core";

type Listener = (...args: unknown[]) => void;

const listeners = new Map<string, Set<Listener>>();

function normalizeChannel(channel: string): string {
  switch (channel) {
    case "app-runtime-info":
      return "app_runtime_info";
    case "overlay-get-bounds":
      return "overlay_get_bounds";
    default:
      return channel;
  }
}

async function invokeByChannel(channel: string, data?: unknown) {
  const command = normalizeChannel(channel);

  switch (channel) {
    case "app-runtime-info":
      return invoke(command);
    case "overlay-get-bounds":
      return invoke(command);
    default:
      return invoke(command, { payload: data });
  }
}

const electronBridge = {
  send(channel: string, data?: unknown) {
    switch (channel) {
      case "minimize":
        return invoke("minimize_main");
      case "hidden":
        return invoke("hide_main");
      case "chatUrl":
        return invoke("chat_url", { url: data });
      case "reInput":
        return invoke("re_input");
      case "set-fixed-mode":
        return invoke("set_fixed_mode", { isFixed: data });
      case "reset":
        return invoke("reset_state");
      case "overlay-set-bounds":
        return invoke("overlay_set_bounds", { payload: data });
      default:
        return Promise.resolve(null);
    }
  },
  invoke(channel: string, data?: unknown) {
    return invokeByChannel(channel, data);
  },
  on(channel: string, func: Listener) {
    const set = listeners.get(channel) ?? new Set<Listener>();
    set.add(func);
    listeners.set(channel, set);
  },
  get(key: string) {
    return invoke("get_value", { key });
  },
  removeListener(channel: string, func: Listener) {
    const set = listeners.get(channel);
    if (!set) {
      return;
    }

    set.delete(func);
    if (set.size === 0) {
      listeners.delete(channel);
    }
  },
};

(globalThis as Record<string, unknown>).electron = electronBridge;

export {};
