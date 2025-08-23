# ChatView (챗뷰)

Electron + React + Vite 로 제작한 데스크톱용 "채팅 오버레이 제어/뷰어" 애플리케이션입니다. 
스트리밍/방송 화면에 특정 서비스의 실시간 채팅을 투명한 오버레이 창 형태로 띄워 고정하거나 상호작용을 제어할 수 있습니다.

현재 지원 주소 패턴:
- `https://weflab.com/page/...`
- `https://chzzk.naver.com/chat/...`

## 핵심 기능
- **오버레이 창 생성**: 입력한 채팅 URL을 별도의 투명/프레임리스(Frameless) 창(webview)으로 생성
- **고정 모드(always-on-top + 클릭 패스스루)**: 고정 활성화 시
  - 항상 위(alwaysOnTop)
  - 마우스 이벤트를 무시(클릭 패스스루)하여 방송 송출 프로그램 / 게임 등 뒤쪽 요소와 상호작용 가능
- **드래그 이동 & 위치/크기 기억**: 창 이동/리사이즈 시 Electron Store에 저장 후 재실행 시 복원
- **재입력 / 리셋**: URL 재입력 시 기존 오버레이 파기 후 새로 생성, 리셋 시 모든 저장 데이터 초기화
- **초기 설정 다이얼로그**: 첫 실행 시 URL 입력 모달 자동 오픈
- **윈도우/맥 플랫폼 UX 고려**: 
  - Win: 시스템 우클릭 메뉴 차단(hookWindowMessage)
  - macOS: 창 닫기 시 앱 숨김 처리(Dock 숨김)로 트레이/백그라운드 동작 유지
- **커스텀 타이틀바**: 최소화/숨김(닫기 대용) 버튼 제공; 드래그 가능한 상단 바
- **버전 표시 & 광고 영역**: 하단 버전 텍스트, 고정 높이 iframe 광고 슬롯

## 기술 스택
| 영역 | 사용 기술 |
|------|-----------|
| 프레임워크 | React 19, TypeScript, Vite |
| 데스크톱 | Electron (Main, Preload, Renderer 분리) |
| 스타일/UI | Tailwind CSS v4, Radix UI, shadcn 스타일 패턴, class-variance-authority, tw-animate-css |
| 상태/폼 | React Hook Form (준비), Zod (URL 검증) |
| 데이터 저장 | electron-store (윈도우 위치/고정 모드/최근 URL) |
| 번들/빌드 | Vite, electron-builder |
| 개발도구 | ESLint, Prettier(import 정렬 + Tailwind 플러그인), TypeScript |

## 동작 구조 개요
1. 사용자는 메인 창(제어판)에서 채팅 URL 입력
2. IPC를 통해 Main Process가 `createOverlayWindow(url)` 실행
3. 투명한 frameless BrowserWindow + 내부 webview 가 해당 URL 로드
4. 고정 토글 시 `set-fixed-mode` IPC → alwaysOnTop 및 ignoreMouseEvents 적용 → 렌더러/오버레이 동기 스타일 업데이트
5. 위치/크기 변경 이벤트(moved/resized) 발생 시 store에 bounds 저장
6. 재실행 후 `get-value(chatUrl)` 요청 시 기존 URL 감지되면 즉시 오버레이 복원

## 프로젝트 구조(요약)
```
chat-view/
 ├─ docs/               # VitePress 기반 문서 (사이트 hero 등)
 └─ packages/           # 실제 앱 (Electron + React)
     ├─ public/         # 아이콘, 폰트, 정적 자원
     ├─ src/
     │   ├─ electron/   # 메인 프로세스 로직 (창, IPC, 입력 캡처 등)
     │   ├─ components/ # UI 컴포넌트 및 TitleBar, Controller
     │   ├─ hooks/      # 커스텀 훅
     │   └─ lib/        # 공용 유틸
     └─ package.json
```

## 주요 IPC 채널
| 채널 | 방향 | 설명 |
|------|------|------|
| `get-value` | Renderer → Main (invoke) | key로 store 값 조회 (chatUrl 조회 시 오버레이 자동 생성) |
| `chatUrl` | Renderer → Main | 새 URL 저장 및 오버레이 재생성 |
| `reInput` | Renderer → Main | URL 재입력 시작: 기존 오버레이 제거 |
| `set-fixed-mode` | Renderer → Main | 고정 모드 토글(alwaysOnTop + ignoreMouseEvents) |
| `fixedMode` | Main → Renderer | 고정 모드 상태 브로드캐스트 |
| `reset` | Renderer → Main | store 초기화 및 오버레이 제거 |
| `minimize` | Renderer → Main | 메인 창 최소화 |
| `hidden` | Renderer → Main | 메인 창 숨김 |

## 설치 및 실행
사전 요구: Node.js 18+ 권장.

### 개발 모드 실행
```bash
npm install
npm run app
```
- `npm run dev`: Vite 개발 서버 (React)
- `npm run app`: Vite + Electron 동시 구동 (concurrently + tsc)

### 빌드 (배포 패키지 생성)
```bash
npm run build
```
결과물: `dist_app/` 내 Electron 빌드 아티팩트 (Windows portable 등)

## 환경 변수
현재 코드상 필수 `.env` 항목은 명시되어 있지 않습니다. 필요 시 `dotenv` 패키지를 통해 확장 가능합니다.

## 데이터 영속 항목 (electron-store Keys)
| Key | 설명 |
|-----|------|
| `chatUrl` | 마지막 사용 채팅 URL |
| `overlayFixed` | 고정 모드 여부(boolean) |
| `overlayWindowBounds` | 오버레이 창 위치/크기(Rect) |

## 커스텀 타이틀바 / 드래그 영역
- 상단 바 전체는 `-webkit-app-region: drag`
- 버튼 영역만 `no-drag` 처리해 클릭 가능

## 오버레이 고정 모드 동작
1. alwaysOnTop 활성화
2. ignoreMouseEvents(true, { forward: true }) → 패스스루
3. 배경 투명 처리 및 webview pointer-events 제거 → 완전한 클릭 관통

## 향후 개선 아이디어
- 지원 플랫폼/채팅 서비스 확장 (예: YouTube, Twitch)
- URL 즐겨찾기/최근 목록 UI
- 다중 오버레이 창 지원
- 커스텀 CSS 오버레이 (폰트/색상 테마)
- 자동 업데이트(electron-updater) 적용
- 단축키(Global shortcuts)로 고정 토글
- 다국어(i18n) 지원
