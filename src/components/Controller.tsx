import React from "react";
import { z } from "zod";

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

//////////////// electron components ////////////////
import TitleBar from "@/components/TitleBar";
/////////////////////////////////////////////////////

const urlSchema = z.string().url().startsWith("http://afreehp.kr/page/");

export default function Component() {
  const [url, setUrl] = React.useState("");
  const [isFixed, setIsFixed] = React.useState(false);
  const [isDialogOpen, setIsDialogOpen] = React.useState(false);
  const [urlError, setUrlError] = React.useState<string | null>(null);
  const [isFirstRun, setIsFirstRun] = React.useState(true);

  React.useEffect(() => {
    const fetchUrl = async () => {
      const savedUrl = await electron.get("chatUrl");
      if (savedUrl) {
        setUrl(savedUrl);
        setIsFirstRun(false);
      } else {
        setIsDialogOpen(true);
      }
    };
    fetchUrl();
  }, []);

  const handleUrlChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const newUrl = e.target.value;
    setUrl(newUrl);
    try {
      urlSchema.parse(newUrl);
      setUrlError(null);
    } catch (error) {
      if (error instanceof z.ZodError) {
        setUrlError("http://afreehp.kr/page/로 시작해야 합니다.");
      }
    }
  };

  const handleFixedToggle = (checked: boolean) => {
    setIsFixed(checked);
    // Here you would typically call electron IPC to update overlay window settings
    console.log("고정 활성화:", checked);
  };

  const handleApply = () => {
    if (!urlError) {
      setIsDialogOpen(false);
      electron.set("chatUrl", url);
      setIsFirstRun(false);
      // Here you would typically call electron IPC to update the URL
      console.log("URL 적용:", url);
    }
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
          <Dialog open={isDialogOpen} onOpenChange={setIsDialogOpen}>
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
          <div className="mt-2 flex items-center justify-end">
            <Label htmlFor="fixed-mode">고정 활성화</Label>
            <Switch
              id="fixed-mode"
              checked={isFixed}
              onCheckedChange={handleFixedToggle}
              className="ml-4"
            />
          </div>
        </div>
      </CardContent>
    </div>
  );
}
