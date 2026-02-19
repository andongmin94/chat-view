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
  };
}

export interface ReleaseNoteData {
  version: string;
  body: string;
}

const GITHUB_RELEASES_API =
  "https://api.github.com/repos/andongmin94/chat-view/releases";

function toVersion(tagName: string) {
  return tagName.replace(/^chat-view-/, "");
}

function toBinary(asset?: GitHubReleaseAsset): ReleaseBinary | null {
  if (!asset) return null;

  return {
    url: asset.browser_download_url,
    sizeMB: Math.round(asset.size / 1024 / 1024),
  };
}

function pickExeAsset(assets: GitHubReleaseAsset[]): GitHubReleaseAsset | undefined {
  const nonSetupExe = assets.find((asset) => {
    const name = asset.name.toLowerCase();
    return name.endsWith(".exe") && !name.includes("setup");
  });

  if (nonSetupExe) {
    return nonSetupExe;
  }

  return assets.find((asset) => asset.name.toLowerCase().endsWith(".exe"));
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

    const exe = toBinary(pickExeAsset(assets));

    return {
      version: toVersion(data.tag_name),
      fileSize: exe?.sizeMB ?? 0,
      assets: { exe },
    };
  } catch (error) {
    console.error("GitHub 릴리즈 정보를 가져오지 못했습니다:", error);
  }
}

async function fetchAllReleases(): Promise<ReleaseNoteData[]> {
  try {
    const releases = await fetchGitHubJson<GitHubRelease[]>(GITHUB_RELEASES_API);

    return releases.map((release) => ({
      version: toVersion(release.tag_name),
      body: release.body?.trim() ?? "",
    }));
  } catch (error) {
    console.error("GitHub 릴리즈 목록을 가져오지 못했습니다:", error);
    return [];
  }
}

export { fetchLatestRelease, fetchAllReleases };
