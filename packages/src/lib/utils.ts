import { clsx, type ClassValue } from "clsx";
import { twMerge } from "tailwind-merge";

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

declare global {
  var electron: {
    send: (channel: string, data?: unknown) => Promise<unknown>;
    invoke: (channel: string, data?: unknown) => Promise<unknown>;
    on: (channel: string, func: (...args: unknown[]) => void) => void;
    get: (key: string) => Promise<unknown>;
    removeListener: (
      channel: string,
      func: (...args: unknown[]) => void,
    ) => void;
  };
}
