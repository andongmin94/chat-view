# ChatView 작업 목표 체크리스트

갱신: 2026-10-05 (Asia/Seoul). 시작 기준 **`fe12146b1adfa173ceeab06b6f9f3fce32038818`**. 인수인계 구현 **`025bb12e26b79dfad335aaaf1798c07a79913cb6`**의 #354를 확인하고 실패 경로부터 수정했다. 선행 수정 **`66fd5a632f9d8e2c194c809ae46ddd0e83e15feb`**는 Windows #355 routine 40/40 통과. 크기 변경 구현 **`be151481f778d816fe51b9bf1f82a3908641e527`**는 #356에서 지정 네 검사 통과, 전체 39/40이다. 새로 발생한 광고 브라우저 검사 실패를 아래에 보존하고 검사 시작 동기화를 보완한다.
목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md)를 따른다. 이 문서가 유일한 현재 작업 목록이다.

## 현재 위치

`[x]`는 명시한 좁은 개발 범위다. 실제 제공자·물리 투컴·배포 완료와 구분한다.

기존 대상 상실/재선택 구현은 유지한다. `VideoOutputPanel::update_capture`는 SourceLost/Failed/Stopped와 끝난 worker를 프레임 표시보다 먼저 처리하고 창 식별·선택 목록·패턴 확인을 폐기한다. 즉시 시작 거절도 같은 경로다. 이전 Capturing 보고나 같은 제목의 새 창으로 자동 재개하지 않는다. 검은 출력/덮개 HWND와 출력 화면 결합은 보존한다. 새로고침 → 대상·출력 선택 → 새 패턴 확인 이후 같은 출력창을 재사용한다. 해당 실제 HUD/선택창/WGC/GPU 검사 `video-target-loss-test.cpp`와 `video-output-lifecycle-test.cpp`는 #354/#355/#356에서 통과했다. 합성 출력 영역/확인 경계와 실제 물리 수신 영상은 구분한다.

**먼저 해결한 #354 실패:** `025bb12e26b79dfad335aaaf1798c07a79913cb6` / run **`37169828714`** / job **`111340331975`**는 빌드 성공, routine **39/40 통과·1건 실패**였다. `chat-view-obs-capture-scene`은 좌표 검사를 통과한 뒤 **scene and all source references drained before OBS shutdown**에서 실패했다. 고정 OBS 32.2.2 `ba2f32bdf791005443988a4955e963663e16b1ed`의 `obs_wait_for_destroy_queue()`는 video/audio thread가 없는 이 fixture에서 즉시 반환한다. `66fd5a632f9d8e2c194c809ae46ddd0e83e15feb`은 scene → input의 두 소유 수준을 실제 `OBS_TASK_DESTROY` 동기 fence로 정리하고 잔여 source 0과 input destructor 두 개 완료를 검사한다. sleep·재시도나 10초 제한을 늘리지 않았다. **Windows #355 / `37212720911` / job `111467003429`: 빌드 성공, routine 40/40 통과**. core 0.09초, WGC 1.24초, output-lifecycle 3.38초, target-loss 1.37초. 실제 OBS frontend 종료/장면 전환 전체 qualification의 완료 근거는 아니다.

**이번 크기 변경 구현:** 매 프레임의 `ContentSize`와 실제 D3D 텍스처 크기를 함께 판정한다. 온전한 전환 프레임은 정확한 콘텐츠 영역만 복사·표시한 뒤 모든 frame/surface 참조를 내려 pool을 재생성한다. 기존처럼 재생성 전에 온전한 축소 프레임까지 버리지 않는다. 확대 도중 실제 surface가 작으면 검게 대기하고 pool을 재생성하며, 요청 크기가 이미 일치해도 실제로 잘린 프레임을 표시하거나 Failed로 취급하지 않는다. 재생성 전에 표시 시각을 무효화하고 이후에도 원래 캡처 시각으로 신선도를 재검사한다. 2초 콘텐츠 수명·최대 4096·pool 2개와 기존 대상 상실/재선택·검은 출력창 정책은 유지한다. 패널·인증·기기등록·OBS 의존성을 추가하지 않는다. [출력 계약](window-video-output.md)도 전환 프레임 보존·잘린 프레임 대기·원래 시각 검사와 맞췄다.

