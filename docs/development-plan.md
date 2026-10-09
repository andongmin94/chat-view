# ChatView 작업 목표 체크리스트

갱신: 2026-10-09 (Asia/Seoul). 시작 기준 **`fb3c908386c4a123bba31cc73aa7986cff233283`**. 최신 OBS와 AGENTS → PRODUCT → development-workflow → architecture → 이 문서를 순서대로 읽었다. **#364 / run `37275017123` / job `111650053171`은 현재 jobs 조회 404이며, 코드 `af41b5c28e3ba96b66e8455fa2a12b6b777e35bb`의 실행 목록도 0건이다.** 삭제·권한·보관 등 원인은 단정하지 않는다. 이전 마지막 확인은 실행 중이었고 최종 성공/실패는 여전히 미확정이다.
이번 시작 기준 `4a4085df1a2cbcd154d80808c8bdd2add5866312`. D1의 최초 채팅 표시에서 이전 설정 페이지의 취소/오래된 navigation 이벤트를 새 자체 문서의 잘못된 종료로 취급할 수 있는 구간을 수정했다. 새 문서 URL 연결 전 구 페이지 이벤트만 무시하고, 새 문서의 소유권·탐색 ID·nonce handshake와 표시 확인을 그대로 요구한다. WebView2 실제 DOM 첫 메시지 4개 고정 전환 회귀를 추가하며 15초 검사 시간은 유지한다. #294/#366의 기존 간헐 실패와 동일한 원인이라고 확정하지 않는다. **코드 `bf272ec18dc648e2f569cb83fa25d1ee40816cd0`, Windows #368 / run `37947737894` / job `113878352527`는 실행 중**이며 OBS 개발 라이브러리 빌드는 성공, ChatView 네이티브 빌드/CTest는 진행 중이다.
#365는 코드 `61f81158cbcd965822e80aa4771be8f25553af85`, run `37935365412`에서 빌드 성공·39/40(WGC static sequence6 실패)이었다. 그 다음 **#366 / code `f00dc3dcb1b2509a6d80a4e4f5af43438e143a18` / run `37941705504` / Windows x64 job `113857516728`은 빌드 성공·37/40, 150.33초, failure**. WGC 첫 크기 증가에서 E_UNEXPECTED(`0x8000FFFF`), native video lifecycle의 resize 실패, 독립적인 native-chat-switch 첫 표시 실패였다. 잘린 성장 프레임에서 pool과 session을 함께 교체한 코드 `4b711c244efba276e3f783c213124390dfab2154`를 원격에 반영했다. **Windows #367 / run `37943089840` / job `113862282591`: 빌드 성공, 39/40, 165.17초, failure.** WGC 12단계 정지 재그리기·실제 픽셀/여백과 패널 재개, target-loss, OBS core 및 native chat switch는 통과했다. 별개 `chat-view-hud-smoke`가 30.53초로 30초 제한을 초과했으며 원인은 미확정이다.
이 문서가 유일한 현재 작업 목록이다. 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md), 영상 계약은 [window-video-output.md](window-video-output.md)를 따른다.

## 현재 위치

`[x]`는 명시한 좁은 개발 범위다. routine·합성 입력·실제 OBS 전체 흐름·실계정·물리 투컴·배포 qualification을 구분한다.

**이번 흐름:** 해제 취소 후 같은 출력창에서 패턴/게임 출력을 준비하는 동안 중지 → 검정 유지 → 새 패턴 확인 후 명시적 재개. 기존 시작 경계는 확인을 소비한 뒤 `mask()`를 호출하고 곧바로 worker를 시작했다. `mask()`의 동기 창 메시지에서 일반 중지가 처리되어도 이전 호출이 돌아와 캡처를 시작할 수 있었다. 패턴 준비도 같은 경계에서 중지 뒤 패턴을 다시 표시할 수 있었다. 실제 함수 본문에 해당 호출 순서를 주입한 로컬 검사에서 두 경로를 각각 재현했다. 자연 발생 Windows/GPU 오류를 재현했다는 뜻은 아니다.

