# ChatView 작업 목표 체크리스트

갱신: 2026-10-16 (Asia/Seoul). **이 문서가 유일한 현재 작업 목록이다.** 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md), 영상 계약은 [window-video-output.md](window-video-output.md)를 따른다. 완료된 조사·중복 기록은 Git 이력에 남긴다.

`[x]`는 명시한 좁은 개발 범위다. 로컬 검사, Windows routine, 실제 OBS 전체 흐름, 실계정, 물리 투컴, 배포 qualification은 서로 대체하지 않는다. OBS만 변경한다.

## 현재 흐름: 게임 PC 새 승인 후 송출 PC·공개 광고 보존과 관리 화면 확인

시작 OBS HEAD `8186b2e14e31df4e6f31dc427fc39d024ed0a70c`. AGENTS → PRODUCT → development-workflow → architecture → 이 문서를 순서대로 확인했다. 직전 구현 `2e28b99d6c73d70a8da50986be72b811bd51ece7` Windows #413/run `38833509555`는 MSVC 성공·CTest **48/49 실패·223.32초**였지만 신규 `redraw-reapproved-live/black`은 각각 **5.09/5.71초 통과**했다. 유일한 실패는 기존 D7의 `public-ad-browser` WebView2 `started1/completed0/id2` 13.59초다. 사용자 채팅 흐름 성공을 전체 빌드 통과나 실방송 안정성으로 확대하지 않는다.

### 구현

- **브라우저 `/account`를 같은 채널의 게임 PC·송출 PC 상태를 보는 읽기 전용 화면으로 보강**한다. 이미 존재하는 역할별 PC 승인 수, 실제 열린 WebSocket 수, 송출 출력 보고 상태 옆에 기존 `Campaigns.status`와 **공개 `Campaigns.snapshot` 서버 조건을 그대로 읽는** 송출 시험 배너 상태를 추가한다. `data-account-ad-state`: `not-selected` / `streaming-disconnected` / `waiting-report` / `report-ready`; `data-account-ad-selected`와 `/campaigns#public-banner-status` 링크를 제공한다.
- 이 표시는 관리자의 인증된 GET으로만 조회하고 브라우저 새로고침이나 게임 PC 재승인으로 송출 승인·채팅 소켓·캠페인 선택·공개 URL·OBS 구성·방송 출력 보고·WGC를 변경하지 않는다. `report-ready`는 **서버의 공개 TEST 응답 조건 충족**이지 실제 OBS 렌더·시청자 광고 노출/영상 HUD 제외/수익 기록이 아니다. 게임 PC의 role 재승인/로그아웃은 송출 PC의 선택 상태를 대신 바꾸지 않으며, 송출 승인 철회 시 광고는 기존 정책대로 사용자가 직접 재선택해야 한다.
- 신규 HTTP/SQLite/WebSocket 회귀 `platform/tests/gaming-reapproval-ad-isolation.test.mts`는 같은 creator의 기존 송출 승인·게임 승인, 별개 creator의 게임 승인을 실제 server gateway에 연결한 채 시작한다. **브라우저 개별 게임 연결 해제 → 정확히 게임 0/송출 1 → 게임 역할만 명시적 신규 승인 → 게임 1/송출 1 → 첫 provider 채팅을 송출·게임 각 소켓 수신 → 게임만 로그아웃** 동안 송출 원래 연결 ID, 새/옛 게임 ID 구분, 광고 선택의 streaming connection FK, 기존 공개 URL, 서버 공개 TEST snapshot 및 출력 보고/상대 creator 보존을 확인한다. GET·public 조회의 DB 변경 없음·다른 owner 비노출도 검사한다.
- 두 번째 테스트는 선택 전 `not-selected` → 송출 선택 후 보고 없을 때 `waiting-report` → 송출 WebSocket 단절 시 `streaming-disconnected` → 사용자가 직접 공개 광고 중지한 뒤 `not-selected`와 동일 URL 잔존을 확인한다. 게임 소켓은 유지하며 송출 소켓 단절이나 게임 역할 승인만으로 광고를 재선택/노출했다고 계산하지 않는다.
- 기존 Chromium management fixture/Playwright 390px에 하나의 creator가 직접 **게임 PC 기존 연결만 해제 → 새 게임 역할·확인 번호 승인 → 수집/실제 소켓 연결 → 동일 송출 역할·광고 상태와 공개 주소 확인 → 새 게임 연결만 해제** 동선을 추가한다. 실제 브라우저 CSRF 폼으로 철회·승인하고 메타데이터/계정 격리·기존 송출 테스트 배너 선택이 초기 상태로 복원됐는지 확인한다. 게임 PC에 OBS를 설치하거나 실행하지 않는다.
- **기능 커밋 `105e2510e73217c03411fe7d01feed0d643936fa` 실제 CI:** CHZZK contract **#77/run `38835792246` 전체 실패**(Node 22/24 Ubuntu·Windows 서비스 검사). Ubuntu24 **327/328 통과**, 기존 326개 및 신규 `account ad summary` 테스트 통과, 신규 `owner revokes and reapproves only gaming chat` 테스트는 SQLite `DatabaseSync`의 **null-prototype row 객체**와 일반 `{ connection_id }`를 `deepStrictEqual`로 비교해 데이터가 같아도 실패했다. 결과의 정확한 **connection_id 필드만 비교**하도록 검사 코드를 수정하며 광고 선택/CSRF/소켓/계정/보고 조건은 유지한다. Chromium **19/19 통과**(동일 sender 유지·게임 단독 개별 철회→새 동의→수집→개별 해제, 390px), 공식 OBS public ad flow **#24/run `38835792158` 성공·13/13 합성 픽셀 검사 통과**. Windows development **#414/run `38835792164` native impact scope 성공, Windows x64 빌드 skipped**(서버/브라우저만 변경).
- **후속 `afbc31ed3fa9d0c459eb39671396da4ca07b0e05`, CHZZK contract #78/run `38836053021`: 전체 실패**. Ubuntu24 기준 **327/328 통과** 및 Chromium **19/19 성공**. 새 HTTP 테스트에서 일부러 네이티브 pending poll을 조회한 직후 동의 승인 수집 poll을 시작해 서버의 정상 `LOGIN_POLL_MS` 429 rate gate에 걸렸다. 실제 게임 PC는 이 간격을 지키므로, 테스트도 **기존 production poll cadence가 지난 뒤** 수집하도록 수정한다. 서버의 429 검사·실행 한도는 변경하지 않는다.
- **최종 코드 `21514f3bfcfdabb0069456d14ebefaca43d8e8c8`: CHZZK contract #79/run `38836241099` completed/success, 5개 job 모두 성공**. Ubuntu/Windows × Node22·24 플랫폼 테스트·엄격 타입·의존성 검사 모두 통과, Ubuntu24 상세 **328/328·실패/건너뜀0**, Chromium **19/19**(390px 게임 단독 재승인·송출/광고 선택·공개 URL 보존·관리 GET 상태) 통과. Windows development **#416/run `38836241076` 범위 판정 성공, Windows x64 native 빌드 skipped**(코드는 서버/브라우저만 변경). 공식 OBS 배너 픽셀 **#24는 기능 커밋 `105e2510`에서 13/13 통과**했으며 최종 테스트 수정 SHA에 새로 실행되진 않았다. #77의 SQLite prototype·#78의 poll cadence 테스트 실패는 각각 테스트 수정 뒤 #79에서 해소했다. 서버 429 간격 제한·CSRF/쿠키·기존 native/WGC/광고 합성 판정과 타임아웃은 변경하지 않았다.
- 실제 두 PC/게임용 OS 창·물리 캡처카드·치지직 실계정/시청자·유료 광고·지급/HP·설치 qualification을 이번 합성 환경 통과로 인증하지 않는다. 기존 D1/D2/D4/D6/D7 간헐 결함은 별도 미완료다.

