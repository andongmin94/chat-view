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
      const savedUrl = await electron.get("chatUrl");
      const savedFixedMode = await electron.get("overlayFixed");

      if (savedUrl) {
        setUrl(savedUrl);
        setIsFirstRun(false);
      } else {
        setIsDialogOpen(true);
      }

      setIsFixed(savedFixedMode || false);
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

  const handleApply = useCallback(() => {
    if (urlError) {
      return;
    }

    setIsDialogOpen(false);
    electron.send("chatUrl", url);
    setIsFirstRun(false);
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
