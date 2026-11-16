#!/usr/bin/env python3
"""테마 빌더 그림 라이브러리 만들기: 보유한 테마 그림을 ES-DE 기종 이름으로 맞춰 세트별로 모은다.

    python3 tools/build_library.py <출력 폴더> <응애팩 Resource 폴더> <CanvasPC 폴더> <Horizon 폴더> <Iconic 폴더>

세트: JoyBasic(기본테마), Retro(응애팩), Canvas(벽골김씨테마 원본), Horizon, Iconic
- 이름은 source/esde.cpp 의 기종 이름 표로 바꾼다 (FC -> nes, 액션-아케이드 -> arcade_action …)
- 크기: 배경 1280x720 이하(JPG), 로고 640x200 이하, 아이콘·게임기 그림 512 이하 (빌더가 넣을 때와 같은 기준)
"""
import os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
src = open(os.path.join(ROOT, "source", "esde.cpp"), encoding="utf-8").read()
ALIAS = {k: v for k, v in re.findall(r'\{"([^"]+)", "([^"]+)"\}', src.split("kAlias")[1])}
IDS = set(re.findall(r'\{"([^"]+)", "[^"]*", "[^"]*"\}', src.split("kAlias")[0]))
LIMIT = {"backgrounds": (1280, 720, True), "logos": (640, 200, False), "icons": (512, 512, False), "controllers": (512, 512, False)}


def norm(stem):
    s = stem.lower().strip()
    if s in IDS:
        return s
    return ALIAS.get(s, s)


def save(srcpath, outdir, name, kind):
    try:
        im = Image.open(srcpath)
        im.load()
    except Exception as e:
        print("skip", srcpath, e)
        return False
    w, h, cover = LIMIT.get(kind, (1280, 720, False))
    sc = max(w / im.width, h / im.height) if cover else min(w / im.width, h / im.height)
    sc = min(1.0, sc)
    if im.mode not in ("RGB", "RGBA"):
        im = im.convert("RGBA")
    if sc < 1.0:
        im = im.resize((max(1, round(im.width * sc)), max(1, round(im.height * sc))), Image.LANCZOS)
    alpha = im.mode == "RGBA" and im.getextrema()[3][0] < 250
    os.makedirs(outdir, exist_ok=True)
    for e in (".png", ".jpg"):
        p = os.path.join(outdir, name + e)
        if os.path.exists(p):
            os.remove(p)
    if alpha:
        im.save(os.path.join(outdir, name + ".png"), optimize=True)
    else:
        im.convert("RGB").save(os.path.join(outdir, name + ".jpg"), quality=85)
    return True


def add_dir(out, set_, kind, d, prefer=None):
    if not os.path.isdir(d):
        return 0
    n = 0
    done = set()
    files = sorted(os.listdir(d), key=lambda f: (" hack" in f.lower() or "-msu1" in f.lower(), f))  # 원판 먼저
    for f in files:
        st, ext = os.path.splitext(f)
        if ext.lower() not in (".png", ".jpg", ".jpeg") or f.startswith("."):
            continue
        name = norm(st)
        if not re.fullmatch(r"[a-z0-9_\-]+", name):  # 한자 등 이름 그대로 못 쓰는 것
            continue
        if name in done:
            continue
        if save(os.path.join(d, f), os.path.join(out, set_, kind), name, kind):
            done.add(name)
            n += 1
    print(set_, kind, n)
    return n


def copy_fonts(out, set_, d):
    import shutil
    if not os.path.isdir(d):
        return
    for f in os.listdir(d):
        if f.lower().endswith((".ttf", ".otf")):
            os.makedirs(os.path.join(out, set_, "fonts"), exist_ok=True)
            shutil.copy(os.path.join(d, f), os.path.join(out, set_, "fonts", f))
        if f.lower().endswith((".txt", ".pdf")):
            os.makedirs(os.path.join(out, set_, "fonts"), exist_ok=True)
            shutil.copy(os.path.join(d, f), os.path.join(out, set_, "fonts", f))


def main():
    out, eungae, canvas, horizon, iconic = sys.argv[1:6]
    jb = os.path.join(ROOT, "themes", "JoyBasic")
    for k in ("backgrounds", "logos", "icons"):
        add_dir(out, "JoyBasic", k, os.path.join(jb, k))
    add_dir(out, "Retro", "backgrounds", os.path.join(eungae, "Background_image1"))
    add_dir(out, "Retro", "logos", os.path.join(eungae, "Logo1"))
    for k in ("backgrounds", "logos", "icons", "ui"):
        add_dir(out, "Canvas", k, os.path.join(canvas, k))
    add_dir(out, "Canvas", "icons", os.path.join(ROOT, "themes", "ByeokgolKim", "icons"))  # 즐겨찾기 카드 2장
    copy_fonts(out, "Canvas", os.path.join(canvas, "fonts"))
    for k in ("backgrounds", "logos", "icons", "ui"):
        add_dir(out, "Horizon", k, os.path.join(horizon, k))
    copy_fonts(out, "Horizon", os.path.join(horizon, "fonts"))
    for k in ("backgrounds", "logos", "icons", "controllers"):
        add_dir(out, "Iconic", k, os.path.join(iconic, k))
    for s, txt in (("Horizon", os.path.join(horizon, "CREDITS-LICENSE.txt")), ("Iconic", os.path.join(iconic, "CREDITS-LICENSE.txt"))):
        if os.path.exists(txt):
            import shutil
            shutil.copy(txt, os.path.join(out, s, "CREDITS-LICENSE.txt"))
    open(os.path.join(out, "README.txt"), "w", encoding="utf-8").write(
        "테마 빌더 그림 라이브러리 (시험판)\n"
        "JoyBasic: Joy Launcher 기본테마 / Retro: 응애팩 페가수스 테마 그림 / Canvas: ES-DE Canvas (Siddy212, CC0)\n"
        "Horizon·Iconic: ES-DE 테마 그림 (CC BY-NC-SA, 각 폴더 CREDITS-LICENSE.txt). 게임기 사진·로고·상표의 권리는 각 권리자에게 있음.\n")


main()
