<div align="center">

<a href="https://chat-view.andongmin.com">
<img src="https://chat-view.andongmin.com/logo.svg" alt="logo" height="200" />
</a>

</div>

# ChatView

Tauri + React + Vite 기반의 데스크톱용 채팅 오버레이 앱입니다.
스트리밍 화면 위에 채팅 웹페이지를 별도 오버레이 창으로 띄우고, 고정/이동/크기 조절 상태를 유지할 수 있습니다.

현재 기본 지원 URL:
- `https://weflab.com/page/...`
- `https://chzzk.naver.com/chat/...`

## 핵심 기능
- URL 입력 후 투명 오버레이 창 생성
- 고정 모드 ON: 항상 위 + 클릭 스루 + 완전 투명 배경
- 고정 모드 OFF: 오버레이 전체 드래그 이동 + 반투명 배경
- 오버레이 위치/크기/고정 상태/최근 URL 로컬 저장 및 복원
- 메인 창 닫기 시 종료 대신 시스템 트레이로 최소화
- 트레이 메뉴에서 창 다시 열기/종료

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
├─ docs/                  # VitePress 문서
└─ packages/              # 실제 앱(Tauri + React)
   ├─ src/                # 렌더러(UI)
   ├─ src/bridge/         # electron API 호환 브리지(tauri invoke 래핑)
   ├─ src-tauri/          # Rust 백엔드, 번들 설정
   └─ public/             # 아이콘/폰트 등 정적 리소스
```

## 실행
사전 요구사항: Node.js 18+, Rust 툴체인, Tauri 빌드 의존성

```bash
cd packages
npm install
npm run tauri:dev
```

참고:
- `npm run dev`: 프론트엔드(Vite)만 실행
- `npm run tauri:dev`: 실제 데스크톱 앱 실행

## 빌드
```bash
cd packages
npm run tauri:build
```

주요 산출물 예시(Windows):
- 실행 파일: `packages/src-tauri/target/release/ChatView.exe`
- MSI: `packages/src-tauri/target/release/bundle/msi/ChatView_버전_x64_ko-KR.msi`
- NSIS: `packages/src-tauri/target/release/bundle/nsis/ChatView_버전_x64-setup.exe`

## 데이터 저장
앱 설정은 OS별 앱 데이터 디렉터리의 `store.json`에 저장됩니다.

저장 항목:
- `chatUrl`
- `overlayFixed`
- `overlayWindowBounds`

## 업데이트 방식
현재 앱 내 업데이트는 "최신 릴리즈 확인 + 다운로드 링크 안내" 방식입니다.
자동 설치형 업데이터(Tauri Updater plugin)는 아직 미적용입니다.

## 비고
- 이 프로젝트는 더 이상 Electron 런타임을 사용하지 않습니다.
- 기존 Electron 레거시 소스는 제거되었습니다.

