import { useEffect, useRef, useState } from "react";

export default function Overlay() {
  const webviewRef = useRef(null);
  const [dimensions, setDimensions] = useState({ width: window.innerWidth, height: window.innerHeight });

  useEffect(() => {
    const setUrlToWebview = (url: any) => {
      console.log(url);
      webviewRef.current.src = url;
    };

    electron.on("url", setUrlToWebview);

    const handleResize = () => {
      setDimensions({ width: window.innerWidth, height: window.innerHeight });
    };

    window.addEventListener("resize", handleResize);

    return () => {
      window.removeEventListener("resize", handleResize);
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
      } as React.CSSProperties}
    />
  );
}