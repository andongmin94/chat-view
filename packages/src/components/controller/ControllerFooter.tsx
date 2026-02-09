import { useMemo } from "react";

import { useUpdateNotice } from "@/components/controller/useUpdateNotice";
import { Button } from "@/components/ui/button";

interface ControllerFooterProps {
  version: string;
}

export default function ControllerFooter({ version }: ControllerFooterProps) {
  const { status, release, message } = useUpdateNotice(version);

  const statusText = useMemo(() => {
    if (status === "idle") {
      return null;
    }
    if (status === "checking") {
      return "업데이트 확인 중...";
    }
    return message;
  }, [message, status]);

  return (
    <div className="pointer-events-auto mr-1 flex items-center justify-between gap-2 text-xs">
      <div className="flex min-w-0 items-center gap-2">
        {statusText && (
          <span
            className="text-muted-foreground max-w-[210px] truncate"
            title={statusText}
          >
            {statusText}
          </span>
        )}
        {status === "available" && release?.downloadUrl && (
          <Button asChild size="sm" className="h-6 px-2 text-xs">
            <a
              href={release.downloadUrl}
              target="_blank"
              rel="noopener noreferrer"
            >
              다운로드
            </a>
          </Button>
        )}
      </div>
      <span className="pointer-events-none shrink-0">v{version}</span>
    </div>
  );
}
