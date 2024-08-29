import { useEffect, useRef, useState } from "react";

export default function Overlay() {
  const webviewRef = useRef(null);

  useEffect(() => {
    const setUrlToWebview = (url : any) => {
        console.log(url);
        webviewRef.current.src = "https://www.naver.com/";
    };

    electron.on("url", setUrlToWebview);
  }, []);

  return (
      <webview ref={webviewRef} className="w-[1000px] h-[600px]"
      style={{ WebkitAppRegion: "drag" } as React.CSSProperties}/>
  );
}