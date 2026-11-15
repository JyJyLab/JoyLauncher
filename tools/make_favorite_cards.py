#!/usr/bin/env python3
"""벽골김씨테마의 '즐겨찾기'·'홈브루 즐겨찾기' 카드 그림 원본(SVG)을 만든다. 자체 제작 그림.

    python3 tools/make_favorite_cards.py favorites favorites.svg
    python3 tools/make_favorite_cards.py hbfav hbfav.svg

- 카드 규격은 themes/ByeokgolKim/icons 의 다른 카드와 같다 (400x500, 흰 바탕 불투명도 65%, 모서리 25).
- SVG 를 PNG 로 바꿀 때는 Montserrat (SIL OFL 1.1) 글꼴이 있는 환경에서 4배(1600x2000)로 그린 뒤 400x500 으로 줄인다.
  (v1.0.1 의 그림은 Chrome --headless --screenshot 으로 그리고 Pillow LANCZOS 로 줄였다.)
- 결과: themes/ByeokgolKim/icons/favorites.png, icons/hbfav.png (파일 이름만으로 카테고리와 연결된다)
"""
import math, sys

def star(cx, cy, R, r, rot=-90):
    pts = []
    for i in range(10):
        a = math.radians(rot + i * 36)
        rad = R if i % 2 == 0 else r
        pts.append(f"{cx + rad * math.cos(a):.2f},{cy + rad * math.sin(a):.2f}")
    return " ".join(pts)

TILES = ["#ff6b5e", "#ffb648", "#5bd08a", "#4aa8ff", "#b47cff", "#ff7fc2", "#35c6bd", "#8f98ad"]

def glyph(i, x, y, s):
    c = 'fill="#fff" fill-opacity=".88"'
    cx, cy = x + s / 2, y + s / 2
    if i == 0:  # 재생 세모
        return f'<polygon points="{cx-5},{cy-7} {cx+8},{cy} {cx-5},{cy+7}" {c}/>'
    if i == 1:  # 십자키
        return f'<path d="M{cx-3},{cy-9}h6v6h6v6h-6v6h-6v-6h-6v-6h6z" {c}/>'
    if i == 2:  # 동그라미
        return f'<circle cx="{cx}" cy="{cy}" r="7.5" fill="none" stroke="#fff" stroke-opacity=".88" stroke-width="4"/>'
    if i == 3:  # 폴더
        return f'<path d="M{cx-9},{cy-6}h7l3,3h8v10h-18z" {c}/>'
    if i == 4:  # 음표
        return (f'<path d="M{cx-3},{cy-9}h9v4h-5v9a4.5,4.5 0 1 1 -4,-4.4z" {c}/>')
    if i == 5:  # 하트
        return (f'<path d="M{cx},{cy+8}l-8,-8a4.7,4.7 0 0 1 8,-5.5a4.7,4.7 0 0 1 8,5.5z" {c}/>')
    if i == 6:  # 줄 3개
        return "".join(f'<rect x="{cx-8}" y="{cy-8+k*6.5}" width="16" height="3.4" rx="1.7" {c}/>' for k in range(3))
    return f'<rect x="{cx-7.5}" y="{cy-7.5}" width="15" height="15" rx="3" {c}/>'  # 네모

def badge(bx, by, br=27):
    """즐겨찾기 별 배지 (흰 테두리 금색 원 + 흰 별)"""
    return (f'<g filter="url(#sh2)"><circle cx="{bx}" cy="{by}" r="{br}" fill="#fff"/><circle cx="{bx}" cy="{by}" r="{br-3.6}" fill="url(#gold)"/></g>'
            f'<polygon points="{star(bx, by+0.8, 15.5, 6.6)}" fill="#fff" stroke="#fff" stroke-width="2.2" stroke-linejoin="round"/>')

def words(big, small):
    """기존 카드의 로고·연도 자리 글자"""
    return (f'<text x="200" y="378" text-anchor="middle" font-family="Montserrat" font-weight="800" font-size="39" letter-spacing="-.6" fill="#454545">{big}</text>'
            f'<text x="200" y="404" text-anchor="middle" font-family="Montserrat" font-weight="700" font-size="18.4" letter-spacing=".3" fill="#454545">{small}</text>')

CARD = '<rect x="12" y="12" width="376" height="476" rx="25" fill="#fff" fill-opacity=".651"/>'