### 미완료

- D1 첫 HUD/WebView2 표시 지연, D2 핸들/자원 증가, D4 OBS frontend/초기 장면, D6 WGC 실제 두 PC/출력 보호, D7 공개 광고 WebView2 NavigationCompleted·OBS CEF 간헐 렌더 및 전체 설치·자원·실계정·정산 qualification은 별도 남는다. 이전 성공 런이나 fixture의 synthetic provider/출력 보고를 실제 지원 완료로 취급하지 않는다.

## G1. 한 화면의 작업과 개인 채팅

- [x] **G1-01 — 투명 HUD와 자체 텍스트 채팅.** 실계정은 G4-06.
- [x] **G1-02 — 클릭 통과·이동/크기·DPI 저장·단축키.** 기존 구현/검사 유지.
- [ ] **G1-03 — 실제 게임 가독성·조작감.** 실환경 입력/포커스 확인 필요.

## G2. OBS 연동과 개인 HUD 제외

HUD가 보이면서 수신 영상에는 없어야 한다. HUD 숨김은 보호용 기능 저하이지 지원 완료가 아니다.

- [x] **G2-01 — 플러그인·설정·HUD 수명 연동.** OBS/HUD 분리 유지.
- [ ] **G2-02 — 원컴 가독 HUD와 제외 동시 달성.** 실제 지원 영상 확인 필요.
- [ ] **G2-03 — 방송/장면·복귀·안내.** D3 문구는 `72775a5`/#342에서 검증. 관리 브라우저는 HUD 보호 밖이다.

