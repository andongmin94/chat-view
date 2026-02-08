import { Button } from "@/components/ui/button";
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

interface ChatUrlDialogProps {
  url: string;
  isDialogOpen: boolean;
  isFirstRun: boolean;
  urlError: string | null;
  onUrlChange: (url: string) => void;
  onApply: () => void;
  onDialogOpenChange: (open: boolean) => void;
}

export default function ChatUrlDialog({
  url,
  isDialogOpen,
  isFirstRun,
  urlError,
  onUrlChange,
  onApply,
  onDialogOpenChange,
}: ChatUrlDialogProps) {
  return (
    <Dialog open={isDialogOpen} onOpenChange={onDialogOpenChange}>
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
            <Input
              id="url"
              value={url}
              onChange={(e) => onUrlChange(e.target.value)}
            />
          </div>
          {urlError && <p className="mt-2 text-sm text-red-500">{urlError}</p>}
        </div>
        <DialogFooter className="grid grid-cols-4">
          <Button
            onClick={onApply}
            disabled={Boolean(urlError)}
            className="col-start-4 text-sm"
          >
            적용
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