**관련 검사:** 기존 `window-capture-test.cpp`의 #353 확대 assertion을 보존·강화하고, 600×300 → 한 번만 다시 그리는 정지 200×300 축소 → 애니메이션 계속 → 600×200 혼합 확대/축소 → 600×300 높이 확대를 같은 캡처에서 검사한다. 크기별 다른 색과 상하/좌우 픽셀로 이전 콘텐츠·이전 여백 잔상을 구분한다. 실패 시 received/presented/surface 크기, status, 재생성/잘림 횟수, HRESULT, 신선도와 고정 합성 픽셀을 함께 남긴다. geometry 숫자만 일시적으로 보관하며 제목/영상/계정 정보를 로그에 쓰지 않는다. timeout 5초, 색상 오차 8, 전체 CTest 40초를 늘리지 않는다. 기존 output resize/minimize/stop/restart/close와 HUD 가시성 검사도 유지한다.

**#356 실제 결과와 추가 실패:** `be151481f778d816fe51b9bf1f82a3908641e527` / **Windows #356 / run `37213887083` / job `111470392044`**는 빌드 성공, routine **39/40 통과·1건 실패**다. `chat-view-obs-capture-scene` 0.57초, `chat-view-window-capture` 2.93초, `chat-view-video-output-lifecycle` 3.13초, `chat-view-video-target-loss` 1.43초로 각각 통과했다. 실패는 변경하지 않았던 `chat-view-public-ad-browser`의 **browser assertion callback**(11.79초)이며, Node 측 `loaded` 신호 대기 중 native child가 종료했다. 이 로그만으로 어느 초기 스크립트 콜백이나 브라우저 내부 상태가 원인이었는지 확정하지 않는다.

**이번 추가 검사 수정:** 광고 제품/페이지/서버/인증 흐름은 바꾸지 않는다. `tests/public-ad-browser-test.cpp`는 Navigate 직후 완료되지 않은 문서에 DOM assertion을 보내던 준비 경로를 보완한다. 원하는 URL의 NavigationStarting/NavigationId와 성공한 NavigationCompleted를 확인한 뒤 기존 DOM 조건을 실행한다. 초기 about:blank 등 다른 탐색의 완료는 인정하지 않는다. 탐색과 DOM 확인은 기존 10초 page-load deadline을 공유하며 환경 시작·콜백·부모 프로세스·전체 CTest 제한이나 재시도 예산을 늘리지 않는다. 콜백 실패 시 스크립트 순번·완료 여부·HRESULT, 탐색 실패 시 ID·완료/성공 여부·WebErrorStatus만 남긴다. URL/DOM/계정은 로그에 쓰지 않는다. [공식 실행 시점](https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/javascript)과 [탐색 ID 계약](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/navigation-events)에 따른 검사 동기화이며, #356의 내부 원인을 확정했다거나 새 브라우저 복구 기능을 만들었다는 뜻은 아니다. 새 native 결과는 아직 없다.

**직전 실패 보존:** `510f8e195a8cb321844dda6ff7f6a1be55bd1e4d`의 **Windows #353 / `37142325494` / job `111259184927`**는 빌드 성공, routine **37/39 통과·2건 실패**였다. 하나는 0 크기 fixture의 위치 보존, 다른 하나는 WGC의 **resize recreates pool and letterboxes without old padding**이다. 당시 resize 로그에는 실제 프레임/표면 크기·픽셀 증거가 없다. #354/#355/#356의 WGC 통과나 이번 코드에서 확인한 전환 프레임 폐기 문제를 그 과거 실패의 확정 원인으로 소급하지 않는다. 원인 재현·연결은 미완료다.

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
- [ ] **G3-03 — HUD 분리 영상.** 실험·물리 검증 필요. WGC→D3D11/D2D, 창/별도 SDR 출력, 패턴·육안 확인·같은 HWND 전환·검정 중지·명시적 해제. 잠금/전원/화면/장치 및 대상 상실은 선택 무효화, 자동 재시작 없음. 콘텐츠 2초 제한은 `4f1f048`/#341 검사. 크기 변경 보완은 위 현재 위치 참조; D6는 미해결. 영상만·최대 4096, 오디오/인코더/네트워크 영상 없음. [계약](window-video-output.md).
- [ ] **G3-04 — 설치·실사용·수신 영상.** 실제 배선/캡처카드/게임/다중 GPU/자원/지연·잠금/hotplug와 HUD 없는 녹화 필요.

