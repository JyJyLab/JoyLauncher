#include "uistyle.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- 색 계산
static SDL_Color Col(int r, int g, int b, int a = 255) {
    auto c8 = [](int v) { return (Uint8)std::max(0, std::min(255, v)); };
    return {c8(r), c8(g), c8(b), c8(a)};
}

static float Lin(Uint8 v) {
    float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
// 상대 휘도 (WCAG)
static float Lum(SDL_Color c) { return 0.2126f * Lin(c.r) + 0.7152f * Lin(c.g) + 0.0722f * Lin(c.b); }
static float Contrast(SDL_Color a, SDL_Color b) {
    float la = Lum(a), lb = Lum(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05f) / (lb + 0.05f);
}

struct Hsl { float h, s, l; };
static Hsl ToHsl(SDL_Color c) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    Hsl o{0, 0, (mx + mn) / 2};
    float d = mx - mn;
    if (d > 1e-6f) {
        o.s = d / (1 - fabsf(2 * o.l - 1));
        if (mx == r) o.h = 60 * fmodf((g - b) / d, 6);
        else if (mx == g) o.h = 60 * ((b - r) / d + 2);
        else o.h = 60 * ((r - g) / d + 4);
        if (o.h < 0) o.h += 360;
    }
    return o;
}
static SDL_Color FromHsl(Hsl v, Uint8 a = 255) {
    float h = fmodf(v.h, 360), s = std::max(0.0f, std::min(1.0f, v.s)), l = std::max(0.0f, std::min(1.0f, v.l));
    if (h < 0) h += 360;
    float c = (1 - fabsf(2 * l - 1)) * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = l - c / 2;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return Col((int)lroundf((r + m) * 255), (int)lroundf((g + m) * 255), (int)lroundf((b + m) * 255), a);
}
static SDL_Color Mix(SDL_Color a, SDL_Color b, float t) {
    return Col((int)lroundf(a.r + (b.r - a.r) * t), (int)lroundf(a.g + (b.g - a.g) * t), (int)lroundf(a.b + (b.b - a.b) * t),
               (int)lroundf(a.a + (b.a - a.a) * t));
}
// 밝기만 옮겨 bg 와의 대비를 ratio 이상으로 (판이 밝으면 어둡게, 어두우면 밝게). 끝까지 안 되면 검정·흰색
static SDL_Color EnsureContrast(SDL_Color c, SDL_Color bg, float ratio) {
    if (Contrast(c, bg) >= ratio) return c;
    Hsl h = ToHsl(c);
    float step = Lum(bg) > 0.4f ? -0.02f : 0.02f;
    for (int i = 0; i < 50; i++) {
        h.l += step;
        if (h.l < 0 || h.l > 1) break;
        SDL_Color t = FromHsl(h, c.a);
        if (Contrast(t, bg) >= ratio) return t;
    }
    return Lum(bg) > 0.4f ? Col(20, 20, 24, c.a) : Col(240, 240, 245, c.a);
}
static SDL_Color BestOf(SDL_Color bg) {  // 흰 / 검 중 대비가 큰 쪽
    SDL_Color w = Col(240, 240, 245), k = Col(24, 24, 30);
    return Contrast(w, bg) >= Contrast(k, bg) ? w : k;
}

static bool Get(const Layout &l, const char *name, SDL_Color &out) {
    auto it = l.colors.find(name);
    if (it == l.colors.end()) return false;
    out = {it->second.r, it->second.g, it->second.b, it->second.a};
    return true;
}

