import fs from "node:fs/promises";
import path from "node:path";
import type { LatestReleaseData } from "./getReleaseData";

const RELEASES_LATEST_URL =
  "https://github.com/andongmin94/chat-view/releases/latest";

function buildWindowsBlocks(assets: LatestReleaseData["assets"]) {
  const blocks: string[] = [];

  if (assets.exe) {
    blocks.push(`  - icon:
      dark: /windows-white.svg
      light: /windows-black.svg
      width: 100px
    title: Windows 다운로드
    linkText: .exe 다운로드 (${assets.exe.sizeMB} MB)
    link: ${assets.exe.url}`);
  } else {
    blocks.push(`  - icon:
      dark: /windows-white.svg
      light: /windows-black.svg
      width: 100px
    title: Windows 다운로드
    linkText: 최신 릴리즈 페이지
    link: ${RELEASES_LATEST_URL}`);
  }

  return [
    "# WINDOWS_DOWNLOADS_START (자동 생성 영역: updateIndexFile.ts가 덮어씀)",
    ...blocks,
    "# WINDOWS_DOWNLOADS_END",
  ].join("\n\n");
}

async function updateIndexMd(releaseData: LatestReleaseData) {
  try {
    const indexPath = path.resolve(__dirname, "..", "index.md");
    let content = await fs.readFile(indexPath, "utf-8");

    content = content.replace(
      /name:\s*ChatView\s+v?[0-9A-Za-z._-]+/,
      `name: ChatView ${releaseData.version}`,
    );

    if (!/# WINDOWS_DOWNLOADS_START/.test(content)) {
      console.warn("WINDOWS_DOWNLOADS_START 마커가 없어 자동 갱신을 건너뜁니다.");
      await fs.writeFile(indexPath, content);
      return;
    }

    const windowsSection = buildWindowsBlocks(releaseData.assets);

    content = content.replace(
      /# WINDOWS_DOWNLOADS_START[\s\S]*?# WINDOWS_DOWNLOADS_END/,
      windowsSection,
    );

    await fs.writeFile(indexPath, content);
    console.log("index.md (exe 전용) 갱신 완료");
  } catch (error) {
    console.error("index.md 업데이트 실패:", error);
  }
}

export { updateIndexMd };

