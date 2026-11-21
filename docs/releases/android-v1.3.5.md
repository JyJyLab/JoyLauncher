## Joy Launcher for Android v1.3.5

스위치판과 테마를 함께 쓰는 Joy Launcher 의 안드로이드판입니다. 이 소스는 `android` 브랜치입니다.

### 바뀐 것
- **목록 가운데 고정 스크롤**: 한 화면을 넘는 게임·카테고리 목록(줄 · 격자)에서 선택한 줄이 화면 가운데에 머물고 목록이 움직입니다. 목록의 처음과 끝에서는 목록이 멈추고 선택이 위·아래 끝까지 갑니다
- **Azahar(3DS) 실행 오류 수정**: ES-DE 식 실행 명령(저장소 문서 주소)으로 에뮬레이터를 열 때 `SecurityException ... does not have permission` 으로 실행되지 않던 문제를 고쳤습니다. 런처가 그 폴더 권한이 없으면 권한 넘기기 없이 다시 실행해, 에뮬레이터가 스스로 받아 둔 폴더 권한을 씁니다
  - Azahar 는 처음 설정에서 **게임 폴더를 롬이 든 기종 폴더(예: `Roms/3ds`)로** 지정해 주세요

> **1.3.4 에서 올리기**: 이 페이지의 `JoyLauncher.apk` 를 받아 덮어 설치하면 `/JoyLauncher` 의 설정 · 테마 · 롬 폴더는 그대로입니다.

### English summary
- **Centered list scrolling**: in game and category lists (text, grid) longer than one screen the selected row stays in the middle and the list moves; at the start and end the list stops and the selection moves to the edge
- **Azahar (3DS) launch fix**: launching with ES-DE style document URIs no longer fails with `SecurityException ... does not have permission`. When the launcher cannot pass the folder permission it retries without it, so the emulator's own folder permission is used. In Azahar's first-run setup, pick the system folder that holds the ROMs (for example `Roms/3ds`) as the games folder
- From 1.3.4: install `JoyLauncher.apk` over the old one — settings, themes and ROM folders stay
