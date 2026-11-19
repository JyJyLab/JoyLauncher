## Joy Launcher for Android v1.3.0

스위치판과 테마를 함께 쓰는 Joy Launcher 의 안드로이드판입니다. 스위치판 릴리스는 태그 `v1.3.0`, 이 소스는 `android` 브랜치입니다.

### 바뀐 것
- **업데이트는 직접 받아 설치**: 앱이 apk 를 받아 설치하던 기능을 없앴습니다 (다른 앱 설치 권한 문제). 옵션 → **업데이트 확인** 이 새 버전과 **릴리스 페이지 주소**를 보여 주고, **[A] 브라우저로 열기** 로 열 수 있습니다. 페이지의 `JoyLauncher.apk` 를 받아 덮어 설치하면 `/JoyLauncher` 의 설정 · 테마는 그대로입니다. 앱에서 "다른 앱 설치" 권한도 뺐습니다
- **메뉴 창이 테마 색으로**: 옵션 · 소리 설정 · 테마 선택 · 앱 목록 · 에뮬레이터 선택 · FTP · 종료 확인 · 진행 · 알림 상자가 둥근 모서리와 테마에 맞춘 색으로 그려집니다 (테마 없음은 예전 색 그대로 모서리만). 기본 테마 4종에 메뉴 색 지정
- **X 버튼 이름**: 어떤 테마든 화면 아래 도움말의 X 는 이 판의 기능인 `BGM` (BGM · 소리 설정) 으로 보입니다 (스위치판 테마의 "홈브루 메뉴" · "홈브류" 등)
- 테마 문법 추가는 스위치판 v1.3.0 노트와 같습니다 (`color.menu…`, `menu_radius`, `menu_border_width`)

> **1.2.0 에서 올리기**: 1.2.0 의 "업데이트 설치" 로 받아도 되고, 이 페이지에서 `JoyLauncher.apk` 를 받아 덮어 설치해도 됩니다 (같은 서명이라 설정 · 테마 유지).

### English summary
- **Updates are installed by you**: the app no longer downloads and installs the apk (install-permission issues). Options → **Check for updates** shows the new version and the **release page address**; **[A] Open in browser** opens it. Download `JoyLauncher.apk` there and install it over the old one — settings and themes in `/JoyLauncher` stay. The "install other apps" permission was removed
- **Menu windows follow the theme**: rounded corners and theme-matched colors (old colors with rounded corners when no theme is used); the four stock themes set their own menu colors
- **X button**: whatever a theme writes after `[X]` ("Homebrew menu" etc.), the hint shows `BGM`, this version's X function
- New theme keys are the same as Switch v1.3.0 (`color.menu…`, `menu_radius`, `menu_border_width`)
- From 1.2.0: either use its "Install update" or download `JoyLauncher.apk` from this page and install it over (same signature, settings and themes stay)