## G4. 치지직부터 자체 플랫폼

- [x] **G4-01 — 공식 인증·채널·구독.** 합성과 실계정은 별도.
- [x] **G4-02 — 서버→WinHTTP→자체 HUD.** D1 미해결.
- [x] **G4-03 — 로그인→브라우저→앱 복귀.** verifier/고정 역할 동의 유지. `24444b6`/#51 실제 Chromium 폼·쿠키·합성 제공자 왕복·역할 승인 확인. 실계정/native 시스템 브라우저 전체 성공은 아님.
- [x] **G4-04 — 실행 갱신·선택 저장·재연결·로그아웃.** D5는 `8fa41db`/#326 종료. 전환·현재 승인 복귀 `7bd8b28`/#344 통과. 현재 승인만 해제하고 다른 저장 계정 보존. 일반 종료/응답 유실은 철회 완료가 아님.
- [ ] **G4-05 — 다중 스트리머 운영.** TLS·계정 격리·단일 upstream·CSRF·암호화 복원·갱신/철회·출력 보고. 브라우저 복구는 `24444b6`/#51 실제 Chromium 9개와 Windows/Linux × Node22/24 전체 계약 통과. 단일 인스턴스, 운영 접근/용량·호스팅·키/백업·실서비스 인증서는 미완료.
- [ ] **G4-06 — 실제 치지직 전체 사용.** 실제 앱/채널·장시간 수신·재연결·철회와 제공자 측 토큰 동작 필요.

## G5. 광고→시청 근거→HP→수익

개인 HUD·OBS 활동 보고·채널 시청자 추정은 검증된 광고 시청 실적이 아니다. 시험 수치는 지급 불가다.

- [x] **G5-01 — 시험 캠페인 목록·선택·중지.** 계정/CSRF·송출 승인 결합·철회·재시작 보존·재선택. 브라우저 복구/전환은 기존 선택을 바꾸지 않는다.
- [x] **G5-02 — 별도 OBS 공개 시험 소스.** `f083dc7`의 OBS 32.2.2/CEF 합성 PNG 13개 검사. 사용자 URL 배치이며 실방송/녹화·개인 HUD 제외 보장은 아님.
- [ ] **G5-03 — 시청시간 집계.** 비지급 활동·누락 기록, 공식 Live API 표본·추정/측정 범위. 0명/미측정 구분, 녹화/미리보기 제외, 소유자만 조회, 공개 조회로 증가하지 않음. 수집 기본 꺼짐, 권한·할당량·보관 조건 확인 후 서버 `CHATVIEW_AUDIENCE_SAMPLING=1`. 실제 광고 노출/주의 미검증.
- [ ] **G5-04 — 서버 HP·수익.** 규격 미정. 비지급/예상/확정 구분, HP 0 지급 조건 임의 추가 금지.
- [ ] **G5-05 — 유료 운영·정산.** 단가·예산·배분·지급·중복/초과·부정 실적·개인정보·광고 고지 필요.

## R. 통합 후 배포

- [ ] **R-01 — 첫 표시/연결 신뢰성과 방송 안정성.** 해당 경로에서 해결하며 독립 구현은 계속.
- [ ] **R-02 — 반복·자원·설치·캡처·패키지 qualification.** routine/개별 flow와 구분.
- [ ] **R-03 — 원컴/투컴 설치본·안내.** 실험/미지원/검증 범위 구분.

## 남은 결함

| ID | 근거 | 범위 |
| --- | --- | --- |
| D1 | `82f36a6`/#294 반복3 첫 gateway frame 표시 미확인. | G4/R-01. 원인 미확정, routine만으로 종료하지 않음. |
| D2 | #292 후반 핸들 +104>64; #294 초기 +284>256, 후반 +8. | R-02. 기존 자원 한도 유지. |
| D4 | CI 캡처 module의 동기 UI-task/join 및 borrowed item은 `510f8e1`에서 보완. #353 fixture 좌표 실패는 #354에서, core queue 정리 실패는 `66fd5a6`/#355에서 통과. #356에서도 core 통과. | core fixture 좁은 범위 통과. 실제 OBS frontend 종료/장면 전환·qualification은 남음. Control Center에 Q4/Q5라는 시험 UI는 없음. |
| D6 | `9e9863a`/#339 WGC 첫 픽셀 10초 실패, 진단만 추가한 #340 미재현. #353 resize/레터박스 조건 실패; #354/#355/#356 WGC 통과. | 이번 강화된 resize flow는 #356 통과했지만 과거 실패의 인과 재현은 미완료. 첫 표시까지 종료하지 않음. |