**수정:** `stop()`은 요청과 해제 예약을 내릴 때 기존 확인 세대도 증가시킨다. 살아 있는 대상 선택·목록·출력/덮개 HWND는 폐기하지 않는다. `begin_capture()`와 `show_pattern()`은 동기 mask 뒤 같은 세대인지 확인하고, 바뀌었으면 이전 작업을 이어가지 않으며 중지 안내도 덮어쓰지 않는다. `identify()` 역시 출력창 준비 뒤 이전 동의를 다시 검사한다. 새 패턴/확인만 재개를 허용한다. [RedrawWindow의 동기 메시지 계약](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-redrawwindow)을 따른 순서 수정이며 새 복구 체계나 대상 상실 재구현이 아니다.

**회귀 검사:** 기존 `video-output-lifecycle-test.cpp`에 scoped cover WndProc를 두어 실제 `WM_PAINT` 중 실제 패널 Stop 명령을 한 번 보낸다. 패턴 준비/worker 시작 각각에서 주입 순서가 실행됐는지, 새 캡처·패턴 부활이 없는지, 중지 안내·검정 9개 표본·기존 창/대상이 남는지 검사한다. 늦은 snapshot과 queued timer가 재개시키지 못해야 하며, 새 패턴 확인 후 실제 WGC의 신선한 400×300 영상으로 돌아간다. 기존 resize·해제 취소·대상 상실 관련 검사를 제거하거나 직접 worker 시작/덮개 숨김으로 우회하지 않았다. 순서·양성 출력 배치/동의·늦은 상태는 합성 입력이고 물리 수신자/driver hang 검증은 아니다.

**유지한 동작:** `af41b5c`의 일반 중지는 아직 파괴되지 않은 출력창의 해제 예약을 취소한다. 이미 파괴된 창의 복원이나 사용자 취소 시간 보장은 아니다. SourceLost/Failed/Stopped·끝난 worker·즉시 시작 거절은 창 식별과 목록/확인을 폐기한다. 같은 제목의 새 창이나 늦은 Capturing으로 자동 재개하지 않는다. 새로고침 → 대상·출력 선택 → 새 패턴 확인으로 같은 검은 출력창을 재사용한다. 정상 대상의 중지와 대상 상실을 구분한다.

**#359/#361 resize 근거:** #359의 기존 #353 단발 확대와 sequence 1–8은 통과했다. sequence 9의 즉시 201×401 → 601×201 → 600×300 재그리기 후 frame=25, Waiting(2), running=1, presented=200×300, received=600×300, surface=200×300, recreates=14, clipped=12, HRESULT=0, fresh=0, 출력 5개 픽셀 모두 검정이었다. 직전은 frame=25/recreates=12/clipped=10이며 출력 최상위·사각형은 정상이었다. 개별 Recreate 요청 로그가 없어 마지막 두 요청 크기나 OS 내부 상태는 단정하지 않는다. `4cbcc5e`의 축별 최대 pool 용량 재사용은 같은 12단계 입력·고유 색·9개 내부 픽셀·여백·pool 용량/정확한 재생성 횟수에서 #361 통과했다. #353 인과를 확정하지 않으며 큰 용량의 세션 내 유지도 자원 qualification 근거는 아니다.

첫 프레임 10초·resize 5초·색상 오차 8·WGC 40초/lifecycle 60초 CTest·콘텐츠 2초·최대4096/pool2개 제한을 유지한다. 이번 변경은 패널·기존 lifecycle 검사·이 계획 세 파일뿐이다. 캡처 엔진·대상 상실·로그인/인증·광고·CMake·의존성은 변경하지 않는다.

## G1. 한 화면의 작업과 개인 채팅

- [x] **G1-01 — 투명 HUD와 자체 텍스트 채팅.** 실계정은 G4-06.
- [x] **G1-02 — 클릭 통과·이동/크기·DPI 저장·단축키.** 기존 구현/검사 유지.
- [ ] **G1-03 — 실제 게임 가독성·조작감.** 실환경 입력/포커스 확인 필요.