## G3. 게임 PC에 OBS 없는 투컴

- [x] **G3-01 — 동일 HUD의 OBS 비종속 실행.** companion·로그인·중복 방지·종료, #387 양방향 동일 HWND·gaming 역할·OBS 모듈 부재 통과.
- [ ] **G3-02 — 동일 계정의 채팅·방송 세션.** 역할 동의·공유·암호화 복원·로그아웃·최근 출력 보고 구현. `8aed41c`/#62 계정 화면은 승인 수/서버 소켓 수를 구분한다. 중지 후 이전 접속 수를 지우며 현재 승인만 복귀한다. 철회/재승인은 #393까지 routine 통과. 기존 승인 충돌 복귀는 #67, 동시 수집 경쟁은 #68/#398까지 통과. 물리 두 PC·장면/소스 전체는 남았다.
- [ ] **G3-03 — HUD 분리 영상.** WGC→D3D11/D2D·별도 SDR 출력·식별 패턴·기본 No 수신 확인·검정 중지·명시적 해제. 잠금/전원/화면/장치/대상 상실은 선택 폐기, 자동 재시작 없음. 동기 Stop 우선권·별도 덮개·실제 HWND 보호 확인 유지. 영상만/최대4096; 오디오·인코더·영상 네트워크 없음. D6/하드웨어 남음.
- [ ] **G3-04 — 설치·실사용·수신 영상.** 게임 PC OBS 없이 확장 SDR HDMI/캡처카드·게임/다중 GPU·자원/지연·잠금/hotplug·HUD 없는 녹화를 검증해야 한다. 화면 전체 캡처로 대체하지 않는다.

## G4. 치지직부터 자체 플랫폼

- [x] **G4-01 — 공식 인증·채널·구독.** 합성과 실계정은 별도.
- [x] **G4-02 — 서버→WinHTTP→자체 HUD.** D1 미해결.
- [x] **G4-03 — 로그인→브라우저→앱 복귀.** verifier/고정 역할 동의 유지. `24444b6`/#51 Chromium 폼·쿠키·합성 제공자·역할 승인 왕복. 대기 중 같은 승인 페이지 재열기는 위 변경/검사 참조. 실계정/native 시스템 브라우저 전체 성공은 아니다.
- [x] **G4-04 — 갱신·선택 저장·재연결·로그아웃.** D5 `8fa41db`/#326 종료. 현재 승인만 해제하고 다른 저장 계정 보존. 프로세스 종료/응답 유실은 철회 완료가 아니다.
- [ ] **G4-05 — 다중 스트리머 운영.** TLS·계정 격리·단일 upstream·CSRF·암호화 복원·갱신/철회·출력 보고 구현, 단일 인스턴스. 운영 접근/용량·호스팅·키/백업·실서비스 인증서 미완료.
- [ ] **G4-06 — 실제 치지직 전체 사용.** 실제 앱/채널·장시간 수신·재연결·철회·제공자 토큰 동작 필요.

## G5. 광고→시청 근거→HP→수익

개인 HUD·OBS 활동 보고·채널 시청자 추정은 검증된 광고 시청 실적이 아니다. 시험 수치는 지급 불가다.

- [x] **G5-01 — 시험 캠페인 목록·선택·중지.** 계정/CSRF·송출 승인 결합·철회·재시작 보존·재선택. 패널/관리 브라우저 전환은 선택을 바꾸지 않는다.
- [x] **G5-02 — 별도 OBS 공개 시험 소스.** 독립 공개 배너·공개 URL 복사/수동 복사·OBS Browser Source 설정 안내 구현. `1cda0c0` 공식 OBS32.2.2/CEF#11/#12 각각 합성 PNG13개 통과. 선택 준비는 #65, 접속·보고 대기 복귀는 #66 서비스/Chromium 통과. D7·실제 방송 수신·관중 노출·HP/정산은 별도.
- [ ] **G5-03 — 시청시간 집계.** 비지급 활동·누락 기록·공식 Live API 표본/추정. 0명/미측정 구분, 녹화/미리보기 제외, 소유자만 조회, 공개 조회로 증가하지 않음. 기본 꺼짐; 권한·할당량·보관 조건 확인 후 `CHATVIEW_AUDIENCE_SAMPLING=1`. 실제 광고 노출/주의 미검증.
- [ ] **G5-04 — 서버 HP·수익.** 규격 미정. 비지급/예상/확정 구분, HP0 지급 조건 임의 추가 금지.
- [ ] **G5-05 — 유료 운영·정산.** 단가·예산·배분·지급·중복/초과·부정 실적·개인정보·광고 고지 필요.

## R. 통합 후 배포

