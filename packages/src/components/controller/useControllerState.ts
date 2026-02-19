import { useCallback, useEffect, useState } from "react";

import {
  chatUrlSchema,
  URL_PREFIX_ERROR,
  URL_REQUIRED_ERROR,
} from "@/components/settings/urlSchema";

function validateUrl(url: string) {
  if (!url) {
    return URL_REQUIRED_ERROR;
  }

  const result = chatUrlSchema.safeParse(url);
  if (!result.success) {
    return URL_PREFIX_ERROR;
  }

  return null;
}

export function useControllerState() {
  const [url, setUrl] = useState("");
  const [isFixed, setIsFixed] = useState(false);
  const [isDialogOpen, setIsDialogOpen] = useState(false);
  const [urlError, setUrlError] = useState<string | null>(null);
  const [isFirstRun, setIsFirstRun] = useState(true);

  useEffect(() => {
    const fetchInitialState = async () => {
      try {
        const savedUrl = await electron.get("chatUrl");
        const savedFixedMode = await electron.get("overlayFixed");

        if (typeof savedUrl === "string" && savedUrl.length > 0) {
          setUrl(savedUrl);
          setIsFirstRun(false);
        } else {
          setIsDialogOpen(true);
        }

        setIsFixed(Boolean(savedFixedMode));
      } catch (error) {
        console.error("Failed to load initial controller state:", error);
        setIsDialogOpen(true);
        setUrlError("초기 상태를 불러오지 못했습니다. URL을 다시 입력해주세요.");
      }
    };

    void fetchInitialState();
  }, []);

  const handleDialogOpenChange = useCallback((open: boolean) => {
    setIsDialogOpen(open);
    electron.send("reInput");
  }, []);

  const handleUrlChange = useCallback((nextUrl: string) => {
    setUrl(nextUrl);
    setUrlError(validateUrl(nextUrl));
  }, []);

  const handleApply = useCallback(async () => {
    if (urlError) {
      return;
    }

    try {
      await electron.send("chatUrl", url);
      setIsDialogOpen(false);
      setIsFirstRun(false);
    } catch (error) {
      console.error("Failed to create overlay window:", error);
      const message =
        error instanceof Error && error.message
          ? error.message
          : "오버레이 창을 생성하지 못했습니다.";
      setUrlError(message);
      setIsDialogOpen(true);
    }
  }, [url, urlError]);

  const handleFixedToggle = useCallback(async (checked: boolean) => {
    setIsFixed(checked);
    await electron.send("set-fixed-mode", checked);
  }, []);

  const handleReset = useCallback(async () => {
    await electron.send("reset");
    setUrl("");
    setIsFixed(false);
    setIsFirstRun(true);
    setIsDialogOpen(true);
    setUrlError(null);
  }, []);

  return {
    url,
    isFixed,
    isDialogOpen,
    urlError,
    isFirstRun,
    handleDialogOpenChange,
    handleUrlChange,
    handleApply,
    handleFixedToggle,
    handleReset,
  };
}