## G2. OBS 연동과 개인 HUD 제외

스트리머에게 HUD가 보이면서 수신 영상에는 없어야 한다. HUD 숨김을 지원 완료로 세지 않는다.

- [x] **G2-01 — 플러그인·설정·HUD 수명 연동.** OBS/HUD 분리 유지.
- [ ] **G2-02 — 원컴 가독 HUD와 제외 동시 달성.** 실제 지원 영상 확인 필요.
- [ ] **G2-03 — 방송/장면·복귀·정확한 안내.** D3 문구는 `72775a5`/#342에서 검증. 관리 브라우저는 HUD 보호 밖이며 실제 영상 검증은 별도.

## G3. 게임 PC에 OBS 없는 투컴

- [x] **G3-01 — 동일 HUD의 OBS 비종속 실행.** companion·로그인·중복 방지·종료 유지.
- [ ] **G3-02 — 동일 계정의 채팅·방송 세션.** 역할 동의·공유·암호화 복원·로그아웃·최근 출력 보고가 있다. 장면/소스·다른 출력·물리 두 PC는 남았다. 현재 승인 복귀는 기존 역할만 사용한다.
- [ ] **G3-03 — HUD 분리 영상.** WGC→D3D11/D2D, 별도 SDR 출력, 패턴·육안 확인·같은 HWND 전환·검정 중지·명시적 해제. 잠금/전원/화면/장치·대상 상실은 선택 무효화, 자동 재시작 없음. 콘텐츠2초는 `4f1f048`/#341, 연속 resize는 `4cbcc5e`/#361, 패널 stop/restart는 `04c1558`/#363 통과 기록. `af41b5c`/#364 최종 결과는 조회 불가/미확정. 동기 Stop 수정 `61f81158`/#365는 lifecycle에서 통과했으나 WGC 정지 소스 resize가 실패. 크기 증가 시 같은 CaptureItem에 새로운 pool/session을 생성하는 `4b711c2`/#367의 12단계 정지 재그리기 및 WGC 패널 검사는 통과했다. 전체 routine은 HUD smoke timeout으로 39/40이며 실배선/장시간 영상은 미검증. 영상만·최대4096, 오디오/인코더/네트워크 영상 없음. D6/하드웨어 남음.
- [ ] **G3-04 — 설치·실사용·수신 영상.** 실제 배선/캡처카드/게임/다중 GPU/자원/지연·잠금/hotplug와 HUD 없는 녹화 필요.

## G4. 치지직부터 자체 플랫폼

- [x] **G4-01 — 공식 인증·채널·구독.** 합성과 실계정은 별도.
- [x] **G4-02 — 서버→WinHTTP→자체 HUD.** D1 미해결.
- [x] **G4-03 — 로그인→브라우저→앱 복귀.** verifier/고정 역할 동의 유지. `24444b6`/#51 실제 Chromium 폼·쿠키·합성 제공자 왕복·역할 승인 확인. 실계정/native 시스템 브라우저 전체 성공은 아님.
- [x] **G4-04 — 실행 갱신·선택 저장·재연결·로그아웃.** D5는 `8fa41db`/#326 종료. 전환·현재 승인 복귀 `7bd8b28`/#344 통과. 현재 승인만 해제하고 다른 저장 계정 보존. 일반 종료/응답 유실은 철회 완료가 아님.
- [ ] **G4-05 — 다중 스트리머 운영.** TLS·계정 격리·단일 upstream·CSRF·암호화 복원·갱신/철회·출력 보고. 브라우저 복구는 `24444b6`/#51 실제 Chromium9개와 Windows/Linux×Node22/24 계약 통과. 단일 인스턴스, 운영 접근/용량·호스팅·키/백업·실서비스 인증서는 미완료.
- [ ] **G4-06 — 실제 치지직 전체 사용.** 실제 앱/채널·장시간 수신·재연결·철회와 제공자 측 토큰 동작 필요.