- [ ] **R-01 — 첫 표시/연결 신뢰성과 방송 안정성.** D1/D7, #390/#392 최초 표시 실패 및 #367 HUD smoke30.53초(기존30초 한도) 원인 미확정.
- [ ] **R-02 — 반복·자원·설치·캡처·패키지 qualification.** routine/개별 flow와 구분.
- [ ] **R-03 — 원컴/투컴 설치본·안내.** 실험/미지원/검증 범위 구분.

## 남은 결함과 보존할 근거

| ID | 근거와 범위 |
| --- | --- |
| D1 | #408 native-logout 첫 수신 문서 surface_ready0/frames0 및 native-chat-switch·native-output HUD startup 지연; `82f36a6`/#294 반복3 첫 gateway frame 미확인; #366 첫 chat-switch render 실패; #390 native-logout unrelated 첫표시15초 실패(로그아웃·다른 계정 저장 전, 전체41/42·165.70초); #392 chat-switch revoked 최초 빈 표시15초 실패(철회 전, 전체43/44·189.48초). 실패 단계/타이머 인과는 미확정. #400 companion-chat-video 첫 빈 DOM 표시15초 실패를 추가한다. #393 성공만으로 종료하지 않는다. G4/R-01·실계정 남음. |
| D2 | #292 후반 핸들+104>64; #294 초기+284>256, 후반+8. R-02 한도 유지. [자원 조사](resource-growth-investigation.md). |
| D4 | #408 obs-capture-scene 기존 10초 제한 초과(원인 미확정); 동기 OBS UI-task/join·borrowed item `510f8e1`, 좌표#354, core queue `66fd5a6`/#355 보완/통과. 실제 frontend 종료/장면 전환·qualification 남음. Control Center에 Q4/Q5 시험 UI 없음. |
| D6 | #339 첫 WGC픽셀10초 실패/#340 미재현; #353 resize/여백 인과 미확정. #359 burst9는 `4cbcc5e`/#361 같은12단계/강화 픽셀 통과. #365 sequence6 received601×201/surface600×500/pool601×500 Waiting·검정. #366 same-pool 재시작0x8000FFFF. `4b711c2`/#367 새 pool/session12단계·lifecycle 통과이나 첫표시/장시간/물리 수신 남음. |
| D7 | #413 public-ad-browser NavigationCompleted 미도착(started1/completed0/id2, 13.59초); #408 public-ad-browser 대기 AbortError(원인 미확정); #371/#406/#407 WebView2 NavigationCompleted 미도착(started1/completed0); OBS CEF#10 배너 CSS/픽셀 실패(inside4082/painted102). `1cda0c0` CEF#11 PNG13/13와 WebView2#374 routine40/40은 원인·반복 안정성·실방송·유료 노출 종결이 아니다. OBS #18/run38069340507/job114263381493 첫 CreateScene8초 무응답은 배너 생성/픽셀 검사 전에 실패해 완료된 픽셀 검사는0개이며 원인 미확정이다. 기존 CEF 실패와 같은 원인으로 단정하지 않는다. |

D3·D5는 좁은 범위에서 종료했다. #366 빌드 성공37/40·150.33초, #367 빌드 성공39/40·165.17초. **#364/run `37275017123`/job `111650053171`/code `af41b5c28e3ba96b66e8455fa2a12b6b777e35bb`는 보존 기록상 조회 불가·최종 미확정**이며 이유는 추정하지 않는다.

#385 패턴 ID 덮임은 notice/chat hint 분리로 보완. #386 chat-switch mismatch 프로필 생성 실패는 고유 경로#387에서40/40 통과했지만 과거 PID 충돌 가설은 미확정. #388 보호 전환41/41·133.31초, #389 동일 승인42/42·152.28초, #391 철회/영상43/43·162.01초. 최초 표시/실방송 완료로 확대하지 않는다.

## 다음 사용자 흐름

두 PC의 관리 화면에 보이는 현재 승인·열린 소켓·공개 광고 상태의 동일 creator 격리를 HTTP·Chromium에서 먼저 확인한다. 그 다음은 **게임 PC 재승인 중 송출 PC 연결이 끊어졌을 때, 게임 채팅은 계속 유지하고 공개 시험 광고만 투명 대기하도록 전환하며 같은 송출 승인으로 명시적으로 돌아오면 재선택 없이 같은 공개 URL이 회복되는 흐름**을 구현한다. 끊긴 송출 승인이 실제 철회된 경우엔 기존 광고 FK/재선택 규칙을 존중한다. 실계정/물리 투컴 HUD 제외, D1/D7 반복 안정성·시청/HP/지급/배포 qualification은 완료로 주장하지 않는다.

main·장기 브랜치·강제 push·게임 PC OBS·자동 장면 변경·렌더 ACK 수익 환산·비밀값 로그/커밋은 사용하지 않는다.
