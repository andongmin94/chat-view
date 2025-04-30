import React from "react";
import { z } from "zod";

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import {
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import TitleBar from "@/components/TitleBar";

const urlSchema = z.string().url().refine((url) => {
  return url.startsWith("http://afreehp.kr/page/") || url.startsWith("https://chzzk.naver.com/chat/");
});

export default function Component() {
  const [url, setUrl] = React.useState("");
  const [isFixed, setIsFixed] = React.useState(false);
  const [isDialogOpen, setIsDialogOpen] = React.useState(false);
  const [urlError, setUrlError] = React.useState<string | null>(null);
  const [isFirstRun, setIsFirstRun] = React.useState(true);

  React.useEffect(() => {
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
    fetchInitialState();
  }, []);

  const handleUrlChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const newUrl = e.target.value;
    setUrl(newUrl);
    try {
      urlSchema.parse(newUrl);
      setUrlError(null);
    } catch (error) {
      if (error instanceof z.ZodError) {
        setUrlError("http://afreehp.kr/page/ 또는 https://chzzk.naver.com/chat/ 로 시작해야 합니다.");
      }
    }
  };

  const handleFixedToggle = async (checked: boolean) => {
    setIsFixed(checked);
    await electron.setFixedMode(checked);
  };

  const handleApply = () => {
    if (!urlError) {
      setIsDialogOpen(false);
      electron.set("chatUrl", url);
      setIsFirstRun(false);
    }
  };

  const handleReset = async () => {
    await electron.reset();
    setUrl("");
    setIsFixed(false);
    setIsFirstRun(true);
    setIsDialogOpen(true);
  };

  return (
    <div>
      <TitleBar />

      <CardHeader className="flex justify-center">
        <CardTitle>채팅 오버레이 제어판</CardTitle>
        <CardDescription>채팅 오버레이 설정을 관리합니다</CardDescription>
      </CardHeader>
      <CardContent>
        <div className="grid w-full items-center gap-4">
          <Dialog
            open={isDialogOpen}
            onOpenChange={(e) => {
              setIsDialogOpen(e);
              electron.send("reInput");
            }}
          >
            <DialogTrigger asChild>
              <Button variant="outline">
                {isFirstRun ? "URL 입력하기" : "URL 다시 입력하기"}
              </Button>
            </DialogTrigger>
            <DialogContent className="mt-5 max-w-[355px] rounded-md p-4">
              <DialogHeader>
                <DialogTitle>채팅 URL 설정</DialogTitle>
                <DialogDescription>
                  채팅 오버레이에 표시할 URL을 입력하세요.
                </DialogDescription>
              </DialogHeader>
              <div className="grid">
                <div className="grid items-center">
                  <Input id="url" value={url} onChange={handleUrlChange} />
                </div>
                {urlError && <p className="text-sm text-red-500">{urlError}</p>}
              </div>
              <DialogFooter className="grid grid-cols-4">
                <Button
                  onClick={handleApply}
                  disabled={!!urlError}
                  className="col-start-4 text-sm"
                >
                  적용
                </Button>
              </DialogFooter>
            </DialogContent>
          </Dialog>
          <div className="mt-2 flex items-center justify-between">
            <div className="flex items-center">
              <Label htmlFor="fixed-mode" className="mr-2">
                고정 활성화
              </Label>
              <Switch
                id="fixed-mode"
                checked={isFixed}
                onCheckedChange={handleFixedToggle}
              />
            </div>
            <AlertDialog>
              <AlertDialogTrigger asChild>
                <Button variant="destructive">리셋</Button>
              </AlertDialogTrigger>
              <AlertDialogContent>
                <AlertDialogHeader>
                  <AlertDialogTitle>정말로 리셋하시겠습니까?</AlertDialogTitle>
                  <AlertDialogDescription>
                    이 작업은 되돌릴 수 없습니다. 모든 설정이 초기화됩니다.
                  </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                  <AlertDialogCancel>취소</AlertDialogCancel>
                  <AlertDialogAction onClick={handleReset}>
                    리셋
                  </AlertDialogAction>
                </AlertDialogFooter>
              </AlertDialogContent>
            </AlertDialog>
          </div>
        </div>
      </CardContent>
    </div>
  );
}
