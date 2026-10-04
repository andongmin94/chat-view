# ChatView 작업 목표 체크리스트

갱신: 2026-10-05 (Asia/Seoul). 이번 시작 기준 **`37d0ccdf5a6fbeb7def86d036da024bed282574d`**. 최신 OBS와 지정 문서를 순서대로 읽고 **#362 / run `37220157340` / job `111488721555`의 빌드 실패**를 확인했다. `f21e00bc113ef1e92be6b3b5e84624fe3f156c9f`의 lifecycle 검사 243행에서 Windows `near` 매크로와 지역 함수명이 충돌했다. CTest는 실행되지 않았으며 네 캡처 검사 모두 미실행이다. 이번 수정은 그 식별자를 `within_tolerance`로 바꾸며 중지·재개 흐름과 모든 검사 조건을 유지한다. 수정 코드 **`04c1558bad2474e7ef444f5c1891b2dbbe7f6ec3`**의 **Windows #363 / run `37242100880` / job `111552735174`는 실행 중**이다. OBS 개발 라이브러리 빌드는 성공했으며 ChatView 빌드·검사 결과는 아직 확정하지 않는다.
이 문서가 유일한 현재 작업 목록이다. 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md), 영상 계약은 [window-video-output.md](window-video-output.md)를 따른다.

## 현재 위치

`[x]`는 명시한 좁은 개발 범위다. routine·합성 입력·실제 OBS 전체 흐름·실계정·물리 투컴·배포 qualification을 구분한다.

**진행 중인 사용자 흐름:** 크기 변경 중 패널 중지 → 검정 유지 → 새 시험 패턴 확인 → 같은 출력창의 새 영상 재개. 구현 `f21e00b`는 단일 사용 확인을 `begin_capture`의 worker 시작 직전에서 소비한다. `start`의 기본-No 대화상자·선택 세대·출력/위상 검사는 유지한다. 정상 대상의 사용자 중지는 대상 선택과 두 HWND를 유지하지만 확인은 폐기한다. 대상 상실·실패·종료는 선택 목록과 창 식별까지 폐기하며, 같은 제목의 새 창이나 늦은 Capturing 보고로 재개하지 않는다. 새로고침 → 대상·출력 선택 → 새 패턴 확인으로 같은 검은 출력창을 재사용한다.

**#362 수정:** `tests/video-output-lifecycle-test.cpp::output_pixels`의 `const auto near`가 Windows SDK `minwindef.h`의 빈 `#define near`로 지워져 MSVC C2513이 발생했다. [Microsoft SDK 헤더](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/shared/minwindef.h)의 정의와 로컬 매크로 적용 컴파일로 확인했다. 식별자만 교체하며 SDK 매크로 해제·조건부 우회·허용치 변경은 하지 않는다. 이전 로컬 22개 시작 경계 검사는 이 Windows 픽셀 함수 컴파일을 포함하지 않았으므로 #362 빌드 성공 근거가 아니었다.

**유지한 연결 검사:** lifecycle의 fixture 직접 worker 시작/덮개 숨김은 제거된 상태다. 패턴 → 실제 시작 경계 → 실제 `update_capture`로 표시한다. 600×300 새 색과 여백을 확인한 뒤 200×300 → 601×201 → 200×300을 기다림 없이 변경하고 실제 패널 중지 명령을 보낸다. 즉시 latch·덮개 가시성·200ms 미만 반환·검정 9개 표본·worker 종료를 검사한다. 중지 뒤 다른 크기/색과 늦은 보고는 재개하지 않는다. 새 패턴만으로는 캡처하지 않으며 확인 후 같은 HWND에서 600×200 새 고유 색·여백·신선한 프레임, 이어 400×300 애니메이션을 검사한다. stale/미래/누락 시각, 잠금/전원/화면/장치, 해제 동의와 활성 종료도 유지한다. 양성 위상·사람의 수신 확인과 늦은 상태 재생은 합성 경계다. 물리 두 모니터/수신 영상의 성공으로 세지 않는다.

**#359/#361 resize 근거 보존:** #359의 기존 #353 단발 확대와 sequence 1–8은 통과했다. sequence 9의 즉시 201×401 → 601×201 → 600×300 재그리기 후 frame=25, Waiting(2), running=1, presented=200×300, received=600×300, surface=200×300, recreates=14, clipped=12, HRESULT=0, fresh=0, 출력 5개 픽셀 모두 검정이었다. 직전은 frame=25/recreates=12/clipped=10이며 출력 최상위·사각형은 정상이었다. 개별 Recreate 요청 로그가 없어 마지막 두 재생성 크기나 OS 내부 상태는 단정하지 않는다. `4cbcc5e`는 축별 최대 pool 용량을 유지해 용량 내 축소/재확대와 늦은 작은 표면 때문에 다시 Recreate하지 않는다. 잘린 프레임은 검정 대기, 정확한 ContentSize만 복사한다. 동일 12단계 입력·9개 내부 픽셀·고유 색·여백·pool 용량/정확한 재생성 횟수는 #361 통과. #353의 동일 원인 해결로 소급하지 않는다. 큰 용량은 세션 종료까지 남을 수 있어 자원 qualification은 별도다.

