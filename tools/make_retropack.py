#!/usr/bin/env python3
"""레트로팩(RetroPack) 만들기: 응애팩 페가수스 테마 그림에서 스위치 런처에 쓰는 기종만 발췌.

    python3 tools/make_retropack.py <Resource 폴더> [themes/RetroPack]

<Resource 폴더> = 응애팩의 pegasus-frontend/themes/Resource (Background_image1, Logo1 이 있는 곳).
- 그림 이름은 기본팩(JoyBasic)과 같은 이름만 고른다 (한자 이름·번호 붙은 중복·다른 기기용 그림 제외).
- 배경은 1280x720 JPG, 로고는 높이 120 이하 PNG 로 줄인다.
- theme.txt 는 기본팩의 이름 연결을 그대로 쓰되, 이 팩에 없는 그림으로 가는 줄은 뺀다.
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
JOYBASIC = os.path.join(HERE, "..", "themes", "JoyBasic")
RENAME = {"액션-아케이드": "arcade_action", "캐주얼-아케이드": "arcade_casual", "비행-아케이드": "arcade_flying",
          "대전-아케이드": "arcade_fighting", "슈팅-아케이드": "arcade_shooting", "전체게임": "default"}
# 기본팩에는 있고 이 팩에는 없는 그림 → 대신 쓸 그림
SUBST = {"arcade": "arcade_action"}


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "themes", "RetroPack")
    keep = {os.path.splitext(n)[0].lower() for n in os.listdir(os.path.join(JOYBASIC, "backgrounds"))}
    os.makedirs(os.path.join(out, "backgrounds"), exist_ok=True)
    os.makedirs(os.path.join(out, "logos"), exist_ok=True)

    def target(n):
        st, ext = os.path.splitext(n)
        if n.startswith(".") or ext.lower() not in (".jpg", ".jpeg", ".png"):
            return None
        st = RENAME.get(st, st)
        return st if st.lower() in keep else None

    have = set()
    for n in sorted(os.listdir(os.path.join(src, "Background_image1"))):
        t = target(n)
        if not t:
            continue
        im = Image.open(os.path.join(src, "Background_image1", n)).convert("RGB")
        if im.size != (1280, 720):
            im = im.resize((1280, 720), Image.LANCZOS)
        im.save(os.path.join(out, "backgrounds", t + ".jpg"), quality=82)
        have.add(t.lower())
    nl = 0
    for n in sorted(os.listdir(os.path.join(src, "Logo1"))):
        t = target(n)
        if not t:
            continue
        im = Image.open(os.path.join(src, "Logo1", n)).convert("RGBA")
        if im.height > 120:
            im = im.resize((int(im.width * 120 / im.height), 120), Image.LANCZOS)
        im.save(os.path.join(out, "logos", t + ".png"), optimize=True)
        nl += 1

    lines = [
        "# 레트로팩 (RetroPack) — Joy Launcher 테마",
        "# 응애팩(안드로이드 페가수스 롬 모음)의 테마 그림에서 발췌했습니다. 그림의 권리는 각 권리자에게 있습니다.",
        "# Excerpt of the theme pictures of the Eungae pack (Android Pegasus collection). Pictures belong to their owners.",
        "",
        "# 테마 선택 창에 보일 이름 / name shown in the theme picker",
        "name=레트로팩",
        "name_en=Retro Pack",
        "",
        "# dim = 배경 어둡게 (0~255) / darken backgrounds",
        "dim=150",
        "",
        "# <카테고리 또는 롬 폴더 이름>=<그림 파일 이름>  (backgrounds/·logos/ 안, 확장자 생략)",
        "# <category or rom folder name>=<image file name>  (in backgrounds/ and logos/, no extension)",
        "",
        "[즐겨찾기 / favorites]",
        "favorites=default",
        "hbfav=default",
    ]
    dropped = []
    for raw in open(os.path.join(JOYBASIC, "theme.txt"), encoding="utf-8").read().splitlines():
        line = raw.strip()
        if line.startswith("["):
            lines += ["", line]
        elif "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            if k.strip().lower() in ("dim", "name", "name_en"):
                continue
            v = SUBST.get(v.strip().lower(), v.strip())
            if v.lower() in have:
                lines.append(f"{k.strip()}={v}")
            else:
                dropped.append(line)
    with open(os.path.join(out, "theme.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"RetroPack: {len(have)} backgrounds, {nl} logos -> {out}")
    if dropped:
        print("not in this pack:", ", ".join(dropped))


if __name__ == "__main__":
    main()
