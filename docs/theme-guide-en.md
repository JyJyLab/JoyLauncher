# Joy Launcher — Adding and Making Themes

Back to the manual: `manual-en.md` (in the release zip: `Manual_en.md`) · 한국어: [테마_만들기.md](테마_만들기.md)

A theme is a set of pictures that changes the **background** of each category (system) and the **logo** above the game list.
Put one folder into `/JoyLauncher/themes/` on the SD card and you have one more theme. No reinstall or code change is needed.

> **To change the screen layout too** (system card grid, positions of the list and pictures, fonts and colors, your own pictures): **[Theme layout guide (layout.txt)](theme-layout-en.md)** — from v1.0.0

---

## 1. What a theme changes

| Screen | What changes |
|---|---|
| Category screen | The **background** of the category under the cursor. It follows the cursor. The title stays "Joy Launcher" text |
| Game list | The category's **background** + a **logo** in place of the title text (only if a logo image exists) |
| "No games found" screen | The `default` background |

- When a background is drawn, the top/bottom bars and the selection bar become translucent so the picture shows through.
- A category without a background picture is shown like the no-theme screen (dark plain color). The `default` background is **not** used for it.

## 2. Quick start (5 steps)

1. On your PC, make a folder with an ASCII name, e.g. `MyTheme`.
2. Inside it make `backgrounds` and put background pictures named **like your ROM folders**.
   e.g. `/roms/GBA` → `backgrounds/GBA.jpg`, `/roms/snes` → `backgrounds/snes.jpg`
3. (Optional) Put transparent PNG logos with the same names into `logos`, e.g. `logos/GBA.png`.
4. (Optional) Write `theme.txt` for darkness and name mapping (section 6).
5. Copy the whole `MyTheme` folder to `/JoyLauncher/themes/` on the SD card, then in the launcher press **Y Options → Theme → MyTheme → A**.

## 3. Folder layout

```
/JoyLauncher/themes/
├─ JoyBasic/                 Basic Pack (RetroPack = Retro Pack, ByeokgolKim = Byeokgol Kim Pack)
└─ MyTheme/                  new theme (folder name = name in the theme picker)
   ├─ backgrounds/
   │  ├─ GBA.jpg             category background
   │  ├─ SFC.png
   │  ├─ favorites.jpg       Favorites category background
   │  └─ default.jpg         background of the "No games found" screen (optional)
   ├─ logos/                 (optional)
   │  ├─ GBA.png             logo shown instead of the game list title
   │  └─ SFC.png
   └─ theme.txt              (optional) darkness and name mapping
```

- Only files directly inside `backgrounds` and `logos` are read. Subfolders in them are ignored.
- Only `.jpg`, `.jpeg` and `.png` are recognized (any letter case).
- `backgrounds` alone or `logos` alone is fine. If both are empty, choosing the theme changes nothing.

## 4. Picture specs

### Backgrounds

| Item | Recommended |
|---|---|
| Size | **1280 × 720** (16:9), the Switch screen size |
| Format | JPG (smaller, loads faster). Use PNG only if you need it |
| File size | 500 KB or less per picture |

- Pictures that are not 16:9 are **scaled to fill the screen, keeping the aspect ratio, and cropped around the center.** Keep important parts in the middle.
- The game list text is drawn over the background, on the left (list) and the right (box art and screenshot). Darken busy or bright pictures with `dim`.
- While you scroll quickly, new backgrounds are not loaded; they load when the cursor stops (about 0.15 s). Large pictures take longer to appear.

### Logos

| Item | Recommended |
|---|---|
| Format | **PNG with transparent background** |
| Display area | **440 × 60** box at the top center |
| Size | About 7:1 fills the box, e.g. 880 × 120 |

- Logos are scaled down to fit inside the 440 × 60 box, keeping the aspect ratio. Tall logos look small.
- Categories without a logo show the category name as text.

### Darkness (dim)

A black layer over the background. Write `dim=0~255` in `theme.txt`.

