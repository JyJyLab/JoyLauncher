#!/usr/bin/env python3
"""테마 빌더 미리보기용 가짜 롬 폴더 (응애팩 형식 폴더 이름 + 페가수스 metadata + 박스·화면 그림)

    python3 tools/make_sample_roms.py <출력 폴더> <글꼴 ttf>
"""
import os, random, sys
from PIL import Image, ImageDraw, ImageFont

out, fontp = sys.argv[1], sys.argv[2]
font = ImageFont.truetype(fontp, 26)
small = ImageFont.truetype(fontp, 16)
# 폴더, 카테고리 이름, 확장자, 게임 수
SYSTEMS = [
    ("ACT", "액션-아케이드", "zip", 24), ("FTG", "대전-아케이드", "zip", 18), ("STG", "슈팅-아케이드", "zip", 16),
    ("NEOGEO", "네오지오", "zip", 12), ("FC", "패미컴", "zip", 30), ("FDS", "패미컴 디스크 시스템", "fds", 6),
    ("SFC", "슈퍼패미컴", "zip", 32), ("GB", "게임보이", "zip", 14), ("GBC", "게임보이 컬러", "zip", 10),
    ("GBA", "게임보이 어드밴스", "zip", 27), ("MD", "메가드라이브", "zip", 26), ("SMS", "마스터시스템", "zip", 8),
    ("GG", "게임기어", "zip", 7), ("MD-CD", "메가 CD", "chd", 5), ("MD-32X", "메가드라이브 32X", "zip", 4),
    ("PCE", "PC엔진", "zip", 15), ("PCE-CD", "PC 엔진 CD", "chd", 6), ("PS1", "플레이스테이션", "chd", 22),
    ("PSP", "PSP", "iso", 10), ("WS", "원더스완", "zip", 5), ("WSC", "원더스완 컬러", "zip", 8),
    ("NGPC", "네오지오 포켓 컬러", "zip", 6), ("MSX", "MSX", "zip", 6), ("MSX2", "MSX2", "zip", 9),
    ("PC-98", "PC-98", "zip", 4), ("DOS", "DOS", "zip", 5), ("POKE MINI", "포켓몬 미니", "zip", 3),
]
A = ["Super", "Mega", "Star", "Dragon", "Final", "Battle", "Legend", "Night", "Cyber", "Thunder", "Royal", "Magic", "Iron", "Neo"]
B = ["Quest", "Racer", "Fighter", "World", "Story", "Force", "Blaster", "Kingdom", "Hunter", "Wars", "Striker", "Island"]
K = ["용사", "전설", "모험", "대전", "레이싱", "퍼즐", "슈팅", "액션", "기사", "마법", "왕국", "탐험"]
DEV = ["Joy Soft", "Retro Works", "Pixel Studio", "Arcade Lab", "Blue Sky", "Moon Games", "Kite Software"]
GENRE = ["액션", "슈팅", "대전", "RPG", "퍼즐", "레이싱", "스포츠", "어드벤처", "플랫포머"]
random.seed(11)
for folder, coll, ext, n in SYSTEMS:
    d = os.path.join(out, folder)
    for sub in ("media/boxFront", "media/screenshot"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    meta = ["collection: %s" % coll, "launch: retroarch {file.path}", ""]
    for i in range(n):
        title = "%s %s %s" % (random.choice(A), random.choice(B), random.choice(K) if i % 3 == 0 else str(i + 1))
        if i == 3:
            title += " - 아주 긴 제목을 가진 게임의 이름이 여기에 길게 이어집니다"
        f = "%s.%s" % (title.replace(":", ""), ext)
        open(os.path.join(d, f), "wb").write(b"x")
        meta.append("game: " + title)
        meta.append("file: " + f)
        if i % 5 != 4:
            meta.append("developer: " + random.choice(DEV))
            meta.append("release: %d-%02d-01" % (random.randint(1985, 2004), random.randint(1, 12)))
            meta.append("players: %d" % random.choice([1, 1, 2, 2, 4]))
            meta.append("genre: " + random.choice(GENRE))
            meta.append("description: %s 의 세계를 무대로 한 %s 게임입니다. 미리보기용 가짜 설명으로, 여러 줄 표시와 긴 글 자르기를 확인할 수 있습니다." % (title, random.choice(GENRE)))
        h = random.random()
        col = Image.new("HSV", (1, 1), (int(h * 255), 150, 190)).convert("RGB").getpixel((0, 0))
        if i % 7 != 5:
            im = Image.new("RGB", (300, 420), col)
            dr = ImageDraw.Draw(im)
            dr.rectangle([8, 8, 291, 411], outline=(255, 255, 255), width=3)
            dr.text((20, 150), title[:14], font=font, fill=(255, 255, 255))
            dr.text((20, 380), coll, font=small, fill=(255, 255, 255))
            im.save(os.path.join(d, "media/boxFront", f.rsplit(".", 1)[0] + ".png"))
            meta.append("assets.boxFront: media/boxFront/" + f.rsplit(".", 1)[0] + ".png")
        if i % 3 != 2:
            im = Image.new("RGB", (320, 240), tuple(c // 2 for c in col))
            dr = ImageDraw.Draw(im)
            for k in range(7):
                x, y = random.randrange(290), random.randrange(210)
                dr.rectangle([x, y, x + 30, y + 20], fill=tuple(min(255, c + 70) for c in col))
            dr.text((10, 10), "SCREEN " + title[:16], font=small, fill=(255, 255, 255))
            im.save(os.path.join(d, "media/screenshot", f.rsplit(".", 1)[0] + ".png"))
            meta.append("assets.screenshot: media/screenshot/" + f.rsplit(".", 1)[0] + ".png")
        meta.append("")
    open(os.path.join(d, "metadata.pegasus.txt"), "w", encoding="utf-8").write("\n".join(meta))
print("ok", len(SYSTEMS))
