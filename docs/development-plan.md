# ChatView 작업 목표 체크리스트

갱신: 2026-10-19 (Asia/Seoul). **이 문서가 유일한 현재 작업 목록이다.** 목표는 [PRODUCT.md](../PRODUCT.md), 운영은 [development-workflow.md](development-workflow.md), 책임은 [architecture.md](architecture.md), 영상 계약은 [window-video-output.md](window-video-output.md)를 따른다. 완료된 조사·중복 기록은 Git 이력에 남긴다.

`[x]`는 명시한 좁은 개발 범위다. 로컬 검사, Windows routine, 실제 OBS 전체 흐름, 실계정, 물리 투컴, 배포 qualification은 서로 대체하지 않는다. OBS만 변경한다.

## 현재 흐름: 송출 PC의 동시 승인 경쟁 후 재수집 브라우저 복귀 안정화

시작 OBS `62198d98af49a5386736779fcb9c17749765d015`. AGENTS → PRODUCT → development-workflow → architecture → 이 개발 계획 순으로 확인했다. 앞선 **CHZZK contract #96**은 Chromium **29/29**이었지만 동일한 fixture·브라우저 코드에서 **#97/run `38871048093` 및 #98/run `39187277542`의 Chromium**은 둘 다 `collection-recollect` 단계에서 실패했다. #98 Node22/24/Ubuntu/Windows **344/344** 통과·Chromium 실패라는 범위를 구분한다.