D3·D5는 좁은 범위에서 종료했다. 자원 상세는 [resource-growth-investigation.md](resource-growth-investigation.md). 별도로 #356 광고 브라우저 콜백 실패는 위와 아래에 남기며 검사 시작 동기화 수정의 재검증 전까지 해결로 세지 않는다.

## 이번 검증 상태

- **선행 수정 코드:** `66fd5a632f9d8e2c194c809ae46ddd0e83e15feb`, Windows #355 / run `37212720911` / job `111467003429`: 빌드 성공, routine 40/40. `chat-view-obs-capture-scene`, `chat-view-video-target-loss`, `chat-view-video-output-lifecycle`, `chat-view-window-capture` 각각 통과. #354의 39/40·core 실패 및 #353의 37/39·두 실패도 보존한다.
- **resize 코드:** `be151481f778d816fe51b9bf1f82a3908641e527`, Windows #356 / run `37213887083` / job `111470392044`: 빌드 성공, routine 39/40, 전체 127.63초. 지정 네 검사는 각각 통과(core 0.57초, target-loss 1.43초, output-lifecycle 3.13초, window-capture 2.93초). 별도 `chat-view-public-ad-browser`는 loaded 신호 전 **browser assertion callback** 실패. 원래 WGC #353 실패의 원인 확정 근거로 이 통과를 사용하지 않는다.
- **resize 코드의 로컬 검사:** 최신 OBS blob SHA를 확인해 읽은 파일만 변경. GCC C++20 `-Wall -Wextra -Werror -pedantic` 및 Clang C++20 ASan+UBSan으로 `video-layout-test.cpp` 각각 **445 assertion 통과**. 온전한 전환 프레임 폐기/잘린 surface 허용 두 변형은 같은 검사에서 각각 실패했다. 이는 정책·기하 검사이며 실제 WGC 재현 성공이 아니다. 원격 반영 코드와 로컬 검사 대상의 blob SHA도 대조했다.
- **이번 추가 광고 검사 동기화:** 읽은 `tests/public-ad-browser-test.cpp` blob `4a385c78212e713faddb8033bd6b8bdcf3da2cde`에서만 수정. 위 NavigationId/완료 gating과 실패 진단을 추가했다. Windows/WebView2 검사는 로컬 미실행이며 원격 결과도 아직 없다. 반영 후 정확한 코드 SHA·run/job ID·실제 결과를 이 절에 갱신한다. 단회 재통과로 #356 내부 원인을 확정하지 않는다.
- **미실행 범위:** 전체 qualification·실제 OBS frontend 종료/장면 전환 전체 흐름·물리 투컴 수신 영상·실계정·유료 정산은 미실행이다. 로컬 Linux에서는 Windows/libobs/WebView2 실행 성공을 주장하지 않는다.

### 다음 사용자 흐름

최신 OBS ref와 지정 문서를 순서대로 읽고 새 Windows 실행 결과부터 확인한다. 광고 검사 동기화 수정이나 다른 실패가 있으면 해당 경로부터 고친다. 이어 **게임 창을 연속 확대/축소해도 새 종횡비와 검은 여백으로 출력 지속**을 검증하며, #353 실패를 received/surface/presented 크기·상태·실제 출력 픽셀과 연결한다. 통과 재실행만으로 D6를 닫지 않고 시간·허용치·재시도 예산을 늘리지 않는다. 대상 상실/재선택은 다시 만들지 않는다.

OBS만 변경하고 main·새 작업 브랜치·검증 태그·강제 push를 사용하지 않는다. 브라우저 복구·인증·기기등록·표시키·옛 ZIP을 다시 만들지 않는다. 게임 PC OBS와 HP/단가/지급 규칙을 임의로 추가하지 않는다. D1/D2/D4/D6·물리 영상은 해당 증거 전까지 남긴다. 매번 qualification을 돌리거나 별도 보고서만 쓰지 말고 코드·관련 검사·이 문서로 인수인계한다.

미확정 입력: 실제 앱/채널·수집 조건/할당량, 투컴 배선/기기, HP/보상 규격, 인프라. 비밀은 대화/공개 저장소에 넣지 않는다.
