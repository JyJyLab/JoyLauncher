#!/usr/bin/env python3
"""PS2·3DS 기종 카드(400x500) 와 로고를 자체 제작 (벽골김씨테마·레트로게임갤러리테마용).

    python3 tools/make_ps2_3ds_icons.py

- 카드 바탕은 같은 테마의 psp.png 카드(반투명 흰 카드·그림자)에서 내용만 지워 쓴다.
- 기기 그림은 도형으로 직접 그림 (실제 사진·타사 그림 미사용), 글자는 tools/fonts/Montserrat.
- 결과: themes/ByeokgolKim/icons/{ps2,n3ds}.png, themes/DcGall/icons/{ps2,n3ds}.png,
        themes/ByeokgolKim/logos/{ps2,n3ds}.png (흰 글자 로고, 500x163)
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
FONT = os.path.join(HERE, "fonts", "Montserrat-Variable.ttf")
S = 4  # 4배로 그린 뒤 줄임


def font(size, weight):
    f = ImageFont.truetype(FONT, size * S)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def blank_card(theme):
    im = Image.open(os.path.join(ROOT, "themes", theme, "icons", "psp.png")).convert("RGBA")
    a = np.array(im)
    inner = np.zeros(a.shape[:2], bool)
    inner[20:478, 20:380] = True
    a[inner] = (255, 255, 255, 166)
    return Image.fromarray(a)


def rr(d, box, r, fill, outline=None, width=0):
    d.rounded_rectangle([v * S for v in box], radius=r * S, fill=fill, outline=outline, width=width * S)


def ell(d, cx, cy, rx, ry, fill, outline=None, width=0):
    d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=fill, outline=outline, width=width * S)


def text_center(d, cx, y, s, f, fill):
    w = d.textlength(s, font=f)
    d.text((cx * S - w / 2, y * S), s, font=f, fill=fill)


def ps2_device(d):
    body, edge = (32, 32, 36, 255), (70, 70, 76, 255)
    # 손잡이 두 개 + 몸통
    for cx in (138, 262):
        ell(d, cx, 222, 40, 54, body)
    rr(d, (88, 142, 312, 222), 34, body)
    rr(d, (88, 142, 312, 222), 34, None, edge, 2)
    # 어깨 버튼
    rr(d, (100, 132, 160, 146), 6, (55, 55, 60, 255))
    rr(d, (240, 132, 300, 146), 6, (55, 55, 60, 255))
    # 방향키
    g = (90, 90, 96, 255)
    rr(d, (118, 158, 142, 202), 3, g)
    rr(d, (108, 168, 152, 192), 3, g)
    # 4버튼 (△○×□)
    for (cx, cy, col, sym) in ((278, 158, (60, 180, 140), "tri"), (298, 180, (220, 70, 80), "cir"),
                               (278, 202, (80, 130, 220), "x"), (258, 180, (225, 120, 190), "sq")):
        ell(d, cx, cy, 10, 10, (55, 55, 60, 255))
        c = col + (255,)
        if sym == "tri":
            d.polygon([((cx) * S, (cy - 5) * S), ((cx - 5) * S, (cy + 4) * S), ((cx + 5) * S, (cy + 4) * S)], outline=c, width=2 * S)
        elif sym == "cir":
            ell(d, cx, cy, 5, 5, None, c, 2)
        elif sym == "x":
            d.line([((cx - 4) * S, (cy - 4) * S), ((cx + 4) * S, (cy + 4) * S)], fill=c, width=2 * S)
            d.line([((cx - 4) * S, (cy + 4) * S), ((cx + 4) * S, (cy - 4) * S)], fill=c, width=2 * S)
        else:
            d.rectangle([(cx - 4) * S, (cy - 4) * S, (cx + 4) * S, (cy + 4) * S], outline=c, width=2 * S)
    # 아날로그 스틱
    for cx in (170, 230):
        ell(d, cx, 220, 22, 22, (18, 18, 20, 255), edge, 2)
        ell(d, cx, 220, 14, 14, (40, 40, 44, 255))
    # 가운데 버튼
    rr(d, (178, 176, 194, 184), 3, (80, 80, 86, 255))
    rr(d, (206, 176, 222, 184), 3, (80, 80, 86, 255))
    ell(d, 200, 196, 4, 4, (200, 40, 40, 255))


def n3ds_device(d):
    blue, dark, edge = (38, 72, 160, 255), (20, 22, 28, 255), (90, 120, 200, 255)
    # 윗판
    rr(d, (118, 96, 282, 196), 10, blue)
    rr(d, (118, 96, 282, 196), 10, None, edge, 2)
    rr(d, (132, 108, 268, 186), 4, dark)
    rr(d, (136, 112, 264, 182), 2, (45, 60, 90, 255))
    # 경첩
    rr(d, (124, 196, 276, 206), 4, (28, 52, 120, 255))
    # 아랫판
    rr(d, (114, 206, 286, 300), 10, blue)
    rr(d, (114, 206, 286, 300), 10, None, edge, 2)
    rr(d, (168, 214, 232, 266), 3, dark)
    rr(d, (171, 217, 229, 263), 2, (45, 60, 90, 255))
    # 슬라이드 패드·방향키
    ell(d, 142, 228, 12, 12, (30, 30, 36, 255), (150, 150, 160, 255), 2)
    g = (30, 30, 36, 255)
    rr(d, (137, 252, 147, 280), 2, g)
    rr(d, (128, 261, 156, 271), 2, g)
    # ABXY
    for cx, cy in ((258, 232), (270, 244), (258, 256), (246, 244)):
        ell(d, cx, cy, 6, 6, (230, 230, 235, 255))
    # 홈 버튼들
    for cx in (184, 200, 216):
        rr(d, (cx - 6, 284, cx + 6, 289), 2, (25, 45, 110, 255))


def card(theme, kind):
    im = blank_card(theme)
    big = im.resize((im.width * S, im.height * S), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    ink, sub = (60, 60, 64, 255), (70, 70, 74, 255)
    if kind == "ps2":
        ps2_device(d)
        f, f2 = font(34, 700), font(26, 800)
        w, w2 = d.textlength("PlayStation", font=f), d.textlength("2", font=f2)
        x = 200 * S - (w + 3 * S + w2) / 2
        d.text((x, 332 * S), "PlayStation", font=f, fill=ink)
        d.text((x + w + 3 * S, 326 * S), "2", font=f2, fill=ink)
        text_center(d, 200, 388, "2000", font(16, 700), sub)
    else:
        n3ds_device(d)
        f1, f2 = font(24, 600), font(40, 800)
        w1, w2 = d.textlength("NINTENDO", font=f1), d.textlength("3DS", font=f2)
        x = 200 * S - (w1 + 8 * S + w2) / 2
        d.text((x, 345 * S), "NINTENDO", font=f1, fill=ink)
        d.text((x + w1 + 8 * S, 332 * S), "3DS", font=f2, fill=ink)
        text_center(d, 200, 388, "2011", font(16, 700), sub)
    return big.resize(im.size, Image.LANCZOS)


def logo(kind):
    W, H = 500, 163
    big = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    white = (255, 255, 255, 255)
    if kind == "ps2":
        f = font(64, 600)
        w = d.textlength("PlayStation", font=f)
        x = (W * S - w - 46 * S) / 2
        d.text((x, 44 * S), "PlayStation", font=f, fill=white)
        d.text((x + w + 8 * S, 32 * S), "2", font=font(48, 700), fill=white)
    else:
        f1, f2 = font(44, 600), font(78, 800)
        w1, w2 = d.textlength("NINTENDO", font=f1), d.textlength("3DS", font=f2)
        x = (W * S - w1 - 14 * S - w2) / 2
        d.text((x, 52 * S), "NINTENDO", font=f1, fill=white)
        d.text((x + w1 + 14 * S, 30 * S), "3DS", font=f2, fill=white)
    return big.resize((W, H), Image.LANCZOS)


def main():
    for theme in ("ByeokgolKim", "DcGall"):
        for kind in ("ps2", "n3ds"):
            card(theme, kind).save(os.path.join(ROOT, "themes", theme, "icons", kind + ".png"), optimize=True)
    for kind in ("ps2", "n3ds"):
        logo(kind).save(os.path.join(ROOT, "themes", "ByeokgolKim", "logos", kind + ".png"), optimize=True)
    print("ok")


if __name__ == "__main__":
    main()
