#!/usr/bin/env python3
"""빌더 시작 마법사용: 견본(templates/<t>)마다 builder_info.txt(이름·설명·순서)와 builder_preview.png(카테고리 화면),
그리고 빈 테마 예시 그림 builder_ui/empty_example.png

    python3 make_template_meta.py <빌더 폴더> <런처 데스크톱 실행 파일> <견본 그림이 다 든 themes 폴더(선택)>
"""
import os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont

root, launcher = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
full = sys.argv[3] if len(sys.argv) > 3 else None
INFO = {
    "JoyBasic": (1, "기본 목록", "왼쪽 카테고리 줄 목록 + 오른쪽 박스 사진. 가장 단순하고 가벼움"),
    "JoyCarousel": (2, "가로 캐러셀", "가운데 카드가 크게, 양옆은 작고 흐리게. 게임은 박스 사진 캐러셀, 밀어내기 화면 전환"),
    "JoyWheel": (3, "세로 휠", "왼쪽에 로고가 세로로 도는 휠 + 오른쪽 게임기 그림. 겹쳐 바뀌는 화면 전환"),
    "ByeokgolKim": (4, "카드 격자", "기종 카드 5×2 격자 + 줄무늬 게임 목록 판 (벽골김씨테마)"),
    "DcGall": (5, "게시판 (디시 갤러리)", "갤러리 카드 + 게시판 표(번호·제목·조회) + 개념글 버튼"),
    "RetroPack": (6, "레트로", "기본 목록 구성 + 응애팩 페가수스 배경·로고"),
    "Horizon": (7, "Horizon (ES-DE)", "큰 세로 아트 5칸 + 오른쪽 목록 판. ES-DE 테마 이식 (CC BY-NC-SA)"),
    "Iconic": (8, "Iconic (ES-DE)", "게임기 그림 판 + 아래 로고 띠. ES-DE 테마 이식 (CC BY-NC-SA)"),
}
tdir = os.path.join(root, "templates")
tmp = tempfile.mkdtemp()
os.makedirs(os.path.join(tmp, "themes"))
for t in os.listdir(tdir):
    order, name, desc = INFO.get(t, (50, t, ""))
    open(os.path.join(tdir, t, "builder_info.txt"), "w", encoding="utf-8").write("name=%s\ndesc=%s\norder=%d\n" % (name, desc, order))
    src = os.path.join(full, t) if full and os.path.isdir(os.path.join(full, t)) else os.path.join(tdir, t)
    shutil.copytree(src, os.path.join(tmp, "themes", t))
    out = os.path.join(tdir, t, "builder_preview.png")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", JOY_BATTERY="76", JOY_MARQUEE_MS="0")
    subprocess.run([launcher, "--root", os.path.join(root, "sample", "roms"), "--home", tmp, "--lang", "ko", "--theme", t,
                    "--cat", "9", "--shot", out], cwd=os.path.dirname(os.path.dirname(launcher)) if False else os.path.dirname(launcher),
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    if os.path.exists(out):
        Image.open(out).convert("RGB").resize((640, 360), Image.LANCZOS).save(out)
        print("preview", t)
shutil.rmtree(tmp)
# 빈 테마 예시: 빈 화면 + 점선 자리
os.makedirs(os.path.join(root, "builder_ui"), exist_ok=True)
im = Image.new("RGB", (640, 360), (30, 30, 38))
d = ImageDraw.Draw(im)
font = ImageFont.truetype(os.path.join(root, "romfs", "font.ttf"), 18)
def dashed(box, label):
    x0, y0, x1, y1 = box
    for x in range(x0, x1, 12):
        d.line([x, y0, min(x + 6, x1), y0], fill=(120, 170, 230), width=2)
        d.line([x, y1, min(x + 6, x1), y1], fill=(120, 170, 230), width=2)
    for y in range(y0, y1, 12):
        d.line([x0, y, x0, min(y + 6, y1)], fill=(120, 170, 230), width=2)
        d.line([x1, y, x1, min(y + 6, y1)], fill=(120, 170, 230), width=2)
    w = d.textlength(label, font=font)
    d.text(((x0 + x1 - w) / 2, (y0 + y1) / 2 - 11), label, font=font, fill=(150, 190, 240))
dashed((200, 20, 440, 60), "+ 카테고리 이름")
dashed((40, 100, 600, 260), "+ 목록 (줄·격자·캐러셀·휠)")
dashed((240, 300, 400, 340), "+ 도움말")
dashed((520, 20, 620, 50), "+ 배터리")
im.save(os.path.join(root, "builder_ui", "empty_example.png"))
print("ok")
