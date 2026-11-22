## Joy Launcher for Android v1.3.6

스위치판과 테마를 함께 쓰는 Joy Launcher 의 안드로이드판입니다. 이 소스는 `android` 브랜치입니다.

### 바뀐 것
- **화면비 자동 맞춤**: 기기 화면비에 맞춰 테마 화면을 고칩니다
  - **16:10 · 4:3 등**: 테마 구성의 가로 자리만 화면 폭에 맞게 비례로 줄입니다. 글자 크기 · 그림 비율은 그대로, 격자 목록은 칸 모양이 유지되도록 열 수를 줄입니다 (5열 → 4:3 에서 4열)
  - **1:1 (예: Anbernic RG Rotate 720x720)**: 테마의 `layout_1x1.txt` 를 씁니다. 기본 테마 5종 모두 정사각 화면 구성이 있습니다 (아카이브플레이 · 벽골김씨 · 레트로게임갤러리 = 전용 구성, 기본 · 레트로 = 내장 1:1 구성)
  - 화면 폭을 넘는 조작 도움말은 글자를 줄여 맞추고, 옵션 · 테마 선택 등 창도 좁은 화면 안에 들어갑니다
  - 테마 만들기: 테마_레이아웃.md 12-1-3, 테마 빌더 1.3.6 의 «화면비» 미리보기
- **기본 테마 받기**: 최신 릴리스에서 받지 못하면 (인터넷 없음 등) 앱에 든 테마를 풉니다

> **1.3.5 에서 올리기**: `JoyLauncher.apk` 를 덮어 설치한 뒤, 테마 선택 창의 «기본 테마 5종 다시 받기» 를 하면 정사각 화면 구성이 든 새 테마로 바뀝니다. 설정 · 롬 폴더는 그대로입니다.

### English summary
- **Screen aspect fitting**: theme screens adapt to the device aspect ratio
  - **16:10, 4:3 …**: horizontal positions scale to the screen width; text sizes and picture ratios stay, grids drop columns to keep the cell shape (5 → 4 on 4:3)
  - **1:1 (e.g. Anbernic RG Rotate 720x720)**: the theme's `layout_1x1.txt` is used; all 5 stock themes have a square layout (Archive Play, WallStreetKim's, Retro Game Gallery = own layout; Basic, Retro = built-in square layout)
  - Control hints wider than the screen shrink to fit; option and theme windows fit narrow screens
  - Theme authors: theme-layout-en.md, theme builder 1.3.6 «화면비» (aspect) preview
- **Stock theme download** falls back to the themes inside the app when the latest release cannot be reached
- From 1.3.5: install `JoyLauncher.apk` over the old one, then use «Download the 5 stock themes again» in the theme picker to get the square layouts. Settings and ROM folders stay
