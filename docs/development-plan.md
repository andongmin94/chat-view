# ChatView 작업 목표 체크리스트

갱신: 2026-10-05 (Asia/Seoul). 이번 시작 기준 **`1338b7f194a40a44be3e8fa0784f60df86d0e79b`**. 지정 문서를 순서대로 읽고 #357의 40/40을 확인했다. 연속 resize 검사 **`7686e7f3599a7e482154403ac85d32aa55f687a7`**의 #359에서 새 실패를 재현한 뒤, **`4cbcc5e5dde15d56f31e5e5b8785c9d245d29cd9`**에서 pool 용량 재사용을 수정했다. **Windows #361 / run `37217766639` / job `111481699383`: 빌드 성공·routine 40/40, 완료**다. 이 문서가 유일한 현재 작업 목록이며, 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md)를 따른다.

## 현재 위치

`[x]`는 명시한 좁은 개발 범위다. 실제 제공자·물리 투컴·배포 완료와 구분한다.

기존 대상 상실/재선택은 유지한다. SourceLost/Failed/Stopped·끝난 worker·즉시 시작 거절은 선택과 패턴을 폐기하며 검은 출력/덮개 HWND와 출력 화면 결합을 보존한다. 같은 제목의 새 창이나 늦은 Capturing으로 재개하지 않는다. 새로고침 → 대상·출력 선택 → 새 패턴 확인 이후 같은 출력창을 재사용한다. 단순 새 프레임 대기는 같은 실행에서 복구 가능하다.

**실제 재현:** #359의 기존 #353 확대 조건과 정지 축소/혼합 resize 및 새 sequence 1–8은 통과했다. sequence 9에서 201×401 → 601×201 → 최종 600×300을 기다림 없이 재그리자 frame 수가 25에서 멈췄다. 최종 상태는 **Waiting(2), running=1, presented=200×300, received=600×300, surface=200×300, recreates=14, clipped=12, HRESULT=0, fresh=0, 출력 5개 픽셀 모두 검정**이었다. 출력은 최상위이고 source/output 사각형도 맞았다. worker 종료나 출력 가림이 아니라 잘린 표면 처리 후 새 완전 프레임 표시가 멈춘 경로다. 직전 sequence 8은 frame=25, recreates=12, clipped=10이었다. 당시 개별 Recreate 요청 크기 로그는 없으므로 마지막 두 재생성이 같은 크기였는지와 OS 내부 상태까지 단정하지 않는다.

**수정 동작:** `window-capture.cpp`와 기존 순수 frame-action 판정은 캡처 세션 중 확보한 가로/세로 최대 용량을 유지한다. 축소·이미 확보한 범위의 재확대는 pool을 재생성하지 않고 새 ContentSize 영역만 복사·새 종횡비로 표시한다. 용량을 넘는 확대만 각 축의 최대값으로 늘린다. 이미 충분한 용량을 요청했는데 늦은 작은 표면이 오면 검정 Waiting으로 버리고 새 표면을 기다리며 같은 용량으로 다시 Recreate하지 않는다. 불필요한 Recreate가 pending frame을 버리는 경로를 제거한 것이며 timeout·소스 강제 재그리기·자동 재시작으로 덮지 않는다. [공식 frame/padding/Recreate 계약](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture), [출력 계약](window-video-output.md).

온전한 프레임 표시 후 참조 해제·필요한 재생성 순서, 원래 콘텐츠 시각의 2초 수명, 최대 4096·pool 2개는 유지한다. 용량 재사용 시에도 콘텐츠 크기 변경은 GPU 작업 전에 시각을 무효화하므로 owner의 오래된 geometry 보호를 잃지 않는다. 큰 pool은 축소 후에도 해당 세션 종료까지 남을 수 있으며 자원 qualification 완료를 뜻하지 않는다. 진단에는 현재 요청 pool 크기를 추가하되 제목/영상/계정을 기록하지 않는다. 패널·기기등록·인증·광고·게임 PC OBS 의존성은 바꾸지 않는다.