- **사용자 흐름:** Bob 채널에서 송출 PC 두 개가 같은 streaming 역할의 명시적 브라우저 동의를 주고 앱에서 동시에 승인 수집을 시도한다. 서버가 단 한 PC의 승인을 원자적으로 부여하고, 진 송출 요청에는 `retry-required`와 원래 확인 번호를 보여준다. 사용자가 기존 송출 승인을 직접 해제해 자리가 비어도 이전 동의로 자동 연결되지 않는다. **원래 요청의 브라우저에서 동일 채널·송출 역할에 명시적으로 다시 동의**한 경우에만, initiating PC가 원래 verifier로 승인 1회를 새로 수집하고, 새 연결에서 출력 보고가 온 후 기존 공개 광고 URL의 캠페인을 사용자가 직접 재선택할 수 있다. Bob 게임 PC와 Alice 기존 승인/광고는 그대로다.
- **교정한 경계:** `BrowserLogin.poll`은 **단조 `performance.now()` 기준 `LOGIN_POLL_MS=1000`**을 엄격히 검사한다. Chromium fixture의 기존 소수점 `setTimeout`은 타이머가 경계 전에 깨어나는 경우 429를 유발할 수 있었다. 이제 **동일 단조 시계가 실제 허용 시각에 도달한 뒤** 단 한 번 승인 수집을 시도한다. 이전 #97/#98의 구체적 실패 assertion은 scrubbed fixture 결과에 없어 이 가설을 유일 원인으로 확정할 수 없으며, 타임아웃 확장/HTTP 반복 재시도/서버 인증 완화는 하지 않았다.
- **브라우저 검증 강화:** 실제 Playwright Chromium에서 두 번째 명시적 동의 버튼 클릭 뒤에는 해당 `/login/<id>/approve` **HTTP 200**과 동일 원래 ID의 `data-login-collection="awaiting-app"` 완료 화면을 모두 확인한다. 단순 버튼 클릭만으로 서버 수집을 시작하지 않는다. 후속 원래 요청 1회 수집→기존 광고 미선택→출력 보고→관리자 직접 선택→시청자 공개 배너 복귀까지 기존 보안/캠페인 검사 전부 유지한다.
- **실제 CI 검증:** 기능 커밋 `2036472f9816f6f135de6bf1c76a4155c6c8dc90`의 [CHZZK contract #99/run `39189448824`](https://github.com/andongmin94/chat-view/actions/runs/39189448824) **completed/success, 전체 5개 작업 성공**. 합성 제공자를 이용한 **실제 Playwright Chromium 관리 브라우저 29/29 성공**: 동시 송출 승인 경쟁→패배 요청의 이전 동의 폐기→기존 승인 직접 철회→원래 요청의 수동 재동의(브라우저 POST 200 및 `awaiting-app`)→다음 허용 시각의 원래 verifier 승인 수집→새 송출 보고→관리자 수동 동일 공개 URL 캠페인 재선택. 합성 증빙 artifact `12159616326`. Ubuntu/Windows × Node22/24 검증 4작업 모두 성공, Ubuntu24 **344/344·fail0·skip0**, locked dependency 취약점 0건. 변경된 브라우저/fixture·문서를 **코드 푸시 1회 + CHZZK Actions 1회**로 검증했고 Windows native/공식 OBS/qualification은 실행하지 않았다. `main`·provider credentials·Origin/CSRF/역할/광고 권한·서버 1000ms poll 한도는 그대로다. **#97/#98의 정확한 fixture 실패 지점은 노출되지 않아 경계 타이밍 가설은 미확정**이며, #99 한 번의 성공으로 반복 안정성·실서비스/실방송 수용을 완료했다고 간주하지 않는다.
- **미완료:** D1 간헐 첫 수신·D2 핸들 증가·D4 OBS 캡처·D6 WGC 분리 영상·D7 OBS/CEF 시작, 실제 치지직 OAuth/권한·장시간 구독, 실제 게임 PC에 OBS 없는 물리 투컴 HUD 제외, 광고 실노출·HP·정산·배포 qualification.


## 직전 완료 흐름: 승인 순간 첫 채팅 표시 검증 복구와 Actions 사용량 절감

시작 원격 `OBS` HEAD `029f834a39f71ef93731d9aba4f12fe246d22833` 및 **Windows development #439/run `38878104924` completed/failure, routine 44/50**를 확인하고 AGENTS → PRODUCT → development-workflow → architecture → 이 문서 순으로 읽었다. `main`은 수정하지 않는다.

- **실제 실패 분리:** 신규 `early-first`를 포함해 `tests/companion-chat-video-test.mts`가 최상위 `try` 블록 안에서 `return`을 사용해 Node 구문 검사에서 `Illegal return statement` 발생, companion 5개 CTest가 **실행 시작 전 실패**했다. 기존 `native-logout` 1개는 **별개 D1 간헐 실패**: `active=1 worker=1 awaiting_login=0 surface_ready=0 frames=0 rows=0 rejected=0 metadata=1 pending_frame=1 awaiting_render=0`, 첫 승인 프레임 뒤 네이티브 첫 표시가 인정되지 않았다. 전체 실패 6개를 첫 텍스트 오류 해결이나 서비스 장애로 뭉뚱그리지 않는다.
- **게임 PC 사용자 흐름:** 브라우저에서 채널·gaming 역할을 명시 승인하기 전에 이미 올라온 합성 첫 글자를, 처음 인증된 서버/WebSocket snapshot→실제 WinHTTP→WebView2 HUD에서 **빈 구독 화면 선행 없이** 텍스트로 표시한다. 기존 C++ renderer DOM·ACK·게임 PC OBS 부재·관리 권한 검사, 로컬 Stop 후 화면 비움 및 같은 승인으로만 복귀 가능 조건은 그대로 둔다. **수정은 테스트 드라이버의 최상위 `return`을 독립 `if/else` 주장으로 교체**하는 최소 변경이며, 오류를 없애기 위해 결과 판정/시간 제한/권한 검사를 완화하지 않는다.
- **Actions 절감:** 기존 Windows workflow는 OBS의 문서·관리 서버 변경에도 Ubuntu `Native change scope`를 먼저 실행한 뒤 skip을 판단했다. `on.push`/`on.pull_request`에서 실제 네이티브·통합 의존 파일만 포함하는 `paths` 필터를 적용해 **문서·비네이티브 플랫폼 변경은 Windows Actions 자체를 예약하지 않는다**. 경로 목록은 기존 scope `case`와 일치시키며 내부 scope 방어를 유지한다. `obs-verify/**` tag 및 수동 `workflow_dispatch` 자격/검증은 그대로 유지. 개발·문서·CI 수정은 구문 확인 뒤 **한 번의 기능 푸시로 묶는다**. Linux unit/CHZZK/API 테스트와 관련 네이티브 첫 표시 CTest는 스킵해 통과했다고 주장하지 않는다.
- **실제 수정 SHA와 검증:** `b371ac66630998fb0e1841f9bee0a27e78165801`에서 [Windows development #440/run `38991323214`](https://github.com/andongmin94/chat-view/actions/runs/38991323214) **completed/success, Windows x64 네이티브 빌드+CTest 50/50·fail0 (총 231.62초)**. 신규 `chat-view-companion-chat-early-first` **2.48초 통과**: 승인 결과가 게임 PC 앱에 도착하기 전 채팅 게시→첫 실제 자체 DOM/ACK→사용자 Stop/원래 승인 보존. 공유 테스트 드라이버를 쓰는 companion 영상 **17.18초**, 철회 **7.39초**, 재승인 **10.70초**, 재표시 **7.08초** 모두 통과. 기존 `native-logout` **23.48초 통과**, `native-chat-surface` **3.44초**, native-display·chat-switch·public-ad-browser 등도 성공. 이전 #439 44/50 실패를 성공으로 고쳐 기록하지 않으며 D1의 간헐 실패는 미해결이다.
- **Actions 사용량 및 검증 경계:** 변경 분기 조각에 대한 로컬 Node 구문 검사, YAML event 파싱·경로 예제 8건 일치 검사와 GitHub 코드 diff를 확인하고 **기능 커밋 1개, Windows Actions 신규 1회**로 검증했다. 수동/배포 태그 자격은 유지했고 플랫폼/문서만 바뀌는 다음 푸시들은 Windows workflow를 만들지 않는다. 이 SHA에서는 별도 CHZZK/공식 OBS/qualification을 실행하지 않았다. 실계정 치지직·장시간 게임 PC HUD·물리 투컴 HUD 제외·D2/D4/D6/D7·실광고/HP/정산/배포 qualification은 미완료다.

## 직전 완료 흐름: D1 첫 채팅 글자 표시를 정확한 소유 문서 nonce에 직접 연결

시작 원격 `OBS` HEAD `f9baeaf4da70e1712f01f3e67f75b9715e7eca16`, Windows development #437/run `38874011246` **49/49 routine 네이티브 성공**(초기 호스트 부트스트랩 한 번 등록 + 첫 WebView2 문서/오버레이/채팅)과 기존 CHZZK #97의 독립 Chromium `collection-recollect` 합성 fixture 실패를 확인했다. AGENTS → PRODUCT → development-workflow → architecture → 이 계획 순서로 검토. 게임 PC에 OBS 없이 쓰는 기존 companion 및 main 원본을 유지하고 OBS만 작업한다.

- **좁은 동작 수정:** 기존 `NativeChatSurface`는 정확한 로컬 문서 URL에서 `NavigationStarting`을 관찰하고 `chat-ready:<nonce>`를 받았어도 별도의 `DOMContentLoaded` 또는 `NavigationCompleted` callback을 기다려야 `ready()`가 true였다. 모듈 코드는 **자체 `#status/#messages` DOM 생성 후에만** nonce 준비 신호를 보낸다. 이제 활성 네이티브 WebView2·일치하는 문서 소유자/탐색 ID·**현재 실제 `get_Source` URL**·문서의 nonce `chat-ready`를 모두 확인한 즉시 첫 메시지 전송을 허용한다. DOMContentLoaded 구독과 `ICoreWebView2_2` 의존을 제거하고 별도의 loaded 플래그를 없애 표시 준비 경로를 단순화한다.
- **실패 보호 유지:** `NavigationCompleted` callback은 성공 대기를 위한 경로가 아니지만 **실패 확인 시 즉시 기존 문서/프레임을 무효화**한다. 예전 URL/다른 페이지/취소된 탐색/프로세스 실패/만료된 nonce/불일치 source는 여전히 게시 불가. 새로운 네트워크·타이머·자동 승인·재로그인·대체 브라우저·OBS scene/캡처·테스트 한도 완화는 하지 않는다. renderer ACK는 여전히 한 개 이상의 실제 DOM 텍스트 행을 확인해야 첫 글자 성공으로 표시한다.
- **실제 검증:** `tests/native-chat-surface-test.cpp`에서 실제 WebView2의 **첫 `NavigationCompleted` 수신 콜백을 의도적으로 해제**한 뒤 (더 이상 DOMContentLoaded 의존도 없음), 정확한 source/nonce handshake로 첫 Unicode DOM 글자·렌더 ACK를 검증한다. 기존 setup↔채팅 문서 전환·취소된 외부 탐색·invalid frame·reopen/closed host 경계도 유지한다. **실제 CI 검증:** 기능 커밋 `8f2b7c00ecde45da8a8335d94fa83ffa2345af7b`의 **Windows development #438/run `38875923202` completed/success**, 실제 Windows x64 컴파일 및 routine **CTest 49/49·실패0(206.08초)**. `chat-view-native-chat-surface`는 첫 `NavigationCompleted` 콜백 없이 **실제 WebView2 source/nonce 준비→첫 Unicode DOM 글자·ACK** 검증 **2.68초 통과**, 다른 URL/문서 전환/잘못된 프레임도 차단했다. `native-display-client` **40.12초**, `native-logout` **22.09초**, `native-chat-switch` **18.00초**, `companion-chat-video` **5.30초**, `companion-chat-video-redraw` **7.00초**, `public-ad-browser` **12.96초** 등 나머지도 통과. CI는 변경된 네이티브 범위에 한정되어 CHZZK contract/공식 OBS 광고 flow는 이 SHA에서 새로 실행되지 않았다.
- **남은 범위:** D1의 이중 브라우저 탐색 완료 이벤트 의존은 제거됐으나 **실제 첫 실행의 장시간/반복 신뢰성 및 실계정 치지직 첫 메시지 표시**는 입증하지 못했다. D1 전체 및 D2/D4/D6/D7, 물리 투컴 HUD 제외, 시청자 광고 노출·HP/정산·배포 qualification은 미해결이다. 현 네이티브 채팅의 source/nonce/탐색 실패 차단·테스트 한도는 그대로다.

## 직전 완료 흐름: D1 WebView2 호스트 부트스트랩과 첫 텍스트 준비 경계 단순화

시작 원격 OBS HEAD `7bae2ae34061a4aeb56d7f76a2da3052f36dbef5`, Windows #436/run `38871299139`의 실제 네이티브 routine **49/49 성공**(첫 DOMContentLoaded+nonce 경로 포함), 이전 CHZZK #97의 synthetic Chromium `collection-recollect` 실패를 확인했다. AGENTS → PRODUCT → development-workflow → architecture → 이 개발 계획 순으로 읽고 OBS만 수정한다.

- **사용자 경험:** 게임 PC의 기존 companion은 OBS 없이 시작한다. WebView2가 초기화돼 채팅 페이지를 열 준비가 된 시점과 채팅 WebSocket의 첫 메시지가 로컬 HUD에 실제 표시된 시점은 독립적이다. 준비 신호 전 채팅/계정 권한을 만들거나 임의 문서를 승인하지 않는다. 초기 호스트 준비에서 불필요한 순차 브라우저 콜백 하나를 제거해 첫 문서 진입 경로를 단순화한다.
- **기존 코드의 개선:** `WebViewHost`는 동일한 문서 시작 단계에 `kOverlayBootstrapScript` 완료 콜백 → `page_health_bootstrap_script()` 완료 콜백을 **직렬로** 기다렸다. 기존 고정 호스트 스크립트의 순서와 실행 독립성(`try/finally`)을 유지하면서 **WebView2 `AddScriptToExecuteOnDocumentCreated`를 한 번만 등록**하고, 그 결과 성공 후에만 composited HUD를 ready로 보고한다. 실수/할당 실패/host close에는 기존 안전 종료. 네트워크·로그인·capture exclusion·타 채널/OBS·새 환경 변수/브라우저/지연 제한·자동 복구 경로는 추가하지 않는다. `NativeChatSurface`는 이전과 똑같이 자신이 소유한 문서의 탐색 ID·DOMContentLoaded+nonce를 검증해야 첫 글자 메시지를 게시한다.
- **실제 회귀 범위:** 기존 `tests/native-chat-surface-test.cpp`의 WebView2 실제 첫 문서 준비·Unicode 첫 메시지/ACK·다른 URL 금지·재시작 검증에 **첫 로드 시 폐쇄 ShadowRoot 오버레이 설치 확인**을 추가했다. 기존 Windows native-display/logout/chat-switch/output/companion/OBS/영상 CTest 및 새 HTML 생성·외부 페이지 상태 감지와 격리를 회귀로 유지한다. 이 단계의 성공은 host 시작의 의존성을 줄였다는 뜻이지 D1의 모든 간헐 첫 화면 지연, 실계정 CHZZK/물리 두 PC/시청자 노출의 증거가 아니다. **검증 완료:** 기능 SHA `f9ac29c9c9d0afb8fe03ff9d088cd316d818cf34`의 **Windows development #437/run `38874011246` completed/success**, Windows x64 네이티브 컴파일 및 **routine CTest 49/49·fail0·skip0(234.68초)**. 기존 네이티브 `chat-view-native-chat-surface` 실제 WebView2 첫 소유 문서/폐쇄 ShadowRoot·Unicode 첫 메시지/ACK·탐색 차단 **6.38초 성공**; `native-display-client` **49.45초**, `native-logout` **33.05초**, `native-chat-switch` **23.78초**, `companion-chat-video` **5.46초**, 공개 광고 Browser Source WebView2 **13.28초** 등 기존 네이티브 routine 전부 통과. 첫 메시지 제한·window/scene/capture 보안 경계를 완화하지 않았고 `main`과 게임 PC OBS 비사용 제약은 그대로다.
- **미완료 구분:** 단일 Windows runner에서의 한 번의 합성 네이티브 routine 성공이며, 첫 실행의 반복 신뢰성/호스트 콜백 간헐 중단(D1 전체), 치지직 실계정, 물리 투컴/캡처 영상, 공식 OBS 배너 별도 합성·관중 노출·배포 qualification은 검증하지 않았다. 이전 `CHZZK #97`의 Chromium `collection-recollect` 실패는 별도 미확정으로 유지한다. D1/D2/D4/D6/D7 중 미해결 범위를 닫지 않는다.

## 직전 완료 흐름: D1 첫 네이티브 채팅 문서 준비와 첫 텍스트 표시

시작 원격 OBS HEAD `fd157b1bbde60df1720367af0f98345c785ac353`, 직전 CHZZK contract #96 Node **340/340·Chromium29/29** 및 Windows #433 scope 성공과 공식 OBS 공개 배너 #34 합성 픽셀 **13/13**을 확인했다. AGENTS → PRODUCT → development-workflow → architecture → 이 문서 순서로 읽었다. 이번 작업은 게임 PC에 OBS를 요구하지 않는 기존 네이티브 HUD 수신 경계에 한정된다.

- **확인한 실제 실패:** Windows development #408/run `38761837035`의 native logout 첫 승인 화면 로그는 `active=1, worker=1, metadata=1, pending_frame=1, surface_ready=0, frames=0`이다. 서버 승인/채팅 연결이 있었고 프레임도 대기 중이었으나 WebView2 소유 문서가 준비 완료로 인정되지 않았다. 같은 run의 native chat-switch HUD startup 실패 및 기존 D1 첫 표시 실패들은 원인 확정 전으로 유지한다. 이 증상은 `NativeChatSurface::ready()`가 마지막 `NavigationCompleted` 성공과 nonce handshake 둘 다를 요구하는 코드와 부합하지만, 이벤트 누락이 모든 D1 실패의 유일한 원인이라고 단정하지 않는다.
- **좁은 기능 수정:** 같은 UI 스레드의 **기존 WebView2**에서 문서 탐색 ID가 일치하는 **main-document `DOMContentLoaded`**도 로드 완료의 근거로 사용한다. 최종 `NavigationCompleted`가 오기 전이라도 정확한 현재 로컬 문서 소유권·`NavigationStarting` ID·문서가 보낸 **nonce-bound `chat-ready`**가 모두 충족될 때만 최초 메시지를 게시한다. 실제 NavigationCompleted 실패, 예전 setup/다른 URL, 프로세스 실패는 이전대로 즉시 무효화한다. 새 브라우저/타이머/재로그인·권한·자동 복구·OBS/영상 변경이나 완화된 테스트 제한은 없다.
- **검증:** `tests/native-chat-surface-test.cpp`에서 **실제 WebView2** 현재 문서의 `NavigationCompleted` **표면 수신 콜백만 해제**한 첫 로드 뒤 `DOMContentLoaded+nonce`로 준비 완료, PostWebMessage, Unicode 첫 DOM 텍스트와 렌더 ACK, 잘못된 문서/탐색 차단을 검증한다. 기존 companion-chat-video 및 native display/logout/switch/output 경로는 Windows native routine의 동일 검사 그대로 둔다. 변경 컴파일·테스트의 실제 SHA/성공·실패는 확인 뒤 기록한다. 통과하더라도 실계정 치지직·실기기 첫 표시 장시간 신뢰성·D1 전체 해결은 별도다.

- **Windows 실제 첫 실행:** 기능 SHA `5fe53659e0ef8492b7a6cf565ff3cf8bd37afee5`의 Windows development **#434/run `38870051231` completed/failure**, 네이티브 코드 **컴파일 성공, routine CTest 48/49 통과**. 새 **`chat-view-native-chat-surface`는 실제 WebView2에서 완료 이벤트를 일부러 수신하지 않는 첫 로드와 Unicode 메시지/ACK 검사 통과(2.62초)**. 기존 native-display(40.32초), native-logout(32.79초), native-chat-switch(28.24초), native-output(16.47초), companion-chat-video 및 redraw 관련 검사 모두 성공. 단 하나 `chat-view-public-ad-browser`가 **선택 후 Stop → 구식 view 확인값으로 다시 Select하여 HTTP 409**를 받는 시험 도우미 위반으로 실패했다. `platform/tools/public-ad-browser-test.mts`의 각 직접 선택 앞에서 실제 `/campaigns` GET으로 승인/캠페인 상태를 다시 검토하도록 수정했으며 사용자의 수동 선택 조건·서버 검사를 완화하지 않았다.
- **CI 범위 오류 보정:** 시험 도우미 수정 `73b64f4bd25b6a7cd6cdd6bb0344dc2256a83956`의 Windows #435/run `38871048205`는 **scope만 성공, native x64 skipped**라 위 WebView2 재선택 오류 해결 증거가 아니다. 기존 Windows workflow 변경 감지에 `platform/tools/public-ad-browser-test.mts`가 빠져 있었다. 정확히 기존 네이티브 테스트 도우미 패턴 `native-*.mts`와 `session-display-test.mts`, `public-ad-browser-test.mts`를 대상으로 포함해 다음 네이티브 커밋에서 **Windows routine을 실제 다시 실행**한다. CI의 시간/한도·표시 보안·채팅 승인은 그대로다.
- **최종 네이티브 재검증:** 검증용 CTest 수정 `73b64f4bd25b6a7cd6cdd6bb0344dc2256a83956`과 Windows CI 범위 보정 `b24480afa81988bb7cf111d929195a471810c2b6`에 대해 **Windows development #436/run `38871299139` completed/success, 실제 Windows x64 네이티브 빌드와 routine CTest 49/49 통과(총 247.68초)**. `chat-view-native-chat-surface` 실제 WebView2 첫 문서/Unicode/nonce 렌더 검증 **3.07초 통과**; 앞선 오류인 `chat-view-public-ad-browser` **13.01초 통과**, native logout **29.44초**, chat switch **19.10초**, companion chat-video **5.12초** 및 redraw **7.63초** 등 모든 routine 검사 성공. 동일 SHA에서 배포 qualification, 실제 치지직/게임 PC·캡처카드 및 반복 자원 검증을 수행한 것은 아니다.
- **분리된 플랫폼 CI 실패:** 시험 도우미 보정 커밋 `73b64f4bd25b6a7cd6cdd6bb0344dc2256a83956`의 **CHZZK contract #97/run `38871048093` completed/failure**. Ubuntu/Windows Node22·24 **4개 job 성공**, 실제 Chromium 관리 브라우저만 기존 `collection-recollect`(동시 송출 승인 경쟁의 수집 복귀) synthetic fixture invariant 단계에서 실패했다. 해당 브라우저 실패는 D1 네이티브 빌드/DOM 성공으로 덮어쓰지 않는다. 아직 원인 미확정이며 조건/한도 완화·동일 상태 무의미한 재실행을 하지 않았다.
- **남은 한계:** 정확한 소유 문서 `DOMContentLoaded`+nonce를 통한 최초 표시 경계는 #436에서 검증됐으나 **모든 D1 간헐 장애의 원인을 해소한 것은 아니다**. 과거 HUD host startup 지연·실서비스 CHZZK·반복 초기 표시 실패를 개별적으로 검증해야 한다. D2/D4/D6/D7, 물리 투컴 HUD 제외, 광고 실제 시청·유료/HP·정산·배포 qualification은 미완료다.

## 직전 완료 흐름: 새 송출 승인·광고 수동 재선택 이후 오래된 관리 폼 차단

시작 원격 OBS HEAD `702b2bfcb3b5e1e617e56624bb894d9981658bbe`; CHZZK contract #93 Node **339/339**, 실제 Chromium **28/28**, Windows #430 scope 성공을 확인했다. 공식 OBS 공개 배너 #33은 이전 결함 D7의 **CreateScene 8초 무응답, 픽셀 검사 0/13**이며 통과로 간주하지 않는다. AGENTS → PRODUCT → development-workflow → architecture → 이 문서 순서로 읽고 작업 브랜치 OBS 외에는 변경하지 않는다.

- **사용자 동선:** Alice 게임 PC 채팅은 살아 있고, 관리 브라우저 A에서 기존 송출 승인만 명시적으로 철회한다. 송출 PC는 같은 채널에서 새 역할을 직접 승인하고 새 출력 보고를 보낸다. 기존 공개 URL은 그대로이나 시험 광고 FK는 **미선택**으로 유지된다. A가 새 승인·현재 캠페인을 읽고 수동 재선택한 이후 **다른 독립 관리 브라우저 B의 오래된 송출 승인용 Stop/Select**, 관리자 계정 전환 및 이전 쿠키는 새 광고 선택을 중지·교체할 수 없다. 새 선택을 직접 변경하려면 B 역시 최신 상태를 다시 확인하고 버튼을 명시적으로 눌러야 한다.
- **구현:** 기존 단일 서비스의 송출 승인 ID+브라우저 화면 확인값, CSRF·Origin·계정 검사를 그대로 사용한다. 광고가 이미 미선택일 때 이전 탭의 Stop은 안전한 **무변경 응답**으로 처리하며, 다른 관리자 브라우저의 새 화면 확인 자격이나 활동 데이터를 삭제하지 않는다. 선택된 광고의 Stop에는 여전히 최신 송출 승인/브라우저 화면 확인을 요구한다. 서버에 별도 역할 이전·장면 제어·DB/권한 API는 추가하지 않는다.
- **검증:** `browser-recovery.test.mts`의 실제 HTTP/SQLite/WebSocket·합성 CHZZK에서 서로 다른 관리 쿠키 두 개, 게임 채팅·타 계정 유지, 기존 송출 승인 철회→새 approval ID/보고→수동 재선택, 오래된 Stop의 DB 무변경, 이전 B의 Stop·Select 409, 최신 B의 직접 Stop→A의 오래된 Select409→최신 A 재선택, B의 관리 계정 Bob 전환/원래 URL 격리까지 하나의 동선으로 검사한다. `management-browser-test.py`의 Chromium 390px에서도 별도 브라우저에서 확보한 오래된 Stop을 새 송출 역할 재승인·재선택 이후 클릭해 409를 확인하고 Bob 관리 전환에도 기존 공개 배너·게임 채팅이 유지되는지 검증한다. **실제 CI 결과:**
  - 기능 커밋 `54bd91e8485a65d03205bec70a3d0211966ffb1d`: CHZZK contract **#94/run `38865817625` 전체 실패**(Ubuntu24 Node **340/340·fail0·skip0**, 실제 Chromium 새 테스트의 문자열 따옴표 누락으로 Python 구문 검사에서 중단). Windows development **#431/run `38865818016` scope 성공, native x64 skipped**.
  - 같은 기능 SHA의 공식 OBS public ad flow **#34/run `38865817785` completed/success**, 공식 OBS 32.2.2/CEF 합성 픽셀 **13/13 통과**, 증빙 artifact `12031072446`. 이전 #33의 CreateScene 실패를 수정했다고 주장하지 않는다. 네이티브/물리 영상·시청자 실제 노출은 별개다.
  - Chromium 문법 보정 `6f7435cbdc177379d6d363de9c0273c2194cf5fc`의 CHZZK contract **#95/run `38866012789` 전체 실패**: Node 4개 플랫폼 작업은 통과했으나 합성 fixture에서 관리 브라우저 만료를 앞서 일괄 진행한 후 그 전에 열어 둔 다른 브라우저로 Stop을 제출하여 **401을 예상외 409로 판단**했다. 증빙의 실제 응답 401을 확인했고, 두 번째 독립 관리 브라우저는 새 송출 승인/출력 보고 뒤(가상 만료 시계 이후) 로그인하고 수동 캠페인 선택 전 상태를 확인하도록 순서를 수정했다. Windows **#432/run `38866012854` scope 성공, native x64 skipped**.
  - 최종 Chromium 수정 `768b0e530b6d9e9756ad58579d18f49e54dc0ab5`: **CHZZK contract #96/run `38866459249` completed/success, 전체 5개 작업 성공**. Ubuntu/Windows × Node22·24 실제 HTTP/SQLite/WebSocket 테스트·엄격 TypeScript·locked dependency 통과, Ubuntu24 **340/340·fail0·skip0**, 취약점 0건. 실제 Chromium 390px 관리 브라우저 **29/29 통과**(독립된 이전 관리자 Stop409→현재 재확인→Bob 전환·원본 송출/선택 유지), 합성 증빙 artifact `12030939173`. Windows development **#433/run `38866459229` scope 성공, native x64 skipped**. 공식 OBS 합성은 최종 Python 시험 보정 SHA에서 새로 실행하지 않았다.
  - 합성 CHZZK/단일 서비스/Chromium 및 별도 공식 OBS fixture 검증이다. **D1/D2/D4/D6/D7**, 실계정 치지직/물리 투컴 HUD 제외/실제 광고 시청·유료/HP·정산·배포 qualification은 여전히 미완료다. 서버의 rate limit·CSRF·Origin/테스트 제한·게임 PC OBS 제약을 완화하지 않았다.

## 직전 완료 흐름: 관리 세션 만료·계정 전환 중 송출 승인 실제 철회와 수동 광고 재선택

시작 원격 OBS HEAD `eb60f17d234cc66d6fbd11436f903c23ab02954d`와 직전 CHZZK contract **#91(337/337·Chromium26/26)**, Windows #428 scope 성공 및 기존 OBS public ad #32의 **CreateScene 8초 무응답/픽셀 0건**을 확인했다. AGENTS → PRODUCT → development-workflow → architecture → 이 문서 순으로 확인했으며 `main`은 그대로 둔다.

- **동작:** 같은 Alice 채널의 게임 PC 채팅을 유지한 채 송출 PC가 **실제 승인 철회**(`/display/signout` 또는 소유자 연결 해제)를 수행하면 송출 WebSocket/출력 보고·선택 캠페인의 정확한 FK가 종료된다. 브라우저 관리 쿠키 만료나 Bob 관리 계정 전환은 별개다. 이전 Alice 쿠키/CSRF·광고 폼 또는 Bob 관리 세션으로 Alice 송출 승인을 대신 복귀시키거나 광고를 선택할 수 없다. 공개 URL은 보존되고 송출 역할을 Alice로 **다시 직접 승인**한 뒤 새 출력 보고를 받아도 광고는 **계속 투명·미선택**이며, Alice 관리 브라우저에서 현 채널·승인을 확인해 시험 광고를 **직접 재선택**한 경우에만 같은 URL의 공개 시험 배너가 복귀한다. 기존 게임 채팅·타 creator·별도 영상·OBS 장면은 유지한다.
- **최소 UI 개선:** 익명 관리 로그인/만료 401 안내를 **단순 송출 소켓 단절(기존 승인+선택 유지, 복귀 후 새 보고)**과 **송출 역할 승인 자체 철회(선택 해제, 새 역할 동의+수동 선택)**로 분리 설명한다. 브라우저 로그인·계정 전환이 승인 자체를 바꿨다고 단정하지 않는다. 인증·Origin·CSRF/서버 제한·송출 역할 검사·캠페인 FK·공개 소스 자격과 기존 화면 구조는 그대로다.
- **실제 회귀:** `browser-recovery.test.mts`에 관리 만료+실제 sender signout, Bob으로 브라우저 전환+Alice sender signout 2개 end-to-end를 추가한다. 실제 HTTP/SQLite/WebSocket·합성 CHZZK로 만료 쿠키 Select/Stop401, 익명/타계정 CSRF403, Bob의 Alice 소스 비노출, 오래된 sender refresh/report 거부, 새 송출 승인 ID, 광고 FK 삭제·새 보고 후 미선택·기존 공개 URL 보존·명시적 재선택, 게임 채팅/타 creator/OAuth grant/upstream 불변을 확인한다. `management-browser-test.py`에서는 390px 실제 브라우저에서 이미 존재하는 sender-reapproval 동선에 만료·Bob 전환·이전 Alice 폼 차단·Alice 재동의·직접 재선택을 연결한다. **확인한 CI:**
  - 기능 `fae639fa1f8d60b454a7c54082127e8e548e5f7f`의 CHZZK contract **#92/run `38862819021` 전체 실패**: Ubuntu24 서비스 Node **338/339·fail1·skip0**. 신규 만료·철회/계정 전환·수동 재선택 테스트 2건은 통과했지만 기존 브라우저 복귀 테스트가 바뀐 안내 문구를 그대로 기대해 실패했다. Chromium도 기존 fixture에서 **Bob에게 이미 독립 송출 승인/공개 소스가 있는데 미승인으로 기대한 단정** 때문에 실패했다. 시험을 완화하거나 제품 권한을 바꾸지 않고 각각 현재 안내의 두 경계 검사, Bob 자신의 송출 역할·Alice URL 격리 확인으로 수정했다. Windows development **#429/run `38862819000` scope 성공, native x64 skipped**.
  - 공식 OBS public ad flow **#33/run `38862819027` 실패**: 첫 `CreateScene` RPC가 기존 **8초 한도 초과**, 배너 합성 픽셀 확인을 시작하기 **전 실패(0/13)**. **D7**의 해당 OBS stage 간헐 오류는 원인 미확정이며 서비스/브라우저 변경의 직접적 회귀로 단정하지 않는다. 제한 증가·재실행 성공 주장 없음. 뒤따른 테스트 보정 SHA에서 공식 OBS를 새로 실행하지 않았다.
  - 최종 보정 `2b15d7f0a041cf312d839281153293cff3c3ba55`의 **CHZZK contract #93/run `38863199780` completed/success, 전체 5개 job 성공**. Ubuntu/Windows × Node22·24 실제 HTTP/SQLite/WebSocket 회귀 **339/339·fail0·skip0**(Ubuntu24), 엄격 TypeScript·locked dependency/취약점 검사 통과(**0건**). 실제 390px Chromium 관리 브라우저 **28/28 통과**, 관리 만료·Bob 채널 전환·철회된 sender 새 승인·출력 보고 후에도 미선택 유지·Alice의 수동 같은 URL 재선택을 클릭으로 검증. synthetic 증빙 artifact `12030387313`. Windows development **#430/run `38863199742` scope 성공, native x64 skipped**.
  - 위 검증은 합성 치지직/단일 서비스/개발 Chromium·실제 HTTP/SQLite/ws 환경이다. 실계정 채팅·물리 투컴 HUD 제외·시청자 광고 노출·유료/HP·정산·배포 qualification은 아니다. **D1/D2/D4/D6/D7 유지.**

## 직전 완료 흐름: 송출 단절·기존 승인 복귀 중 복수 관리 탭 동선

시작 원격 OBS HEAD `9cfe9b7434c4a864c8b24e1eec0bcfd9ee9c5e1c` 및 CHZZK contract #88 (Node 334/334, Chromium 24/24), 공식 OBS #30 (합성 13/13)을 확인했다. AGENTS → PRODUCT → development-workflow → architecture → 이 계획 순서로 읽었고 `main`은 변경하지 않는다.

- **사용자 흐름:** 게임 PC의 기존 자체 채팅/WebSocket과 별도 creator를 유지한 상태에서 송출 PC WebSocket만 끊긴다. 두 관리 탭은 같은 로그인 쿠키로 현재 캠페인 상태를 확인하지만, 이전 탭이 기억한 선택·중지 버튼은 최신 화면을 덮지 못한다. 송출 PC는 **기존 송출 승인으로 명시적으로 복귀**하고 새 출력 보고 전까지 공개 배너가 투명하다. 보고가 들어오면 동일 connection ID/FK/공개 URL의 배너가 다시 준비된다. 관리 GET, 브라우저 뒤로가기, 잘못된 POST는 광고 재선택·다른 계정 전환·게임 영상·OBS 장면을 실행하지 않는다.
- **좁은 수정:** 유효한 최신 브라우저 캠페인 화면이 별도로 있는 경우, 오래된 탭의 409는 *그 최신 확인값을 취소하지 않는다*. 반면 송출 역할 승인 자체가 철회·교체됐다면 확인값을 무효화한다. 기존 소유자/송출 승인 ID·CSRF·Origin·rate limit·유효한 명시적 Select/Stop 조건은 그대로 유지한다. 새 DB/마이그레이션/클라이언트 API/자동 송출 조작은 없다.
- **통합 검증:** `stale-campaign-management.test.mts`의 실제 HTTP/SQLite/WebSocket은 동일 쿠키 다중 탭의 오래된 Select·Stop 409, 송출 소켓 단절·기존 승인 갱신·새 보고, 게임 채팅 수신·타 creator 격리, 광고 FK·공개 URL 불변, 이전 보고 무효, 이후 최신 탭의 명시적 Stop/Select를 확인한다. Chromium 390px 브라우저에서는 게임 재승인 중 실제 두 탭 폼 클릭으로 세 단계의 409·상태 확인·동일 공개 URL 복귀를 확인한다. **실제 CI 완료:**
  - 기능 커밋 `c552a511ebcc839f6e1b0a6c4130e83df6401025`의 **CHZZK contract #89/run `38854988629` completed/success, 전체 5개 job 성공**. Ubuntu/Windows × Node22·24 통합 회귀·엄격 TypeScript·locked dependency 검사 성공; Ubuntu24 **335/335, fail0, skip0**, 신규 같은 브라우저의 복수 탭 송출 단절·복귀 회귀 통과, 취약점 **0건**. 합성 제공자와 실제 Chromium 관리 브라우저 **24/24 성공**, 390px 다중 탭 오래된 Select/Stop409와 송출 복귀·새 보고·공개 URL 확인, artifact `12026942408`.
  - 동일 기능 SHA의 **공식 OBS public ad flow #31/run `38854988550` completed/success, OBS·CEF 공개 시험 배너 합성 픽셀 13/13 통과**, artifact `12026597828`. **Windows development #426/run `38854988495` scope 성공, native x64 skipped**(서버/브라우저 변경만). 전체 native 반복 시험·하드웨어 qualification은 실행하지 않았다.
  - 단일 인스턴스 합성 CHZZK·HTTP/SQLite/WebSocket/Chromium/OBS 시험 장면이며 **실계정/시청자 실제 노출·물리 투컴 HUD 제외의 증거가 아니다**. 기존 D1/D2/D4/D6/D7, 유료 광고·HP/정산 및 배포 검증은 미완료.

## 이전 완료 흐름: 브라우저 뒤로가기·오래된 탭과 실제 송출 승인 철회 구분

- 이미 완료된 **게임 PC 재승인 중 송출 WebSocket 단절→기존 승인으로 직접 복귀→새 유효 보고** 경로는 `streaming-return-during-game-reapproval.test.mts` 및 Chromium 동선에 있다. 동일 송출 connection ID/광고 FK/공개 URL 보존, 게임 채팅 유지, 옛 보고 무효화, 실제 승인 철회 시 수동 재선택까지 회귀로 확인했다. 본 흐름은 이를 재구현하지 않는다.
- **이번 변경:** 관리 브라우저의 `/campaigns` 조회마다 해당 브라우저 전용 일회성 화면 확인값을 발급하고 선택·중지 폼에 싣는다. 기존 세션 쿠키·CSRF·Origin 검사를 그대로 통과해야 하며, 같은 쿠키의 다른 탭이 최신 화면을 읽었다면 **옛 탭/뒤로가기 폼은 409**로 차단된다. 캠페인 선택·중지 성공은 이전처럼 같은 owner의 다른 관리 브라우저 확인 자격도 무효화한다. 보고·송출 승인·채팅·공개 URL을 조회/409로 바꾸지 않는다. DB/마이그레이션/새 외부 API나 자동 광고 실행은 추가하지 않는다.
- **사용자 구분 안내:** `stale-page`는 오래된 관리 화면/다른 관리자 동작으로 다시 확인이 필요함을 뜻하며 송출 승인 철회라고 단정하지 않는다. `sender-missing`은 현재 송출 승인 없음, `sender-changed`는 확인했던 승인과 다른 새 승인임을 설명하고 수동 역할 승인·광고 재선택을 안내한다. 네트워크 소켓만 끊어져 같은 송출 승인이 유효하면 이전 광고 선택은 유지하고 새 출력 보고 전까지만 투명 대기한다.
- **관련 변경 검사:** `stale-campaign-management.test.mts`의 실제 HTTP/SQLite/WebSocket 동일 쿠키 새 탭·뒤로가기 유사 순서·CSRF/Origin 차단·송출 철회/교체, 기존 재승인/송출 복귀 회귀; `management-browser-test.py`의 Chromium 390px 실제 뒤로가기+2탭 stale Stop409·실제 송출 철회 old Select409·재승인 후 수동 재선택; `ad-pages`와 `campaign-recovery-page` 렌더링. **실제 CI 완료:**
  - 기능 `afbdad4df73f43826a0a9fc81ccf0791c8e80cea`에서 CHZZK contract **#87/run `38853524874` 실패**: Ubuntu24 Node **331/334**(3개 실패는 기존 `audience-flow` 전용 도우미가 새 `view` 확인값 없이 POST한 문제), 신규 stale/송출 복귀 회귀는 통과. 동일 SHA의 실제 Chromium 390px **24/24 성공**, 증빙 artifact `12026137296`. 공식 OBS public ad flow **#30/run `38853524861` 성공**, OBS 32.2.2/CEF **13/13 합성 픽셀 통과**, 증빙 artifact `12026306124`. Windows development **#424/run `38853524855` scope 성공, native x64 skipped**.
  - 시험 도우미 보정 `ad3e845a37c5c96823ecc742332d4892ede26718`의 **CHZZK contract #88/run `38853722605` completed/success, 전체 5개 job 통과**. Ubuntu·Windows × Node22·24 플랫폼 검사 모두 성공; Ubuntu24 **334/334, fail0, skip0**·locked dependency 취약점 **0건**·strict TypeScript 통과. 실제 Chromium **24/24 통과**, 증빙 artifact `12026580663`. Windows development **#425/run `38853722530` scope 성공, native x64 skipped**. 해당 시험 도우미 후속 커밋에는 공식 OBS 합성을 재실행하지 않았으며 #30은 바로 앞 기능 SHA의 확인 결과다.
  - 전부 단일 서비스·합성 CHZZK·HTTP/SQLite/WebSocket/Chromium/공식 OBS 개발 fixture의 근거다. **D1/D2/D4/D6/D7**, 실계정 치지직, 물리 투컴·개인 HUD 송출 제외, 실제 광고 시청, 유료·HP·정산·반복/설치/배포 qualification은 미완료. 새 구조·부당한 광고/OBS 동작·게임 PC OBS·검증 한도 완화는 없다.

## 이전 완료 흐름: 복수 관리 브라우저의 오래된 캠페인 선택/중지 요청 차단

시작 OBS HEAD `386c9db6e8b3d1f3db671011b306b2c8f74dbc0b`에서 AGENTS → PRODUCT → development-workflow → architecture → 이 문서를 순서대로 읽었다. 직전 `827fa97b1ad617524fb66526561f490a258d6a95`의 CHZZK contract #83/run `38845683296`은 Node **330/330·Chromium21/21 성공**, 공식 OBS public ad flow #26/run `38845382854`의 13/13 PNG 합성은 기능 커밋 `7c395b02`의 결과였다. 이번 SHA 검증으로 대체하지 않는다.

### 현재 사용자 동선 및 보안 경계

- 채팅 중인 게임 PC를 유지하고 새 송출 역할을 명시적으로 승인한 뒤 광고를 직접 재선택하는 도중, 서로 다른 **관리 브라우저 A·B**가 같은 채널의 광고 상태를 보게 된다. B가 광고를 중지하거나 재선택한 뒤 A의 **오래된 폼**을 그대로 제출해 B의 결정을 무단으로 덮지 못하도록 한다. 다른 브라우저가 자기 **관리 계정을 전환**한 경우 이전 쿠키/CSRF/채널로 캠페인을 다시 실행하지 않는다.
- 기존 `Browser` 레코드에 캠페인 화면을 읽었을 당시의 **소유자 채널·송출 승인 connection ID**만 저장한다. 실제 `/campaigns`를 확인한 브라우저에서만 POST 선택을 허용하며, `Campaigns.select/stop`이 성공하면 같은 owner의 모든 관리 브라우저가 갖고 있던 캠페인 화면의 실행 자격을 즉시 무효화한다. 따라서 다른 브라우저의 Stop 뒤 stale Select와 새 Select 뒤 stale Stop은 **409 안내 화면**으로 끝난다. POST를 자동 replay하거나 서버 권한·광고 선택을 변경하지 않는다. 이 상태는 별도 DB 테이블/마이그레이션/단기 토큰/새 외부 API가 아니고 현재 single-instance 관리자 세션의 보수적 확인 장치다.
- 송출 PC 승인 자체가 해제/교체됐으면 기존 화면이 기억한 **connection ID와 현재 송출 승인 ID가 다르므로** 선택을 거부한다. 최신 채널·송출 승인·광고 선택을 관리 브라우저에서 새로 읽고 사용자가 직접 버튼을 눌러야 기존 공개 URL의 시험 배너를 다시 선택한다. 브라우저에 저장된 원래 계정과 CSRF는 기존 쿠키/Origin·SameSite 검사에 맡기며 타 브라우저 계정 전환이 다른 브라우저의 로그인까지 철회하지 않는다.
- `platform/tests/stale-campaign-management.test.mts`의 실제 HTTP/SQLite/owner cookie 검사에서 두 브라우저의 Stop→stale Select 차단→재확인/Select→stale Stop 차단→재확인/Stop과 오래된 계정 전환 쿠키의 POST 거부, 송출 승인 교체 뒤 stale form 재선택 거부를 확인한다. 기존 sender chat/공개 source/다른 creator 권한·조회 불변성·CSRF를 유지한다.
- 실제 Chromium 390px 두 관리 브라우저 컨텍스트에서 B가 중지 → A stale 재선택 차단 화면과 새로고침 → A 직접 재선택 → B stale 중지 차단 → B 계정 전환 후 다른 채널의 상태 확인을 클릭으로 수행한다. 새로고침은 그 자체로 광고·송출 보고·게임 영상/OBS 장면을 바꾸지 않는다.
- 수정은 플랫폼 서비스/브라우저·검사와 유일한 이 개발 계획에 한정한다. CHZZK contract Node/Chromium CI와 해당 SHA의 Windows scope만 검증하며 기존 native 빌드와 공식 OBS 합성은 **새로 실행된 경우만** 보고한다. 실패를 숨기는 timeout/보안 판정 완화·오래된 호환 경로 추가·게임 PC OBS 요구는 없다.

### 남은 범위

- **기능 `3eb5caf8773609bff5e33aef8bc8c39c3d4347a5` 실제 CI:** CHZZK contract **#84/run `38849739788` 전체 실패**. Ubuntu24 Node **330/332 통과·skip0**, Chromium **22/22 성공**(390px 두 인증된 관리자 화면에서 Stop→stale Select 409→새로 확인해 Select→stale Stop 409→관리 계정 전환/격리). 기존 `campaign-flow`에서 알 수 없는 캠페인 ID의 404가 새 stale-form 확인의 409보다 우선해야 하므로 실제 고정 `TEST_CAMPAIGN.id`와 일치하지 않으면 404로 유지한다. 기존 `streaming-conflict-flow`의 철회된 sender 화면에서 새 송출 승인으로 옮긴 뒤 stale Select가 303일 것이라는 기대는 새 정책과 불일치한다. **409 차단 → 실제 /campaigns 다시 읽기 → 현재 송출 승인에 명시적 Select**를 기존 회귀에 추가해 서버의 auto-reselect를 허용하지 않는다.
- 공식 OBS public ad flow **#27/run `38849739759` 전체 실패**: 공식 OBS32.2.2 합성 픽셀 **placed-960·scaled-480·local-hide·return-after-stop 4건은 통과**했고, 그 뒤 기존 synthetic driver가 새로 확인하지 않은 광고 선택을 직접 POST해 **409**를 받았다. `platform/tools/obs-ad-scene-test.mts`는 최초 선택/중지·복귀 뒤 재선택·최종 중지 **각각의 명시적 폼 동작 전에** 현재 `/campaigns`를 읽도록 고친다. 새로운 409 정책과 무관한 OBS 배너 픽셀 성능 한도·scene 제어는 바꾸지 않는다. Windows development **#421/run `38849739740` scope 성공, Windows native x64 skipped**(service/browser).
- **수정 커밋 `ebecf6a5b6bab6067b24b0181bb545d9c66b1b9c` 실제 CI:** CHZZK contract **#85/run `38850199343` 전체 실패**, Ubuntu24 서비스 테스트 **332/332 통과·fail/skip0**, Chromium 실제 관리 화면 **22/22 통과**. 이어지는 엄격 TypeScript 검사에서 새 `#confirmCampaignView`의 `b.campaignView?.owner` 확인이 `connectionId` 접근의 narrow로 이어지지 않아 **TS18048** 발생. 해당 값을 지역 변수에 담아 존재 여부를 확인하도록 수정한다(권한·409 차단 검사 그대로). Windows development **#422/run `38850199191` scope 성공, native x64 skipped**.
- **공식 OBS public ad flow #28/run `38850199080`: completed/success, 공식 OBS32.2.2·CEF 공개 배너 합성 픽셀 **13/13 성공**. 각 광고 선택·중지를 현재 `/campaigns` 조회와 연계한 synthetic fixture가 포함돼 `#27`의 잘못된 stale POST 실패를 해소했다. Artifact `12025071000`. 실제 방송/시청자 광고 노출·수익 증거가 아니며 최종 타입 수정 SHA에서는 공식 OBS를 새로 실행하지 않는다.
- **최종 기능 코드 `979556d418552fce71ff13fc045c8d4bde53a013`: CHZZK contract #86/run `38850461390` completed/success, 전체 5개 job 성공.** Ubuntu/Windows × Node22·24의 332개 플랫폼 HTTP/SQLite/ws 회귀·엄격 TypeScript·locked dependency 검사 통과. Ubuntu24 세부 **332/332·실패/취소/skip0**, 취약점 감사 0건, 실제 Chromium 브라우저 **22/22 통과**(390px A/B 동일 creator 광고 Stop→stale Select 409→직접 재선택→stale Stop 409→다른 관리 채널로 계정 전환 후 원래 source/송출 승인 보존). Chromium 증빙 artifact `12024748889`.
- **공식 OBS public ad flow #29/run `38850461405` completed/success, OBS 32.2.2·CEF 합성 픽셀 13/13 통과**(광고 Stop, 기존 URL 재선택, scene-away/return, browser shutdown 모드 포함; 증빙 artifact `12025465192`). #27의 오래된 fixture POST409는 실제 사용자처럼 현재 /campaigns 상태 확인 후 명시적으로 선택하는 새 테스트 동선으로 바로잡았다. Windows development **#423/run `38850461438` scope 성공, native x64 build skipped**(서비스·브라우저만 변경). 앞선 #84 Node 330/332·#85 Node332/332 뒤 TS18048 오류는 각각 분리 수정한 후 #86에서 해소됐다. CI 판정·시간·픽셀/CSRF/Origin/서버 제한을 완화하지 않았다.
- 이 성공은 **단일 인스턴스 synthetic provider·WebSocket·SQLite 및 공식 OBS 개발 fixture**의 좁은 검사다. 관리 브라우저 계정 전환/선택 경쟁을 다중 서버 배포·실계정 운영으로 확대하지 않는다. D1/D2/D4/D6/D7 간헐 오류, 실계정 CHZZK, 물리 투컴 HUD 제외·광고 실제 시청, 유료 광고·HP·정산·자원/설치/배포 qualification은 미완료다.

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

이번 재수집 브라우저 흐름이 Chromium에서 통과하면 **실제 치지직 등록 앱/채팅 읽기 권한으로 OAuth→Session→CHAT 첫 메시지**를 챗뷰 자체 HUD에 표시하는 G4-06 검증으로 넘어간다. 현재 계정 인증·등록 애플리케이션·물리 장비가 없어 그 성공을 합성으로 대신하지 않는다. 실계정 전에도 독립적으로 개선 가능한 한 흐름은 **구독 중단 직후의 메시지가 신규 세션의 첫 채팅에 유입되지 않는지** 확인하는 수신 경계다. 병행 최우선 물리 검증은 **게임 PC에 OBS 없이 HUD를 보고, 송출 PC의 녹화 영상에는 HUD가 없는지** 실측하는 G3-04다. D1/D2/D4/D6/D7, 유료 광고·HP·정산 및 배포 qualification은 미완료이며, 관련 변경은 사전 확인 후 한 번에 묶는다.

main·장기 브랜치·강제 push·게임 PC OBS·자동 장면 변경·렌더 ACK 수익 환산·비밀값 로그/커밋은 사용하지 않는다.
