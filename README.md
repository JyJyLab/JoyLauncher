# Joy Launcher

**한국어** | [English](README.en.md)

닌텐도 스위치용 홈브루 게임 런처입니다. 안드로이드용 **페가수스(Pegasus) 롬 모음**을 SD 카드에 그대로 복사하면 `metadata.pegasus.txt` 를 읽어 카테고리별로 보여 주고, 고른 게임을 **RetroArch 코어 / PPSSPP** 로 바로 실행합니다.

![카테고리 화면](docs/images/01_categories.png)

## 특징
- 안드로이드용 페가수스 목록의 실행 설정(RetroArch 코어·PPSSPP 앱)을 스위치용으로 자동 변환 (`/JoyLauncher/cores.txt` 로 대체 규칙 지정)
- 목록 파일이 없어도 기종별 폴더의 롬을 자동 인식, ES-DE `gamelist.xml` 지원
- 박스·화면 사진, 화면 사진이 없으면 미리보기 영상에서 장면 추출
- 카테고리별 배경 테마(무료 기본 테마 **JoyBasic** 포함), 실행 기록, 게임 목록 캐시(빠른 시작)
- 즐겨찾기(− 버튼), 한국어 / 영어 화면, 직접 넣은 mp3·ogg 배경 음악, HOME 화면과 같은 배터리 표시
- 스위치 성능 범위(아케이드 ~ PS1, PSP) 밖의 기종은 자동 제외·숨김
- **깃헙 릴리스 자동 업데이트** (Y 옵션 → 업데이트) — 런처 본체만 바뀝니다. 테마는 릴리스 zip 을 내려받아 `JoyLauncher/themes/` 를 직접 복사

## 설치
1. [Releases](https://github.com/JyJyLab/JoyLauncher/releases) 에서 최신 `JoyLauncher_<버전>.zip` 을 받아 `switch/` 와 `JoyLauncher/` 를 SD 카드 루트에 복사
2. 스위치용 RetroArch(코어 포함)와 PPSSPP 를 설치
3. 롬 모음을 `/roms` 에 복사하고 앨범(홈브루 메뉴)에서 Joy Launcher 실행
   - 롬 파일·폴더 이름은 영문(ASCII)을 권장합니다 (한글 이름은 목록에서 빠지거나 깨질 수 있음)

자세한 사용법: [docs/설명서.md](docs/설명서.md) · English: [docs/manual-en.md](docs/manual-en.md)

## 빌드
- 스위치: devkitPro (devkitA64, libnx) + portlibs `switch-sdl2 switch-sdl2_image switch-sdl2_ttf switch-sdl2_mixer switch-ffmpeg switch-curl switch-dav1d` → `make`
- 데스크톱 테스트(리눅스): `make -f Makefile.linux` (SDL2, SDL2_mixer, FFmpeg, libcurl 개발 패키지)
- 기본 테마: `python3 tools/make_joybasic.py` (Pillow) → `themes/JoyBasic`
- 릴리스: `vX.Y.Z` 태그를 올리면 GitHub Actions 가 빌드해 릴리스(`joylauncher.nro`, `.sha256`, zip)를 만듭니다. 런처의 자동 업데이트는 최신 릴리스를 사용합니다.

## 라이선스
- Joy Launcher: GPL-3.0 (`LICENSE`) — GPL 로 빌드된 FFmpeg 를 정적 링크하므로
- 나눔고딕 글꼴: SIL Open Font License 1.1 (`OFL.txt`)
- stb_image_write: Public Domain / MIT
- 롬·BIOS·다른 테마 그림은 포함하지 않습니다. 기본 테마 JoyBasic 은 자체 제작입니다.
