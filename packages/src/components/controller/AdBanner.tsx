export default function AdBanner() {
  return (
    <>
      <div className="fixed bottom-0 left-0 h-[121px] w-full">
        <iframe
          src="https://andongmin.com/ad/chat-view"
          className="h-full w-full border-0"
          sandbox="allow-scripts allow-same-origin"
        />
        <a
          href="https://chat-view.andongmin.com/"
          target="_blank"
          rel="noopener noreferrer"
          className="absolute top-0 left-0 h-full w-full"
        />
      </div>
      <div className="pb-[121px]">{/* 기존 컨텐츠 래퍼 */}</div>
    </>
  );
}