## G5. 광고→시청 근거→HP→수익

개인 HUD·OBS 활동 보고·채널 시청자 추정은 검증된 광고 시청 실적이 아니다. 시험 수치는 지급 불가다.

- [x] **G5-01 — 시험 캠페인 목록·선택·중지.** 계정/CSRF·송출 승인 결합·철회·재시작 보존·재선택. 브라우저 복구/전환은 기존 선택을 바꾸지 않는다.
- [x] **G5-02 — 별도 OBS 공개 시험 소스.** `f083dc7`의 OBS32.2.2/CEF 합성 PNG13개 검사. 사용자 URL 배치이며 실방송/녹화·개인 HUD 제외 보장은 아님.
- [ ] **G5-03 — 시청시간 집계.** 비지급 활동·누락 기록, 공식 Live API 표본·추정/측정 범위. 0명/미측정 구분, 녹화/미리보기 제외, 소유자만 조회, 공개 조회로 증가하지 않음. 수집 기본 꺼짐, 권한·할당량·보관 조건 확인 후 서버 `CHATVIEW_AUDIENCE_SAMPLING=1`. 실제 광고 노출/주의 미검증.
- [ ] **G5-04 — 서버 HP·수익.** 규격 미정. 비지급/예상/확정 구분, HP0 지급 조건 임의 추가 금지.
- [ ] **G5-05 — 유료 운영·정산.** 단가·예산·배분·지급·중복/초과·부정 실적·개인정보·광고 고지 필요.

## R. 통합 후 배포

- [ ] **R-01 — 첫 표시/연결 신뢰성과 방송 안정성.** #366 첫 채팅 표시 실패(D1) 뒤 #367 통과는 인과 미확정. #367 HUD smoke 30초 초과(30.53초)는 원인 미확정이며 임계시간 유지. 해당 경로에서 해결하며 독립 구현은 계속.
- [ ] **R-02 — 반복·자원·설치·캡처·패키지 qualification.** routine/개별 flow와 구분.
- [ ] **R-03 — 원컴/투컴 설치본·안내.** 실험/미지원/검증 범위 구분.

## 남은 결함

| ID | 근거 | 범위 |
| --- | --- | --- |
| D1 | `82f36a6`/#294 반복3 첫 gateway frame 표시 미확인; #366 native-chat-switch 첫 private chat renders 실패, #367 통과. 이번 새 자체 문서의 초기 구-navigation 취소 처리 수정은 native 검증 대기. | G4/R-01. 과거 실패와 동일 원인 불명·실계정 미검증, routine만으로 종료하지 않음. |
| D2 | #292 후반 핸들 +104>64; #294 초기 +284>256, 후반 +8. | R-02. 기존 자원 한도 유지. |
| D4 | CI 캡처 module의 동기 UI-task/join 및 borrowed item은 `510f8e1`에서 보완. #353 좌표는 #354, core queue 정리는 `66fd5a6`/#355에서 통과. #356/#357/#359/#361/#363도 core 통과 기록. | core fixture만 확인. 실제 OBS frontend 종료/장면 전환·qualification 남음. Control Center에 Q4/Q5 시험 UI는 없음. |
| D6 | `9e9863a`/#339 WGC 첫 픽셀10초 실패, #340 미재현. #353 단발 resize/레터박스 실패. #359 별도 burst단계9 재현 후 `4cbcc5e`/#361 동일 입력·강화 assertion 통과. | 연속 burst만 좁게 확인. #353 인과와 첫 표시 문제는 미해결. #363의 통과와 #365 lifecycle 통과도 종료 근거가 아님; #366는 session+same-pool 재시작의 E_UNEXPECTED, #367는 새로운 pool/session 구현에서 12단계 WGC 통과(원래 #353의 인과 미확정).  #365 sequence 6에서 received=601×201, surface=600×500, pool=601×500, Waiting·검정 정지 재현. |

D3·D5는 좁은 범위에서 종료했다. 자원 상세는 [resource-growth-investigation.md](resource-growth-investigation.md).

