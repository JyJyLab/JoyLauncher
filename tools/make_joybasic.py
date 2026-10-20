#!/usr/bin/env python3
"""Joy Launcher 기본 테마 "JoyBasic" 생성기.

배경은 색 그라디언트와 단순한 도형, 로고는 나눔고딕 Bold 글자로만 만든다.
회사 로고·상표 그림·게임 화면은 쓰지 않으므로 함께 배포해도 저작권 문제가 없다.
(나눔고딕은 SIL Open Font License — 저장소의 OFL.txt)

사용: python3 tools/make_joybasic.py [출력 폴더]   (기본: themes/JoyBasic)
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "romfs", "font_bold.ttf")
W, H = 1280, 720

# 계열별 색 (왼쪽 위 → 오른쪽 아래)
PAL = {
    "nin_home": ((150, 30, 45), (40, 10, 30)),
    "nin_hand": ((110, 40, 140), (25, 15, 55)),
    "sega": ((25, 70, 170), (10, 18, 55)),
    "sony": ((40, 50, 120), (12, 12, 35)),
    "nec": ((200, 90, 30), (60, 20, 15)),
    "snk": ((170, 130, 30), (50, 30, 10)),
    "bandai": ((20, 130, 130), (8, 35, 45)),
    "pc": ((30, 120, 70), (10, 35, 25)),
    "arcade": ((90, 30, 130), (20, 10, 40)),
    "etc": ((70, 80, 95), (18, 20, 28)),
    "act": ((180, 35, 35), (45, 10, 20)),
    "cas": ((40, 150, 80), (10, 40, 30)),
    "fly": ((30, 120, 190), (10, 30, 60)),
    "ftg": ((210, 100, 20), (60, 20, 10)),
    "stg": ((120, 50, 190), (25, 10, 55)),
    "default": ((45, 55, 95), (14, 16, 30)),
}

# (파일 이름, 로고 글자, 색 계열). 파일 이름은 응애팩 등 흔한 롬 폴더 이름과 같게 (ASCII)
SYSTEMS = [
    ("default", "JOY LAUNCHER", "default"),
    ("favorites", "FAVORITES", "snk"),
    ("arcade_action", "ACTION ARCADE", "act"),
    ("arcade_casual", "CASUAL ARCADE", "cas"),
    ("arcade_flying", "FLYING ARCADE", "fly"),
    ("arcade_fighting", "FIGHTING ARCADE", "ftg"),
    ("arcade_shooting", "SHOOTING ARCADE", "stg"),
    ("ARCADE", "ARCADE", "arcade"),
    ("MODEL2", "MODEL 2", "arcade"),
    ("MODEL3", "MODEL 3", "arcade"),
    ("NAOMI", "NAOMI", "arcade"),
    ("FC", "FC", "nin_home"),
    ("FC hack", "FC HACK", "nin_home"),
    ("SFC", "SFC", "nin_home"),
    ("SFC hack", "SFC HACK", "nin_home"),
    ("SFC-MSU1", "SFC MSU-1", "nin_home"),
    ("N64", "N64", "nin_home"),
    ("NGC", "NGC", "nin_home"),
    ("GB", "GB", "nin_hand"),
    ("GBC", "GBC", "nin_hand"),
    ("GBA", "GBA", "nin_hand"),
    ("NDS", "NDS", "nin_hand"),
    ("VB", "VB", "nin_hand"),
    ("POKE MINI", "POKE MINI", "nin_hand"),
    ("GAME WATCH", "GAME & WATCH", "nin_hand"),
    ("SMS", "SMS", "sega"),
    ("SEGA-MS", "SMS", "sega"),
    ("GG", "GG", "sega"),
    ("MD", "MD", "sega"),
    ("MD hack", "MD HACK", "sega"),
    ("MD-CD", "MD-CD", "sega"),
    ("SEGA-CD", "MD-CD", "sega"),
    ("MD-32X", "32X", "sega"),
    ("SEGA-32X", "32X", "sega"),
    ("SS", "SS", "sega"),
    ("DC", "DC", "sega"),
    ("PS1", "PS1", "sony"),
    ("PS2", "PS2", "sony"),
    ("PSP", "PSP", "sony"),
    ("PCE", "PCE", "nec"),
    ("PCE-CD", "PCE-CD", "nec"),
    ("PC-FX", "PC-FX", "nec"),
    ("PC-98", "PC-98", "nec"),
    ("NGPC", "NGPC", "snk"),
    ("NEOGEO-CD", "NEOGEO CD", "snk"),
    ("WS", "WS", "bandai"),
    ("WSC", "WSC", "bandai"),
    ("LYNX", "LYNX", "etc"),
    ("ATARI", "ATARI", "etc"),
    ("3DO", "3DO", "etc"),
    ("CD-i", "CD-i", "etc"),
    ("PICO-8", "PICO-8", "etc"),
    ("OPENBOR", "OPENBOR", "etc"),
    ("MSX", "MSX", "pc"),
    ("MSX2", "MSX2", "pc"),
    ("DOS", "DOS", "pc"),
    ("PC", "PC", "pc"),
]

# theme.txt: 카테고리·롬 폴더 이름 → 그림 파일 이름 (대소문자 무시)
ALIASES = [
    ("카테고리 이름 (응애팩 등)", [
        ("액션-아케이드", "arcade_action"), ("캐주얼-아케이드", "arcade_casual"), ("비행-아케이드", "arcade_flying"),
        ("대전-아케이드", "arcade_fighting"), ("슈팅-아케이드", "arcade_shooting"), ("Play Station", "PS1"),
    ]),
    ("런처가 폴더로 추정한 기종 이름", [
        ("아케이드 (FBNeo)", "ARCADE"), ("아케이드 (MAME)", "ARCADE"), ("드림캐스트", "DC"), ("게임보이", "GB"),
        ("패미컴", "FC"), ("슈퍼패미컴", "SFC"), ("메가드라이브", "MD"), ("마스터시스템", "SMS"), ("게임기어", "GG"),
        ("PC엔진", "PCE"), ("원더스완", "WSC"), ("네오지오 포켓", "NGPC"), ("새턴", "SS"), ("아타리 링크스", "LYNX"),
        ("버추얼보이", "VB"),
    ]),
    ("롬 폴더 이름", [
        ("snes", "SFC"), ("superfamicom", "SFC"), ("nes", "FC"), ("famicom", "FC"), ("fds", "FC"),
        ("megadrive", "MD"), ("genesis", "MD"), ("segacd", "MD-CD"), ("megacd", "MD-CD"), ("sega32x", "MD-32X"),
        ("32x", "MD-32X"), ("mastersystem", "SMS"), ("gamegear", "GG"), ("saturn", "SS"), ("dreamcast", "DC"),
        ("psx", "PS1"), ("ps", "PS1"), ("pcengine", "PCE"), ("tg16", "PCE"), ("pcecd", "PCE-CD"), ("pcfx", "PC-FX"),
        ("pc98", "PC-98"), ("ngp", "NGPC"), ("neogeopocket", "NGPC"), ("neogeocd", "NEOGEO-CD"),
        ("wonderswan", "WS"), ("wonderswancolor", "WSC"), ("gameboy", "GB"), ("gameboycolor", "GBC"),
        ("gameboyadvance", "GBA"), ("virtualboy", "VB"), ("pokemini", "POKE MINI"), ("gw", "GAME WATCH"),
        ("lynx", "LYNX"), ("atarilynx", "LYNX"), ("atari2600", "ATARI"), ("cdi", "CD-i"), ("pico8", "PICO-8"),
        ("fbneo", "ARCADE"), ("fba", "ARCADE"), ("mame", "ARCADE"), ("mame2003plus", "ARCADE"), ("neogeo", "ARCADE"),
        ("cps1", "ARCADE"), ("cps2", "ARCADE"), ("cps3", "ARCADE"), ("dos", "DOS"),
    ]),
]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background(key, label, fam):
    c1, c2 = PAL[fam]
    rnd = random.Random(key)  # 같은 이름이면 늘 같은 그림
    # 대각선 그라디언트 (작게 그려서 키움 → 부드럽고 빠름)
    sw, sh = 160, 90
    g = Image.new("RGB", (sw, sh))
    px = g.load()
    for y in range(sh):
        for x in range(sw):
            t = (x / sw * 0.6 + y / sh * 0.4)
            px[x, y] = lerp(c1, c2, min(1.0, t))
    img = g.resize((W, H), Image.BICUBIC).convert("RGBA")
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    # 큰 반투명 원 몇 개
    for _ in range(4):
        r = rnd.randint(160, 420)
        cx, cy = rnd.randint(-100, W + 100), rnd.randint(-100, H + 100)
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, rnd.randint(8, 18)))
    # 가는 사선 무늬
    step = 46
    for i in range(-H, W + H, step):
        d.line((i, 0, i + H, H), fill=(255, 255, 255, 7), width=2)
    img = Image.alpha_composite(img, over.filter(ImageFilter.GaussianBlur(1.5)))
    # 오른쪽 아래에 큰 글자 워터마크
    wm = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wm)
    size = 260 if len(label) <= 4 else max(110, int(1150 / max(1, len(label)) * 1.6))
    font = ImageFont.truetype(FONT, size)
    bb = wd.textbbox((0, 0), label, font=font)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    wd.text((W - tw - 50 - bb[0], H - th - 70 - bb[1]), label, font=font, fill=(255, 255, 255, 22))
    img = Image.alpha_composite(img, wm)
    return img.convert("RGB")


def logo(label):
    font = ImageFont.truetype(FONT, 104)
    tmp = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    bb = tmp.textbbox((0, 0), label, font=font)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    pad = 16
    img = Image.new("RGBA", (tw + pad * 2, th + pad * 2), (0, 0, 0, 0))
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((pad - bb[0] + 4, pad - bb[1] + 5), label, font=font, fill=(0, 0, 0, 170))
    img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(5)))
    ImageDraw.Draw(img).text((pad - bb[0], pad - bb[1]), label, font=font, fill=(255, 255, 255, 255))
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "themes", "JoyBasic")
    bgd, lgd = os.path.join(out, "backgrounds"), os.path.join(out, "logos")
    os.makedirs(bgd, exist_ok=True)
    os.makedirs(lgd, exist_ok=True)
    for key, label, fam in SYSTEMS:
        background(key, label, fam).save(os.path.join(bgd, key + ".jpg"), quality=88, optimize=True)
        if key != "default":
            logo(label).save(os.path.join(lgd, key + ".png"), optimize=True)
    lines = [
        "# JoyBasic — Joy Launcher 기본 테마 (자체 제작, 자유롭게 사용·수정 가능)",
        "# JoyBasic — Joy Launcher default theme (self-made, free to use and modify)",
        "# 배경은 색 그라디언트, 로고는 나눔고딕 Bold 글자로 만들었습니다 (tools/make_joybasic.py).",
        "",
        "# dim = 배경 어둡게 (0~255) / darken backgrounds",
        "dim=40",
        "",
        "# <카테고리 또는 롬 폴더 이름>=<그림 파일 이름>  (backgrounds/·logos/ 안, 확장자 생략)",
        "# <category or rom folder name>=<image file name>  (in backgrounds/ and logos/, no extension)",
    ]
    for title, pairs in ALIASES:
        lines += ["", "[" + title + "]"] + [f"{k}={v}" for k, v in pairs]
    with open(os.path.join(out, "theme.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"JoyBasic: {len(SYSTEMS)} backgrounds, {len(SYSTEMS) - 1} logos -> {out}")


if __name__ == "__main__":
    main()