첫 프레임 10초·각 resize 5초·색상 오차 8·WGC 40초/lifecycle 60초 CTest·콘텐츠 2초·최대 4096/pool 2개 제한을 유지한다. 이번에는 제품 엔진·패널·대상 상실·인증·광고·CMake·의존성을 바꾸지 않는다. 출력 계약은 정상 중지와 대상 상실의 재개 절차, 단일 사용 확인을 소비하는 실제 시작 경계, 직접 시작/덮개 숨김 없는 패널 검사 범위를 현재 코드와 맞췄다.

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
- [ ] **G3-03 — HUD 분리 영상.** 실험·물리 검증 필요. WGC→D3D11/D2D, 별도 SDR 출력, 패턴·육안 확인·같은 HWND 전환·검정 중지·명시적 해제. 잠금/전원/화면/장치·대상 상실은 선택 무효화, 자동 재시작 없음. 콘텐츠2초는 `4f1f048`/#341, 연속 resize/pool 재사용은 `4cbcc5e`/#361. 패널 stop/restart `f21e00b`/#362는 빌드 실패·CTest 미실행, 식별자 수정 `04c1558`/#363의 Windows 재검증 실행 중. 영상만·최대4096, 오디오/인코더/네트워크 영상 없음. D6/하드웨어 남음.
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

- [ ] **R-01 — 첫 표시/연결 신뢰성과 방송 안정성.** 해당 경로에서 해결하며 독립 구현은 계속.
- [ ] **R-02 — 반복·자원·설치·캡처·패키지 qualification.** routine/개별 flow와 구분.
- [ ] **R-03 — 원컴/투컴 설치본·안내.** 실험/미지원/검증 범위 구분.

## 남은 결함

| ID | 근거 | 범위 |
| --- | --- | --- |
| D1 | `82f36a6`/#294 반복3 첫 gateway frame 표시 미확인. | G4/R-01. 원인 미확정, routine만으로 종료하지 않음. |
| D2 | #292 후반 핸들 +104>64; #294 초기 +284>256, 후반 +8. | R-02. 기존 자원 한도 유지. |
| D4 | CI 캡처 module의 동기 UI-task/join 및 borrowed item은 `510f8e1`에서 보완. #353 fixture 좌표는 #354, core queue 정리는 `66fd5a6`/#355에서 통과. #356/#357/#359/#361도 core 통과. | core fixture만 확인. 실제 OBS frontend 종료/장면 전환·qualification 남음. Control Center에 Q4/Q5 시험 UI는 없음. |
| D6 | `9e9863a`/#339 WGC 첫 픽셀10초 실패, 진단만 추가한 #340 미재현. #353 단발 resize/레터박스 실패. #359 별도 burst단계9 재현 후 `4cbcc5e`/#361 동일 입력·강화 assertion 통과. | 연속 burst만 좁게 확인. #353 인과와 첫 표시 문제는 여전히 미해결. |

D3·D5는 좁은 범위에서 종료했다. 자원 상세는 [resource-growth-investigation.md](resource-growth-investigation.md).

## 보존할 실행 근거

| 실행 / 정확한 코드 | run / Windows x64 job | 실제 결과 |
| --- | --- | --- |
| #353 / `510f8e195a8cb321844dda6ff7f6a1be55bd1e4d` | `37142325494` / `111259184927` | 빌드 성공, routine37/39. core의 user position preserved 및 WGC의 resize recreates pool and letterboxes without old padding 실패. 당시 resize 크기·픽셀 로그가 없어 원인 미확정. |
| #354 / `025bb12e26b79dfad335aaaf1798c07a79913cb6` | `37169828714` / `111340331975` | 빌드 성공,39/40. core 좌표는 통과했지만 scene and all source references drained before OBS shutdown 실패. target-loss/output-lifecycle/window-capture 통과. |
| #355 / `66fd5a632f9d8e2c194c809ae46ddd0e83e15feb` | `37212720911` / `111467003429` | 빌드 성공,40/40. core-only에서 obs_wait_for_destroy_queue가 즉시 반환하던 경합을 실제 destroy-task fence로 수정. 잔여 source0·input destructor 두 개 완료 통과. |
| #356 / `be151481f778d816fe51b9bf1f82a3908641e527` | `37213887083` / `111470392044` | 빌드 성공,39/40,127.63초. core0.57초·WGC2.93초·output-lifecycle3.13초·target-loss1.43초 통과. public-ad-browser가 loaded 전에 browser assertion callback 실패(11.79초). |
| #357 / `614688f2d7821f14e6cd2ced898cc8559ed363dc` | `37214818540` / `111473101019` | 빌드 성공,40/40,129.21초. core0.11초·WGC2.54초·output-lifecycle3.50초·target-loss1.28초·public-ad-browser15.23초 통과. |
| #359 / `7686e7f3599a7e482154403ac85d32aa55f687a7` | `37216401948` / `111477714374` | 빌드 성공,39/40,132.61초. WGC9.99초 실패: sequence9, 위 frame/픽셀 근거. core0.08초·output-lifecycle8.23초·target-loss1.33초·public-ad-browser15.21초 통과. |
| #361 / `4cbcc5e5dde15d56f31e5e5b8785c9d245d29cd9` | `37217766639` / `111481699383` | 빌드 성공, routine40/40,136.28초, 완료. core0.08초·WGC6.73초·output-lifecycle3.18초·target-loss1.38초·public-ad-browser15.46초 통과. |
| #362 / `f21e00bc113ef1e92be6b3b5e84624fe3f156c9f` | `37220157340` / `111488721555` | **빌드 실패, CTest 전체 미실행.** lifecycle 243행 `const auto near`의 C2513. 네 지정 캡처 검사 모두 미실행이며 실행 실패/통과 개수를 부여하지 않는다. |
| #363 / `04c1558bad2474e7ef444f5c1891b2dbbe7f6ec3` | `37242100880` / `111552735174` | **실행 중, 최종 결과 없음.** OBS 개발 라이브러리 성공, ChatView 빌드·검사 단계. 네 지정 캡처 검사의 개별 결과는 아직 확인하지 못했다. |