void MenuStyleFrom(const Layout &l, bool themed, MenuStyle &o) {
    const SDL_Color kWhite = {235, 235, 240, 255}, kGrey = {150, 150, 165, 255}, kAccent = {255, 196, 64, 255},
                    kSel = {70, 70, 96, 255}, kOn = {90, 220, 120, 255};
    o = MenuStyle();
    o.themed = themed;
    if (!themed) {  // 테마 없음: 예전 색 그대로
        o.panel = {28, 28, 40, 245};
        o.border = o.panel;
        o.title = kAccent;
        o.text = o.selText = kWhite;
        o.dim = kGrey;
        o.sel = kSel;
        o.on = kOn;
        o.track = {60, 60, 80, 255};
        o.borderW = 0;
        return;
    }
    SDL_Color c;
    // 판: color.menu → background_color → bar → 기본 어두운 색
    bool own = Get(l, "menu", o.panel);
    if (!own) {
        if (l.hasClear) o.panel = {l.clear.r, l.clear.g, l.clear.b, 255};
        else if (Get(l, "bar", c)) o.panel = c;
        else o.panel = {20, 20, 26, 255};
        o.panel.a = 240;
    }
    bool light = Lum(o.panel) > 0.4f;
    Hsl ph = ToHsl(o.panel);
    // 테두리: 판과 같은 색상, 밝기만 크게 다르게
    if (!Get(l, "menu_border", o.border)) {
        Hsl b = ph;
        b.l = light ? std::max(0.0f, ph.l - 0.32f) : std::min(1.0f, ph.l + 0.32f);
        if (ph.s > 0.08f) b.s = std::max(ph.s, 0.35f);
        o.border = EnsureContrast(FromHsl(b), o.panel, 2.2f);  // 판과 확실히 구분
    }
    // 글자: 테마 글자 색 (판과 대비 4.5:1 미만이면 흰/검)
    if (!Get(l, "menu_text", o.text)) {
        if (!Get(l, "text", o.text)) o.text = light ? Col(34, 34, 40) : kWhite;
        o.text.a = 255;
        if (Contrast(o.text, o.panel) < 4.5f) o.text = BestOf(o.panel);
    }
    if (!Get(l, "menu_dim", o.dim)) {
        if (!Get(l, "dim", o.dim)) o.dim = Mix(o.text, o.panel, 0.35f);
        o.dim.a = 255;
        if (Contrast(o.dim, o.panel) < 2.5f) o.dim = Mix(o.text, o.panel, 0.3f);
    }
    // 제목: 판 색상의 보색 (회색 판이면 테마 강조 색 → 노랑), 대비 4.5:1 맞춤
    if (!Get(l, "menu_title", o.title)) {
        if (ph.s >= 0.2f) {
            Hsl t = {ph.h + 180, std::max(ph.s, 0.6f), light ? 0.35f : 0.68f};
            o.title = FromHsl(t);
        } else if (!Get(l, "accent", o.title)) {
            o.title = kAccent;
        }
        o.title.a = 255;
        o.title = EnsureContrast(o.title, o.panel, 4.5f);
    }
    // 선택 막대: 판과 테두리 사이 색. 글자와 대비 4.5:1 이 안 나오면 판 쪽으로 당김
    bool ownSel = Get(l, "menu_select", o.sel), ownSelText = Get(l, "menu_select_text", o.selText);
    for (float t : {0.38f, 0.3f, 0.22f, 0.15f, 0.1f}) {
        if (!ownSel) o.sel = Mix(o.panel, o.border, t);
        o.sel.a = 255;
        if (!ownSelText) o.selText = Contrast(o.text, o.sel) >= 4.5f ? o.text : BestOf(o.sel);
        if (ownSel || ownSelText || Contrast(o.selText, o.sel) >= 4.5f) break;
    }
    o.on = EnsureContrast(kOn, o.panel, 3.0f);
    o.track = Mix(o.panel, o.border, 0.5f);
    o.track.a = 255;
    o.radius = l.menuRadius >= 0 ? l.menuRadius : 14;
    o.selRadius = std::min(o.radius, 8);
    o.borderW = l.menuBorderW >= 0 ? l.menuBorderW : 2;
}

// ---------------------------------------------------------------- 둥근 귀퉁이
void RoundCornerPixels(int R, int bw, SDL_Color fill, SDL_Color border, std::vector<Uint8> &px) {
    int n = 2 * R;
    px.assign((size_t)n * n * 4, 0);
    float rr = (float)R, inner = (float)(R - bw);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int nf = 0, nb = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float dx = x + (sx + 0.5f) / 4 - rr, dy = y + (sy + 0.5f) / 4 - rr;
                    float d2 = dx * dx + dy * dy;
                    if (d2 > rr * rr) continue;
                    if (bw > 0 && d2 > inner * inner) nb++;
                    else nf++;
                }
            if (nf + nb == 0) continue;
            float wf = nf * fill.a, wb = nb * border.a, ws = wf + wb;
            Uint8 *p = &px[((size_t)y * n + x) * 4];
            if (ws <= 0) continue;
            p[0] = (Uint8)lroundf((fill.r * wf + border.r * wb) / ws);
            p[1] = (Uint8)lroundf((fill.g * wf + border.g * wb) / ws);
            p[2] = (Uint8)lroundf((fill.b * wf + border.b * wb) / ws);
            p[3] = (Uint8)lroundf(ws / 16);
        }
}

// ---------------------------------------------------------------- 도움말 X 버튼 이름
static bool BgmLike(const std::string &s) {
    std::string t;
    for (char ch : s) t += (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    return t.find("bgm") != std::string::npos || t.find("sound") != std::string::npos || t.find("music") != std::string::npos ||
           s.find("소리") != std::string::npos || s.find("음악") != std::string::npos;
}

std::string FixXLabel(const std::string &s, const std::string &label, bool replaceAny) {
    std::string out = s;
    for (size_t p = out.find("[X]"); p != std::string::npos; p = out.find("[X]", p + 3)) {
        size_t b = p + 3;
        while (b < out.size() && out[b] == ' ') b++;
        size_t e = b;
        while (e < out.size() && out[e] != '[' && out[e] != '\n' && !(out[e] == ' ' && e + 1 < out.size() && out[e + 1] == ' ')) e++;
        while (e > b && out[e - 1] == ' ') e--;
        if (e <= b) continue;
        std::string cur = out.substr(b, e - b);
        if (cur == label || (!replaceAny && !BgmLike(cur))) continue;
        out.replace(b, e - b, label);
    }
    return out;
}