**같은 실패 조건으로 재검증:** #359의 동일 12단계 입력·고유 색·9개 내부 픽셀·검은 여백·정확한 크기·신선도·frame 진행 조건을 그대로 재사용했다. A→B→A·같은 크기 새 색·홀수 크기·마지막 네 단계의 즉시 중간 재그리기를 유지했다. 각 단계의 pool 최대 용량과 정확한 Recreate 횟수 검사도 추가하여 용량 내에서 flush가 다시 발생하면 실패한다. **#361의 실제 WGC 검사는 6.73초에 통과**했으며 기존 output resize/minimize/stop/restart/close/HUD 가시성과 #353 assertion까지 이어서 통과했다. 첫 프레임 10초·각 resize 5초·색상 오차 8·전체 CTest 40초 제한은 바꾸지 않았다. #359에서 관측한 연속 burst 정지 경로의 좁은 수정/회귀 근거이며, #353의 과거 원인이나 전체 하드웨어 지원을 확정한 것은 아니다.

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
- [ ] **G3-03 — HUD 분리 영상.** 실험·물리 검증 필요. WGC→D3D11/D2D, 창/별도 SDR 출력, 패턴·육안 확인·같은 HWND 전환·검정 중지·명시적 해제. 잠금/전원/화면/장치·대상 상실은 선택 무효화, 자동 재시작 없음. 콘텐츠 2초 제한은 `4f1f048`/#341 검사. 연속 resize의 pool 재사용/픽셀 회귀는 `4cbcc5e`/#361 통과. D6 및 하드웨어는 남음. 영상만·최대 4096, 오디오/인코더/네트워크 영상 없음.
- [ ] **G3-04 — 설치·실사용·수신 영상.** 실제 배선/캡처카드/게임/다중 GPU/자원/지연·잠금/hotplug와 HUD 없는 녹화 필요.

## G4. 치지직부터 자체 플랫폼

- [x] **G4-01 — 공식 인증·채널·구독.** 합성과 실계정은 별도.
- [x] **G4-02 — 서버→WinHTTP→자체 HUD.** D1 미해결.
- [x] **G4-03 — 로그인→브라우저→앱 복귀.** verifier/고정 역할 동의 유지. `24444b6`/#51 실제 Chromium 폼·쿠키·합성 제공자 왕복·역할 승인 확인. 실계정/native 시스템 브라우저 전체 성공은 아님.
- [x] **G4-04 — 실행 갱신·선택 저장·재연결·로그아웃.** D5는 `8fa41db`/#326 종료. 전환·현재 승인 복귀 `7bd8b28`/#344 통과. 현재 승인만 해제하고 다른 저장 계정 보존. 일반 종료/응답 유실은 철회 완료가 아님.
- [ ] **G4-05 — 다중 스트리머 운영.** TLS·계정 격리·단일 upstream·CSRF·암호화 복원·갱신/철회·출력 보고. 브라우저 복구는 `24444b6`/#51 실제 Chromium 9개와 Windows/Linux × Node22/24 계약 통과. 단일 인스턴스, 운영 접근/용량·호스팅·키/백업·실서비스 인증서는 미완료.
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
| D4 | CI 캡처 module의 동기 UI-task/join 및 borrowed item은 `510f8e1`에서 보완. #353 fixture 좌표는 #354, core queue 정리는 `66fd5a6`/#355에서 통과. #356/#357/#359/#361도 core 통과. | core fixture만 확인. 실제 OBS frontend 종료/장면 전환·qualification 남음. Control Center에 Q4/Q5 시험 UI는 없음. |
| D6 | `9e9863a`/#339 WGC 첫 픽셀 10초 실패, 진단만 추가한 #340 미재현. #353 단발 resize/레터박스 실패. #359는 별도 연속 burst 단계9 재현 후 `4cbcc5e`/#361 동일 입력·강화 assertion 통과. | 연속 burst의 pool 재사용 수정은 좁게 확인. #353의 인과와 첫 표시 문제는 여전히 미해결. |

D3·D5는 좁은 범위에서 종료했다. 자원 상세는 [resource-growth-investigation.md](resource-growth-investigation.md).

## 보존할 실행 근거

| 실행 / 정확한 코드 | run / Windows x64 job | 실제 결과 |
| --- | --- | --- |
| #353 / `510f8e195a8cb321844dda6ff7f6a1be55bd1e4d` | `37142325494` / `111259184927` | 빌드 성공, routine 37/39. core의 user position preserved 및 WGC의 resize recreates pool and letterboxes without old padding 실패. 당시 resize 크기·픽셀 로그가 없어 원인 미확정. |
| #354 / `025bb12e26b79dfad335aaaf1798c07a79913cb6` | `37169828714` / `111340331975` | 빌드 성공, 39/40. core 좌표는 통과했지만 scene and all source references drained before OBS shutdown 실패. target-loss/output-lifecycle/window-capture 통과. |
| #355 / `66fd5a632f9d8e2c194c809ae46ddd0e83e15feb` | `37212720911` / `111467003429` | 빌드 성공, 40/40. core-only에서 obs_wait_for_destroy_queue가 즉시 반환하던 경합을 실제 destroy-task fence로 수정. 잔여 source 0·input destructor 두 개 완료 통과. |
| #356 / `be151481f778d816fe51b9bf1f82a3908641e527` | `37213887083` / `111470392044` | 빌드 성공, 39/40, 127.63초. core 0.57초·WGC 2.93초·output-lifecycle 3.13초·target-loss 1.43초 통과. 별도 public-ad-browser가 loaded 전에 browser assertion callback 실패(11.79초). |
| #357 / `614688f2d7821f14e6cd2ced898cc8559ed363dc` | `37214818540` / `111473101019` | 빌드 성공, 40/40, 129.21초. core 0.11초·WGC 2.54초·output-lifecycle 3.50초·target-loss 1.28초·public-ad-browser 15.23초 통과. |
| #359 / `7686e7f3599a7e482154403ac85d32aa55f687a7` | `37216401948` / `111477714374` | 빌드 성공, 39/40, 132.61초. WGC 9.99초 실패: sequence9, 위 frame/픽셀 증거 참조. core 0.08초·output-lifecycle 8.23초·target-loss 1.33초·public-ad-browser 15.21초 통과. |
| #361 / `4cbcc5e5dde15d56f31e5e5b8785c9d245d29cd9` | `37217766639` / `111481699383` | **빌드 성공, routine 40/40, 136.28초, 완료.** core 0.08초·WGC 6.73초·output-lifecycle 3.18초·target-loss 1.38초·public-ad-browser 15.46초 통과. |

