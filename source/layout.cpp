#include "layout.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

// 내장 기본 구성: 테마가 없을 때(그리고 layout.txt 가 없는 테마)의 화면 그대로.
// 화면 크기는 1280x720, rect = 왼쪽, 위, 폭, 높이.
static const char *kDefault = R"LAYOUT(
[categories.background]
type=image
source=background
rect=0,0,1280,720
fit=cover
z=0

[categories.topbar]
type=rect
rect=0,0,1280,76
color=bar
z=10

[categories.bottombar]
type=rect
rect=0,668,1280,52
color=bar
z=10

[categories.list]
type=list
style=text
rect=28,92,720,546
row_height=42
size=27
color=dim
selected_color=text
selection=selection
text_offset=12,8
count=on
count_color=dim
count_size=22
count_offset=18,12
z=30

[categories.preview]
type=preview
rect=800,96,440,510
columns=2
rows=2
gap=10,10
z=30

[categories.title]
type=text
text={app}
font=bold
size=34
color=accent
rect=320,18,640,0
align=center
z=50

[categories.applet]
type=text
text={applet}
size=22
color=#eb3c3c
rect=100,26,0,0
z=50

[categories.battery]
type=battery
rect=1056,28,200,20
color=text
z=50

[categories.hint]
type=text
text={hint}
size=22
color=text
rect=640,680,0,0
align=center
scroll=off
z=50

[games.background]
type=image
source=background
rect=0,0,1280,720
fit=cover
z=0

[games.topbar]
type=rect
rect=0,0,1280,76
color=bar
z=10

[games.bottombar]
type=rect
rect=0,668,1280,52
color=bar
z=10

[games.list]
type=list
style=text
rect=28,92,720,546
row_height=42
size=27
color=dim
selected_color=text
selection=selection
text_offset=12,8
z=30

[games.media]
type=media
rect=800,92,440,300
z=30

[games.info]
type=info
rect=800,404,440,256
color=dim
title_color=text
title_size=27
size=22
desc_size=20
z=30

[games.title]
type=text
text={category}
logo=on
logo_rect=420,8,440,60
font=bold
size=34
color=accent
rect=320,18,640,0
align=center
z=50

[games.applet]
type=text
text={applet}
size=22
color=#eb3c3c
rect=100,26,0,0
z=50

[games.prev]
type=text
text=◀ L
size=22
color=dim
rect=30,26,0,0
z=50

[games.battery]
type=battery
rect=1056,28,200,20
color=text
z=50

[games.pager]
type=pager
size=22
color=dim
z=51

[games.counter]
type=text
text={index} / {count}
size=22
color=dim
rect=40,680,0,0
z=50

[games.hint]
type=text
text={hint}
size=22
color=text
rect=640,680,0,0
align=center
scroll=off
z=50
)LAYOUT";

const char *LayoutDefaultText() { return kDefault; }

const Elem *Screen::Find(const std::string &name) const {
    for (const auto &e : elems)
        if (e.name == name) return &e;
    return nullptr;
}

const Elem *Screen::List() const {
    for (const auto &e : elems)
        if (e.type == ElemType::List && e.visible) return &e;
    return nullptr;
}

namespace {

std::string Lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return s;
}

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ParseInt(const std::string &s, int &out) {
    std::string t = Trim(s);
    if (t.empty()) return false;
    char *end = nullptr;
    long v = strtol(t.c_str(), &end, 10);
    if (*end) return false;
    out = (int)std::max(-100000L, std::min(100000L, v));
    return true;
}

// "a,b,c" -> 정수들. 개수가 n 이 아니면 false
bool ParseInts(const std::string &s, int *out, int n) {
    std::stringstream ss(s);
    std::string part;
    int i = 0;
    while (std::getline(ss, part, ',')) {
        if (i >= n || !ParseInt(part, out[i])) return false;
        i++;
    }
    return i == n;
}