#357 이후 광고 검사 재통과로 #356 내부 원인을 확정하지 않는다. #359의 더 강한 burst 실패/수정 후 통과는 #353의 동일 원인 해결로 소급하지 않는다. 위 routine은 development이고 qualification/package/전체 OBS 단계는 미실행이다.

## 이번 검증 상태

- **원격 코드:** `04c1558bad2474e7ef444f5c1891b2dbbe7f6ec3`. 원격 비교에서 검사 식별자 세 줄과 작업 계획만 변경됐고, 원격 test blob이 로컬과 일치함을 확인했다.
- **수정 범위:** 최신 OBS에서 읽은 lifecycle blob `22743d5b204f833392fb222a7c210a6d4b68251d`를 로컬에서 SHA 일치 확인한 뒤 comparator 이름만 교체했다. 수정 blob `d468252058cc5f9645e1a89d0c27bf8a7b5803a3`. 실행 입력·assertion·시간/색상 기준과 제품 파일은 그대로다. 후속 문서 갱신은 이 계획과 기존 출력 계약만 포함한다.
- **로컬 실행:** 실제 `output_pixels` 함수 본문에 SDK와 같은 빈 `near` 매크로를 적용하고 GDI 호출만 메모리 표본으로 대체했다. 원본은 GCC/Clang 모두 선언 지점에서 컴파일 실패, 수정본은 C++20 `-Wall -Wextra -Werror -pedantic` 및 Clang ASan+UBSan에서 각각 **1,042 assertion 통과**. 네 fit의 내부 9개/여백 각 표본, 채널별 ±8 허용/±9 거부, 무효 픽셀/DC 실패/해제, 검정 표본을 확인했다. 식별자 외 함수/파일 차이 없음도 검사했다. 색상 오차를 9로 늘리거나 하단 여백/내부 표본 일부를 빼는 세 변형도 기존 로컬 검사에서 각각 실패했다. 이는 매크로/픽셀 판정 회귀이며 Windows/WGC 실행이 아니다.
- **수정 후 Windows:** **#363 / run `37242100880` / Windows x64 job `111552735174`: in_progress, conclusion=null**. OBS 개발 라이브러리는 성공, ChatView 빌드·검사 실행 중이다. `chat-view-obs-capture-scene`, `chat-view-video-target-loss`, `chat-view-video-output-lifecycle`, `chat-view-window-capture` 각각의 결과는 아직 미확인이다. #362의 CTest 미실행과 이번 결과를 이전 #361 통과로 대신하지 않는다. 문서 갱신은 native 실행을 취소하지 않는다.
- **미실행 범위:** 로컬 Windows/WGC/libobs/WebView2, 전체 qualification·실제 OBS frontend 전체 흐름·물리 투컴 수신 영상·실계정·유료 정산.

### 다음 사용자 흐름

최신 OBS ref와 지정 문서를 순서대로 읽고 **#363 / `37242100880` / `111552735174`**의 실제 결과부터 확인한다. **크기 변경 중 패널 중지 → 새 패턴 확인 → 같은 출력창의 새 영상 재개**를 마무리한다. 실패가 있으면 해당 경로부터 고친다. 프레임 크기/상태·덮개 픽셀·HWND 보존을 함께 확인하며 대상 상실/재선택과 엔진 resize를 처음부터 다시 만들지 않는다.

OBS만 변경한다. main·새 작업 브랜치·검증 태그·강제 push, 새 브라우저 복구/인증/기기등록/표시키/옛 ZIP, 게임 PC OBS, 임의 HP/단가/지급 규칙을 추가하지 않는다. D1/D2/D4/D6는 해당 근거 전까지 남긴다. 별도 보고서나 매번 전체 qualification 대신 코드·관련 검사·이 문서를 갱신한다.

미확정 입력: 실제 앱/채널·수집 조건/할당량, 투컴 배선/기기, HP/보상 규격, 인프라. 비밀은 대화/공개 저장소에 넣지 않는다.