## 보존할 실행 근거

아래 완료 결과는 이전에 확인해 보존한 기록이다. 이번에 새로 실행한 결과와 구분한다.

| 실행 / 정확한 코드 | run / Windows x64 job | 실제 결과 |
| --- | --- | --- |
| #353 / `510f8e195a8cb321844dda6ff7f6a1be55bd1e4d` | `37142325494` / `111259184927` | 빌드 성공, routine37/39. core user position preserved 및 WGC resize recreates pool and letterboxes without old padding 실패. 당시 resize 크기·픽셀 로그가 없어 원인 미확정. |
| #354 / `025bb12e26b79dfad335aaaf1798c07a79913cb6` | `37169828714` / `111340331975` | 빌드 성공,39/40. core 좌표 통과, scene and all source references drained before OBS shutdown 실패. target-loss/output-lifecycle/window-capture 통과. |
| #355 / `66fd5a632f9d8e2c194c809ae46ddd0e83e15feb` | `37212720911` / `111467003429` | 빌드 성공,40/40. core-only의 실제 destroy-task fence 수정. 잔여 source0·input destructor 두 개 완료 통과. |
| #356 / `be151481f778d816fe51b9bf1f82a3908641e527` | `37213887083` / `111470392044` | 빌드 성공,39/40,127.63초. core0.57초·WGC2.93초·output-lifecycle3.13초·target-loss1.43초 통과. public-ad-browser가 loaded 전에 browser assertion callback 실패(11.79초). |
| #357 / `614688f2d7821f14e6cd2ced898cc8559ed363dc` | `37214818540` / `111473101019` | 빌드 성공,40/40,129.21초. core0.11초·WGC2.54초·output-lifecycle3.50초·target-loss1.28초·public-ad-browser15.23초 통과. |
| #359 / `7686e7f3599a7e482154403ac85d32aa55f687a7` | `37216401948` / `111477714374` | 빌드 성공,39/40,132.61초. WGC9.99초 실패: sequence9, 위 frame/픽셀 근거. core0.08초·output-lifecycle8.23초·target-loss1.33초·public-ad-browser15.21초 통과. |
| #361 / `4cbcc5e5dde15d56f31e5e5b8785c9d245d29cd9` | `37217766639` / `111481699383` | 빌드 성공,40/40,136.28초. core0.08초·WGC6.73초·output-lifecycle3.18초·target-loss1.38초·public-ad-browser15.46초 통과. |
| #362 / `f21e00bc113ef1e92be6b3b5e84624fe3f156c9f` | `37220157340` / `111488721555` | 빌드 실패, CTest 전체 미실행. lifecycle243행 const auto near의 Windows 매크로 충돌/C2513. 네 지정 캡처 검사 모두 미실행. |
| #363 / `04c1558bad2474e7ef444f5c1891b2dbbe7f6ec3` | `37242100880` / `111552735174` | 빌드 성공·40/40,127.02초, completed/success. core0.06초·WGC6.73초·output-lifecycle4.76초·target-loss5.30초·public-ad-browser15.04초 통과. |
| #364 / `af41b5c28e3ba96b66e8455fa2a12b6b777e35bb` | `37275017123` / `111650053171` | **최종 결과 미확정.** 이전 마지막 기록은 OBS 개발 라이브러리 성공·ChatView 빌드/검사 실행 중. 2026-10-09에는 jobs404, 해당 SHA 실행목록0건. 네 지정 검사의 결과도 확인 불가. |
| #365 / `61f81158cbcd965822e80aa4771be8f25553af85` | `37935365412` / `113836005952` | **빌드 성공·39/40, 138.56초, failure.** core0.09초·output-lifecycle9.58초·target-loss1.46초·public-ad-browser15.23초 통과. WGC8.94초 실패: static sequence6, content 601×201, surface600×500, requested pool601×500, frames27, recreate3, clipped4, Waiting, fresh0, 5개 고정 픽셀 검정. |
| #366 / `f00dc3dcb1b2509a6d80a4e4f5af43438e143a18` | `37941705504` / `113857516728` | **빌드 성공·37/40, 150.33초, failure.** WGC5.56초: 첫 확대 ContentSize600×300, surface400×300에서 E_UNEXPECTED(0x8000FFFF), Failed/black; output-lifecycle6.49초 resize 실패. 독립 native-chat-switch28.15초 initial private chat renders 실패(D1). core0.09초·target-loss1.39초·public-ad-browser19.60초 통과. |
| #367 / `4b711c244efba276e3f783c213124390dfab2154` | `37943089840` / `113862282591` | **빌드 성공·39/40, 165.17초, failure.** `chat-view-window-capture`6.82초(정지 단발 `601×201`, 연속12단계/픽셀/여백)·video-output-lifecycle7.22초·target-loss3.72초·OBS core0.07초·native-chat-switch15.52초·public-ad-browser15.15초 통과. **별개 `chat-view-hud-smoke` 30.53초 timeout**; 정확한 대기 단계/원인 미확정. |
| #368 / `bf272ec18dc648e2f569cb83fa25d1ee40816cd0` | `37947737894` / `113878352527` | **in_progress, 결과 없음.** OBS 개발 라이브러리 성공, ChatView build/CTest 진행 중. 새로운 native-chat-surface 첫 DOM4회/기존 native-chat-switch/WinHTTP와 전체 40회 결과는 미확인. |