bool ParseBool(const std::string &s, bool &out) {
    std::string t = Lower(Trim(s));
    if (t == "on" || t == "true" || t == "yes" || t == "1") { out = true; return true; }
    if (t == "off" || t == "false" || t == "no" || t == "0") { out = false; return true; }
    return false;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// #rgb, #rrggbb, #rrggbbaa, "r,g,b[,a]", 또는 색 이름
bool ParseColor(const std::string &s, LColor &out) {
    std::string t = Trim(s);
    if (t.empty()) return false;
    if (t[0] == '#') {
        std::string h = t.substr(1);
        for (char c : h)
            if (Hex(c) < 0) return false;
        int v[4] = {0, 0, 0, 255};
        if (h.size() == 3) {
            for (int i = 0; i < 3; i++) v[i] = Hex(h[i]) * 17;
        } else if (h.size() == 6 || h.size() == 8) {
            for (size_t i = 0; i < h.size() / 2; i++) v[i] = Hex(h[i * 2]) * 16 + Hex(h[i * 2 + 1]);
        } else {
            return false;
        }
        out.name.clear();
        out.r = v[0]; out.g = v[1]; out.b = v[2]; out.a = v[3];
        return true;
    }
    if (t.find(',') != std::string::npos) {
        int v[4] = {0, 0, 0, 255};
        if (!ParseInts(t, v, 4) && !ParseInts(t, v, 3)) return false;
        for (int &x : v) x = std::max(0, std::min(255, x));
        out.name.clear();
        out.r = v[0]; out.g = v[1]; out.b = v[2]; out.a = v[3];
        return true;
    }
    for (char c : t)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    out.name = Lower(t);
    return true;
}

bool ParseChoice(const std::string &s, std::initializer_list<const char *> names, int &out) {
    std::string t = Lower(Trim(s));
    int i = 0;
    for (const char *n : names) {
        if (t == n) { out = i; return true; }
        i++;
    }
    return false;
}

Elem NewElem(const std::string &name) {
    Elem e;
    e.name = name;
    e.color.name = "text";
    e.selColor.name = "text";
    e.selection.name = "selection";
    e.countColor.name = "dim";
    e.titleColor.name = "text";
    return e;
}

// 요소 속성 하나. 모르는 키·잘못된 값이면 false (why 에 이유)
bool SetProp(Elem &e, const std::string &key, const std::string &val, bool isNew, std::string &why) {
    auto bad = [&](const char *msg) { why = msg; return false; };
    int v4[4];
    bool b;
    if (key == "type") {
        int t;
        if (!ParseChoice(val, {"rect", "image", "text", "list", "preview", "media", "info", "battery", "pager"}, t))
            return bad("type: rect / image / text / list / preview / media / info / battery / pager");
        e.type = (ElemType)t;
        if (isNew && (e.type == ElemType::List || e.type == ElemType::Info || e.type == ElemType::Pager)) e.color.name = "dim";
        if (isNew && e.type == ElemType::Image) e.align = e.valign = 1;  // 그림은 칸 가운데가 기본
        return true;
    }
    if (key == "visible") { if (!ParseBool(val, b)) return bad("on / off"); e.visible = b; return true; }
    if (key == "z") return ParseInt(val, e.z) || bad("number");
    if (key == "rect") {
        if (!ParseInts(val, v4, 4)) return bad("rect = x,y,w,h");
        e.x = v4[0]; e.y = v4[1]; e.w = v4[2]; e.h = v4[3]; e.hasRect = true;
        return true;
    }
    if (key == "x") { e.hasRect = true; return ParseInt(val, e.x) || bad("number"); }
    if (key == "y") { e.hasRect = true; return ParseInt(val, e.y) || bad("number"); }
    if (key == "w") { e.hasRect = true; return ParseInt(val, e.w) || bad("number"); }
    if (key == "h") { e.hasRect = true; return ParseInt(val, e.h) || bad("number"); }
    if (key == "color") return ParseColor(val, e.color) || bad("color");
    // text
    if (key == "text") { e.text = val; return true; }
    if (key == "font") {
        int f;
        if (!ParseChoice(val, {"regular", "bold"}, f)) return bad("regular / bold");
        e.bold = f == 1;
        return true;
    }
    if (key == "size") return (ParseInt(val, e.size) && e.size >= 6 && e.size <= 200) || bad("6~200");
    if (key == "align") return ParseChoice(val, {"left", "center", "right"}, e.align) || bad("left / center / right");
    if (key == "valign") return ParseChoice(val, {"top", "center", "bottom"}, e.valign) || bad("top / center / bottom");
    if (key == "wrap") { if (!ParseBool(val, b)) return bad("on / off"); e.wrap = b; return true; }
    if (key == "scroll") { if (!ParseBool(val, b)) return bad("on / off"); e.scroll = b; return true; }
    if (key == "logo") { if (!ParseBool(val, b)) return bad("on / off"); e.logo = b; return true; }
    if (key == "logo_rect") {
        if (!ParseInts(val, v4, 4)) return bad("logo_rect = x,y,w,h");
        e.lx = v4[0]; e.ly = v4[1]; e.lw = v4[2]; e.lh = v4[3];
        return true;
    }
    // image
    if (key == "file") { e.file = val; return true; }
    if (key == "source") {
        int s;
        if (!ParseChoice(val, {"background", "logo", "icon", "boxart", "screenshot"}, s))
            return bad("background / logo / icon / boxart / screenshot");
        e.source = Lower(Trim(val));
        return true;
    }
    if (key == "folder") { e.folder = val; return true; }
    if (key == "fallback") { e.fallback = val; return true; }
    if (key == "fit") return ParseChoice(val, {"contain", "cover", "stretch"}, e.fit) || bad("contain / cover / stretch");
    if (key == "alpha") return (ParseInt(val, e.alpha) && e.alpha >= 0 && e.alpha <= 255) || bad("0~255");
    if (key == "dim") return (ParseInt(val, e.dim) && e.dim >= 0 && e.dim <= 255) || bad("0~255");
    if (key == "tint") { if (!ParseColor(val, e.tint)) return bad("color"); e.hasTint = true; return true; }
    // list
    if (key == "style") return ParseChoice(val, {"text", "grid"}, e.style) || bad("text / grid");
    if (key == "row_height") {
        char *end = nullptr;
        double d = strtod(val.c_str(), &end);
        if (end == val.c_str() || *end || d < 8 || d > 720) return bad("8~720");
        e.rowH = (float)d;
        return true;
    }
    if (key == "columns") return (ParseInt(val, e.columns) && e.columns >= 1 && e.columns <= 40) || bad("1~40");
    if (key == "rows") return (ParseInt(val, e.rows) && e.rows >= 0 && e.rows <= 60) || bad("0~60");
    if (key == "gap") {
        if (ParseInts(val, v4, 2)) { e.gapX = v4[0]; e.gapY = v4[1]; return true; }
        if (ParseInt(val, v4[0])) { e.gapX = e.gapY = v4[0]; return true; }
        return bad("gap = x,y");
    }
    if (key == "selected_color") return ParseColor(val, e.selColor) || bad("color");
    if (key == "selection") return ParseColor(val, e.selection) || bad("color");
    if (key == "text_offset") {
        if (ParseInts(val, v4, 2)) { e.textX = v4[0]; e.textY = v4[1]; return true; }
        if (ParseInt(val, v4[0])) { e.textX = v4[0]; e.textY = -1; return true; }
        return bad("text_offset = x,y");
    }
    if (key == "count") { if (!ParseBool(val, b)) return bad("on / off"); e.count = b; return true; }
    if (key == "count_color") return ParseColor(val, e.countColor) || bad("color");
    if (key == "count_size") return (ParseInt(val, e.countSize) && e.countSize >= 6 && e.countSize <= 200) || bad("6~200");
    if (key == "count_offset") {
        if (ParseInts(val, v4, 2)) { e.countX = v4[0]; e.countY = v4[1]; return true; }
        if (ParseInt(val, v4[0])) { e.countX = v4[0]; e.countY = -1; return true; }
        return bad("count_offset = x,y");
    }
    if (key == "card") { e.card = val; return true; }
    if (key == "card_color") { if (!ParseColor(val, e.cardColor)) return bad("color"); e.hasCardColor = true; return true; }
    if (key == "selector") { e.selector = val; return true; }
    if (key == "card_grow") return ParseInt(val, e.cardGrow) || bad("number");
    if (key == "selector_grow") return ParseInt(val, e.selectorGrow) || bad("number");
    if (key == "selected_scale") {
        char *end = nullptr;
        double d = strtod(val.c_str(), &end);
        if (end == val.c_str() || *end || d < 0.5 || d > 2.0) return bad("0.5~2.0");
        e.selScale = (float)d;
        return true;
    }
    if (key == "image") {
        int s;
        if (!ParseChoice(val, {"icon", "boxart", "logo", "none"}, s)) return bad("icon / boxart / logo / none");
        e.image = Lower(Trim(val));
        return true;
    }
    if (key == "padding") {
        if (ParseInts(val, v4, 4)) { for (int i = 0; i < 4; i++) e.pad[i] = v4[i]; return true; }
        if (ParseInt(val, v4[0])) { for (int i = 0; i < 4; i++) e.pad[i] = v4[0]; return true; }
        return bad("padding = left,top,right,bottom");
    }
    if (key == "label") return ParseChoice(val, {"auto", "on", "off"}, e.label) || bad("auto / on / off");
    if (key == "label_height") return (ParseInt(val, e.labelH) && e.labelH >= 0) || bad("number");
    // info / media
    if (key == "title_color") return ParseColor(val, e.titleColor) || bad("color");
    if (key == "title_size") return (ParseInt(val, e.titleSize) && e.titleSize >= 6 && e.titleSize <= 200) || bad("6~200");
    if (key == "desc_size") return (ParseInt(val, e.descSize) && e.descSize >= 6 && e.descSize <= 200) || bad("6~200");
    if (key == "overlay") { if (!ParseBool(val, b)) return bad("on / off"); e.overlay = b; return true; }
    why = "unknown key";
    return false;
}

void Parse(Layout &l, std::istream &in, bool builtin) {
    std::string line, section;
    Screen *screen = nullptr;
    int cur = -1;                      // screen->elems 안의 번호
    std::set<std::string> fresh;       // 이번에 새로 생긴 요소 ("screen.name") 중 type 을 아직 안 받은 것
    std::set<std::string> created;     // 이번에 새로 생긴 요소
    bool skip = false;
    int no = 0;
    auto problem = [&](const std::string &msg) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d: ", no);
        l.problems.push_back(buf + msg);
    };
    while (std::getline(in, line)) {
        no++;
        if (no == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line = line.substr(3);
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            screen = nullptr;
            cur = -1;
            skip = false;
            if (section == "theme" || section == "names") continue;
            size_t dot = section.find('.');
            std::string sc = section.substr(0, dot), name = dot == std::string::npos ? "" : section.substr(dot + 1);
            if (sc == "categories") screen = &l.categories;
            else if (sc == "games") screen = &l.games;
            if (!screen || name.empty()) {
                problem("[" + section + "]: [theme], [names], [categories.<name>], [games.<name>]");
                screen = nullptr;
                skip = true;
                continue;
            }
            for (size_t i = 0; i < screen->elems.size(); i++)
                if (screen->elems[i].name == name) cur = (int)i;
            if (cur < 0) {
                screen->elems.push_back(NewElem(name));
                cur = (int)screen->elems.size() - 1;
                fresh.insert(section);
                created.insert(section);
            }
            continue;
        }
        if (skip) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) { problem("'=' 가 없음: " + line); continue; }
        std::string key = Lower(Trim(line.substr(0, eq))), val = line.substr(eq + 1);
        // 줄 끝 설명: 공백 뒤의 # 또는 ; (값 맨 앞의 # 은 색)
        val = Trim(val);
        for (size_t i = 1; i < val.size(); i++)
            if ((val[i] == '#' || val[i] == ';') && (val[i - 1] == ' ' || val[i - 1] == '\t')) {
                val.erase(i);
                break;
            }
        val = Trim(val);
        if (section == "theme") {
            if (key == "font") l.font = val;
            else if (key == "font_bold") l.fontBold = val;
            else if (key == "background_color") {
                if (ParseColor(val, l.clear) && l.clear.name.empty()) l.hasClear = true;
                else problem("background_color: #rrggbb");
            } else if (key.rfind("color.", 0) == 0 && key.size() > 6) {
                LColor c;
                if (ParseColor(val, c) && c.name.empty()) l.colors[key.substr(6)] = c;
                else problem(key + ": #rrggbb / #rrggbbaa");
            } else {
                problem("[theme] " + key + ": unknown key");
            }
            continue;
        }
        if (section == "names") {
            if (!key.empty() && !val.empty()) l.names[key] = val;
            continue;
        }
        if (section.empty() || !screen || cur < 0) { problem("section 밖의 줄: " + line); continue; }
        Elem &e = screen->elems[cur];
        std::string why;
        if (key == "type") fresh.erase(section);
        if (!SetProp(e, key, val, created.count(section) > 0, why)) problem("[" + section + "] " + key + "=" + val + " (" + why + ")");
    }
    // type 이 없는 새 요소는 버린다
    for (const auto &sec : fresh) {
        size_t dot = sec.find('.');
        Screen &s = sec.compare(0, dot, "categories") == 0 ? l.categories : l.games;
        std::string name = sec.substr(dot + 1);
        s.elems.erase(std::remove_if(s.elems.begin(), s.elems.end(), [&](const Elem &e) { return e.name == name; }), s.elems.end());
        l.problems.push_back("[" + sec + "]: type 이 없음");
    }
    for (Screen *s : {&l.categories, &l.games})
        std::stable_sort(s->elems.begin(), s->elems.end(), [](const Elem &a, const Elem &b) { return a.z < b.z; });
    if (builtin) l.problems.clear();
}

}  // namespace

void LayoutDefault(Layout &l) {
    l = Layout();
    std::istringstream in(kDefault);
    Parse(l, in, true);
}

bool LayoutLoad(Layout &l, const std::string &path) {
    LayoutDefault(l);
    std::ifstream in(path);
    if (!in) return false;
    l.custom = true;
    Parse(l, in, false);
    return true;
}