| Value | Result |
|---|---|
| `0` | picture as is |
| `40` | slightly darker (JoyBasic) |
| `150` | quite dark (**default when there is no `theme.txt` or no `dim` line**) |
| `255` | fully black |

## 5. Naming pictures — the order the launcher looks for them

For each category the launcher uses the **first picture it finds** in this order. Backgrounds and logos are looked up separately.

1. A picture mapped in `theme.txt` to the **category name** (or its `shortname`) — always first
2. Category with one ROM folder: a picture mapped to the **ROM folder name**, then a file with the ROM folder name
3. A file named like the category `shortname` or the **category name**
4. Category merged from several ROM folders: the picture of the first folder that has one (mapping first, then file name)

Rules:

- **English letter case is ignored.** `GBA.jpg` = `gba.jpg`
- Otherwise names must match **exactly**, including spaces, brackets and hyphens (`FC hack` ≠ `FC_hack`).
- The **ROM folder name** is the folder the ROM file is **directly** in. For `/roms/GBA/hack/a.gba` it is `hack`.
- The **category name** is
  - the `collection:` value of `metadata.pegasus.txt` when there is a Pegasus list;
  - for folders detected without a list, the **Korean name** in the table below (used even with the English interface);
  - plus the `shortname:` value, if the list has one.

### Special names

| Picture name | Used for |
|---|---|
| `favorites` | Favorites category (games added with the − button) |
| `hbfav` | Homebrew Favorites category |
| `default` (or `main`, `all`) | "No games found" screen |

### System names for folders detected without a list

Folders with ROMs but no list file get these category names. Map pictures by this name or by the ROM folder name.

| Category name (English display) | ROM folder names recognized (`*` = starts with) |
|---|---|
| 아케이드 (FBA2012) (Arcade) | fba2012\*, fbalpha2012\* |
| 아케이드 (FBNeo) (Arcade) | fbneo\*, fba\*, arcade\*, cps1, cps2, cps3, neogeo, finalburn\* |
| 아케이드 (MAME) (Arcade) | mame\* |
| NAOMI | naomi\*, atomiswave\* |
| 드림캐스트 (Dreamcast) | dc, dreamcast, segadreamcast |
| PlayStation | psx, ps1, playstation, playstation1, ps |
| PSP | psp, pspminis |
| GBA | gba, gameboyadvance |
| 게임보이 (Game Boy) | gb, gbc, gameboy, gameboycolor |
| 패미컴 (Famicom) | nes, fc, famicom, fds |
| 슈퍼패미컴 (Super Famicom) | snes, sfc, superfamicom, supernintendo |
| 메가드라이브 (Mega Drive) | md, megadrive, genesis, segacd, megacd, mdcd |
| 32X | 32x, sega32x |
| 마스터시스템 (Master System) | sms, mastersystem, mark3, segamastersystem |
| 게임기어 (Game Gear) | gg, gamegear |
| PC엔진 (PC Engine) | pce, pcengine, tg16, turbografx\*, pcecd, supergrafx, sgx |
| 원더스완 (WonderSwan) | ws, wsc, wonderswan\* |
| 네오지오 포켓 (Neo Geo Pocket) | ngp, ngpc, neogeopocket\* |
| NDS | nds, nintendods |
| N64 | n64, nintendo64 |
| 새턴 (Saturn) | ss, saturn, segasaturn |
| PC-98 | pc98, pc9801 |
| DOS | dos, msdos |
| MSX | msx, msx1, msx2 |
| 아타리 링크스 (Atari Lynx) | lynx, atarilynx |
| 버추얼보이 (Virtual Boy) | vb, virtualboy |
| PC-FX | pcfx |

> Write the Korean name (without the English part in brackets) in `theme.txt`. Mapping by ROM folder name (e.g. `snes=SFC`) is easier and works the same.
>
> A folder like `FBNEO ACT` is also detected by its first word (`FBNEO`). Map its picture by the **full folder name** (`FBNEO ACT`) or by the category name.

## 6. Writing theme.txt

