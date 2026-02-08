import {
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/card";
import AdBanner from "@/components/controller/AdBanner";
import ChatUrlDialog from "@/components/controller/ChatUrlDialog";
import ControllerFooter from "@/components/controller/ControllerFooter";
import ControlPanel from "@/components/controller/ControlPanel";
import { useControllerState } from "@/components/controller/useControllerState";
import TitleBar from "@/components/TitleBar";

import packageJson from "../../package.json";

export default function Controller() {
  const {
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
  } = useControllerState();

  return (
    <>
      <TitleBar />

      <CardHeader className="flex justify-center">
        <CardTitle>채팅 오버레이 제어판</CardTitle>
        <CardDescription>채팅 오버레이 설정을 관리합니다</CardDescription>
      </CardHeader>
      <CardContent>
        <div className="grid w-full items-center gap-4">
          <ChatUrlDialog
            url={url}
            isDialogOpen={isDialogOpen}
            isFirstRun={isFirstRun}
            urlError={urlError}
            onUrlChange={handleUrlChange}
            onApply={handleApply}
            onDialogOpenChange={handleDialogOpenChange}
          />
          <ControlPanel
            isFixed={isFixed}
            onFixedModeChange={handleFixedToggle}
            onReset={handleReset}
          />
        </div>
      </CardContent>
      <ControllerFooter version={packageJson.version} />
      <AdBanner />
    </>
  );
}
