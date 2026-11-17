## Joy Launcher for Android v1.2.0 (첫 배포)

닌텐도 스위치용 Joy Launcher 의 안드로이드판입니다. **테마 문법·폴더 구성이 스위치판과 같아 테마를 서로 그대로 씁니다.** 스위치판 릴리스는 태그 `v1.2.0`, 이 소스는 `android` 브랜치입니다.

### 설치
- `JoyLauncher.apk` 설치 (안드로이드 7.0 이상, arm64) → 처음 실행 때 **모든 파일 접근** 허용
- 롬은 저장소의 `ROMs`·`Roms`·`roms` 폴더, 페가수스 `game_dirs.txt`, ES-DE 롬 폴더를 자동으로 찾습니다. 그 밖은 `/JoyLauncher/roots.txt`
- 설정·테마는 저장소 루트의 `/JoyLauncher` 폴더 (기본 테마 4종이 처음 실행 때 풀립니다)
- 다음 버전부터는 옵션 → **업데이트 확인** 으로 받습니다

### 주요 기능
- **기종 제한 없음**: 설치된 에뮬레이터가 있으면 무엇이든 실행 (NDS·3DS·PS2·GC/Wii·스위치·PS3·Xbox 360 …). 기종·에뮬레이터 표는 ES-DE 안드로이드판의 것(MIT)이고 `/JoyLauncher/custom_systems` 로 더할 수 있습니다
- **에뮬레이터 고르기**: ① 페가수스 메타데이터의 실행 명령(`am start …`) 그대로 ② 없으면 롬 폴더 이름 → 기종 → 설치된 에뮬레이터. 옵션 "에뮬레이터 선택" 또는 `emulators.txt` 로 바꾸기
- **미리보기 영상**: 고른 게임에 영상이 있으면 화면 사진 자리에서 재생 (읽는 동안은 사진)
- **소리 선택 (X 버튼)**: 공통 BGM · 테마 BGM · 영상 소리 · 무음 중 하나
- **앱 즐겨찾기**: 설치된 앱을 골라 카테고리로 (옵션 → 앱 목록)
- 컨트롤러에 맞춘 버튼 표시(닌텐도 계열 + −, 그 밖 START SELECT), A·B 바꾸기, 컨트롤러가 없으면 **화면 컨트롤러**(터치)
- 고해상도 화면에서도 또렷한 글자·그림, PC 테마 빌더의 FTP 보내기 받기, 즐겨찾기·최근 실행·나만의 카테고리
- 페가수스 목록의 **webp 그림**(JoiPlay 등), 설명의 `\n` 줄바꿈, 일본어·한자(기기의 한중일 글꼴)도 페가수스처럼 표시
- **꺼짐 기록**: 런처가 갑자기 꺼지면 다음에 켤 때 `/JoyLauncher/crash_log.txt` 에 원인을 남깁니다 (문제를 알릴 때 함께 보내 주세요)

설명서: `docs/설명서.md` (`android` 브랜치) · 롬·BIOS·에뮬레이터는 포함하지 않습니다.

### English summary
Android version of Joy Launcher; **themes are shared with the Switch version** (same syntax and folders). Switch release: tag `v1.2.0`; this source: `android` branch.
- Install `JoyLauncher.apk` (Android 7.0+, arm64) and allow **All files access**. ROMs are found in `ROMs`/`Roms`/`roms`, Pegasus `game_dirs.txt` and the ES-DE ROM folder; settings and themes live in `/JoyLauncher`. Later versions come through Options → **Check for updates**
- Runs any system you have an emulator for (ES-DE Android system/emulator tables, MIT; extend with `/JoyLauncher/custom_systems`). Emulator choice: the Pegasus `am start` command first, then folder name → system → installed emulator; change it in Options → Choose emulator or `emulators.txt`
- Preview videos play in place of the screenshot; **sound choice (X)**: Common BGM, Theme BGM, Video sound or Silent; App Favorites; button names follow the controller; A/B swap; on-screen touch controller; sharp text on high-resolution screens; FTP theme receiving from the PC Theme Builder
- webp pictures of Pegasus packs (JoiPlay etc.), `\n` line breaks in descriptions and Japanese/Chinese text (device CJK font) are shown like Pegasus does
- **Crash log**: after an unexpected exit, the next start writes the cause to `/JoyLauncher/crash_log.txt`
