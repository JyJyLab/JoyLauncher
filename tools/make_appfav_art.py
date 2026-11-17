#!/usr/bin/env python3
"""안드로이드판 '앱 즐겨찾기' 그림(appfav) 만들기: 기본테마·레트로테마의 배경과 로고.
(벽골김씨테마·레트로게임갤러리테마의 카드 = tools/make_favorite_cards.py appfav)

    python3 tools/make_appfav_art.py

make_extra_art.py 의 세트별 그리기 함수를 그대로 써서 hbfav 그림과 같은 모양으로 만든다.
스위치판은 appfav 그림을 쓰지 않으므로(hbfav 사용) 테마에 함께 넣어 두어도 서로 영향이 없다.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# make_extra_art 는 인자(라이브러리 폴더, 글꼴)를 읽고 바로 돈다: 빈 폴더를 주어 그리기 함수만 빌려 쓴다
sys.argv = [sys.argv[0], tempfile.mkdtemp(), os.path.join(HERE, "fonts", "Montserrat-Variable.ttf")]
sys.path.insert(0, HERE)
import make_extra_art as m  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

COLOR = (58, 150, 190)
orig_glyph = m.glyph


def glyph(kind, size, color=(255, 255, 255)):
    """appfav: 휴대 게임기(둥근 네모 + 화면 테두리 + 양쪽 버튼) 안에 별"""
    if kind != "appfav":
        return orig_glyph(kind, size, color)
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = size
    w, h = s * 0.9, s * 0.5
    x0, y0 = (s - w) / 2, (s - h) / 2
    lw = max(2, int(s * 0.035))
    m.rr(d, [x0, y0, x0 + w, y0 + h], int(h * 0.3), outline=color, width=lw)
    d.rectangle([x0 + w * 0.22, y0 + h * 0.16, x0 + w * 0.78, y0 + h * 0.84], outline=color, width=lw)
    r = s * 0.035
    for cx, cy in ((x0 + w * 0.11, y0 + h * 0.36), (x0 + w * 0.89, y0 + h * 0.64)):
        d.ellipse([cx - r * 1.5, cy - r * 1.5, cx + r * 1.5, cy + r * 1.5], fill=color)
    for cx, cy in ((x0 + w * 0.11, y0 + h * 0.68), (x0 + w * 0.89, y0 + h * 0.34)):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)
    st = orig_glyph("star", int(s * 0.26), color)
    im.alpha_composite(st, (int(s / 2 - s * 0.13), int(s / 2 - s * 0.13)))
    return im


m.glyph = glyph
# (영문 글자, 부제, 한글, 색): 부제가 GAMES 면 배경의 큰 글자는 영문 글자만 쓴다
m.IDS["appfav"] = ("APP FAVORITES", "GAMES", "앱 즐겨찾기", COLOR)

out = [
    ("JoyBasic", "backgrounds", m.joybasic_bg, "jpg"), ("JoyBasic", "logos", m.joybasic_logo, "png"),
    ("RetroPack", "backgrounds", m.retro_bg, "jpg"), ("RetroPack", "logos", m.retro_logo, "png"),
]
for theme, kind, fn, ext in out:
    im = fn("appfav")
    p = os.path.join(ROOT, "themes", theme, kind, "appfav." + ext)
    if ext == "jpg":
        im.convert("RGB").save(p, quality=88)
    else:
        im.save(p, optimize=True)
    print(p, im.size)
