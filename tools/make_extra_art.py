#!/usr/bin/env python3
"""라이브러리 세트에 빠진 공통 그림 만들기: default(전체)·favorites(즐겨찾기)·hbfav(홈브루 즐겨찾기)
와 아케이드 세분류 5개(arcade_action·casual·fighting·flying·shooting).

    python3 tools/make_extra_art.py <라이브러리 폴더> <Montserrat 가변 ttf> [--force]

세트마다 원래 그림 모양을 따라 만든다 (그 세트에 있는 종류만, 이미 있는 그림은 건드리지 않음 — --force 면 다시 만듦).
- JoyBasic: 배경 = 색 그러데이션 + 동그라미 + 큰 흐린 글자, 로고 = 흰 굵은 글자 + 그림자
- Retro: 배경 = 어두운 바탕 + 네온 빛, 로고 = 아이콘 + 한글 글자
- Canvas: 로고 = 흰 기울임 굵은 글자, 아이콘 = 흰 카드 (아케이드 카드의 아래 글자만 바꾸거나 새 카드)
- Horizon: 배경 = 위 색 → 아래 어둠, 로고 = 흰 글자, 아이콘 = 세로 그림 (아케이드 그림 색 입히기 / 기호 그림)
- Iconic: 배경 = 아케이드·기본 배경에 색 입히기, 로고·아이콘 = 색 글자, 게임기 = 아케이드 기계·폴더·스위치 그림
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont

LIB, MONT = sys.argv[1], sys.argv[2]
FORCE = "--force" in sys.argv
HERE = os.path.dirname(os.path.abspath(__file__))
KOFONT = os.path.join(HERE, "..", "romfs", "font_bold.ttf")

IDS = {
    # id: (영문 글자, 부제, 한글, 색)
    "default": ("ALL GAMES", "JOY LAUNCHER", "전체 게임", (59, 111, 216)),
    "favorites": ("FAVORITES", "GAMES", "즐겨찾기", (224, 169, 28)),
    "hbfav": ("HOMEBREW", "FAVORITES", "홈브루 즐겨찾기", (228, 64, 95)),
    "arcade_action": ("ACTION", "ARCADE", "액션 아케이드", (211, 58, 44)),
    "arcade_casual": ("CASUAL", "ARCADE", "캐주얼 아케이드", (58, 166, 91)),
    "arcade_fighting": ("FIGHTING", "ARCADE", "대전 아케이드", (224, 112, 42)),
    "arcade_flying": ("FLYING", "ARCADE", "비행 아케이드", (42, 155, 216)),
    "arcade_shooting": ("SHOOTING", "ARCADE", "슈팅 아케이드", (138, 75, 216)),
}
KINDS = {"JoyBasic": ["backgrounds", "logos"], "Retro": ["backgrounds", "logos"], "Canvas": ["logos", "icons"],
         "Horizon": ["backgrounds", "logos", "icons"], "Iconic": ["backgrounds", "logos", "icons", "controllers"]}


def font(size, weight=800, italic=False):
    path = MONT.replace("Montserrat[", "Montserrat-Italic[") if italic else MONT
    if italic and not os.path.exists(path):
        path = os.path.join(os.path.dirname(MONT), "Montserrat-Italic.ttf")
    f = ImageFont.truetype(path, size)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def kofont(size):
    return ImageFont.truetype(KOFONT, size)


def find(set_, kind, id_):
    d = os.path.join(LIB, set_, kind)
    for e in (".png", ".jpg"):
        p = os.path.join(d, id_ + e)
        if os.path.exists(p):
            return p
    return None


def save(im, set_, kind, id_):
    d = os.path.join(LIB, set_, kind)
    os.makedirs(d, exist_ok=True)
    for e in (".png", ".jpg"):
        p = os.path.join(d, id_ + e)
        if os.path.exists(p):
            os.remove(p)
    if im.mode == "RGBA" and im.getextrema()[3][0] < 250:
        im.save(os.path.join(d, id_ + ".png"), optimize=True)
    else:
        im.convert("RGB").save(os.path.join(d, id_ + ".jpg"), quality=88)


def text_size(d, s, f):
    b = d.textbbox((0, 0), s, font=f)
    return b[2] - b[0], b[3] - b[1], b[0], b[1]


def fit_font(s, maxw, size, weight=800, italic=False, ko=False):
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    while size > 8:
        f = kofont(size) if ko else font(size, weight, italic)
        if text_size(d, s, f)[0] <= maxw:
            return f
        size -= 2
    return f


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def tint(im, color, amount=0.55):
    """색 입히기: 밝기는 두고 색을 accent 쪽으로"""
    im = im.convert("RGBA")
    g = ImageEnhance.Color(im.convert("RGB")).enhance(0.25)
    over = Image.new("RGB", im.size, color)
    t = Image.blend(g, ImageChops.multiply(g, over), amount)
    t = Image.blend(t, ImageChops.screen(t, Image.new("RGB", im.size, shade(color, 0.35))), 0.35)
    out = t.convert("RGBA")
    out.putalpha(im.getchannel("A"))
    return out


def rr(d, box, radius, **kw):
    """둥근 네모 (작은 크기에서도 반지름이 넘치지 않게)"""
    x0, y0, x1, y1 = box
    r = max(0, min(int(radius), int(min(x1 - x0, y1 - y0) / 2) - 2))
    if r <= 0:
        d.rectangle(box, **kw)
    else:
        d.rounded_rectangle(box, radius=r, **kw)


# ---------------------------------------------------------------- 기호 (별·스위치·격자·아케이드 기호)
def glyph(kind, size, color=(255, 255, 255)):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = size
    if kind in ("favorites", "star"):
        pts = []
        for i in range(10):
            r = s * (0.46 if i % 2 == 0 else 0.2)
            a = -math.pi / 2 + i * math.pi / 5
            pts.append((s / 2 + r * math.cos(a), s / 2 + r * math.sin(a)))
        d.polygon(pts, fill=color)
    elif kind == "hbfav":  # 스위치 본체 모양 + 작은 별
        w, h = s * 0.86, s * 0.5
        x0, y0 = (s - w) / 2, (s - h) / 2
        jw = w * 0.17
        rr(d, [x0, y0, x0 + jw, y0 + h], int(jw * 0.42), fill=color)
        rr(d, [x0 + w - jw, y0, x0 + w, y0 + h], int(jw * 0.42), fill=color)
        d.rectangle([x0 + jw + s * 0.02, y0 + s * 0.02, x0 + w - jw - s * 0.02, y0 + h - s * 0.02], outline=color, width=max(2, int(s * 0.03)))
        st = glyph("star", int(s * 0.32), color)
        im.alpha_composite(st, (int(s / 2 - s * 0.16), int(s / 2 - s * 0.16)))
    elif kind == "default":  # 2x2 격자
        g = s * 0.08
        c = (s - g * 3) / 2
        for i in range(2):
            for j in range(2):
                x, y = g + i * (c + g), g + j * (c + g)
                rr(d, [x, y, x + c, y + c], int(c * 0.18), fill=color)
    elif kind == "arcade_action":  # 번개
        d.polygon([(s * .58, s * .06), (s * .22, s * .56), (s * .46, s * .56), (s * .38, s * .94), (s * .8, s * .4), (s * .54, s * .4), (s * .66, s * .06)], fill=color)
    elif kind == "arcade_casual":  # 공
        d.ellipse([s * .18, s * .18, s * .82, s * .82], outline=color, width=int(s * .07))
        d.arc([s * .18, s * .35, s * .82, s * .65], 0, 360, fill=color, width=int(s * .05))
        d.ellipse([s * .42, s * .42, s * .58, s * .58], fill=color)
    elif kind == "arcade_fighting":  # VS
        f = font(int(s * 0.62), 900, italic=True)
        w, h, ox, oy = text_size(d, "VS", f)
        d.text(((s - w) / 2 - ox, (s - h) / 2 - oy), "VS", font=f, fill=color)
    elif kind == "arcade_flying":  # 비행기
        d.polygon([(s * .5, s * .1), (s * .56, s * .45), (s * .9, s * .6), (s * .9, s * .67), (s * .56, s * .6), (s * .55, s * .8),
                   (s * .66, s * .88), (s * .66, s * .93), (s * .5, s * .88), (s * .34, s * .93), (s * .34, s * .88), (s * .45, s * .8),
                   (s * .44, s * .6), (s * .1, s * .67), (s * .1, s * .6), (s * .44, s * .45)], fill=color)
    elif kind == "arcade_shooting":  # 과녁
        for r, w in ((0.4, 0.06), (0.26, 0.06)):
            d.ellipse([s * (.5 - r), s * (.5 - r), s * (.5 + r), s * (.5 + r)], outline=color, width=int(s * w))
        d.ellipse([s * .44, s * .44, s * .56, s * .56], fill=color)
        d.line([(s * .5, s * .02), (s * .5, s * .98)], fill=color, width=int(s * .035))
        d.line([(s * .02, s * .5), (s * .98, s * .5)], fill=color, width=int(s * .035))
    return im


# ---------------------------------------------------------------- 세트별
def joybasic_bg(id_):
    en, sub, ko, col = IDS[id_]
    W, H = 1280, 720
    im = Image.new("RGB", (W, H))
    top, bot = shade(col, 0.95), shade(col, 0.32)
    px = im.load()
    for y in range(H):
        c = mix(top, bot, y / H)
        for x in range(W):
            px[x, y] = c
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    for cx, cy, r, a in ((300, 120, 260, 26), (980, 520, 360, 20), (640, 260, 180, 14), (120, 620, 220, 16)):
        od.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, a))
    im = Image.alpha_composite(im.convert("RGBA"), ov)
    label = en if sub in ("JOY LAUNCHER", "GAMES") else en + " " + sub
    if id_ == "hbfav":
        label = "HOMEBREW FAVORITES"
    f = fit_font(label, W - 120, 120, 600)
    tx = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(tx)
    w, h, ox, oy = text_size(d, label, f)
    d.text(((W - w) / 2 - ox, H - h - 70 - oy), label, font=f, fill=(255, 255, 255, 50))
    im.alpha_composite(tx)
    return im.convert("RGB")


def joybasic_logo(id_):
    en, sub, ko, col = IDS[id_]
    label = {"default": "ALL GAMES", "hbfav": "HOMEBREW"}.get(id_, en if not id_.startswith("arcade_") else en + " " + sub)
    f = font(84, 700)
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    w, h, ox, oy = text_size(d, label, f)
    im = Image.new("RGBA", (w + 24, 113), (0, 0, 0, 0))
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).text((12 - ox + 3, (113 - h) / 2 - oy + 4), label, font=f, fill=(0, 0, 0, 170))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(3)))
    ImageDraw.Draw(im).text((12 - ox, (113 - h) / 2 - oy), label, font=f, fill=(255, 255, 255, 255))
    return im


def retro_bg(id_):
    en, sub, ko, col = IDS[id_]
    W, H = 1280, 720
    im = Image.new("RGB", (W, H), (14, 14, 20))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([340, 60, 940, 660], fill=col + (150,))
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    im = Image.alpha_composite(im.convert("RGBA"), glow)
    # 바닥 격자 (레트로 네온)
    d = ImageDraw.Draw(im)
    for i in range(-12, 13):
        d.line([(640 + i * 30, 470), (640 + i * 140, 720)], fill=col + (90,), width=2)
    for k in range(7):
        y = 470 + int((k / 6) ** 1.8 * 250)
        d.line([(0, y), (W, y)], fill=col + (70,), width=2)
    g = glyph(id_, 260, (255, 255, 255))
    gl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gl.alpha_composite(g, (510, 150))
    im.alpha_composite(gl.filter(ImageFilter.GaussianBlur(10)).point(lambda v: v) )
    im.alpha_composite(gl)
    return im.convert("RGB")


def retro_logo(id_):
    en, sub, ko, col = IDS[id_]
    W, H = 640, 160
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    g = glyph(id_, 120, col)
    bg = Image.new("RGBA", (140, 140), (0, 0, 0, 0))
    ImageDraw.Draw(bg).rounded_rectangle([4, 4, 136, 136], radius=24, fill=(255, 255, 255, 235))
    bg.alpha_composite(g, (10, 10))
    im.alpha_composite(bg, (10, 10))
    f = fit_font(ko, W - 190, 70, ko=True)
    d = ImageDraw.Draw(im)
    w, h, ox, oy = text_size(d, ko, f)
    for dx, dy in ((-3, 0), (3, 0), (0, -3), (0, 3), (-2, -2), (2, 2), (-2, 2), (2, -2)):
        d.text((170 - ox + dx, (H - h) / 2 - oy + dy), ko, font=f, fill=(20, 20, 30, 255))
    d.text((170 - ox, (H - h) / 2 - oy), ko, font=f, fill=(255, 255, 255, 255))
    return im


def canvas_logo(id_):
    en, sub, ko, col = IDS[id_]
    label = en if id_ in ("favorites", "default") else (en + " " + sub if id_.startswith("arcade_") else "HOMEBREW")
    f = font(96, 900, italic=True)
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    w, h, ox, oy = text_size(d, label, f)
    W = min(500, w + 20)
    if w + 20 > 500:
        f = fit_font(label, 480, 96, 900, italic=True)
        w, h, ox, oy = text_size(d, label, f)
    im = Image.new("RGBA", (500, max(120, h + 30)), (0, 0, 0, 0))
    ImageDraw.Draw(im).text(((500 - w) / 2 - ox, (im.height - h) / 2 - oy), label, font=f, fill=(255, 255, 255, 255))
    return im


def canvas_card(id_):
    en, sub, ko, col = IDS[id_]
    base = find("Canvas", "icons", "arcade")
    if id_.startswith("arcade_") and base:
        im = Image.open(base).convert("RGBA")
        d = ImageDraw.Draw(im)
        bgc = im.getpixel((200, 440))
        d.rectangle([40, 380, 360, 412], fill=bgc)
        f = font(22, 700)
        w, h, ox, oy = text_size(d, en, f)
        d.text(((400 - w) / 2 - ox, 386 - oy), en, font=f, fill=(95, 95, 100, 255))
        # 오른쪽 위 작은 기호
        g = glyph(id_, 54, col)
        im.alpha_composite(g, (318, 30))
        return im
    # 새 카드: 흰 둥근 카드 + 기호 + 글자 (Canvas 카드 모양)
    im = Image.new("RGBA", (400, 500), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([12, 12, 388, 488], radius=26, fill=(250, 250, 251, 255), outline=(225, 225, 230, 255), width=2)
    g = glyph(id_, 190, col)
    im.alpha_composite(g, (105, 80))
    f = fit_font(en, 300, 46, 900)
    w, h, ox, oy = text_size(d, en, f)
    d.text(((400 - w) / 2 - ox, 330 - oy), en, font=f, fill=(60, 60, 66, 255))
    f2 = font(22, 700)
    w, h, ox, oy = text_size(d, sub, f2)
    d.text(((400 - w) / 2 - ox, 388 - oy), sub, font=f2, fill=(110, 110, 118, 255))
    return im


def horizon_bg(id_):
    en, sub, ko, col = IDS[id_]
    W, H = 1280, 720
    im = Image.new("RGB", (W, H))
    top, bot = shade(col, 0.85), (16, 16, 20)
    px = im.load()
    for y in range(H):
        c = mix(top, bot, min(1, y / (H * 0.95)))
        for x in range(W):
            px[x, y] = c
    return im


def horizon_logo(id_):
    en, sub, ko, col = IDS[id_]
    label = en + (" " + sub if id_.startswith("arcade_") else "")
    if id_ == "hbfav":
        label = "HOMEBREW"
    f = fit_font(label, 620, 72, 800)
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    w, h, ox, oy = text_size(d, label, f)
    im = Image.new("RGBA", (min(640, w + 10), h + 16), (0, 0, 0, 0))
    ImageDraw.Draw(im).text(((im.width - w) / 2 - ox, 8 - oy), label, font=f, fill=(255, 255, 255, 255))
    return im


def horizon_icon(id_):
    en, sub, ko, col = IDS[id_]
    W, H = 261, 512
    base = find("Horizon", "icons", "arcade")
    if id_.startswith("arcade_") and base:
        im = tint(Image.open(base).convert("RGBA").resize((W, H)), col, 0.7)
    else:
        im = Image.new("RGBA", (W, H))
        px = im.load()
        for y in range(H):
            c = mix(shade(col, 1.05), shade(col, 0.25), y / H)
            for x in range(W):
                px[x, y] = c + (255,)
        g = glyph(id_, 190, (255, 255, 255))
        im.alpha_composite(g, ((W - 190) // 2, 130))
    # 아래 이름 띠
    d = ImageDraw.Draw(im)
    band = Image.new("RGBA", (W, 90), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    for y in range(90):
        bd.line([(0, y), (W, y)], fill=(0, 0, 0, int(200 * y / 90)))
    im.alpha_composite(band, (0, H - 90))
    f = fit_font(en, W - 30, 34, 800)
    w, h, ox, oy = text_size(d, en, f)
    d.text(((W - w) / 2 - ox, H - 50 - oy), en, font=f, fill=(255, 255, 255, 255))
    return im


def iconic_bg(id_):
    en, sub, ko, col = IDS[id_]
    src = find("Iconic", "backgrounds", "arcade" if id_.startswith("arcade_") else ("switch" if id_ == "hbfav" else "default"))
    im = Image.open(src).convert("RGBA").resize((1280, 720)) if src else Image.new("RGBA", (1280, 720), shade(col, 0.5) + (255,))
    im = tint(im, col, 0.45)
    if id_ == "favorites":
        ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
        for x, y, s in ((980, 120, 120), (1120, 300, 70), (860, 360, 60), (1060, 520, 90)):
            ov.alpha_composite(glyph("star", s, (255, 230, 140)), (x, y))
        im.alpha_composite(ov)
    return im.convert("RGB")


def iconic_logo(id_, w=216, h=88):
    en, sub, ko, col = IDS[id_]
    label = en if not id_.startswith("arcade_") else en
    im = Image.new("RGBA", (w * 3, h * 3), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bs = int(h * 3 * 0.8)
    badge = Image.new("RGBA", (bs, bs), (0, 0, 0, 0))
    bd = ImageDraw.Draw(badge)
    bd.ellipse([0, 0, bs - 1, bs - 1], fill=(255, 255, 255, 255))
    bd.ellipse([8, 8, bs - 9, bs - 9], fill=col + (255,))
    g = glyph(id_, int(bs * 0.62), (255, 255, 255))
    badge.alpha_composite(g, ((bs - g.width) // 2, (bs - g.height) // 2))
    im.alpha_composite(badge, (6, int(h * 3 * 0.1)))
    gx = bs + 18
    f = fit_font(label, w * 3 - gx - 10, int(h * 3 * 0.55), 900, italic=True)
    tw, th, ox, oy = text_size(d, label, f)
    y = (h * 3 - th) / 2 - oy - (h * 0.25 if id_.startswith("arcade_") else 0)
    d.text((gx - ox, y), label, font=f, fill=col + (255,), stroke_width=7, stroke_fill=(255, 255, 255, 255))
    if id_.startswith("arcade_") or id_ == "hbfav":
        s2 = "ARCADE" if id_.startswith("arcade_") else "FAVORITES"
        f2 = fit_font(s2, w * 3 - gx - 10, int(h * 3 * 0.24), 800)
        sw, sh2, ox2, oy2 = text_size(d, s2, f2)
        d.text((gx - ox2, y + th + 10 - oy2 + oy), s2, font=f2, fill=(60, 60, 70, 255), stroke_width=4, stroke_fill=(255, 255, 255, 255))
    bb = im.getbbox() or (0, 0, im.width, im.height)
    im = im.crop(bb)
    im.thumbnail((w, h), Image.LANCZOS)
    return im


def iconic_icon(id_):
    lg = iconic_logo(id_, 210, 90)
    im = Image.new("RGBA", (220, 220), (0, 0, 0, 0))
    im.alpha_composite(lg, ((220 - lg.width) // 2, (220 - lg.height) // 2))
    return im


def iconic_controller(id_):
    en, sub, ko, col = IDS[id_]
    if id_.startswith("arcade_"):
        src = find("Iconic", "controllers", "arcade")
        im = Image.open(src).convert("RGBA")
        g = glyph(id_, int(im.height * 0.3), col)
        badge = Image.new("RGBA", (g.width + 20, g.height + 20), (0, 0, 0, 0))
        ImageDraw.Draw(badge).ellipse([0, 0, badge.width - 1, badge.height - 1], fill=(255, 255, 255, 240))
        badge.alpha_composite(g, (10, 10))
        im.alpha_composite(badge, (im.width - badge.width - 6, im.height - badge.height - 6))
        return im
    src = find("Iconic", "controllers", "switch" if id_ == "hbfav" else "default")
    im = Image.open(src).convert("RGBA")
    g = glyph("star" if id_ != "default" else "default", int(im.height * 0.38), col)
    im.alpha_composite(g, ((im.width - g.width) // 2, (im.height - g.height) // 2 + int(im.height * 0.05)))
    return im


MAKERS = {
    ("JoyBasic", "backgrounds"): joybasic_bg, ("JoyBasic", "logos"): joybasic_logo,
    ("Retro", "backgrounds"): retro_bg, ("Retro", "logos"): retro_logo,
    ("Canvas", "logos"): canvas_logo, ("Canvas", "icons"): canvas_card,
    ("Horizon", "backgrounds"): horizon_bg, ("Horizon", "logos"): horizon_logo, ("Horizon", "icons"): horizon_icon,
    ("Iconic", "backgrounds"): iconic_bg, ("Iconic", "logos"): iconic_logo, ("Iconic", "icons"): iconic_icon,
    ("Iconic", "controllers"): iconic_controller,
}

made = []
for set_, kinds in KINDS.items():
    if not os.path.isdir(os.path.join(LIB, set_)):
        continue
    # 이 도구가 만든 그림 목록 (원래 그림은 --force 여도 건드리지 않음)
    mf = os.path.join(LIB, set_, "generated.txt")
    gen = set(open(mf, encoding="utf-8").read().split()) if os.path.exists(mf) else set()
    for kind in kinds:
        for id_ in IDS:
            key = f"{kind}/{id_}"
            if find(set_, kind, id_) and (key not in gen or not FORCE):
                continue
            im = MAKERS[(set_, kind)](id_)
            save(im, set_, kind, id_)
            gen.add(key)
            made.append(f"{set_}/{key}")
    if gen:
        open(mf, "w", encoding="utf-8").write("\n".join(sorted(gen)) + "\n")
print(len(made), "개 만듦")
for m in made:
    print(" ", m)
