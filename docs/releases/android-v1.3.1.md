## Joy Launcher for Android v1.3.1

스위치판과 테마를 함께 쓰는 Joy Launcher 의 안드로이드판입니다. 스위치판 릴리스는 태그 `v1.3.1`, 이 소스는 `android` 브랜치입니다. 테마 문법은 바뀌지 않았습니다 (스위치판 v1.3.1 과 같은 수준).

### 바뀐 것
- **롬 폴더 자동 찾기 넓힘**: 저장소 맨 위와 **한 단계 아래**의 `ROMs`·`Roms`·`roms` (예: `/JY_Data/Roms`), 기종 폴더(GBA·NDS…)가 모인 폴더를 찾습니다. 메타데이터 없이 기종 폴더만 있어도 게임을 읽고, 롬을 넣거나 빼면 다음 시작 때 다시 읽습니다. `roots.txt` 에 기종 폴더 하나만 적어도 됩니다
- **처음 실행 "롬 폴더 확인"**: 자동으로 찾은 위치(내장 / SD 카드)를 보여 주고 **A = 이대로 사용** / **B = 직접 선택** — 안드로이드 기본 **파일 관리자**에서 ROMS 폴더까지 들어가 "이 폴더 사용 → 허용". 직접 고르면 자동 찾기는 끄고 고른 폴더만 씁니다
- **롬 폴더 설정** (옵션 → 롬 및 앱 설정): 내장 · SD 카드 · 기타 **여러 곳을 함께 등록**, 등록 해제(X), 자동 찾기 켜기/끄기, 파일 관리자가 안 되는 기기는 **앱 안에서 찾아보기**. 맨 위에 "지금 쓰는 롬 폴더: 등록 n곳 + 자동 m곳" 표시. 게임은 모든 위치에서 합쳐 보이고 같은 기종 폴더는 한 카테고리, 같은 파일은 한 번만 (`roots.txt` 에 저장)
- **옵션 창 분류**: 맨 위 = **롬 및 앱 설정** (게임 목록 새로 고침 · 롬 폴더 설정 · 앱 즐겨찾기 목록 관리 · 에뮬레이터 선택) · **테마 설정** (테마 선택 · 화면 효과 · 미리보기 영상 · PC에서 테마 받기) · **시스템 설정** (BGM · 소리 · A·B 버튼 바꾸기 · 화면 컨트롤러) · 언어 · 업데이트 확인 · 종료. 분류에서 연 창은 B 로 닫으면 그 분류로 돌아옵니다
- 게임 목록이 없는 화면에서 **A = 롬 폴더 고르기**
- 처음 목록을 읽는 동안 "게임 목록을 읽는 중" 화면을 보여 주고, 그 사이 눌린 버튼은 버립니다 (다 읽자마자 게임이 실행되던 문제)

> **1.3.0 에서 올리기**: 이 페이지의 `JoyLauncher.apk` 를 받아 덮어 설치하면 `/JoyLauncher` 의 설정 · 테마 · 롬 폴더는 그대로입니다 (처음 실행 창은 새로 설치할 때만 나옵니다). 1.3.1 시험판(1.3.1-test1) 위에도 덮어 설치됩니다.

### English summary
- **Wider automatic ROM folder search**: `ROMs` / `Roms` / `roms` at the top of a storage **or one level below** (e.g. `/JY_Data/Roms`) and folders holding system folders (GBA, NDS …). Plain system folders without metadata are read, and added / removed ROMs are picked up at the next start. A single system folder can be listed in `roots.txt`
- **First start "ROM folders" window**: shows the places found (internal / SD card) — **A = use them** / **B = choose myself** in the Android **file manager** (go into the ROMS folder, "Use this folder → Allow"). Choosing yourself turns the automatic search off
- **ROM folders** (Options → ROMs & apps): **several places at once** (internal, SD card, others), remove with X, automatic search on/off, **browse in the app** when the file manager does not work, "ROM folders in use: n registered + m found" at the top. Games from all places are combined; the same system folder name is one category and the same file appears once (saved in `roots.txt`)
- **Options are grouped**: **ROMs & apps** (refresh · ROM folders · Manage App Favorites · choose emulator) · **Theme settings** (choose theme · screen effects · preview videos · receive themes from PC) · **System settings** (BGM / sound · swap A and B · on-screen controller) · Language · Check for updates · Exit. Windows opened from a group return to it with B
- On the "no games" screen **A = choose the ROM folder**
- A "Reading the game list" screen is shown while the list is read for the first time; buttons pressed meanwhile are ignored
- From 1.3.0: download `JoyLauncher.apk` from this page and install it over the old one — settings, themes and ROM folders stay. It also installs over the 1.3.1 test build