```ini
# Comment lines start with #, ; or [
dim=60

[category name -> picture]
# backgrounds/arcade_action.jpg, logos/arcade_action.png
액션-아케이드=arcade_action
# category merged from several folders
Play Station=PS1

[ROM folder name -> picture]
# /roms/snes -> backgrounds/SFC.jpg
snes=SFC
genesis=MD
```

- Format: `<category or ROM folder name>=<picture file name>`
- The picture name is the file name in `backgrounds/` and `logos/`. **The extension may be left out or written.** (`SFC` = `SFC.jpg`)
- One line maps **both background and logo**. Without a logo picture only the background changes.
- If the mapped picture does not exist, the line is skipped and the next step (section 5) is tried.
- Spaces around `=` and at the end of a line are ignored.
- A comment may follow a value: `snes=SFC   # Super Famicom` (`#` or `;` **after a space**). **v0.9.1 and older** read such a comment as part of the picture name, so put comments on their own line if the theme must also work there.
- `dim` is case-insensitive (`DIM=60` works)
- Save as **UTF-8** (Windows Notepad: "Save as → Encoding UTF-8"; a BOM is fine).
- If the same name appears twice, the lower line wins.

> **Map non-ASCII (e.g. Korean) category names in theme.txt.** Files with non-ASCII names are sometimes not created when copied over the Switch FTP. Keep picture files ASCII and write `<category>=<ascii picture>`.

## 7. Copy to the SD card and apply

1. Copy the theme folder into `/JoyLauncher/themes/` (card reader or FTP).
   - **Use ASCII letters and digits** for folder and file names.
2. In the launcher: **Y Options → Theme** — the new folder is listed right away (sorted by name).
3. Pick it and press **A**. The choice is saved as `theme=` in `settings.txt` and kept on the next start.
4. **"Default (no background)"** turns themes off.

### After changing or adding pictures

The launcher reads the picture list when a theme is chosen. If you add or replace pictures while the launcher is running, **choose the same theme again in Theme and press A** (or restart the launcher).

## 8. Reusing Pegasus theme pictures

Picture folders of Pegasus themes can be put into a theme folder as they are.

| Folders read | Purpose |
|---|---|
| `backgrounds/`, `Background_image1/`, `Resource/Background_image1/` | backgrounds |
| `logos/`, `Logo1/`, `Resource/Logo1/` | logos |

If the same name exists in several folders, the leftmost folder in the table wins. When picture names differ from your category or ROM folder names, map them in `theme.txt`.

## 9. Customizing the included packs, naming a theme

- **"Download <pack name> again"** in the theme picker replaces the stock pictures of Basic Pack (`JoyBasic`), Retro Pack (`RetroPack`) or Byeokgol Kim Pack (`ByeokgolKim`) with new ones (pictures you added stay).
- To change stock pictures, **copy the folder under another name (e.g. `JoyBasicMy`)** and edit the copy.
- The name shown in the theme picker comes from `name=My Theme` in `theme.txt` (`name_en=` for the English interface); without it the folder name is shown. (from v1.0.0)
- JoyBasic can be regenerated with `tools/make_joybasic.py` (Python + Pillow) in the repository — a starting point for a theme with your own colors or text.

## 10. Troubleshooting

| Problem | Check |
|---|---|
| Folder not in the theme picker | Path is `/JoyLauncher/themes/<folder>/` (not one level deeper), folder name does not start with `.` |
| Choosing the theme changes nothing | Spelling of `backgrounds/`·`logos/`, extensions (jpg·jpeg·png) |
| One category has no background | Picture name matches the ROM folder or category name exactly (section 5); otherwise map it in `theme.txt` |
| Wrong picture shown | An earlier step in section 5 matched. Map the category name directly in `theme.txt` |
| Korean category mapping fails | `theme.txt` is UTF-8, the name matches the one shown with the Korean interface |
| Old picture after replacing it | Choose the theme again and press A (section 7) |
| Background appears slowly | Shrink pictures to 1280 × 720 JPG |
| Text hard to read | Raise `dim` |

## 11. Copyright

Company logos, game screenshots and pictures from other people's themes may be fine for personal use, but **distributing them can infringe copyright.** If you share a theme, use only pictures you made or are allowed to use.
