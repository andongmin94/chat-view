<div align="center">

<a href="https://chat-view.andongmin.com">
<img src="https://chat-view.andongmin.com/logo.svg" alt="logo" height="200" />
</a>

</div>

# ChatView

ChatView는 스트리밍 화면 위에 채팅창을 깔끔하게 띄워 주는 데스크톱 오버레이 앱입니다.

Tauri + React + Vite를 기반으로 제작되었으며, 채팅 웹페이지를 투명한 별도 창으로 띄운 뒤 위치·크기·고정 상태를 자유롭게 조절할 수 있습니다. 방송 중에도 시청자 채팅을 화면 위에서 바로 확인할 수 있어 편리합니다.

현재 아래 채팅 페이지를 기본으로 지원합니다.

- `https://weflab.com/page/...`
- `https://chzzk.naver.com/chat/...`

## 핵심 기능

- **투명 오버레이 창 생성** — URL을 입력하면 채팅 페이지가 투명 창으로 열립니다.
- **고정 모드 ON** — 창이 항상 최상위에 고정되고, 클릭이 뒤쪽 앱으로 통과(클릭 스루)하며, 배경이 완전히 투명해집니다.
- **고정 모드 OFF** — 오버레이 창 전체를 드래그해서 이동할 수 있고, 배경이 반투명으로 표시됩니다.
- **상태 자동 저장** — 오버레이의 위치, 크기, 고정 여부, 최근 사용 URL이 로컬에 저장되어 다음 실행 시 자동으로 복원됩니다.
- **시스템 트레이 지원** — 메인 창을 닫아도 앱이 종료되지 않고 트레이로 최소화됩니다. 트레이 메뉴에서 창을 다시 열거나 앱을 종료할 수 있습니다.

## 기술 스택

| 영역 | 사용 기술 |
|------|-----------|
| UI | React 19, TypeScript, Vite |
| Desktop Runtime | Tauri v2 (Rust + WebView) |
| UI 스타일 | Tailwind CSS v4, Radix UI, shadcn/ui |
| 상태/폼 | React Hook Form, Zod |
| 데이터 저장 | 앱 데이터 디렉터리의 `store.json` |
| 번들/배포 | Tauri Bundler (NSIS, MSI, app, dmg 등) |

## 프로젝트 구조

```text
chat-view/
├─ docs/                  # VitePress 기반 문서 사이트
└─ packages/              # 실제 앱 소스 (Tauri + React)
   ├─ src/                # 프론트엔드 UI 코드
   ├─ src/bridge/         # Tauri invoke를 래핑한 브리지 모듈
   ├─ src-tauri/          # Rust 백엔드 및 번들 설정
   └─ public/             # 아이콘, 폰트 등 정적 리소스
```

## 실행 방법

개발 환경에서 앱을 실행하려면 아래 사전 요구사항이 필요합니다:

- Node.js 18 이상
- Rust 툴체인
- Tauri 빌드 의존성

모두 준비되었다면 다음 명령어를 실행해 주세요.

```bash
cd packages
npm install
npm run tauri:dev
```

> **참고**
> - `npm run dev` — 프론트엔드(Vite) 개발 서버만 실행됩니다.
> - `npm run tauri:dev` — Tauri 데스크톱 앱 전체가 실행됩니다.

## 빌드

배포용 바이너리를 만들려면 아래 명령어를 사용합니다.

```bash
cd packages
npm run tauri:build
```

빌드가 완료되면 다음과 같은 산출물이 생성됩니다. (Windows 기준)

- 실행 파일: `packages/src-tauri/target/release/ChatView.exe`
- MSI: `packages/src-tauri/target/release/bundle/msi/ChatView_버전_x64_ko-KR.msi`
- NSIS: `packages/src-tauri/target/release/bundle/nsis/ChatView_버전_x64-setup.exe`

## 데이터 저장

앱의 설정 정보는 OS별 앱 데이터 디렉터리 아래 `store.json` 파일에 자동으로 저장됩니다.

저장되는 항목은 다음과 같습니다:

- `chatUrl` — 마지막으로 사용한 채팅 URL
- `overlayFixed` — 고정 모드 활성화 여부
- `overlayWindowBounds` — 오버레이 창의 위치와 크기

## 업데이트 방식

현재 앱 내에서 최신 릴리즈를 확인하고 다운로드 링크를 안내하는 방식으로 업데이트를 지원합니다. 자동 설치형 업데이터(Tauri Updater Plugin)는 아직 적용되지 않았습니다.

## 비고

- 이 프로젝트는 더 이상 Electron 런타임을 사용하지 않습니다. 기존 Electron 레거시 소스는 모두 제거되었습니다.

