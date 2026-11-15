#!/usr/bin/env python3
"""벽골김씨테마(ByeokgolKim) 그림 준비: 원본 CanvasPC 테마 폴더의 그림을 런처에 맞는 크기로 옮긴다.

    python3 tools/make_byeokgolkim.py <CanvasPC 폴더> [themes/ByeokgolKim]

- icons/ 는 400x500 (카드 칸의 약 2배), ui/card.png 는 같은 비율로 줄인다 (스위치에서 처음 읽는 시간 단축).
- 배경은 JPG, 로고·선택 테두리·목록 판·글꼴은 그대로.
- theme.txt · layout.txt · README.txt 는 themes/ByeokgolKim 에 있는 것을 그대로 둔다 (손으로 쓴 파일).
"""
import os
import shutil
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "themes", "ByeokgolKim")
    for d in ("icons", "logos", "ui", "backgrounds", "fonts"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    n = 0
    for name in sorted(os.listdir(os.path.join(src, "icons"))):
        if not name.lower().endswith(".png"):
            continue
        im = Image.open(os.path.join(src, "icons", name)).convert("RGBA")
        if im.width > 400:
            im = im.resize((400, round(im.height * 400 / im.width)), Image.LANCZOS)
        im.save(os.path.join(out, "icons", name), optimize=True)
        n += 1
    card = Image.open(os.path.join(src, "ui", "card.png")).convert("RGBA")
    card.resize((card.width // 2, card.height // 2), Image.LANCZOS).save(os.path.join(out, "ui", "card.png"), optimize=True)
    shutil.copyfile(os.path.join(src, "ui", "selector.png"), os.path.join(out, "ui", "selector.png"))
    shutil.copyfile(os.path.join(src, "ui", "gamelist-background-light-medium.png"), os.path.join(out, "ui", "panel.png"))
    for name in sorted(os.listdir(os.path.join(src, "logos"))):
        if name.lower().endswith(".png"):
            shutil.copyfile(os.path.join(src, "logos", name), os.path.join(out, "logos", name))
    Image.open(os.path.join(src, "backgrounds", "default.png")).convert("RGB").save(
        os.path.join(out, "backgrounds", "default.jpg"), quality=90)
    shutil.copyfile(os.path.join(src, "fonts", "regular.ttf"), os.path.join(out, "fonts", "regular.ttf"))
    shutil.copyfile(os.path.join(src, "Canvas-original-LICENSE"), os.path.join(out, "LICENSE-Canvas.txt"))
    print(f"ByeokgolKim: {n} icons -> {out}")


if __name__ == "__main__":
    main()
