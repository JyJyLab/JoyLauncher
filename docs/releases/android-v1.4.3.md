## Joy Launcher for Android v1.4.3

스위치판과 테마를 함께 쓰는 Joy Launcher 의 안드로이드판입니다. 이 소스는 `android` 브랜치입니다.

### 바뀐 것
- **목록 파일 밖의 롬도 보임**: 롬 폴더에 `gamelist.xml` · `metadata.pegasus.txt` 가 있어도, 목록에 없는 롬을 같은 카테고리에 함께 보여 줍니다 (ES-DE · Pegasus 와 같은 동작)
  - 목록에 없는 롬은 파일 이름이 제목으로 보입니다
  - gamelist.xml 에서 숨김(`hidden`) 으로 표시한 게임은 계속 숨깁니다
  - metadata.pegasus.txt 는 컬렉션에 `extensions:` 가 있을 때만 추가합니다. `ignore-files` · `ignore-extensions` 도 지킵니다
  - 목록 파일이 있는 폴더도 롬 파일을 넣고 빼면 다음 실행 때 다시 읽습니다
- **기본 테마 5종을 스위치판과 같게**: 3DS · PS2 그림, 아카이브플레이 아이콘 (테마 선택 창 «기본 테마 5종 다시 받기»)
- 테마 빌더 1.4.3 (스위치 릴리스 v1.4.3 에 포함): 안드로이드 미리보기 · 공통 점검 · 테마 BGM — 빌더로 만든 테마는 두 판 공통

> **1.3.6 에서 올리기**: `JoyLauncher.apk` 를 덮어 설치하면 됩니다. 설정 · 롬 폴더는 그대로입니다.

### English summary
- **ROMs outside the list files now show**: folders with `gamelist.xml` or `metadata.pegasus.txt` also list ROMs that are not in the file, in the same category (same as ES-DE and Pegasus). Unlisted ROMs use the file name as title; `hidden` games stay hidden; Pegasus collections add files only when they declare `extensions:`. ROM changes in list folders are detected on the next start
- **Stock themes match the Switch version** (3DS / PS2 art, Archive Play icons) — use «Download the 5 stock themes again»
- Theme builder 1.4.3 (in the Switch release v1.4.3): Android preview, common check, theme BGM — builder themes work on both versions
- From 1.3.6: install `JoyLauncher.apk` over the old one
