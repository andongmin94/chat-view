import fs from "node:fs/promises";
import path from "node:path";

async function updateIndexMd(releaseData: {
  version: string;
  fileSize: number;
  downloadUrl: string;
}) {
  try {
    const indexPath = path.resolve(__dirname, "..", "index.md");
    let content = await fs.readFile(indexPath, "utf-8");

    // name 필드 업데이트
    content = content.replace(
      /name: 챗뷰 v[0-9]+\.[0-9]+\.[0-9]+/,
      `name: 챗뷰 ${releaseData.version}`
    );

    // 다운로드 링크 업데이트
    content = content.replace(
      /linkText: ChatView\.exe \([0-9]+ MB\)/,
      `linkText: ChatView.exe (${releaseData.fileSize} MB)`
    );

    content = content.replace(
      /link: https:\/\/github\.com\/andongmin94\/chat-view\/releases\/download\/v[0-9]+\.[0-9]+\.[0-9]+\/ChatView\.exe/,
      `link: ${releaseData.downloadUrl}`
    );

    await fs.writeFile(indexPath, content);
    console.log("✅ index.md 파일이 최신 릴리즈 정보로 업데이트되었습니다");
  } catch (error) {
    console.error("❌ index.md 업데이트 실패:", error);
  }
}

export { updateIndexMd };
