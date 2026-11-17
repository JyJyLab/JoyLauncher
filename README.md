# Joy Launcher (Android)

**한국어** | [English](README.en.md)

안드로이드용 게임 런처입니다. 닌텐도 스위치용 [Joy Launcher](https://github.com/JyJyLab/JoyLauncher) 의 포크로, **테마 문법과 폴더 구성이 같아 테마를 서로 그대로 쓸 수 있습니다.**
롬 폴더의 **페가수스(Pegasus) 메타데이터**나 **ES-DE 목록**을 읽어 카테고리별로 보여 주고, 고른 게임을 설치된 에뮬레이터 앱으로 바로 실행합니다.

![카테고리 화면](docs/images/01_categories.png)

## 특징
- **기종 제한 없음**: 설치된 에뮬레이터가 있으면 무엇이든 실행 (레트로 기종부터 NDS·3DS·PS2·GC/Wii·스위치·PS3·Xbox 360 등). 에뮬레이터 표는 ES-DE 안드로이드판의 것(기종 195·에뮬레이터 106)을 쓰고, 같은 문법의 파일로 새 기종·에뮬레이터를 더할 수 있습니다
- **에뮬레이터 고르기 = 페가수스 + ES-DE 방식**
  1. 롬 모음의 `metadata.pegasus.txt` 에 적힌 실행 명령(`am start …`)을 그대로 실행
  2. 없으면 롬 폴더 이름으로 기종을 알아내 설치된 에뮬레이터를 고름 (옵션의 "에뮬레이터 선택" 으로 바꿀 수 있음)
- 롬 폴더 자동 찾기: 내부 저장소·SD 카드의 `ROMs`·`Roms`·`roms`, 페가수스의 `game_dirs.txt`, ES-DE 의 롬 폴더
- 설정·테마는 저장소 루트의 **`/JoyLauncher`** 폴더: 파일 관리자로 자유롭게 고칠 수 있습니다
- **앱 즐겨찾기**: 설치된 앱을 골라 카테고리로 모아 실행 (옵션 → 앱 목록)
- **소리 선택 (X 버튼)**: 공통 BGM · 테마 BGM · 영상 소리 · 무음 중 하나. 테마마다 자기 BGM 을 넣을 수 있음 (`themes/<테마>/bgm`)
- **컨트롤러에 맞춘 버튼 표시**: 닌텐도 계열은 `+` `−`, 그 밖은 `START` `SELECT`. A·B 의 선택/취소 바꾸기 설정
- **화면 컨트롤러**: 컨트롤러가 없으면 터치 버튼이 화면에 나타납니다
- 박스·화면 사진, **미리보기 영상 재생**: 고른 게임에 영상이 있으면 화면 사진 자리에서 재생 (옵션에서 끌 수 있음)
- 테마: 카테고리별 배경·로고, `layout.txt` 로 화면 구성(격자·캐러셀·글꼴·색·화면 전환). 기본 테마 4종 포함. PC 테마 빌더(스위치판)의 "스위치로 보내기"(FTP)도 그대로 받음
- 즐겨찾기·최근 실행 보기 탭, 나만의 카테고리, 한국어 / 영어, 전지 표시
- 화면은 테마의 1280x720 좌표 그대로 두고 글자·그림만 기기 해상도로 그려 고해상도 화면에서도 또렷합니다

## 설치
1. [릴리스](https://github.com/JyJyLab/JoyLauncher/releases) 에서 **안드로이드판**(태그 `android-v…`)의 `JoyLauncher.apk` 를 받아 설치 (안드로이드 7.0 이상, arm64). 스위치판은 태그 `v…` 의 릴리스입니다
   - 이후 새 버전은 옵션 창의 "업데이트 확인" 으로 받습니다
2. 처음 실행하면 **모든 파일 접근** 권한을 묻습니다 → 허용 (롬 폴더를 읽고 `/JoyLauncher` 폴더를 쓰기 위해)
3. 롬은 저장소의 `ROMs`(또는 `Roms`·`roms`) 폴더에. 다른 곳이면 `/JoyLauncher/roots.txt` 에 한 줄씩 적습니다
4. 에뮬레이터는 따로 설치합니다 (RetroArch, DuckStation, PPSSPP 등)

자세한 사용법: [docs/설명서.md](docs/설명서.md) · English: [docs/manual-en.md](docs/manual-en.md)

테마 만들기(스위치판과 공통): [docs/테마_만들기.md](docs/테마_만들기.md) · [docs/테마_레이아웃.md](docs/테마_레이아웃.md) · English: [theme-guide-en.md](docs/theme-guide-en.md) · [theme-layout-en.md](docs/theme-layout-en.md)

## 빌드
- 안드로이드: `android/README.md` (Gradle 없이 NDK + CMake, SDL2·SDL2_image·SDL2_ttf·SDL2_mixer 소스 빌드 → apk)
- 데스크톱 시험판(리눅스): `make -f Makefile.linux` → `./joylauncher_desktop` (화면 스냅샷·실행 인자 확인용. 안드로이드 기능은 `source/platform_desktop.cpp` 가 흉내 냄)
- 테마 빌더: 스위치판 저장소의 것을 그대로 씁니다

## 라이선스
- Joy Launcher: GPL-3.0 (`LICENSE`)
- 기종·에뮬레이터 표(`romfs/systems/es_systems.xml`, `es_find_rules.xml`): ES-DE Frontend, MIT (`romfs/systems/LICENSE.ES-DE.txt`)
- SDL2·SDL2_image·SDL2_ttf·SDL2_mixer: zlib License, FreeType: FTL, stb / dr_libs: Public Domain / MIT
- 나눔고딕 글꼴: SIL Open Font License 1.1 (`OFL.txt`)
- 테마 팩: 스위치판과 같습니다 (각 테마 폴더의 `README.txt`)
- 롬·BIOS·에뮬레이터는 포함하지 않습니다.
