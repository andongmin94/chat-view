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
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";

interface ControlPanelProps {
  isFixed: boolean;
  onFixedModeChange: (checked: boolean) => void | Promise<void>;
  onReset: () => void | Promise<void>;
}

export default function ControlPanel({
  isFixed,
  onFixedModeChange,
  onReset,
}: ControlPanelProps) {
  return (
    <div className="mt-2 flex items-center justify-between">
      <div className="flex items-center">
        <Label htmlFor="fixed-mode" className="mr-2">
          고정 활성화
        </Label>
        <Switch
          id="fixed-mode"
          checked={isFixed}
          onCheckedChange={onFixedModeChange}
        />
      </div>
      <AlertDialog>
        <AlertDialogTrigger asChild>
          <Button variant="destructive">리셋</Button>
        </AlertDialogTrigger>
        <AlertDialogContent className="mt-5 max-w-[355px] rounded-md p-4">
          <AlertDialogHeader>
            <AlertDialogTitle>정말로 리셋하시겠습니까?</AlertDialogTitle>
            <AlertDialogDescription>
              이 작업은 되돌릴 수 없습니다. 모든 설정이 초기화됩니다.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>취소</AlertDialogCancel>
            <AlertDialogAction onClick={onReset}>리셋</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
