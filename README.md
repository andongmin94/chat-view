# ChatView — OBS-based broadcasting platform

챗뷰는 스트리머가 하나의 스크린에서 게임·작업과 채팅을 함께 보는 OBS 기반 방송지원 플랫폼입니다. 개인 채팅 HUD는 스트리머에게만, 선택한 광고는 시청자에게 표시합니다. 치지직 자체 연동부터 시작하고, 게임 PC에 OBS가 없는 투컴과 시청시간 기반 광고 HP·수익을 목표로 합니다.

**개발 브랜치: OBS.** main은 초기 Electron 제품의 사용 경험 참조이며 병합·재개발 대상이 아닙니다.

## 문서의 역할

[제품 목표](PRODUCT.md)는 최종 제품과 확정 제약, [개발 운영](docs/development-workflow.md)은 구현·검증 방식, [아키텍처](docs/architecture.md)는 책임과 구조를 정의합니다. **현재 구현·검증 결과·다음 작업은 [개발 현황](docs/development-plan.md) 한 곳**에 기록합니다. 새 개발 세션은 [AGENTS.md](AGENTS.md)부터 읽습니다.

## 현재 사용할 수 있는 개발 경로

기존 네이티브 OBS 제어기·투명 WebView2 HUD·클릭 통과·위치/크기/DPI 저장·제한된 복구·설정/진단 기능이 있습니다. 치지직 인증/채팅 구독 모듈과 챗뷰 자체 렌더러, 읽기 전용 서버 전달, WinHTTP 수신기와 네이티브 연결창도 구현되어 있습니다.

같은 `chat-view-hud.exe`를 `--companion`으로 실행하면 로컬 OBS 부모나 공유 메모리 없이 시작하도록 구현했습니다. 개발 경고 동의 후 기존 연결창을 열며, 중복 companion 실행을 막고 **Ctrl+Alt+Shift+Q**로 종료합니다. OBS 제어 모드는 기존 부모 종료 연동을 유지합니다. 잘못된 OBS 실행 인수를 독립 모드로 바꾸지 않습니다.

**개발자용 연결 경로이지 배포된 다중 사용자 서비스가 아닙니다.** 실계정 치지직 전체 검증, 일반 사용자용 기기 등록/자동 갱신, 송출 PC 연동과 검증된 투컴 영상 전달, 광고·HP·수익은 아직 완료되지 않았습니다. 독립 실행만으로 HDMI에서 HUD가 제외되지는 않습니다. 실제 방송에 쓰기 전 해당 영상 구성을 검증해야 합니다.

## 개발용 자체 채팅 연결

[platform/README.md](platform/README.md)에 따라 비공개 개발자 설정으로 로컬 연동 도구를 실행하고 채널을 승인합니다. 관리 화면에서 **표시 연결용 1회 키 발급**을 선택합니다. HUD의 **Ctrl+Alt+Shift+C** 연결창에서 서비스 origin과 표시 키를 입력합니다. HTTPS가 기본이며 로컬 도구만 명시적 허용 후 `http://127.0.0.1:47831`을 사용합니다.

표시 키는 치지직 비밀번호·개발자 비밀키가 아닙니다. 입력값은 사용/창 닫기 시 비우고 저장하지 않습니다. 창을 닫아도 연결은 유지되며 **연결 종료**는 개인 채팅을 지웁니다. 만료·철회·연결 손실도 전달을 종료합니다. 현재 5분 이하 표시 권한은 개발 계약이며, 최종 사용자에게 수동 재연결을 반복시키는 제품 목표가 아닙니다.

실행 방식·보안 경계는 [네이티브 표시 계약](docs/native-display-client.md), 서버 헤더/경로는 [전달 계약](docs/display-delivery.md)을 봅니다. 일반 스트리머에게 개발자 비밀키나 이 로컬 도구를 배포하지 않습니다.

## 기존 외부 페이지와 창 조작

OBS의 **Tools → ChatView Settings...**에서 치지직·SOOP·YouTube 방송/채팅 URL 또는 위플랩 페이지를 설정할 수 있습니다. 허용된 HTTPS 호스트·경로를 정규화하며 새 창과 다른 문서 이동은 제한됩니다. 자체 채팅 연결에 쓰는 로컬 관리 URL을 이 설정에 넣지 않습니다. 설정 변경으로 문서가 교체되면 기존 자체 채팅 전달을 끝내고 다른 페이지로 메시지를 보내지 않습니다.

**Ctrl+Alt+Shift+H**로 편집 모드를 켜고 이동/크기를 조정한 뒤 다시 잠급니다. 잠금 모드는 비활성·클릭 통과를 사용하며 위치는 `%LOCALAPPDATA%\ChatView\hud.ini`에 저장합니다. 상태 문구 `READY TO STREAM`은 현재 ChatView 내부 판정이지 방송 전체의 준비·전송 성공 보장이 아닙니다.

## 캡처와 배포 제한

현재 캡처 위험 정책은 Display Capture 상황에서 HUD를 숨길 수 있습니다. 이는 보호 동작이며, 스트리머가 계속 읽는 상태로 송출에서 제외되는 최종 요구를 충족했다는 뜻은 아닙니다. Windows 캡처 제외 설정, 장치 연결, 렌더링 성공은 물리적 HDMI 영상의 비공개 보장이 아닙니다.

배포 패키지는 **해당 커밋의 전체 qualification이 성공한 결과만** 사용합니다. 일반 development CI는 패키지를 올리지 않습니다. 설치하려면 검증된 패키지를 풀고 OBS를 종료한 뒤 `install.cmd`를 실행합니다. 기본 OBS 경로는 `C:\Program Files\obs-studio`이며 다른 경로는 `install.ps1 -ObsPath`로 지정합니다. `uninstall.cmd`로 제거하며 사용자 설정은 유지됩니다. 이 설치기는 게임 PC용 배포 설치기가 아닙니다.

## 개발 빌드

현재 설정은 Windows x64, Windows 10 2004+, Visual Studio 2026 C++ 도구, SDK 10.0.26100, CMake 3.28+, OBS 개발 라이브러리와 WebView2 SDK를 사용합니다. 버전은 실제 workflow를 확인합니다. Node는 개발용 도구/통합 테스트에 필요하며 네이티브 사용자 실행 환경에 추가한 런타임이 아닙니다.

```powershell
npm --prefix platform ci --ignore-scripts
$env:OBS_CMAKE_PREFIX = "C:\path\to\obs-development-prefix"
$env:WEBVIEW2_SDK_DIR = ./scripts/restore-webview2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-relwithdebinfo
ctest --test-dir build/windows-x64 --build-config RelWithDebInfo --label-exclude qualification --output-on-failure
./build/windows-x64/rundir/obs-plugins/64bit/chat-view-hud.exe --companion
```

개발/배포 검사의 구분은 [운영 원칙](docs/development-workflow.md)을 따릅니다. 진단 내보내기는 로컬 동작이며 채팅·표시 키·URL을 제품 로그에 추가하지 않습니다. 공유 전 진단 파일을 확인합니다. 새 클라우드 계정 기능의 데이터 처리는 별도로 설계해야 합니다.