#357 이후 광고 검사 재통과로 #356 내부 원인을 확정하지 않는다. #359의 별도 burst 수정/통과는 #353의 동일 원인 해결로 소급하지 않는다. 기존 routine은 development이며 qualification/package/전체 OBS 단계는 미실행이었다. #364 조회 불가를 성공·실패·현재 실행 중 어느 것으로도 새로 분류하지 않는다.

## 이번 검증 상태

- **이번 D1 구현:** `NativeChatSurface`의 `NavigationStarting`은 이전 setup 페이지의 취소 이벤트를 새 문서 소유권 검사 전에 걸러낸다. 새 URL 설치 중에만 이전 문서의 unowned 탐색을 무시하고, 새 문서의 예기치 않은 탐색·오류·중복 핸드셰이크는 기존대로 무효화한다. 앱 첫 접속의 실제 `WebViewHost`/WebView2 이벤트 순서를 대상으로 한다. 로그인·권한·모델·메시지 스키마는 변경하지 않는다.
- **연결 검사:** 기존 실제 WebView2 native-chat-surface 테스트에 setup/reload → 즉시 native own document → 새 첫 메시지와 실제 DOM 한 행의 고정 4회 전환을 추가했다. native-chat-switch의 기존 15초 실패 경로에는 worker/문서 준비/렌더 프레임·행·거부 수·membership 존재 여부만 출력한다. 이는 권한·메시지·토큰을 로그에 넣지 않는다.
- **검증 상태:** 원격 코드 `bf272ec18dc648e2f569cb83fa25d1ee40816cd0`, Windows #368 / `37947737894` / `113878352527`는 마지막 확인 시 ChatView 빌드/CTest 단계 in_progress, 결론 없음. 이전 navigation 조건을 복제한 로컬 C++20 재현에서 원본은 취소 이벤트를 통해 실패했고 수정본은 GCC 14/14, Clang ASan+UBSan 14/14 assertion 통과. **이는 실제 WinHTTP/WebView2 검사가 아니다.** 이 수정만으로 #294/#366 D1이나 #367 별도 HUD smoke 30초 초과를 해결했다고 선언하지 않는다.

