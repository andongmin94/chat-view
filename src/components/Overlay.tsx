import { useEffect, useRef, useState } from "react";

export default function Overlay() {
  const webviewRef = useRef(null);
  const [dimensions, setDimensions] = useState({ width: window.innerWidth, height: window.innerHeight });
  const [isFixed, setIsFixed] = useState(false);

  useEffect(() => {
    const setUrlToWebview = (url: string) => {
      console.log(url);
      if (webviewRef.current) {
        webviewRef.current.src = url;
      }
    };

    const handleFixedMode = (fixed: boolean) => {
      setIsFixed(fixed);
    };

    electron.on("url", setUrlToWebview);
    electron.on("fixedMode", handleFixedMode);

    const handleResize = () => {
      setDimensions({ width: window.innerWidth, height: window.innerHeight });
    };

    window.addEventListener("resize", handleResize);

    return () => {
      window.removeEventListener("resize", handleResize);
      electron.removeAllListeners("url");
      electron.removeAllListeners("fixedMode");
    };
  }, []);

  return (
    <webview
      ref={webviewRef}
      className="webview"
      style={{
        width: `${dimensions.width}px`,
        height: `${dimensions.height}px`,
        WebkitAppRegion: "drag",
        backgroundColor: isFixed ? 'transparent' : 'rgba(200, 200, 200, 0.5)',
        pointerEvents: isFixed ? 'none' : 'auto',
      } as React.CSSProperties}
    />
  );
}