def cart(cx, by, ang, top, bot, label):
    """게임 카트리지 하나 (가로 108, 세로 124, 아래 가운데가 기준점)"""
    gid = f"c{abs(int(cx))}{abs(int(ang))}"
    o = [f'<linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bot}"/></linearGradient>']
    o.append(f'<g transform="translate({cx},{by}) rotate({ang})" filter="url(#sh)">')
    o.append(f'<rect x="-54" y="-124" width="108" height="124" rx="9" fill="url(#{gid})"/>')
    o.append('<rect x="-54" y="-124" width="108" height="5" rx="2.5" fill="#fff" fill-opacity=".22"/>')
    for k in range(3):  # 손잡이 홈
        o.append(f'<rect x="-30" y="{-116 + k * 5.2}" width="60" height="2.2" rx="1.1" fill="#000" fill-opacity=".22"/>')
    o.append('<rect x="-44" y="-97" width="88" height="68" rx="6" fill="#000" fill-opacity=".25"/>')
    o.append(f'<g transform="translate(-41,-94)">{label}</g>')
    o.append('<polygon points="-7,-19 7,-19 0,-9" fill="#000" fill-opacity=".25"/>')
    o.append('</g>')
    return "".join(o)

# 딱지 그림 (82x62)
L_MID = ('<clipPath id="lm"><rect width="82" height="62" rx="4"/></clipPath><g clip-path="url(#lm)">'
         '<rect width="82" height="62" fill="#7fd3ff"/><circle cx="61" cy="17" r="9.5" fill="#ffe066"/>'
         '<path d="M-6,62L20,26L44,62Z" fill="#3fae6a"/><path d="M22,62L52,34L90,62Z" fill="#2c8f56"/>'
         '<rect y="54" width="82" height="8" fill="#256f47"/></g>')
L_LEFT = ('<clipPath id="ll"><rect width="82" height="62" rx="4"/></clipPath><g clip-path="url(#ll)">'
          '<rect width="82" height="62" fill="#3b2a6b"/><circle cx="24" cy="30" r="13" fill="none" stroke="#fff" stroke-opacity=".9" stroke-width="5"/>'
          '<circle cx="12" cy="11" r="1.6" fill="#fff"/><circle cx="40" cy="50" r="1.6" fill="#fff"/><circle cx="52" cy="14" r="1.6" fill="#fff"/></g>')
L_RIGHT = ('<clipPath id="lr"><rect width="82" height="62" rx="4"/></clipPath><g clip-path="url(#lr)">'
           '<rect width="82" height="62" fill="#fff1c9"/><polygon points="46,16 70,31 46,46" fill="#e8553a"/>'
           '<rect x="0" y="52" width="82" height="10" fill="#f2b632"/></g>')

def build_favorites(card=True):
    o = [build(card).split("</defs>")[0] + "</defs>"]  # svg 머리와 공통 정의
    if card:
        o.append(CARD)
    o.append(cart(140, 272, -13, "#45cdc0", "#1f968c", L_LEFT))
    o.append(cart(260, 272, 13, "#ff8163", "#de4a31", L_RIGHT))
    o.append(cart(200, 266, 0, "#4b4c54", "#2a2b30", L_MID))
    o.append(badge(292, 262))
    o.append(words("FAVORITES", "GAMES"))
    o.append('</svg>')
    return "\n".join(o)

