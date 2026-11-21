# Joy Launcher

[한국어](README.md) | **English**

A homebrew game launcher for the Nintendo Switch. Copy an Android **Pegasus ROM pack** to the SD card as-is: Joy Launcher reads `metadata.pegasus.txt`, shows the games by category and launches them with **RetroArch cores / PPSSPP**.

> There is also an **Android version** that uses the same themes: the [`android` branch](https://github.com/JyJyLab/JoyLauncher/tree/android) of this repository, releases tagged `android-v…` (Switch releases are tagged `v…`)

![Categories](docs/images/en_01_categories.png)

## Features
- Converts the Android launch settings of Pegasus lists (RetroArch cores, PPSSPP app) for the Switch (mapping rules in `/JoyLauncher/cores.txt`)
- Finds ROMs in per-system folders even without a list file; reads ES-DE packs (`ES-DE/gamelists`, `downloaded_media`) — formats can be mixed per folder
- Box art and screenshots; grabs a frame from the preview video when there is no screenshot
- Per-category background themes with four theme packs included (**Basic Theme, Retro Theme, WallStreetKim's Theme, Retro Game Gallery Theme**), launch log, cached game list (fast start)
- Theme layouts (`layout.txt`) and extension syntax (v1.1.0+): cell elements, conditions (`visible_if`, `hidden_if`), placeholders, `[strings]` text overrides, automatic ES-DE system-name matching
- Screen effects: smooth list movement, horizontal/vertical carousel lists, fade/slide screen transitions, background crossfade (can be turned off in Options → Theme settings)
- **Theme builder (PC, Windows/Linux)**: make themes while seeing the Switch screen as it will look, send them to the Switch over FTP — [docs/테마_빌더.md](docs/테마_빌더.md) (Korean)
- **Receive themes from PC (FTP)** screen in the launcher (no need to take out the SD card); delete themes in the theme picker
- View tabs in the game list (ZL·ZR): All · Favorites · Recently played
- Custom categories (`/JoyLauncher/categories.txt`): combine and hide categories, your own pictures folder used by every theme
- NDS games run with the DraStic Switch port (`/switch/DrasticDS.nro`) when it is installed
- Homebrew Favorites: nro games picked in the options (ports and others) gathered at the top of the category list
- Favorites (− button), Korean / English interface, HOME-style battery indicator
- Background music: Common BGM (`/JoyLauncher/bgm`) or **Theme BGM** (the theme's `bgm` folder) (v1.2.0+)
- Shows games only for systems that have a RetroArch core on the Switch (PSP: PPSSPP); systems too slow for the Switch are hidden by default
- **Automatic updates from GitHub Releases** (Y Options → Update) — the launcher only; get the theme packs from "Download the 4 stock themes" in the theme picker or from the release zip

## Installation
1. Download the latest `JoyLauncher_<version>.zip` from [Releases](https://github.com/JyJyLab/JoyLauncher/releases) and copy `switch/` and `JoyLauncher/` to the root of the SD card
2. Install RetroArch for Switch (with cores) and PPSSPP
3. Copy your ROM pack to `/roms` and start Joy Launcher from the Album (homebrew menu)
   - Use ASCII names for ROM files and folders (non-ASCII names may drop out of the list or break)
   - ES-DE packs: ROMs to `/roms`, `ES-DE/gamelists` and `downloaded_media` to `/ES-DE`

Manual: [docs/manual-en.md](docs/manual-en.md)

Adding your own theme: [docs/theme-guide-en.md](docs/theme-guide-en.md) · Changing the screen layout with a theme (`layout.txt`: grids, positions, fonts, colors): [docs/theme-layout-en.md](docs/theme-layout-en.md)

## Building
- Switch: devkitPro (devkitA64, libnx) + portlibs `switch-sdl2 switch-sdl2_image switch-sdl2_ttf switch-sdl2_mixer switch-ffmpeg switch-curl switch-dav1d` → `make`
- Desktop test build (Linux): `make -f Makefile.linux` (SDL2, SDL2_mixer, FFmpeg, libcurl dev packages)
- Theme builder (PC): `cd builder && make` (Linux, SDL2, SDL2_image, SDL2_ttf dev packages → `builder/build/joythemebuilder`) · `make WIN=1` (Windows cross build with mingw-w64 `x86_64-w64-mingw32-g++-posix` and SDL2 mingw dev files in `builder/third_party/sdl-mingw` or `SDLW=<path>` → `builder/build-win/JoyThemeBuilder.exe`)
- Theme packs: Basic Theme `python3 tools/make_joybasic.py` (Pillow) → `themes/JoyBasic` · Retro Theme `tools/make_retropack.py <Eungae theme Resource folder>` → `themes/RetroPack` · WallStreetKim's Theme pictures `tools/make_byeokgolkim.py <source folder>` → `themes/ByeokgolKim`
- Release: pushing a `vX.Y.Z` tag makes GitHub Actions build and publish a release (`joylauncher.nro`, `.sha256`, zip). The launcher's updater uses the latest release.

## Contributors

| Contributor | Contribution |
|---|---|
| **월스트리트킴** (Wall Street Kim, Retro Game Gallery) | 30% |

## Sponsor

If Joy Launcher helps you, you can support it — donations go to test devices and development time.

- [Buy Me a Coffee](https://buymeacoffee.com/jyjy) — card payment, no account needed
- [GitHub Sponsors](https://github.com/sponsors/Angler-JJ-Lee)
- Korea: [ctee](https://ctee.kr/place/jayjay)

## License
- Joy Launcher: GPL-3.0 (`LICENSE`) — it statically links FFmpeg built with GPL options
- NanumGothic font: SIL Open Font License 1.1 (`OFL.txt`)
- stb_image_write: Public Domain / MIT
- No ROMs or BIOS files are included.
- Theme packs: **Basic Theme** (`themes/JoyBasic`) is self-made and free to use and modify. **Retro Theme** (`themes/RetroPack`) is an excerpt of the Eungae pack theme pictures; **WallStreetKim's Theme** (`themes/ByeokgolKim`) was arranged by WallStreetKim from the ES-DE theme Canvas (Siddy212, CC0 1.0) and uses the font Cafe24 Ohsquare air. Console photos, logos and trademarks in those two packs belong to their owners and are separate from the launcher's GPL. **Retro Game Gallery Theme** (`themes/DcGall`) uses the font Noto Sans KR (SIL OFL 1.1, `fonts/OFL-NotoSansKR.txt`) and icons from the ES-DE theme Canvas (Siddy212, CC0); the "dcinside" text is a parody label, unrelated to DCinside. (See `README.txt` in each folder)
- Theme builder library: the Horizon and Iconic pictures are CC BY-NC-SA (non-commercial, attribution, share-alike; `library/<set>/CREDITS-LICENSE.txt`)
