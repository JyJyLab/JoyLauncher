#!/usr/bin/env python3
"""예시 테마 2종 만들기 (JoyCarousel 가로 캐러셀, JoyWheel 세로 휠): layout.txt·theme.txt·UI 그림 + 라이브러리 그림 복사

    python3 make_examples.py <라이브러리 폴더> <출력 themes 폴더>
"""
import os, shutil, sys
from PIL import Image, ImageDraw, ImageFilter

lib, out = sys.argv[1], sys.argv[2]
CORE = ["default", "favorites", "hbfav", "arcade", "arcade_action", "arcade_casual", "arcade_fighting", "arcade_flying", "arcade_shooting",
        "neogeo", "nes", "fds", "snes", "gb", "gbc", "gba", "nds", "megadrive", "mastersystem", "gamegear", "segacd", "sega32x",
        "pcengine", "pcenginecd", "psx", "psp", "wswan", "wswanc", "ngp", "ngpc", "msx1", "msx2", "pc98", "dos", "pokemini"]
HERE = os.path.dirname(os.path.abspath(__file__))


def fill(theme, kind, set_):
    src = os.path.join(lib, set_, kind)
    dst = os.path.join(out, theme, kind)
    os.makedirs(dst, exist_ok=True)
    n = 0
    for f in os.listdir(src):
        st = os.path.splitext(f)[0].lower()
        if st in CORE:
            shutil.copy(os.path.join(src, f), os.path.join(dst, f))
            n += 1
    return n


def rounded(w, h, r, fill, shadow=0):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if shadow:
        sh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(sh).rounded_rectangle([shadow, shadow, w - shadow, h - shadow], r, fill=(0, 0, 0, 150))
        im = sh.filter(ImageFilter.GaussianBlur(shadow / 2))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([shadow, shadow - 4, w - shadow, h - shadow - 4], r, fill=fill)
    else:
        d.rounded_rectangle([0, 0, w - 1, h - 1], r, fill=fill)
    return im


def ring(w, h, r, color, width):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle([6, 6, w - 7, h - 7], r, outline=color + (200,), width=width + 6)
    im = glow.filter(ImageFilter.GaussianBlur(5))
    ImageDraw.Draw(im).rounded_rectangle([8, 8, w - 9, h - 9], r, outline=color + (255,), width=width)
    return im


def write(theme, name, text):
    p = os.path.join(out, theme, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w", encoding="utf-8").write(text)


for t in ("JoyCarousel", "JoyWheel"):
    shutil.rmtree(os.path.join(out, t), ignore_errors=True)
    os.makedirs(os.path.join(out, t, "ui"), exist_ok=True)
    os.makedirs(os.path.join(out, t, "fonts"), exist_ok=True)
    shutil.copy(os.path.join(lib, "Canvas", "fonts", "regular.ttf"), os.path.join(out, t, "fonts", "regular.ttf"))
    fill(t, "backgrounds", "JoyBasic")
    fill(t, "logos", "JoyBasic")
    fill(t, "icons", "Canvas")
    shutil.copy(os.path.join(HERE, t + ".layout.txt"), os.path.join(out, t, "layout.txt"))
    shutil.copy(os.path.join(HERE, t + ".theme.txt"), os.path.join(out, t, "theme.txt"))
    shutil.copy(os.path.join(HERE, "README.txt"), os.path.join(out, t, "README.txt"))

rounded(300, 300, 34, (250, 250, 252, 255), shadow=14).save(os.path.join(out, "JoyCarousel", "ui", "card.png"))
ring(300, 300, 40, (255, 196, 64), 6).save(os.path.join(out, "JoyCarousel", "ui", "selector.png"))
rounded(1280, 330, 0, (0, 0, 0, 0)).save(os.path.join(out, "JoyCarousel", "ui", "blank.png"))
grad = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
gd = ImageDraw.Draw(grad)
for y in range(720):
    a = int(max(0, (y - 300) / 420) * 210)
    gd.line([0, y, 1280, y], fill=(8, 10, 20, a))
grad.save(os.path.join(out, "JoyCarousel", "ui", "shade.png"))
# 휠: 왼쪽 어두운 띠 + 선택 막대
band = Image.new("RGBA", (520, 720), (0, 0, 0, 0))
bd = ImageDraw.Draw(band)
for x in range(520):
    a = int(220 * (1 - max(0, x - 380) / 140)) if x > 380 else 220
    bd.line([x, 0, x, 720], fill=(10, 12, 24, a))
band.save(os.path.join(out, "JoyWheel", "ui", "band.png"))
rounded(480, 120, 18, (255, 255, 255, 40)).save(os.path.join(out, "JoyWheel", "ui", "pill.png"))
ring(500, 140, 26, (120, 200, 255), 4).save(os.path.join(out, "JoyWheel", "ui", "selector.png"))
rounded(600, 64, 14, (120, 200, 255, 70)).save(os.path.join(out, "JoyWheel", "ui", "rowsel.png"))
print("ok")