def build(card=True):
    o = []
    o.append('<svg xmlns="http://www.w3.org/2000/svg" width="400" height="500" viewBox="0 0 400 500">')
    o.append('''<defs>
<linearGradient id="jl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#22c8f2"/><stop offset="1" stop-color="#00a4d6"/></linearGradient>
<linearGradient id="jr" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff5b4a"/><stop offset="1" stop-color="#ea3526"/></linearGradient>
<linearGradient id="body" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3a3a3f"/><stop offset="1" stop-color="#242428"/></linearGradient>
<linearGradient id="scr" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2a3146"/><stop offset="1" stop-color="#161a26"/></linearGradient>
<linearGradient id="gold" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffd95a"/><stop offset="1" stop-color="#f6a300"/></linearGradient>
<linearGradient id="gloss" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".28"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>
<radialGradient id="stick" cx=".4" cy=".35" r=".7"><stop offset="0" stop-color="#5a5a60"/><stop offset="1" stop-color="#1c1c20"/></radialGradient>
<filter id="sh" x="-20%" y="-20%" width="140%" height="160%"><feGaussianBlur in="SourceAlpha" stdDeviation="5"/><feOffset dy="7"/><feComponentTransfer><feFuncA type="linear" slope=".38"/></feComponentTransfer><feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="sh2" x="-30%" y="-30%" width="160%" height="170%"><feGaussianBlur in="SourceAlpha" stdDeviation="2.2"/><feOffset dy="2.5"/><feComponentTransfer><feFuncA type="linear" slope=".4"/></feComponentTransfer><feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<clipPath id="scrclip"><rect x="114" y="160" width="172" height="98" rx="2"/></clipPath>
</defs>''')
    if card:
        o.append(CARD)
    # ---- 스위치 본체 (50..350 x 145..273)
    o.append('<g filter="url(#sh)">')
    o.append('<path d="M96,145H82A32,32 0 0 0 50,177V241A32,32 0 0 0 82,273H96Z" fill="url(#jl)"/>')
    o.append('<path d="M304,145H318A32,32 0 0 1 350,177V241A32,32 0 0 1 318,273H304Z" fill="url(#jr)"/>')
    o.append('<rect x="96" y="145" width="208" height="128" rx="3" fill="url(#body)"/>')
    o.append('</g>')
    # 조이콘 윗면 반사
    o.append('<path d="M96,145H82A32,32 0 0 0 50,177V190Q52,152 96,149Z" fill="#fff" fill-opacity=".22"/>')
    o.append('<path d="M304,145H318A32,32 0 0 1 350,177V190Q348,152 304,149Z" fill="#fff" fill-opacity=".2"/>')
    # 레일
    o.append('<rect x="95" y="145" width="2.4" height="128" fill="#101014" fill-opacity=".75"/>')
    o.append('<rect x="302.6" y="145" width="2.4" height="128" fill="#101014" fill-opacity=".75"/>')
    # 왼쪽 조이콘: − 버튼, 스틱, 방향 버튼, 캡처
    o.append('<rect x="80" y="155.5" width="9" height="2.8" rx="1" fill="#1e1e22"/>')
    o.append('<circle cx="73" cy="180" r="11.5" fill="#15151a"/><circle cx="73" cy="180" r="9" fill="url(#stick)"/>')
    for dx, dy in ((0, -8.6), (0, 8.6), (-8.6, 0), (8.6, 0)):
        o.append(f'<circle cx="{73+dx}" cy="{218+dy}" r="4.3" fill="#1e1e22"/>')
    o.append('<rect x="76" y="241" width="8.6" height="8.6" rx="1.6" fill="#1e1e22"/>')
    # 오른쪽 조이콘: + 버튼, ABXY, 스틱 (홈 버튼 자리는 별 배지가 덮음)
    o.append('<path d="M311,156.9h3.6v-3.6h2.8v3.6h3.6v2.8h-3.6v3.6h-2.8v-3.6h-3.6z" fill="#1e1e22"/>')
    for dx, dy in ((0, -8.6), (0, 8.6), (-8.6, 0), (8.6, 0)):
        o.append(f'<circle cx="{327+dx}" cy="{180+dy}" r="4.3" fill="#1e1e22"/>')
    o.append('<circle cx="327" cy="218" r="11.5" fill="#15151a"/><circle cx="327" cy="218" r="9" fill="url(#stick)"/>')
    # 화면
    o.append('<rect x="107" y="153" width="186" height="112" rx="4" fill="#0c0c0f"/>')
    o.append('<rect x="114" y="160" width="172" height="98" rx="2" fill="url(#scr)"/>')
    s, gap = 31, 8
    x0 = 114 + (172 - (4 * s + 3 * gap)) / 2
    y0 = 160 + (98 - (2 * s + gap)) / 2
    for i, col in enumerate(TILES):
        x, y = x0 + (i % 4) * (s + gap), y0 + (i // 4) * (s + gap)
        o.append(f'<rect x="{x}" y="{y}" width="{s}" height="{s}" rx="7" fill="{col}"/>')
        o.append(f'<rect x="{x}" y="{y}" width="{s}" height="{s/2}" rx="7" fill="url(#gloss)"/>')
        o.append(glyph(i, x, y, s))
    o.append('<path d="M114,160H230L170,258H114Z" fill="#fff" fill-opacity=".05" clip-path="url(#scrclip)"/>')
    o.append(badge(291, 262))
    o.append(words("HOMEBREW", "FAVORITES"))
    o.append('</svg>')
    return "\n".join(o)

if __name__ == "__main__":
    open(sys.argv[2], "w").write(build_favorites() if sys.argv[1] == "favorites" else build())