#357의 광고 검사 동기화 재통과로 #356 브라우저 내부 원인을 확정하지 않는다. #359의 단발 #353 조건은 통과했지만 더 강한 burst 조건이 실패한 것이며, #361 수정 후 통과도 #353의 동일 원인 해결로 소급하지 않는다. 위 routine은 development 실행이고 qualification/package/전체 OBS 단계는 미실행이다.

## 이번 검증 상태

- **원격 구현:** `4cbcc5e5dde15d56f31e5e5b8785c9d245d29cd9`. #359 결과를 확인한 `770038c23d66e7c457a7219b3741d732cb30929f` 위에서 worker·snapshot의 pool 용량·기존 frame 판정·관련 순수/실제 픽셀 검사·출력 계약·이 계획만 수정했다. 원격 diff 7개 파일과 검사한 5개 코드/test blob SHA를 대조했다. 다른 제품/인증/광고 파일과 CMake 한도는 바꾸지 않았다.
- **로컬 실행:** GCC C++20 `-Wall -Wextra -Werror -pedantic`, Clang C++20 ASan+UBSan으로 수정한 `video-layout-test.cpp` 각각 **1,116 assertion 통과**. 축소 때 재생성/충분한 용량의 늦은 작은 표면마다 재생성 두 변형은 같은 검사에서 각각 실패했다. 12개 fit·108개 내부 표본·검정 여백/표식 위치·고유 색 비중첩 및 기존 시간/색상/종료 조건 보존 검사도 통과했다. 현재 SHA에서 원본 blob을 확인한 기존 `video-output-check-test.cpp`는 두 컴파일러에서 각각 **57 assertion 통과**했다. Windows/WGC/libobs/WebView2는 로컬 미실행이다.
- **Windows 실제 결과:** **#361 / `37217766639` / `111481699383`: completed/success, 빌드 성공·40/40**을 최종 job 상태와 전체 로그에서 확인했다. 지정 네 검사 `chat-view-obs-capture-scene`, `chat-view-video-target-loss`, `chat-view-video-output-lifecycle`, `chat-view-window-capture` 모두 통과했다. 연속 resize 입력이나 픽셀/시간 기준을 낮추지 않았으며 새 pool 용량/재생성 assertion까지 통과했다. #359의 실패 로그는 위에 보존한다.
- **미실행 범위:** 전체 qualification·실제 OBS frontend 전체 흐름·물리 투컴 수신 영상·실계정·유료 정산. #361의 반복/자원/설치/패키지/공식 OBS 실행 단계는 development 모드에서 skipped였다. 합성 창의 실제 WGC 픽셀 검사는 물리 게임/배선의 지원 인증이 아니다.

### 다음 사용자 흐름

최신 OBS ref와 지정 문서를 순서대로 확인한 뒤 **실제 companion 패널에서 크기 변경 중 사용자 중지 → 검정 유지 → 새 시험 패턴 확인 후 같은 출력창으로 재개**를 연결한다. 이번 엔진의 연속 resize를 처음부터 다시 만들지 말고 기존 패널의 중지/확인/owner 상태 소비와 결합한다. 실제 프레임 상태·검은 덮개 픽셀·HWND 보존을 함께 확인하고 시간/색상/재시도 예산을 늘리지 않는다. D6의 #353 인과와 첫 표시 기록은 별도로 유지한다.

OBS만 변경하고 main·새 작업 브랜치·검증 태그·강제 push를 사용하지 않는다. 브라우저 복구·인증·기기등록·표시키·옛 ZIP을 다시 만들지 않는다. 게임 PC OBS와 HP/단가/지급 규칙을 임의로 추가하지 않는다. D1/D2/D4/D6는 해당 근거 전까지 남긴다. 별도 보고서나 매번 전체 qualification 대신 코드·관련 검사·이 문서를 갱신한다.

미확정 입력: 실제 앱/채널·수집 조건/할당량, 투컴 배선/기기, HP/보상 규격, 인프라. 비밀은 대화/공개 저장소에 넣지 않는다.