- **#365 명확한 실패:** 기존 `601×201` 단발 정지 재그리기에서 WGC는 크기 601×201을 보고했으나 이전 pool의 600×500 표면으로 전달했다. 기존 로직은 잘린 프레임을 폐기하고 601×500으로 Recreate했으나, 정지 소스의 새 프레임이 5초 안에 나타나지 않았다. 출력 보호는 검정으로 유지됐고 HRESULT=0이며 worker는 살아 있었다. 이전 #359와 단계/표면은 다르며 원인이 동일하다고 확정하지 않는다.
- **#366 실패에서 확인한 문제:** 성장 첫 프레임(600×300)이 표면 400×300으로 도착했을 때 session.Close → 같은 pool.Recreate → CreateCaptureSession 경로가 `0x8000FFFF`로 실패했다. 이 로그만으로 세 개 호출 중 정확히 어디서 던졌는지는 확정할 수 없다. WGC는 Failed, 출력은 검정 유지. Lifecycle 역시 후속 resize 실패. 별도 `native-chat-switch`의 initial private chat renders 실패는 변경한 GPU 코드의 직접 원인으로 간주하지 않으며 D1에 보존한다.
- **새 구현:** `WindowCapture::run`의 `ResizeOnly`에서 프레임·텍스처를 놓고 검정을 표시한 후, 기존 pool 이벤트·session·pool을 정리하고 새로운 2버퍼 pool을 확대된 축별 용량으로 생성한다. 새 session은 동일 `GraphicsCaptureItem`을 사용해 초기 프레임을 요청한다. `Recreate`는 기존의 완전한 성장 프레임에만 그대로 사용한다. 정상 사용자 중지·대상 상실 시에는 새 session을 시작하지 않고, 이미 충분한 용량에서 도착한 작은 표면에는 반복 재생성/재시작하지 않는다. 전체 화면 캡처, 게임 redraw 요청, 시간·픽셀 완화 없음.
- **검사:** `4b711c2`/#367에서 원래 `window-capture-test.cpp` 12회 정지 재그리기·색상/여백·pool 횟수와 5초 제한을 유지한 실제 WinRT/GPU 검사 **통과**. `video-layout-test.cpp`의 601×201 경계 검사도 통과. panel lifecycle, target-loss, OBS core도 위 행처럼 통과. 다만 HUD smoke 30.53초 timeout으로 40/40은 아니며 장시간·물리 수신 검증도 아니다.
- **원격 상태:** `OBS` 코드 `4b711c244efba276e3f783c213124390dfab2154`, #367 run `37943089840`, Windows x64 job `113862282591` 최종 completed/failure(39/40). 첫 시도 `f00dc3d`/#366 실패, 기존 #365 실패와 #364 조회 불가는 별도 보존한다. 이번 결과 갱신은 이 계획 문서만 변경하며 `[skip ci]`로 새 네이티브 실행을 만들지 않는다.
- **미실행:** 실제 물리 투컴·캡처카드, OBS frontend 전체 흐름, CHZZK 실계정, 배포 qualification·장시간 자원 검증. D1/D2/D4/D6는 미해결.

### 다음 사용자 흐름

D1의 새 코드에 대한 Windows `native-chat-surface`, `native-chat-switch`, `native-display-client`와 관련 routine 결과를 우선 확인한다. 첫 메시지가 표시되지 않으면 안전한 초기 단계 관측으로 수신·문서 로딩·DOM 처리 중 위치를 구분해 고친다. 원인 근거 없이 한도를 늘리거나 전송 체계를 재작성하지 않는다. 별개 #367 HUD smoke 30초 초과는 보존하고 영향 범위를 확인한다. #364 조회 불가는 통과로 덮지 않는다. 대상 상실/재선택·엔진 resize를 다시 만들거나 시간/색상/재시도 한도를 늘리지 않는다.

OBS만 변경한다. main·새 작업 브랜치·검증 태그·강제 push, 새 브라우저 복구/인증/기기등록/표시키/옛 ZIP, 게임 PC OBS, 임의 HP/단가/지급 규칙을 추가하지 않는다. D1/D2/D4/D6는 해당 근거 전까지 남긴다. 별도 보고서 대신 이 문서를 갱신한다.

미확정 입력: 실제 앱/채널·수집 조건/할당량, 투컴 배선/기기, HP/보상 규격, 인프라. 비밀은 대화/공개 저장소에 넣지 않는다.
