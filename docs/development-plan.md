# ChatView 작업 목표 체크리스트

갱신: 2026-10-16 (Asia/Seoul). **이 문서가 유일한 현재 작업 목록이다.** 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md), 영상 계약은 [window-video-output.md](window-video-output.md)를 따른다. 완료된 조사·중복 기록은 Git 이력에 남긴다.

`[x]`는 명시한 좁은 개발 범위다. 로컬 검사, Windows routine, 실제 OBS 전체 흐름, 실계정, 물리 투컴, 배포 qualification은 서로 대체하지 않는다. OBS만 변경한다.

## 현재 흐름: 송출 PC 승인 직접 철회 → 게임 PC 채팅 유지 → 새 송출 승인 후 기존 시험 배너 직접 재선택

시작 OBS HEAD `705f76d6ad37c2f8ad47e7fbea58604317f0e456`에서 AGENTS → PRODUCT → development-workflow → architecture → development-plan 순서로 확인했다. 직전 `a056a596ac0f22c0ee6d0c35446baa36532bb57a`/CHZZK contract **#81/run `38838932680`의 Node 330/330·Chromium20/20 성공**, 공식 OBS 공개 합성 #25의 13/13은 **직전 기능 SHA `f3022b65`**에서 실행됐다. Windows native #418은 scope만 성공, x64 빌드는 skipped. 이들 이전 결과를 새 SHA의 통과로 계산하지 않는다.

### 사용자 흐름과 보존하는 경계

- 동일 creator의 게임 PC companion이 **실제 열린 채팅 WebSocket을 유지하는 상태**에서, 소유자가 관리 브라우저의 기존 송출 PC 연결만 CSRF 보호된 `이 연결 해제`로 명시적으로 철회한다. 서버는 원래 송출 `connection_id`의 광고 선택 FK를 연쇄 삭제하고 오래된 단기 영상/출력 보고는 무효화한다. 게임 PC 역할·소켓·치지직 upstream·다른 creator 승인과 OBS 없는 게임 PC 영상 제어는 변경하지 않는다.
- `/account` 관리 화면은 게임 PC 승인/열린 채팅과 송출 PC 0/0 상태를 구분한다. **게임 PC가 연결되어 있고 송출 승인이 0건일 때만** `data-sender-reapproval="required"` 안내와 `/campaigns#campaign-selection-title` 링크를 표시해, **송출 PC에서 원래 역할 새 승인 → 기존 공개 시험 캠페인 직접 재선택** 순서를 설명한다. 기존 광고 선택이 사라졌을 때 실제 공개 URL 자체가 사라졌다고 표현하지 않는다. 읽기 GET은 승인을/광고를 자동 재개하지 않는다.
- `/campaigns`는 기존 `sourceId`가 있지만 광고 선택이 없는 때 `data-banner-manual-reselect="sender-required" | "ready"`로 상황을 구분한다. 송출 승인이 0이면 새 송출 역할 동의만 안내하고, 새 송출 승인이 생기면 **기존 광고가 아직 미선택**임을 표시한다. 동일 owner의 새 송출 승인·새 WebSocket·신선한 `broadcast:report`만으로는 광고를 표시하지 않는다. **브라우저에서 캠페인을 새로 직접 선택**한 뒤에만 기존 URL의 공개 시험 배너가 나타난다. 이는 서버 공개 TEST 판정이며 OBS 시청자 노출/HP/지급이 아니다.
- 기존 서버 `SessionStore`·`Campaigns`·`DisplayGateway`의 역할/카드 광고 FK, `connection_id`, refresh, WebSocket 및 CSRF 권한을 재사용한다. 새 API/DB schema/플랫폼 의존성·재로그인 자동화·게임 PC OBS·송출 장면 자동 변경을 도입하지 않는다.
- 기존 HTTP/SQLite/ws `platform/tests/streaming-return-during-game-reapproval.test.mts`의 **실제 송출 승인 철회/재승인** 검사를 확장한다. 동작 전후 1 게임/0 송출 → 1 게임/1 송출, 읽기 전용 상태 및 `sender-required`→`ready` 안내, 기존 URL·선택 FK 삭제/재선택, 게임 소켓 유지/메시지 수신, 새 역할만 출력 권한 취득을 확인한다.
- Chromium 390px `management-browser-fixture.mts` + `management-browser-test.py`에 별도 **게임 연결 유지 상태의 송출 승인 직접 철회/재승인/광고 수동 재선택** 동선을 추가한다. 브라우저가 소유자 권한·고정 role·6자리 확인 번호를 실제 확인/동의하고, fixture stdio는 앱 native 요청 시작·승인 수집·송출 보고만 맡는다. 원래 Alice 게임 소켓은 송출 철회 중 계속 합성 제공자 메시지를 수신한다. 공개 소스는 old sender revoke 직후 hidden, 새 sender consent/보고 직후에도 hidden, 기존 URL의 캠페인 수동 선택 후에만 visible. 다른 owner Bob 및 기존 제공자 upstream/쿠키·CSRF/광고 선택의 비결정적 자동 복원은 차단한다.

### 실제 검증·미완료

- 변경 커밋의 **CHZZK contract 전체 Node/Chromium 및 변경 범위·공식 OBS 흐름 결과는 실행 뒤 SHA에 맞춰 기록한다.** 네이티브/OBS/WGC/브라우저 관련 간헐 오류 D1/D2/D4/D6/D7, 실계정 치지직, 물리 두 PC HUD 제외 영상, 실제 관중 광고 노출·HP·지급·설치/반복 자원 qualification은 여전히 미완료다. 합성 report/서버 `visible`을 지급 또는 실방송 입증으로 취급하지 않는다. 타임아웃·보안 판정·제3자 권한은 완화하지 않는다.

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

이번 **게임 PC 채팅 유지 + 송출 승인만 명시적 철회/새 역할 동의 + 새 보고로는 광고 미복구 + 브라우저 수동 캠페인 선택 후 동일 URL 복귀**를 실제 Node·Chromium 및 공식 OBS 테스트 결과로 확정한다. 그 다음에는 **송출 PC 승인 재발급과 캠페인 재선택 사이에 다른 관리 브라우저가 계정을 바꾸거나 광고를 중지한 경우**, 기존 광고 선택을 무단 재실행하지 않고 현재 소유자/요청의 확인을 다시 요구하는 사용자 동선으로 간다. 실계정/물리 수신 HUD 제외, 간헐 WebView2·OBS 결함, 시청/HP/보상/배포 검증은 별도 미완료다.

main·장기 브랜치·강제 push·게임 PC OBS·자동 장면 변경·렌더 ACK 수익 환산·비밀값 로그/커밋은 사용하지 않는다.
