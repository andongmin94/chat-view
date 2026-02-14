interface GitHubReleaseAsset {
  name: string;
  browser_download_url: string;
  size: number;
}

interface GitHubRelease {
  tag_name: string;
  body?: string;
  assets?: GitHubReleaseAsset[];
}

interface ReleaseBinary {
  url: string;
  sizeMB: number;
}

export interface LatestReleaseData {
  version: string;
  fileSize: number;
  assets: {
    exe: ReleaseBinary | null;
    msi: ReleaseBinary | null;
  };
}

export interface ReleaseNoteData {
  version: string;
  body: string;
}

const GITHUB_RELEASES_API =
  "https://api.github.com/repos/andongmin94/chat-view/releases";

function toVersion(tagName: string) {
  return tagName.replace("chat-view-", "");
}

function toBinary(asset?: GitHubReleaseAsset): ReleaseBinary | null {
  if (!asset) return null;

  return {
    url: asset.browser_download_url,
    sizeMB: Math.round(asset.size / 1024 / 1024),
  };
}

async function fetchGitHubJson<T>(url: string): Promise<T> {
  const response = await fetch(url, {
    headers: { "User-Agent": "Node.js" },
  });

  if (!response.ok) {
    throw new Error(`GitHub API 응답 오류: ${response.status} ${response.statusText}`);
  }

  return response.json() as Promise<T>;
}

async function fetchLatestRelease(): Promise<LatestReleaseData | undefined> {
  try {
    const data = await fetchGitHubJson<GitHubRelease>(`${GITHUB_RELEASES_API}/latest`);
    const assets = data.assets ?? [];

    const exe = toBinary(assets.find((asset) => asset.name.endsWith(".exe")));
    const msi = toBinary(
      assets.find((asset) => asset.name.endsWith(".msi") && /x64/i.test(asset.name))
    );

    // 메인 노출용 파일 크기 (우선 exe, 없으면 msi)
    const primary = exe ?? msi;

    return {
      version: toVersion(data.tag_name),
      fileSize: primary?.sizeMB ?? 0,
      assets: { exe, msi },
    };
  } catch (error) {
    console.error("GitHub 릴리즈 정보 가져오기 실패:", error);
  }
}

// 릴리즈 노트 생성/사이드바용 최소 데이터
async function fetchAllReleases(): Promise<ReleaseNoteData[]> {
  try {
    const releases = await fetchGitHubJson<GitHubRelease[]>(GITHUB_RELEASES_API);

    return releases.map((release) => ({
      version: toVersion(release.tag_name),
      body: release.body?.trim() ?? "",
    }));
  } catch (error) {
    console.error("GitHub 릴리즈 정보 가져오기 실패:", error);
    return [];
  }
}

export { fetchLatestRelease, fetchAllReleases };
