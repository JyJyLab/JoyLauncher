// Joy Launcher 테마 빌더: 런처의 실제 화면 코드로 미리보기하며 layout.txt·theme.txt·그림을 편집한다.
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "misc/cpp/imgui_stdlib.h"

#include "../source/esde.h"
#include "joy_api.h"
#include "schema.h"
#include "util.h"
#include "ftp.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <thread>
#include <tuple>

static const int SW = 1280, SH = 720;

// ---------------------------------------------------------------- 상태
struct Builder {
    SDL_Renderer *r = nullptr;
    SDL_Window *win = nullptr;
    SDL_Texture *preview = nullptr;
    std::string home = "work", roms = "sample/roms", library = "library", templates = "templates";
    std::string theme;  // work/themes/<theme>
    IniDoc doc;
    ThemeTxt ttxt;
    bool games = false;  // 지금 편집하는 화면
    std::string sel;     // 선택한 요소 "categories.list" / "categories.list.title"
    std::vector<std::pair<std::string, std::string>> undo, redo;  // (layout, theme.txt)
    std::string status;
    Uint32 statusUntil = 0;
    bool snap = true;
    int grid = 4;
    bool showBounds = true;
    bool en = false;
    // 끌기
    bool dragging = false, resizing = false;
    int handle = -1;
    ImVec2 dragStart;
    int ox = 0, oy = 0, ow = 0, oh = 0;
    // 대화상자
    std::string newName = "MyTheme", newTemplate = "JoyBasic";
    bool newCore = true, newEmpty = false;
    int wizard = 0;  // 시작 마법사 단계 (0 끔)
    std::string delAsk;     // 지울지 묻는 테마 (빈칸 = 아님)
    bool delThenCreate = false;  // 지운 뒤 같은 이름으로 새로 만들기
    bool openManage = false;
    bool winCats = false, winTheme = false, winStrings = false, winRaw = false, winProblems = false;  // 따로 뜨는 설정 창
    std::string exportDir;
    // 그림 가져오기 대기 (끌어 놓기·파일 고르기)
    std::string pendKind, pendStem, pendProp;  // pendProp: 속성 파일 고르기 (rect 요소의 file 등)
    // 파일 고르기 창
    bool browseOpen = false;
    std::string browseDir, browseFilter;
    std::function<void(const std::string &)> browseDone;
    // 라이브러리: 세트 -> 종류 -> 이름(소문자) -> 경로
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> lib;
    std::vector<std::string> libSets;
    std::string assetKind = "backgrounds", assetFilter;
    std::string bulkSet;
    uintmax_t themeSize = 0;
    Uint32 sizeAt = 0;
    std::string tabReq;   // 처음 열 아래 탭 (시험용 --panel)
    bool assetUser = false;  // 그림 넣을 곳: false 테마 / true 내 그림 폴더 (work/categories, 모든 테마 공통)
    int catSel = -1;
    std::string rawText;  // layout.txt 직접 편집
    bool rawDirty = false;
};
static Builder B;

static void Status(const std::string &s) {
    B.status = s;
    B.statusUntil = SDL_GetTicks() + 4000;
}
static std::string ThemeDir() { return B.home + "/themes/" + B.theme; }

// ---------------------------------------------------------------- 그림 텍스처 캐시 (빌더 화면용 작은 그림)
struct TexEntry { SDL_Texture *t = nullptr; int w = 0, h = 0; fs::file_time_type mtime; };
static std::map<std::string, TexEntry> gTex;
static TexEntry *Tex(const std::string &path, int maxSide = 160) {
    std::error_code ec;
    auto mt = fs::last_write_time(P(path), ec);
    if (ec) return nullptr;
    std::string key = path + "#" + std::to_string(maxSide);
    auto it = gTex.find(key);
    if (it != gTex.end() && it->second.mtime == mt) return it->second.t ? &it->second : nullptr;
    if (it != gTex.end() && it->second.t) SDL_DestroyTexture(it->second.t);
    TexEntry e;
    e.mtime = mt;
    if (SDL_Surface *s = IMG_Load(path.c_str())) {
        double sc = std::min(1.0, (double)maxSide / std::max(s->w, s->h));
        SDL_Surface *d = ScaleDown(s, std::max(1, (int)(s->w * sc)), std::max(1, (int)(s->h * sc)));
        e.w = s->w;
        e.h = s->h;
        SDL_FreeSurface(s);
        if (d) {
            e.t = SDL_CreateTextureFromSurface(B.r, d);
            SDL_FreeSurface(d);
        }
    }
    gTex[key] = e;
    return e.t ? &gTex[key] : nullptr;
}
static void Thumb(const std::string &path, float box) {
    TexEntry *e = path.empty() ? nullptr : Tex(path);
    if (!e) {
        ImGui::Dummy(ImVec2(box, box));
        return;
    }
    float sc = std::min(box / e->w, box / e->h);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(box, box));
    ImVec2 a(p.x + (box - e->w * sc) / 2, p.y + (box - e->h * sc) / 2);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + box, p.y + box), IM_COL32(60, 60, 70, 255));
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)e->t, a, ImVec2(a.x + e->w * sc, a.y + e->h * sc));
}

// ---------------------------------------------------------------- 문서 <-> 런처
// 화면 구성 반영은 다음 프레임 처음에 (그리는 도중에 다시 읽으면 레이어·속성 창이 들고 있는 요소가 사라져 꺼짐)
static bool gApplyPending = false, gReloadPending = false;
static void Apply() { gApplyPending = true; }
static void FlushApply() {
    if (gReloadPending) {
        JoyLoadTheme(B.theme);
        gReloadPending = false;
        gApplyPending = true;
    }
    if (gApplyPending) {
        JoySetLayoutText(B.doc.Str());
        gApplyPending = false;
    }
}
static void SaveFiles() {
    if (B.theme.empty()) return;
    WriteFile(ThemeDir() + "/layout.txt", B.doc.Str());
    WriteFile(ThemeDir() + "/theme.txt", B.ttxt.Str());
}
static void PushUndo() {
    B.undo.push_back({B.doc.Str(), B.ttxt.Str()});
    if (B.undo.size() > 200) B.undo.erase(B.undo.begin());
    B.redo.clear();
}
// 문서를 바꾼 뒤: 미리보기 반영 + 저장
static void Changed() {
    Apply();
    SaveFiles();
    B.rawDirty = false;
}
static void Undo() {
    if (B.undo.empty()) return;
    B.redo.push_back({B.doc.Str(), B.ttxt.Str()});
    B.doc.Parse(B.undo.back().first);
    B.ttxt.Parse(B.undo.back().second);
    B.undo.pop_back();
    Changed();
    gReloadPending = true;
}
static void Redo() {
    if (B.redo.empty()) return;
    B.undo.push_back({B.doc.Str(), B.ttxt.Str()});
    B.doc.Parse(B.redo.back().first);
    B.ttxt.Parse(B.redo.back().second);
    B.redo.pop_back();
    Changed();
    gReloadPending = true;
}
static void OpenTheme(const std::string &name) {
    B.theme = name;
    std::string text;
    ReadFile(ThemeDir() + "/layout.txt", text);
    B.doc.Parse(text);
    ReadFile(ThemeDir() + "/theme.txt", text);
    B.ttxt.Parse(text);
    B.undo.clear();
    B.redo.clear();
    B.sel.clear();
    JoyLoadTheme(name);
    gReloadPending = false;
    JoySetLayoutText(B.doc.Str());
    gApplyPending = false;
    B.sizeAt = 0;
    Status("열림: " + name);
}
// 그림 파일을 바꾼 뒤 (런처가 그림 목록을 다시 읽게)
static void JoyReloadCategoriesSafe();
static void AssetsChanged() {
    if (B.assetUser) JoyReloadCategoriesSafe();
    gReloadPending = true;
    B.sizeAt = 0;
}

static void JoyReloadCategoriesSafe() { JoyReloadCategories(); }

// ---------------------------------------------------------------- 요소 찾기
static const Elem *FindElemKey(const std::string &key) {
    size_t d1 = key.find('.');
    if (d1 == std::string::npos) return nullptr;
    const Screen &sc = key.compare(0, d1, "games") == 0 ? JoyLayout().games : JoyLayout().categories;
    std::string rest = key.substr(d1 + 1);
    size_t d2 = rest.find('.');
    const Elem *p = sc.Find(rest.substr(0, d2));
    if (!p || d2 == std::string::npos) return p;
    std::string ch = rest.substr(d2 + 1);
    for (const auto &c : p->children)
        if (c.name == ch) return &c;
    return nullptr;
}
static bool IsChildKey(const std::string &key) { return std::count(key.begin(), key.end(), '.') == 2; }
static std::string ScreenPrefix() { return B.games ? "games." : "categories."; }

// 효과 값 (문서에 없으면 기본 구성·파서 결과에서)
static std::string ColorStr(const LColor &c) {
    if (!c.name.empty()) return c.name;
    char b[16];
    if (c.a == 255) snprintf(b, sizeof(b), "#%02x%02x%02x", c.r, c.g, c.b);
    else snprintf(b, sizeof(b), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return b;
}
static std::string EffVal(const Elem &e, const std::string &k) {
    char b[96];
    auto ch = [](int i, std::initializer_list<const char *> v) { int n = 0; for (auto s : v) if (n++ == i) return std::string(s); return std::string(); };
    if (k == "rect") { snprintf(b, sizeof(b), "%d,%d,%d,%d", e.x, e.y, e.w, e.h); return b; }
    if (k == "visible") return e.visible ? "on" : "off";
    if (k == "z") return std::to_string(e.z);
    if (k == "color") return ColorStr(e.color);
    if (k == "state") return ch(e.state, {"all", "selected", "normal"});
    if (k == "visible_if") return e.visibleIf;
    if (k == "hidden_if") return e.hiddenIf;
    if (k == "text") return e.text;
    if (k == "size") return std::to_string(e.size);
    if (k == "font") return e.bold ? "bold" : "regular";
    if (k == "align") return ch(e.align, {"left", "center", "right"});
    if (k == "valign") return ch(e.valign, {"top", "center", "bottom"});
    if (k == "wrap") return e.wrap ? "on" : "off";
    if (k == "scroll") return e.scroll ? "on" : "off";
    if (k == "selected_color") return ColorStr(e.selColor);
    if (k == "logo") return e.logo ? "on" : "off";
    if (k == "logo_rect") { snprintf(b, sizeof(b), "%d,%d,%d,%d", e.lx, e.ly, e.lw, e.lh); return b; }
    if (k == "source") return e.source;
    if (k == "file") return e.file;
    if (k == "folder") return e.folder;
    if (k == "fallback") return e.fallback;
    if (k == "fit") return ch(e.fit, {"contain", "cover", "stretch"});
    if (k == "alpha") return std::to_string(e.alpha);
    if (k == "dim") return std::to_string(std::max(0, e.dim));
    if (k == "tint") return e.hasTint ? ColorStr(e.tint) : "";
    if (k == "style") return e.style == 2 ? "carousel" : e.style ? "grid" : "text";
    if (k == "direction") return e.direction ? "vertical" : "horizontal";
    if (k == "visible_items") return std::to_string(e.visibleItems);
    if (k == "item_size") { snprintf(b, sizeof(b), "%d,%d", e.itemW, e.itemH); return b; }
    if (k == "spacing") return std::to_string(e.spacing);
    if (k == "center_scale") { snprintf(b, sizeof(b), "%g", e.centerScale); return b; }
    if (k == "side_scale") { snprintf(b, sizeof(b), "%g", e.sideScale); return b; }
    if (k == "side_alpha") return std::to_string(e.sideAlpha);
    if (k == "anim_ms") return std::to_string(e.animMs);
    if (k == "rows") return std::to_string(e.rows);
    if (k == "columns") return std::to_string(e.columns);
    if (k == "row_height") { snprintf(b, sizeof(b), "%g", e.rowH); return b; }
    if (k == "gap") { snprintf(b, sizeof(b), "%d,%d", e.gapX, e.gapY); return b; }
    if (k == "selection") return ColorStr(e.selection);
    if (k == "text_offset") { snprintf(b, sizeof(b), "%d,%d", e.textX, e.textY); return b; }
    if (k == "label") return ch(e.label, {"auto", "on", "off"});
    if (k == "label_height") return std::to_string(e.labelH);
    if (k == "image") return e.image;
    if (k == "padding") { snprintf(b, sizeof(b), "%d,%d,%d,%d", e.pad[0], e.pad[1], e.pad[2], e.pad[3]); return b; }
    if (k == "card") return e.card;
    if (k == "card_color") return e.hasCardColor ? ColorStr(e.cardColor) : "";
    if (k == "card_grow") return std::to_string(e.cardGrow);
    if (k == "selector") return e.selector;
    if (k == "selector_grow") return std::to_string(e.selectorGrow);
    if (k == "selected_scale") { snprintf(b, sizeof(b), "%g", e.selScale); return b; }
    if (k == "count") return e.count ? "on" : "off";
    if (k == "count_color") return ColorStr(e.countColor);
    if (k == "count_size") return std::to_string(e.countSize);
    if (k == "count_offset") { snprintf(b, sizeof(b), "%d,%d", e.countX, e.countY); return b; }
    if (k == "overlay") return e.overlay ? "on" : "off";
    if (k == "title_color") return ColorStr(e.titleColor);
    if (k == "title_size") return std::to_string(e.titleSize);
    if (k == "desc_size") return std::to_string(e.descSize);
    return "";
}

// 색 이름 -> 실제 색 (미리보기 견본용)
static ImVec4 ResolveColor(const std::string &v) {
    LColor c;
    std::string t = Trim(v);
    auto hex = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 0; };
    if (!t.empty() && t[0] == '#') {
        std::string h = t.substr(1);
        int vv[4] = {0, 0, 0, 255};
        if (h.size() == 3) for (int i = 0; i < 3; i++) vv[i] = hex(h[i]) * 17;
        else for (size_t i = 0; i + 1 < h.size() && i / 2 < 4; i += 2) vv[i / 2] = hex(h[i]) * 16 + hex(h[i + 1]);
        return ImVec4(vv[0] / 255.f, vv[1] / 255.f, vv[2] / 255.f, vv[3] / 255.f);
    }
    if (t.find(',') != std::string::npos) {
        int vv[4] = {0, 0, 0, 255};
        sscanf(t.c_str(), "%d,%d,%d,%d", &vv[0], &vv[1], &vv[2], &vv[3]);
        return ImVec4(vv[0] / 255.f, vv[1] / 255.f, vv[2] / 255.f, vv[3] / 255.f);
    }
    auto it = JoyLayout().colors.find(Lower(t));
    if (it != JoyLayout().colors.end()) return ImVec4(it->second.r / 255.f, it->second.g / 255.f, it->second.b / 255.f, it->second.a / 255.f);
    if (t == "dim") return ImVec4(150 / 255.f, 150 / 255.f, 165 / 255.f, 1);
    if (t == "accent") return ImVec4(1, 196 / 255.f, 64 / 255.f, 1);
    if (t == "selection") return ImVec4(70 / 255.f, 70 / 255.f, 110 / 255.f, 210 / 255.f);
    if (t == "bar") return ImVec4(20 / 255.f, 20 / 255.f, 26 / 255.f, 200 / 255.f);
    return ImVec4(235 / 255.f, 235 / 255.f, 240 / 255.f, 1);
}
static std::string ColorHex(const ImVec4 &c) {
    char b[16];
    int r = (int)(c.x * 255 + .5f), g = (int)(c.y * 255 + .5f), bl = (int)(c.z * 255 + .5f), a = (int)(c.w * 255 + .5f);
    if (a >= 255) snprintf(b, sizeof(b), "#%02x%02x%02x", r, g, bl);
    else snprintf(b, sizeof(b), "#%02x%02x%02x%02x", r, g, bl, a);
    return b;
}

// ---------------------------------------------------------------- 파일 고르기 창
static void Browse(const std::string &filter, std::function<void(const std::string &)> done) {
    B.browseOpen = true;
    B.browseFilter = filter;
    B.browseDone = done;
    if (B.browseDir.empty() || !IsDir(B.browseDir)) {
        const char *h = getenv("USERPROFILE");
        if (!h) h = getenv("HOME");
        B.browseDir = h ? h : ".";
    }
}
static void BrowseWindow() {
    if (!B.browseOpen) return;
    ImGui::OpenPopup("파일 고르기");
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("파일 고르기", &B.browseOpen)) {
        ImGui::TextWrapped("파일을 고르거나, 창에 파일을 끌어 놓아도 됩니다.");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##dir", &B.browseDir);
        ImGui::SameLine();
        if (ImGui::Button("위 폴더")) B.browseDir = U8(P(B.browseDir).parent_path());
        ImGui::BeginChild("list", ImVec2(0, -40), true);
#ifdef _WIN32
        for (char d = 'C'; d <= 'Z'; d++) {
            std::string root = std::string(1, d) + ":\\";
            if (IsDir(root)) { ImGui::SameLine(); if (ImGui::SmallButton(root.c_str())) B.browseDir = root; }
        }
        ImGui::NewLine();
#endif
        for (const auto &d : ListDir(B.browseDir, true))
            if (ImGui::Selectable(("[폴더] " + d).c_str())) B.browseDir = U8(P(B.browseDir) / P(d));
        for (const auto &f : ListDir(B.browseDir, false)) {
            std::string e = Ext(f);
            if (!B.browseFilter.empty() && B.browseFilter.find(e.empty() ? "?" : e) == std::string::npos) continue;
            if (ImGui::Selectable(f.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                std::string full = U8(P(B.browseDir) / P(f));
                B.browseOpen = false;
                if (B.browseDone) B.browseDone(full);
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("닫기")) B.browseOpen = false;
        if (!B.browseOpen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- 라이브러리
static const char *kKindKo(const std::string &k) {
    if (k == "backgrounds") return "배경";
    if (k == "logos") return "로고";
    if (k == "icons") return "아이콘";
    if (k == "controllers") return "게임기 그림";
    if (k == "ui") return "UI 그림";
    if (k == "fonts") return "글꼴";
    return k.c_str();
}
static void ScanLibrary() {
    B.lib.clear();
    B.libSets = ListDir(B.library, true);
    for (const auto &set : B.libSets)
        for (const auto &kind : ListDir(B.library + "/" + set, true))
            for (const auto &f : ListDir(B.library + "/" + set + "/" + kind, false))
                if (IsImageName(f) || kind == "fonts") B.lib[set][kind][Lower(Stem(f))] = B.library + "/" + set + "/" + kind + "/" + f;
    if (B.bulkSet.empty() && !B.libSets.empty()) B.bulkSet = B.libSets[0];
}
// 그림을 넣을 곳: 테마 폴더 / 내 그림 폴더 (work/categories/<종류>, 모든 테마 공통)
static std::string AssetDir(const std::string &kind) { return (B.assetUser ? B.home + "/categories" : ThemeDir()) + "/" + kind; }
// 테마 폴더의 그림 (종류 폴더 안 이름(소문자) -> 경로)
static std::map<std::string, std::string> ThemeImages(const std::string &kind) {
    std::map<std::string, std::string> m;
    std::string dir = AssetDir(kind);
    for (const auto &f : ListDir(dir, false))
        if (IsImageName(f)) m[Lower(Stem(f))] = dir + "/" + f;
    return m;
}
// 주요 기종 (스위치 런처에서 실제로 쓰는 기종 + 응애팩 아케이드 카테고리)
static const std::set<std::string> kCore = {
    "default", "favorites", "hbfav", "arcade", "arcade_action", "arcade_casual", "arcade_fighting", "arcade_flying", "arcade_shooting",
    "neogeo", "nes", "fds", "snes", "gb", "gbc", "gba", "nds", "megadrive", "mastersystem", "gamegear", "segacd", "sega32x",
    "pcengine", "pcenginecd", "psx", "psp", "wswan", "wswanc", "ngp", "ngpc", "msx1", "msx2", "pc98", "dos", "pokemini"};
static bool AssignAsset(const std::string &kind, const std::string &stem, const std::string &src) {
    // 라이브러리 그림은 이미 알맞은 크기: 그대로 복사 (다시 저장하면 화질이 떨어짐)
    if (src.rfind(B.library + "/", 0) == 0 && kind != "ui") {
        std::error_code ec;
        std::string dir = AssetDir(kind);
        fs::create_directories(P(dir), ec);
        for (const char *e : {".png", ".jpg", ".jpeg", ".PNG", ".JPG"}) fs::remove(P(dir + "/" + stem + e), ec);
        fs::copy_file(P(src), P(dir + "/" + stem + "." + Ext(src)), fs::copy_options::overwrite_existing, ec);
        if (!ec) return true;
    }
    std::string err;
    std::string out = ImportImage(src, AssetDir(kind), stem, kind, err);
    if (out.empty()) { Status("그림을 읽지 못함: " + err); return false; }
    return true;
}

// 세트의 한 종류를 테마에 채우기. coreOnly: 주요 기종만
static int FillKind(const std::string &set, const std::string &kind, bool coreOnly, bool onlyMissing) {
    auto have = ThemeImages(kind);
    int n = 0;
    for (const auto &kv : B.lib[set][kind]) {
        if (coreOnly && !kCore.count(kv.first)) continue;
        if (onlyMissing && have.count(kv.first)) continue;
        if (AssignAsset(kind, kv.first, kv.second)) n++;
    }
    return n;
}

// ---------------------------------------------------------------- 요소 추가·삭제
static std::string UniqueName(const std::string &base, const std::string &prefix) {
    for (int i = 1; i < 1000; i++) {
        std::string n = base + std::to_string(i);
        if (!B.doc.Find(prefix + n) && !FindElemKey(prefix + n)) return n;
    }
    return base;
}
static void AddElem(const std::string &type, bool child) {
    std::string listKey;
    if (child) {
        listKey = B.sel;
        if (IsChildKey(listKey)) listKey = listKey.substr(0, listKey.find_last_of('.'));
        const Elem *le = FindElemKey(listKey);
        if (!le || le->type != ElemType::List) { Status("칸 안 요소는 목록을 고른 뒤 추가"); return; }
    }
    PushUndo();
    std::string prefix = child ? listKey + "." : ScreenPrefix();
    std::string name = UniqueName(type == "text" ? "text" : type == "image" ? "image" : "box", prefix);
    std::string sec = prefix + name;
    B.doc.Set(sec, "type", type);
    if (child) {
        B.doc.Set(sec, "rect", type == "text" ? "0,8,0,0" : "0,0,0,0");
        if (type == "text") { B.doc.Set(sec, "text", "{name}"); B.doc.Set(sec, "align", "center"); B.doc.Set(sec, "size", "20"); }
        if (type == "rect") { B.doc.Set(sec, "color", "#ffffff30"); B.doc.Set(sec, "z", "-1"); B.doc.Set(sec, "state", "selected"); }
        if (type == "image") { B.doc.Set(sec, "source", "icon"); B.doc.Set(sec, "rect", "8,8,-8,-40"); }
    } else {
        if (type == "text") { B.doc.Set(sec, "text", "새 글자"); B.doc.Set(sec, "rect", "540,330,200,0"); B.doc.Set(sec, "size", "28"); }
        if (type == "rect") { B.doc.Set(sec, "rect", "490,260,300,200"); B.doc.Set(sec, "color", "#00000080"); }
        if (type == "image") { B.doc.Set(sec, "rect", "490,260,300,200"); B.doc.Set(sec, "source", "logo"); }
    }
    B.sel = sec;
    Changed();
}
static void DeleteSel() {
    if (B.sel.empty()) return;
    const Elem *e = FindElemKey(B.sel);
    std::string dflt = LayoutDefaultText();
    bool builtin = !IsChildKey(B.sel) && dflt.find("[" + B.sel + "]") != std::string::npos;
    PushUndo();
    if (builtin) {  // 기본 요소는 지울 수 없음: 숨김
        B.doc.Set(B.sel, "visible", "off");
        Status("기본 요소는 숨김으로 처리");
    } else {
        B.doc.RemoveSec(B.sel);
        B.sel.clear();
    }
    (void)e;
    Changed();
}
static void DuplicateSel() {
    if (B.sel.empty()) return;
    const IniSec *s = B.doc.Find(B.sel);
    const Elem *e = FindElemKey(B.sel);
    if (!e) return;
    PushUndo();
    std::string prefix = B.sel.substr(0, B.sel.find_last_of('.') + 1);
    std::string name = UniqueName(e->name + "_", prefix), sec = prefix + name;
    B.doc.Set(sec, "type", kTypeNames[(int)e->type]);
    if (s)
        for (const auto &kv : s->kv)
            if (!kv.raw && kv.key != "type") B.doc.Set(sec, kv.key, kv.val);
    char b[64];
    snprintf(b, sizeof(b), "%d,%d,%d,%d", e->x + 16, e->y + 16, e->w, e->h);
    B.doc.Set(sec, "rect", b);
    B.sel = sec;
    Changed();
}

// ---------------------------------------------------------------- 요소 팔레트 (빈 화면에 넣을 수 있는 요소)
// name: 기본 요소 이름(숨겨져 있으면 다시 보이게) 또는 "+이름" (새 요소), kvs: "키=값;키=값"
struct PresetSec { const char *name, *kvs; };
struct Preset { int scr; const char *group, *label, *desc; std::vector<PresetSec> secs; };  // scr: 1 카테고리 / 2 게임 / 3 둘 다
static const char *kListReset[] = {"style", "direction", "columns", "rows", "gap", "item_size", "spacing", "visible_items", "center_scale",
                                   "side_scale", "side_alpha", "image", "padding", "label", "label_height", "card", "card_color", "card_grow",
                                   "selector", "selector_grow", "selected_scale", "row_height", "count", "text_offset", "size"};
static const std::vector<Preset> &Presets() {
    static const std::vector<Preset> v = {
        {3, "배경", "배경 그림", "카테고리마다 바뀌는 배경 (테마의 backgrounds/)", {{"background", "fit=cover"}}},
        {3, "배경", "위 막대", "화면 위 반투명 막대", {{"topbar", "color=bar"}}},
        {3, "배경", "아래 막대", "화면 아래 반투명 막대", {{"bottombar", "color=bar"}}},
        {1, "목록", "줄 목록", "카테고리 이름을 한 줄씩 (게임 수 표시)", {{"list", "style=text;rect=28,92,720,546;row_height=42;size=27;count=on;text_offset=12,8"}}},
        {1, "목록", "카드 격자", "기종 카드를 바둑판처럼 (5×2)", {{"list", "style=grid;rect=60,110,1160,520;columns=5;rows=2;gap=20,20;image=icon;padding=12;card_color=#ffffff20;selection=accent;selected_scale=1.06;size=22"}}},
        {1, "목록", "가로 캐러셀", "가운데 카드가 크게, 양옆은 작고 흐리게 (◀▶)", {{"list", "style=carousel;direction=horizontal;rect=0,200,1280,330;item_size=230,230;spacing=250;visible_items=7;center_scale=1.35;side_scale=0.72;side_alpha=140;image=icon;card_color=#ffffff30;label=auto;size=22"}}},
        {1, "목록", "세로 휠", "왼쪽에 로고가 세로로 도는 휠 (▲▼)", {{"list", "style=carousel;direction=vertical;rect=20,0,480,720;item_size=400,96;spacing=112;visible_items=7;center_scale=1.2;side_scale=0.78;side_alpha=110;image=logo;card_color=#ffffff18;size=26"}}},
        {2, "목록", "게임 목록 (줄)", "게임 제목을 한 줄씩", {{"list", "style=text;rect=28,92,720,546;row_height=42;size=27;text_offset=12,8"}}},
        {2, "목록", "박스 격자", "박스 사진 바둑판 (6×2)", {{"list", "style=grid;rect=40,100,1200,540;columns=6;rows=2;gap=16,16;image=boxart;padding=6;selection=accent;selected_scale=1.06;size=18"}}},
        {2, "목록", "박스 캐러셀", "박스 사진이 가로로 흐름, 가운데 크게", {{"list", "style=carousel;direction=horizontal;rect=0,96,1280,330;item_size=190,266;spacing=200;visible_items=9;center_scale=1.18;side_scale=0.78;side_alpha=150;image=boxart;size=18"}}},
        {2, "목록", "제목 휠", "게임 제목이 세로로 도는 휠", {{"list", "style=carousel;direction=vertical;rect=30,60,560,600;item_size=540,52;spacing=56;visible_items=11;center_scale=1.1;side_scale=0.86;side_alpha=120;image=none;label=on;label_height=52;padding=0;size=24;selection=#00000000;color=dim;selected_color=text"}}},
        {1, "기종", "카테고리 이름", "커서가 있는 카테고리 이름", {{"+name", "type=text;text={category};rect=0,40,1280,0;align=center;size=40;font=bold"}}},
        {1, "기종", "게임 수", "카테고리의 게임 수", {{"+count", "type=text;text={games} GAMES;rect=0,96,1280,0;align=center;size=22;color=dim"}}},
        {1, "기종", "기종 그림 (아이콘)", "카테고리의 게임기 그림 (icons/)", {{"+machine", "type=image;source=icon;rect=760,120,440,400;fit=contain"}}},
        {1, "기종", "기종 로고", "카테고리 로고 (logos/)", {{"+logo", "type=image;source=logo;rect=760,40,440,100;fit=contain"}}},
        {1, "기종", "박스 미리보기", "그 카테고리 게임의 박스 사진 2×2", {{"preview", "rect=800,96,440,510"}}},
        {2, "게임", "화면 사진", "게임 화면 사진 (+박스 사진 겹침)", {{"media", "rect=800,92,440,300"}}},
        {2, "게임", "게임 정보 (묶음)", "제목·제작사·장르·설명을 차례로", {{"info", "rect=800,404,440,256"}}},
        {2, "게임", "게임 제목", "선택한 게임 제목 (★ 즐겨찾기 표시)", {{"+game_title", "type=text;text={fav?★ :}{title};rect=640,420,600,0;size=30;font=bold;scroll=on"}}},
        {2, "게임", "제작사·연도·장르", "값이 있는 것만 · 로 이어서", {{"+meta", "type=text;text={developer,year,genre| · };rect=640,466,600,0;size=20;color=dim"}}},
        {2, "게임", "설명", "게임 설명 (여러 줄, 있을 때만)", {{"+desc", "type=text;text={description};visible_if=description;rect=640,500,600,140;wrap=on;size=19;color=dim"}}},
        {2, "게임", "카테고리 이름·로고", "게임 화면 위 카테고리 (로고가 있으면 로고)", {{"title", "text={category};logo=on"}}},
        {2, "게임", "보기 탭", "ZL/ZR 전체 · 즐겨찾기 · 최근 실행, 고른 탭 강조",
         {{"+tab_all_bg", "type=rect;rect=470,26,110,34;color=accent;visible_if={view=all?1:}"},
          {"+tab_fav_bg", "type=rect;rect=585,26,120,34;color=accent;visible_if={view=fav?1:}"},
          {"+tab_recent_bg", "type=rect;rect=710,26,120,34;color=accent;visible_if={view=recent?1:}"},
          {"+tab_all", "type=text;text=전체;rect=470,26,110,34;align=center;valign=center;size=20"},
          {"+tab_fav", "type=text;text=★ 즐겨찾기;rect=585,26,120,34;align=center;valign=center;size=20"},
          {"+tab_recent", "type=text;text=최근 실행;rect=710,26,120,34;align=center;valign=center;size=20"}}},
        {2, "게임", "쪽 표시", "카테고리 순번 «2 / 13  R ▶»", {{"pager", ""}}},
        {2, "게임", "게임 번호", "«12 / 340»", {{"counter", ""}}},
        {2, "게임", "실행 기록", "실행 횟수·마지막 날짜 (있을 때만)", {{"+played", "type=text;text={playcount}회 실행 · {lastplayed};visible_if=playcount;rect=1240,650,0,0;align=right;size=17;color=accent"}}},
        {3, "기타", "배터리", "남은 배터리", {{"battery", ""}}},
        {3, "기타", "시계", "지금 시각", {{"+clock", "type=text;text={clock};rect=32,22,0,0;size=22;color=dim"}}},
        {3, "기타", "조작 도움말", "[A] 열기 … (문구 탭에서 바꿈)", {{"hint", ""}}},
        {3, "기타", "글자", "자유 글자 ({자리표} 사용)", {{"+text", "type=text;text=새 글자;rect=540,330,200,0;size=28"}}},
        {3, "기타", "그림", "내 그림 파일 (속성에서 가져오기)", {{"+image", "type=image;rect=490,260,300,200;fit=contain"}}},
        {3, "기타", "네모 (판)", "반투명 판·장식", {{"+box", "type=rect;rect=490,260,300,200;color=#00000080"}}},
    };
    return v;
}
static bool IsBuiltin(const std::string &key) { return std::string(LayoutDefaultText()).find("[" + key + "]") != std::string::npos; }
static void ApplyPreset(const Preset &p) {
    PushUndo();
    std::string first;
    for (const auto &ps : p.secs) {
        std::string name = ps.name;
        if (name[0] == '+') name = UniqueName(name.substr(1) + "_", ScreenPrefix());
        std::string key = ScreenPrefix() + name;
        if (name == "list")
            for (const char *k : kListReset) B.doc.Erase(key, k);
        std::string kvs = ps.kvs;
        size_t pos = 0;
        while (pos < kvs.size()) {
            size_t semi = kvs.find(';', pos);
            std::string kv = kvs.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            size_t eq = kv.find('=');
            if (eq != std::string::npos) B.doc.Set(key, kv.substr(0, eq), kv.substr(eq + 1));
            if (semi == std::string::npos) break;
            pos = semi + 1;
        }
        if (IsBuiltin(key)) B.doc.Set(key, "visible", "on");
        if (first.empty()) first = key;
    }
    B.sel = first;
    Changed();
    Status(std::string("넣음: ") + p.label);
    // 그림을 쓰는 요소인데 테마에 그 그림이 없으면 알려 줌
    std::string kvs;
    for (const auto &ps : p.secs) kvs += std::string(ps.name) + ";" + ps.kvs + ";";
    for (auto [needle, kind, ko] : std::initializer_list<std::tuple<const char *, const char *, const char *>>{
             {"background;", "backgrounds", "배경"}, {"image=icon", "icons", "아이콘"}, {"source=icon", "icons", "아이콘"},
             {"image=logo", "logos", "로고"}, {"source=logo", "logos", "로고"}})
        if (kvs.find(needle) != std::string::npos && ListDir(ThemeDir() + "/" + kind, false).empty()) {
            Status(std::string("넣음: ") + p.label + " — 테마에 " + ko + " 그림이 아직 없음: 아래 [그림] 탭에서 세트를 골라 \"전부 채우기\"");
            B.assetKind = kind;
            B.tabReq = "assets";
            break;
        }
}
static void PalettePanel() {
    int scr = B.games ? 2 : 1;
    const char *group = "";
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    int col = 0;
    for (const auto &p : Presets()) {
        if (!(p.scr & scr)) continue;
        if (strcmp(group, p.group) != 0) {
            group = p.group;
            ImGui::SeparatorText(group);
            col = 0;
        }
        if (col % 2) ImGui::SameLine();
        if (ImGui::Button(p.label, ImVec2(bw, 0))) ApplyPreset(p);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.desc);
        col++;
    }
}

// ---------------------------------------------------------------- 디자인 (고른 요소의 자주 쓰는 항목)
static void SetKV(const std::string &k, const std::string &v) {
    PushUndo();
    B.doc.Set(B.sel, k, v);
    Changed();
}
static bool ChoiceButtons(const char *label, const std::string &cur, std::initializer_list<std::pair<const char *, const char *>> opts, std::string &out) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110);
    bool hit = false;
    int i = 0;
    for (auto [lbl, val] : opts) {
        if (i++) ImGui::SameLine();
        bool on = cur == val;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(lbl)) { out = val; hit = true; }
        if (on) ImGui::PopStyleColor();
    }
    return hit;
}
static void ColorSwatches(const char *label, const std::string &key, const std::string &cur) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110);
    int i = 0;
    for (const char *c : {"text", "dim", "accent", "#ffffff", "#000000", "#00000080", "#ffffff30"}) {
        if (i++) ImGui::SameLine();
        ImVec4 col = ResolveColor(c);
        ImGui::PushID(c);
        if (ImGui::ColorButton(c, col, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(22, 22))) SetKV(key, c);
        if (cur == c) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 60, 255), 0, 0, 2);
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImVec4 c = ResolveColor(cur);
    float f[4] = {c.x, c.y, c.z, c.w};
    ImGui::PushID(key.c_str());
    if (ImGui::ColorEdit4("##pick", f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        static Uint32 at = 0;
        if (SDL_GetTicks() - at > 800) PushUndo();
        at = SDL_GetTicks();
        B.doc.Set(B.sel, key, ColorHex(ImVec4(f[0], f[1], f[2], f[3])));
        Changed();
    }
    ImGui::PopID();
}
static void DesignPanel(const Elem &e) {
    std::string v;
    auto cur = [&](const char *k) { return B.doc.Has(B.sel, k) ? B.doc.Get(B.sel, k) : EffVal(e, k); };
    switch (e.type) {
    case ElemType::Text: {
        int sz = e.size;
        std::string sc = sz <= 19 ? "s" : sz <= 26 ? "m" : sz <= 36 ? "l" : "x";
        if (ChoiceButtons("글자 크기", sc, {{"작게", "s"}, {"보통", "m"}, {"크게", "l"}, {"제목", "x"}}, v))
            SetKV("size", v == "s" ? "18" : v == "m" ? "24" : v == "l" ? "32" : "44");
        if (ChoiceButtons("굵기", e.bold ? "bold" : "regular", {{"보통", "regular"}, {"굵게", "bold"}}, v)) SetKV("font", v);
        if (ChoiceButtons("정렬", cur("align"), {{"왼쪽", "left"}, {"가운데", "center"}, {"오른쪽", "right"}}, v)) SetKV("align", v);
        ColorSwatches("색", "color", cur("color"));
        ImGui::TextUnformatted("내용");
        ImGui::SameLine(110);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##content", e.text.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (const auto &ph : kPlaceholders)
                if (ImGui::Selectable((std::string(ph.key) + "   " + ph.desc).c_str())) SetKV("text", ph.key);
            ImGui::EndCombo();
        }
        break;
    }
    case ElemType::List: {
        std::string shape = e.style == 2 ? (e.direction ? "wheel" : "carousel") : e.style == 1 ? "grid" : "text";
        if (ChoiceButtons("모양", shape, {{"줄 목록", "text"}, {"격자", "grid"}, {"가로 캐러셀", "carousel"}, {"세로 휠", "wheel"}}, v)) {
            for (const auto &p : Presets()) {
                if (!(p.scr & (B.games ? 2 : 1)) || p.secs.size() != 1 || std::string(p.secs[0].name) != "list") continue;
                std::string kv = p.secs[0].kvs;
                bool match = (v == "text" && kv.find("style=text") != std::string::npos) || (v == "grid" && kv.find("style=grid") != std::string::npos) ||
                             (v == "carousel" && kv.find("direction=horizontal") != std::string::npos) ||
                             (v == "wheel" && kv.find("direction=vertical") != std::string::npos);
                if (match) { std::string keep = B.sel; ApplyPreset(p); B.sel = keep; break; }
            }
        }
        if (e.style != 0 &&
            ChoiceButtons("칸 그림", cur("image"), {{"아이콘", "icon"}, {"로고", "logo"}, {"배경", "background"}, {"박스", "boxart"}, {"화면", "screenshot"}, {"없음", "none"}}, v))
            SetKV("image", v);
        int am = e.animMs >= 0 ? e.animMs : JoyLayout().animMs;
        if (ChoiceButtons("움직임", am <= 0 ? "0" : am <= 200 ? "150" : "300", {{"바로", "0"}, {"부드럽게", "150"}, {"느긋하게", "300"}}, v)) SetKV("anim_ms", v);
        ColorSwatches("선택 표시", "selection", cur("selection"));
        ColorSwatches("글자 색", "color", cur("color"));
        int sz = e.size;
        if (ChoiceButtons("글자 크기", sz <= 20 ? "s" : sz <= 26 ? "m" : "l", {{"작게", "s"}, {"보통", "m"}, {"크게", "l"}}, v))
            SetKV("size", v == "s" ? "18" : v == "m" ? "24" : "30");
        break;
    }
    case ElemType::Image: {
        std::string src = e.source.empty() ? "file" : e.source;
        if (ChoiceButtons("그림", src, {{"배경", "background"}, {"로고", "logo"}, {"아이콘", "icon"}, {"박스", "boxart"}, {"화면", "screenshot"}, {"내 파일", "file"}}, v)) {
            if (v == "file") {
                PushUndo();
                B.doc.Erase(B.sel, "source");
                Changed();
            } else {
                SetKV("source", v);
            }
        }
        if (src == "file") {
            ImGui::TextUnformatted("파일");
            ImGui::SameLine(110);
            if (ImGui::Button(e.file.empty() ? "그림 가져오기..." : e.file.c_str())) {
                std::string sel = B.sel;
                Browse("png jpg jpeg", [sel](const std::string &path) {
                    std::string err, stem = Stem(U8(P(path).filename()));
                    for (auto &ch : stem) if ((unsigned char)ch >= 0x80 || ch == ' ') ch = '_';
                    std::string out = ImportImage(path, ThemeDir() + "/ui", stem, "ui", err);
                    if (out.empty()) { Status("그림을 읽지 못함: " + err); return; }
                    PushUndo();
                    B.doc.Set(sel, "file", "ui/" + U8(P(out).filename()));
                    Changed();
                    AssetsChanged();
                });
            }
        }
        if (ChoiceButtons("맞추기", cur("fit"), {{"안에 맞춤", "contain"}, {"가득 (잘림)", "cover"}, {"늘림", "stretch"}}, v)) SetKV("fit", v);
        int al = e.alpha, dm = std::max(0, e.dim);
        ImGui::TextUnformatted("불투명도");
        ImGui::SameLine(110);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##alpha", &al, 0, 255)) { B.doc.Set(B.sel, "alpha", std::to_string(al)); Changed(); }
        if (ImGui::IsItemActivated()) PushUndo();
        ImGui::TextUnformatted("어둡게");
        ImGui::SameLine(110);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##dim", &dm, 0, 255)) { B.doc.Set(B.sel, "dim", std::to_string(dm)); Changed(); }
        if (ImGui::IsItemActivated()) PushUndo();
        break;
    }
    case ElemType::Rect:
        ColorSwatches("색", "color", cur("color"));
        break;
    default:
        ColorSwatches("색", "color", cur("color"));
        if (e.type == ElemType::Info || e.type == ElemType::Pager) {
            int sz = e.size;
            if (ChoiceButtons("글자 크기", sz <= 20 ? "s" : sz <= 24 ? "m" : "l", {{"작게", "s"}, {"보통", "m"}, {"크게", "l"}}, v))
                SetKV("size", v == "s" ? "18" : v == "m" ? "22" : "28");
        }
        break;
    }
}

// ---------------------------------------------------------------- 속성 창
static bool PropRow(const PropDef &pd, const Elem &e) {
    std::string sec = B.sel, k = pd.key;
    bool has = B.doc.Has(sec, k);
    std::string cur = has ? B.doc.Get(sec, k) : EffVal(e, k);
    bool changed = false;
    std::string nv = cur;
    ImGui::PushID(pd.key);
    if (has) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .85f, .4f, 1));
    ImGui::TextUnformatted(pd.label);
    if (has) ImGui::PopStyleColor();
    if (pd.help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pd.help);
    ImGui::SameLine(178);
    ImGui::SetNextItemWidth(-28);
    switch (pd.kind) {
    case PK_BOOL: {
        bool b = cur == "on" || cur == "true" || cur == "1" || cur == "yes";
        if (ImGui::Checkbox("##v", &b)) { nv = b ? "on" : "off"; changed = true; }
        break;
    }
    case PK_INT: {
        int v = atoi(cur.c_str());
        if (ImGui::DragInt("##v", &v, 0.5f)) { nv = std::to_string(v); changed = true; }
        break;
    }
    case PK_FLOAT: {
        float v = (float)atof(cur.c_str());
        if (ImGui::DragFloat("##v", &v, 0.05f, 0, 0, "%.2f")) { char b[32]; snprintf(b, sizeof(b), "%g", v); nv = b; changed = true; }
        break;
    }
    case PK_RECT:
    case PK_INT4:
    case PK_INT2: {
        int n = pd.kind == PK_INT2 ? 2 : 4, v[4] = {0, 0, 0, 0};
        sscanf(cur.c_str(), "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]);
        if (n == 4 ? ImGui::DragInt4("##v", v, 0.5f) : ImGui::DragInt2("##v", v, 0.5f)) {
            char b[64];
            if (n == 4) snprintf(b, sizeof(b), "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
            else snprintf(b, sizeof(b), "%d,%d", v[0], v[1]);
            nv = b;
            changed = true;
        }
        break;
    }
    case PK_COLOR: {
        ImVec4 c = ResolveColor(cur);
        float col[4] = {c.x, c.y, c.z, c.w};
        if (ImGui::ColorEdit4("##c", col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs)) {
            nv = ColorHex(ImVec4(col[0], col[1], col[2], col[3]));
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-28);
        if (ImGui::BeginCombo("##named", cur.c_str())) {
            std::vector<std::string> names = {"text", "dim", "accent", "selection", "bar"};
            for (const auto &kv : JoyLayout().colors)
                if (std::find(names.begin(), names.end(), kv.first) == names.end()) names.push_back(kv.first);
            for (const auto &n : names)
                if (ImGui::Selectable(n.c_str(), n == cur)) { nv = n; changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    case PK_CHOICE: {
        std::vector<std::string> opts;
        std::string c = pd.choices;
        size_t p = 0;
        while (true) {
            size_t q = c.find('|', p);
            opts.push_back(c.substr(p, q == std::string::npos ? q : q - p));
            if (q == std::string::npos) break;
            p = q + 1;
        }
        if (ImGui::BeginCombo("##v", cur.empty() ? "(없음)" : cur.c_str())) {
            for (const auto &o : opts)
                if (ImGui::Selectable(o.empty() ? "(없음)" : o.c_str(), o == cur)) { nv = o; changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    case PK_TEXT:
        if (ImGui::InputText("##v", &nv, ImGuiInputTextFlags_EnterReturnsTrue) || (ImGui::IsItemDeactivatedAfterEdit())) changed = nv != cur;
        break;
    case PK_TEXTML: {
        if (ImGui::InputText("##v", &nv)) changed = true;
        ImGui::SetNextItemWidth(-28);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 178);
        if (ImGui::BeginCombo("##ph", "자리표 넣기", ImGuiComboFlags_HeightLarge)) {
            for (const auto &ph : kPlaceholders)
                if (ImGui::Selectable((std::string(ph.key) + "   " + ph.desc).c_str())) { nv = cur + ph.key; changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    case PK_FILE:
    case PK_FOLDER: {
        bool folder = pd.kind == PK_FOLDER;
        if (ImGui::BeginCombo("##v", cur.empty() ? "(없음)" : cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("(없음)", cur.empty())) { nv = ""; changed = true; }
            std::error_code ec;
            for (fs::recursive_directory_iterator it(P(ThemeDir()), ec), end; !ec && it != end; it.increment(ec)) {
                std::string rel = U8(fs::relative(it->path(), P(ThemeDir()), ec));
                std::replace(rel.begin(), rel.end(), '\\', '/');
                if (folder ? !it->is_directory(ec) : (!it->is_regular_file(ec) || !IsImageName(rel))) continue;
                if (!folder && (rel.rfind("backgrounds/", 0) == 0 || rel.rfind("logos/", 0) == 0)) continue;  // 기종별 그림은 그림 창에서
                if (ImGui::Selectable(rel.c_str(), rel == cur)) { nv = rel; changed = true; }
            }
            ImGui::EndCombo();
        }
        if (!folder) {
            ImGui::SameLine();
            if (ImGui::SmallButton("가져오기")) {
                std::string key = k, sel = B.sel;
                Browse("png jpg jpeg", [key, sel](const std::string &path) {
                    std::string err, stem = Stem(U8(P(path).filename()));
                    for (auto &ch : stem) if ((unsigned char)ch >= 0x80 || ch == ' ') ch = '_';
                    std::string out = ImportImage(path, ThemeDir() + "/ui", stem, "ui", err);
                    if (out.empty()) { Status("그림을 읽지 못함: " + err); return; }
                    PushUndo();
                    B.doc.Set(sel, key, "ui/" + U8(P(out).filename()));
                    Changed();
                    AssetsChanged();
                });
            }
        }
        break;
    }
    }
    if (has) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 20);
        if (ImGui::SmallButton("x")) {
            PushUndo();
            B.doc.Erase(sec, k);
            Changed();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("기본값으로 (이 줄 지우기)");
    }
    ImGui::PopID();
    if (changed && nv != cur) {
        static std::string lastKey;
        static Uint32 lastAt = 0;
        // 끌기 조절은 한 번만 실행 취소에 쌓이게
        if (lastKey != sec + "/" + k || SDL_GetTicks() - lastAt > 800) PushUndo();
        lastKey = sec + "/" + k;
        lastAt = SDL_GetTicks();
        B.doc.Set(sec, k, nv);
        Changed();
        return true;
    }
    return false;
}

static void PropertiesPanel() {
    if (B.sel.empty()) {
        ImGui::TextWrapped("화면이나 왼쪽 레이어에서 요소를 고르면 디자인 항목이 켜집니다. 요소는 왼쪽 [요소 넣기] 에서 추가.");
        ImGui::BeginDisabled();
        ImGui::SeparatorText("디자인");
        std::string dummy;
        ChoiceButtons("글자 크기", "", {{"작게", "s"}, {"보통", "m"}, {"크게", "l"}, {"제목", "x"}}, dummy);
        ChoiceButtons("정렬", "", {{"왼쪽", "left"}, {"가운데", "center"}, {"오른쪽", "right"}}, dummy);
        ChoiceButtons("모양", "", {{"줄 목록", "text"}, {"격자", "grid"}, {"가로 캐러셀", "carousel"}, {"세로 휠", "wheel"}}, dummy);
        ImGui::EndDisabled();
        return;
    }
    const Elem *e = FindElemKey(B.sel);
    if (!e) {
        ImGui::TextWrapped("요소를 찾지 못함 (type 이 없거나 이름이 바뀜): %s", B.sel.c_str());
        return;
    }
    bool child = IsChildKey(B.sel);
    ImGui::Text("%s  [%s]%s", B.sel.c_str(), kTypeKo[(int)e->type], child ? "  칸 안 요소" : "");
    if (B.doc.Find(B.sel) == nullptr) ImGui::TextDisabled("기본 요소 (바꾼 속성만 layout.txt 에 적힘)");
    if (ImGui::Button("복제")) DuplicateSel();
    ImGui::SameLine();
    if (ImGui::Button("삭제")) DeleteSel();
    if (child || B.doc.Find(B.sel)) {
        ImGui::SameLine();
        if (ImGui::Button("type 바꾸기")) ImGui::OpenPopup("type");
        if (ImGui::BeginPopup("type")) {
            for (int i = 0; i < 9; i++)
                if (ImGui::Selectable(kTypeKo[i])) { PushUndo(); B.doc.Set(B.sel, "type", kTypeNames[i]); Changed(); }
            ImGui::EndPopup();
        }
    }
    ImGui::SeparatorText("디자인");
    DesignPanel(*e);
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("세부 속성 (layout.txt 의 모든 항목)")) {
        if (e->type == ElemType::List && !child) {
            ImGui::SeparatorText("칸 안 요소");
            if (ImGui::Button("+ 칸 글자")) AddElem("text", true);
            ImGui::SameLine();
            if (ImGui::Button("+ 칸 그림")) AddElem("image", true);
            ImGui::SameLine();
            if (ImGui::Button("+ 칸 네모")) AddElem("rect", true);
        }
        return;
    }
    unsigned bit = 1u << (int)e->type;
    const char *group = "";
    for (const auto &pd : kProps) {
        if (!(pd.types & bit)) continue;
        if ((pd.types & CHILD_ONLY) && !child) continue;
        if (strcmp(group, pd.group) != 0) {
            group = pd.group;
            ImGui::SeparatorText(group);
        }
        PropRow(pd, *e);
    }
    if (e->type == ElemType::List && !child) {
        ImGui::SeparatorText("칸 안 요소");
        ImGui::TextWrapped("칸(줄)마다 반복해서 그릴 글자·그림·네모. 자리는 칸 기준.");
        if (ImGui::Button("+ 칸 글자")) AddElem("text", true);
        ImGui::SameLine();
        if (ImGui::Button("+ 칸 그림")) AddElem("image", true);
        ImGui::SameLine();
        if (ImGui::Button("+ 칸 네모")) AddElem("rect", true);
    }
}

// ---------------------------------------------------------------- 레이어 창
static void LayersPanel() {
    if (ImGui::RadioButton("카테고리 화면", !B.games)) { B.games = false; B.sel.clear(); }
    ImGui::SameLine();
    if (ImGui::RadioButton("게임 화면", B.games)) { B.games = true; B.sel.clear(); }
    ImGui::Separator();
    float half = ImGui::GetContentRegionAvail().y * 0.55f;
    if (ImGui::CollapsingHeader("요소 넣기", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("palette", ImVec2(0, half), false);
        PalettePanel();
        ImGui::EndChild();
    }
    ImGui::SeparatorText("레이어 (위가 앞)");
    ImGui::BeginChild("layers");
    const Screen &sc = B.games ? JoyLayout().games : JoyLayout().categories;
    std::string prefix = ScreenPrefix();
    for (int i = (int)sc.elems.size() - 1; i >= 0; i--) {
        const Elem &e = sc.elems[i];
        std::string key = prefix + e.name;
        ImGui::PushID(key.c_str());
        bool vis = e.visible;
        if (ImGui::Checkbox("##vis", &vis)) { PushUndo(); B.doc.Set(key, "visible", vis ? "on" : "off"); Changed(); }
        ImGui::SameLine();
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "%s  · %s  z%d", e.name.c_str(), kTypeKo[(int)e.type], e.z);
        bool edited = B.doc.Find(key) != nullptr;
        if (!edited) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.7f, .7f, .75f, 1));
        if (ImGui::Selectable(lbl, B.sel == key)) B.sel = key;
        if (!edited) ImGui::PopStyleColor();
        for (const auto &c : e.children) {
            std::string ck = key + "." + c.name;
            ImGui::PushID(ck.c_str());
            ImGui::Indent(28);
            snprintf(lbl, sizeof(lbl), "└ %s  · %s", c.name.c_str(), kTypeKo[(int)c.type]);
            if (ImGui::Selectable(lbl, B.sel == ck)) B.sel = ck;
            ImGui::Unindent(28);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- 미리보기 화면 (선택·끌기)
static SDL_Rect BoundsOf(const std::string &key) {
    auto it = JoyBounds().find(key);
    if (it != JoyBounds().end() && it->second.w > 0) return it->second;
    if (const Elem *e = FindElemKey(key)) return {e->x, e->y, std::max(e->w, 8), std::max(e->h, 8)};
    return {0, 0, 0, 0};
}
// 점을 덮는 요소들 (위에 있는 것부터)
static std::vector<std::string> HitAll(int x, int y) {
    std::vector<std::string> v;
    const Screen &sc = B.games ? JoyLayout().games : JoyLayout().categories;
    std::string prefix = ScreenPrefix();
    for (int i = (int)sc.elems.size() - 1; i >= 0; i--) {
        const Elem &e = sc.elems[i];
        for (int j = (int)e.children.size() - 1; j >= 0; j--) {
            std::string ck = prefix + e.name + "." + e.children[j].name;
            auto it = JoyBounds().find(ck);
            if (it == JoyBounds().end()) continue;
            SDL_Point p = {x, y};
            if (SDL_PointInRect(&p, &it->second)) v.push_back(ck);
        }
        auto it = JoyBounds().find(prefix + e.name);
        if (it == JoyBounds().end()) continue;
        SDL_Point p = {x, y};
        if (SDL_PointInRect(&p, &it->second)) v.push_back(prefix + e.name);
    }
    return v;
}
static void SetRect(const std::string &key, int x, int y, int w, int h) {
    char b[64];
    snprintf(b, sizeof(b), "%d,%d,%d,%d", x, y, w, h);
    B.doc.Set(key, "rect", b);
    B.doc.Erase(key, "x");
    B.doc.Erase(key, "y");
    B.doc.Erase(key, "w");
    B.doc.Erase(key, "h");
    Apply();
}
static int Snap(int v) { return B.snap && B.grid > 1 ? (int)std::lround(v / (double)B.grid) * B.grid : v; }

// 지금 카테고리의 기종 이름 (그림 찾기용)
static std::string CurrentSystemId() {
    auto cols = JoyCollections();
    int c = JoyCategory();
    if (c < 0 || c >= (int)cols.size()) return "";
    for (const std::string &n : {cols[c].shortName, cols[c].name, cols[c].folder}) {
        std::string id = EsdeSystemName(Lower(n));
        if (!id.empty()) return id;
    }
    return Lower(cols[c].shortName.empty() ? cols[c].name : cols[c].shortName);
}
static void Canvas() {
    // 위 도구 줄
    auto cols = JoyCollections();
    int cat = JoyCategory();
    ImGui::SetNextItemWidth(220);
    std::string cur = cat < (int)cols.size() ? cols[cat].name + " (" + std::to_string(cols[cat].games) + ")" : "";
    if (ImGui::BeginCombo("카테고리", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)cols.size(); i++)
            if (ImGui::Selectable((cols[i].name + " (" + std::to_string(cols[i].games) + ")##" + std::to_string(i)).c_str(), i == cat)) JoySetCategory(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    int gi = JoyGameIndex();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("게임", &gi)) JoySetGameIndex(gi);
    ImGui::SameLine();
    for (auto [lbl, bt] : std::initializer_list<std::pair<const char *, int>>{{"◀", 12}, {"▲", 13}, {"▼", 15}, {"▶", 14}, {"L", 20}, {"R", 21}, {"ZL", 22}, {"ZR", 23}}) {
        if (ImGui::Button(lbl)) { if (bt >= 22) B.games = true; JoyNav(bt); }
        ImGui::SameLine();
    }
    if (ImGui::Button("A")) { if (!B.games) { JoyNav(0); B.games = true; } }
    ImGui::SameLine();
    if (ImGui::Button("B")) { if (B.games) { JoyNav(1); B.games = false; } }
    ImGui::SameLine();
    static const char *kViews[] = {"전체", "즐겨찾기", "최근 실행"};
    ImGui::TextDisabled("보기:%s", kViews[JoyView() % 3]);
    ImGui::SameLine();
    if (ImGui::Button("카테고리·기종 설정 (F2)")) { B.winCats = true; B.assetFilter = CurrentSystemId(); B.tabReq = "assets"; }
    ImGui::SameLine();
    if (ImGui::Button("테마 설정 (F3)")) B.winTheme = true;
    ImGui::SameLine();
    if (ImGui::Button("★ 즐겨찾기")) { B.games = true; JoyNav(30); }
    ImGui::SameLine();
    if (ImGui::Checkbox("English", &B.en)) JoySetLang(B.en);

    // 화면: 남은 자리에 가장 크게, 가운데 정렬
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float sc = std::min(avail.x / SW, (avail.y - 4) / SH);
    sc = std::max(0.2f, sc);
    ImVec2 c0 = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(c0.x + std::max(0.0f, (avail.x - SW * sc) / 2), c0.y + std::max(0.0f, (avail.y - SH * sc) / 2)));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)B.preview, ImVec2(SW * sc, SH * sc));
    bool hovered = ImGui::IsItemHovered();
    // 화면 밖(빈 곳)을 누르면 선택 해제
    if (!hovered && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0)) B.sel.clear();
    // 카테고리 목록을 두 번 누르면 그 기종의 그림·이름 설정 창
    if (hovered && !B.games && ImGui::IsMouseDoubleClicked(0)) {
        int mx2 = (int)((ImGui::GetMousePos().x - p0.x) / sc), my2 = (int)((ImGui::GetMousePos().y - p0.y) / sc);
        for (const auto &h : HitAll(mx2, my2))
            if (h.rfind("categories.list", 0) == 0) {
                B.winCats = true;
                B.assetFilter = CurrentSystemId();
                B.tabReq = "assets";
                break;
            }
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto toScr = [&](int x, int y) { return ImVec2(p0.x + x * sc, p0.y + y * sc); };
    ImVec2 mp = ImGui::GetMousePos();
    int mx = (int)((mp.x - p0.x) / sc), my = (int)((mp.y - p0.y) / sc);

    // 자리 표시
    if (B.showBounds && hovered && !B.dragging) {
        auto hits = HitAll(mx, my);
        if (!hits.empty()) {
            SDL_Rect r = BoundsOf(hits[0]);
            dl->AddRect(toScr(r.x, r.y), toScr(r.x + r.w, r.y + r.h), IM_COL32(120, 200, 255, 160));
            ImGui::SetTooltip("%s", hits[0].c_str());
        }
    }
    SDL_Rect sr = B.sel.empty() ? SDL_Rect{0, 0, 0, 0} : BoundsOf(B.sel);
    const Elem *se = B.sel.empty() ? nullptr : FindElemKey(B.sel);
    bool child = IsChildKey(B.sel);
    // 손잡이 (폭·높이가 정해진 요소만)
    bool sizable = se && se->w > 0 && se->h > 0 && !child;
    ImVec2 hs[8];
    if (se && sr.w > 0) {
        dl->AddRect(toScr(sr.x, sr.y), toScr(sr.x + sr.w, sr.y + sr.h), IM_COL32(255, 200, 60, 255), 0, 0, 2);
        if (sizable) {
            float x0 = p0.x + se->x * sc, y0 = p0.y + se->y * sc, x1 = p0.x + (se->x + se->w) * sc, y1 = p0.y + (se->y + se->h) * sc;
            hs[0] = {x0, y0}; hs[1] = {(x0 + x1) / 2, y0}; hs[2] = {x1, y0}; hs[3] = {x1, (y0 + y1) / 2};
            hs[4] = {x1, y1}; hs[5] = {(x0 + x1) / 2, y1}; hs[6] = {x0, y1}; hs[7] = {x0, (y0 + y1) / 2};
            for (auto &h : hs) dl->AddRectFilled(ImVec2(h.x - 4, h.y - 4), ImVec2(h.x + 4, h.y + 4), IM_COL32(255, 200, 60, 255));
        }
    }
    // 누르기: 손잡이 → 크기, 선택 요소 위 → 이동, 아니면 고르기 (같은 곳을 다시 누르면 아래 요소로)
    if (hovered && ImGui::IsMouseClicked(0)) {
        int h = -1;
        if (sizable)
            for (int i = 0; i < 8; i++)
                if (fabsf(mp.x - hs[i].x) < 7 && fabsf(mp.y - hs[i].y) < 7) h = i;
        SDL_Point pt = {mx, my};
        auto hits = HitAll(mx, my);
        if (h < 0) {
            if (ImGui::GetIO().KeyAlt && !hits.empty()) {  // Alt+누르기: 겹친 요소 중 다음 아래 요소
                auto it = std::find(hits.begin(), hits.end(), B.sel);
                B.sel = it == hits.end() || it + 1 == hits.end() ? hits[0] : *(it + 1);
            } else if (!(se && SDL_PointInRect(&pt, &sr))) {
                B.sel = hits.empty() ? "" : hits[0];  // 아무 요소도 없는 곳 = 선택 해제
            }
        }
        se = B.sel.empty() ? nullptr : FindElemKey(B.sel);
        if (se) {
            PushUndo();
            B.dragging = true;
            B.handle = h;
            B.dragStart = mp;
            B.ox = se->x; B.oy = se->y; B.ow = se->w; B.oh = se->h;
            if (B.doc.Find(B.sel) == nullptr || !B.doc.Has(B.sel, "rect")) {}  // 첫 이동 때 rect 를 적음
        }
    }
    if (B.dragging) {
        if (ImGui::IsMouseDown(0)) {
            int dx = (int)((mp.x - B.dragStart.x) / sc), dy = (int)((mp.y - B.dragStart.y) / sc);
            if (dx || dy) {
                int x = B.ox, y = B.oy, w = B.ow, h = B.oh;
                switch (B.handle) {
                case -1: x = Snap(B.ox + dx); y = Snap(B.oy + dy); break;
                case 0: x = Snap(B.ox + dx); y = Snap(B.oy + dy); w = B.ow - (x - B.ox); h = B.oh - (y - B.oy); break;
                case 1: y = Snap(B.oy + dy); h = B.oh - (y - B.oy); break;
                case 2: y = Snap(B.oy + dy); h = B.oh - (y - B.oy); w = Snap(B.ox + B.ow + dx) - x; break;
                case 3: w = Snap(B.ox + B.ow + dx) - x; break;
                case 4: w = Snap(B.ox + B.ow + dx) - x; h = Snap(B.oy + B.oh + dy) - y; break;
                case 5: h = Snap(B.oy + B.oh + dy) - y; break;
                case 6: x = Snap(B.ox + dx); w = B.ow - (x - B.ox); h = Snap(B.oy + B.oh + dy) - y; break;
                case 7: x = Snap(B.ox + dx); w = B.ow - (x - B.ox); break;
                }
                if (B.handle >= 0) { w = std::max(4, w); h = std::max(4, h); }
                SetRect(B.sel, x, y, w, h);
            }
        } else {
            B.dragging = false;
            // 움직이지 않았으면 쌓은 실행 취소를 되돌림
            if (!B.undo.empty() && B.undo.back().first == B.doc.Str()) B.undo.pop_back();
            SaveFiles();
        }
    }
    // 방향키로 1점 (Shift 10점) 옮기기
    if (se && !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused()) {
        int dx = 0, dy = 0, st = ImGui::GetIO().KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -st;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = st;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -st;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = st;
        if (dx || dy) {
            PushUndo();
            SetRect(B.sel, se->x + dx, se->y + dy, se->w, se->h);
            SaveFiles();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSel();
    }
    if (hovered) ImGui::GetIO().WantCaptureKeyboard = true;
}

// ---------------------------------------------------------------- 나만의 카테고리 (work/categories.txt — 런처 설정, 테마와 별개)
struct CatDef {
    std::string name, image, include, exclude, genre, games, position = "top";
    bool hideIncluded = true;
};
static std::vector<CatDef> gCats;
static std::vector<std::string> gCatHide;
static std::string CatPath() { return B.home + "/categories.txt"; }
static void LoadCats() {
    gCats.clear();
    gCatHide.clear();
    std::string text;
    ReadFile(CatPath(), text);
    ThemeTxt t;
    t.Parse(text);
    bool hide = false;
    for (const auto &l0 : t.lines) {
        std::string l = Trim(l0);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        if (l[0] == '[') {
            std::string n = l.substr(1, l.find(']') == std::string::npos ? std::string::npos : l.find(']') - 1);
            hide = Lower(n) == "hide";
            if (!hide) { gCats.push_back(CatDef()); gCats.back().name = n; }
            continue;
        }
        if (hide) { gCatHide.push_back(l); continue; }
        std::string k, v;
        if (gCats.empty() || !ThemeTxt::Split(l, k, v)) continue;
        CatDef &c = gCats.back();
        k = Lower(k);
        if (k == "image") c.image = v;
        else if (k == "include") c.include = v;
        else if (k == "exclude") c.exclude = v;
        else if (k == "genre") c.genre = v;
        else if (k == "games") c.games = v;
        else if (k == "position") c.position = v;
        else if (k == "hide_included") c.hideIncluded = Lower(v) != "off";
    }
}
static void SaveCats() {
    std::string o = "# Joy Launcher 나만의 카테고리 (테마 빌더에서 만듦). 설명: 테마_레이아웃.md 12장\n";
    for (const auto &c : gCats) {
        o += "\n[" + c.name + "]\n";
        if (!c.image.empty()) o += "image = " + c.image + "\n";
        if (!c.include.empty()) o += "include = " + c.include + "\n";
        if (!c.exclude.empty()) o += "exclude = " + c.exclude + "\n";
        if (!c.genre.empty()) o += "genre = " + c.genre + "\n";
        if (!c.games.empty()) o += "games = " + c.games + "\n";
        o += std::string("hide_included = ") + (c.hideIncluded ? "on" : "off") + "\n";
        o += "position = " + c.position + "\n";
    }
    if (!gCatHide.empty()) {
        o += "\n[hide]\n";
        for (const auto &h : gCatHide) o += h + "\n";
    }
    WriteFile(CatPath(), o);
    JoyReloadCategories();
}
static std::vector<std::string> SplitComma(const std::string &v) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= v.size()) {
        size_t q = v.find(',', p);
        std::string part = Trim(v.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (!part.empty()) out.push_back(part);
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return out;
}
static std::string JoinComma(const std::vector<std::string> &v) {
    std::string o;
    for (const auto &s : v) o += (o.empty() ? "" : ", ") + s;
    return o;
}
static std::string CatImageId(const CatDef &c) { return Lower(c.image.empty() ? c.name : c.image); }

static void CategoriesPanel() {
    ImGui::TextWrapped("여러 롬 폴더·카테고리를 묶은 내 카테고리 (예: PlayStation = PS1 + PS2 + PSP). 테마와 별개로 스위치의 /JoyLauncher/categories.txt 에 저장됩니다. 그림은 [그림] 탭에서 \"내 그림 폴더\" 를 고르고 넣으면 모든 테마에서 쓰입니다.");
    ImGui::BeginChild("catlist", ImVec2(230, 0), true);
    for (int i = 0; i < (int)gCats.size(); i++)
        if (ImGui::Selectable((gCats[i].name + "##" + std::to_string(i)).c_str(), B.catSel == i)) B.catSel = i;
    ImGui::Separator();
    if (ImGui::Button("+ 추가")) {
        CatDef c;
        c.name = "새 카테고리 " + std::to_string(gCats.size() + 1);
        c.image = "mycat" + std::to_string(gCats.size() + 1);
        gCats.push_back(c);
        B.catSel = (int)gCats.size() - 1;
        SaveCats();
    }
    ImGui::SameLine();
    if (ImGui::Button("삭제") && B.catSel >= 0 && B.catSel < (int)gCats.size()) {
        gCats.erase(gCats.begin() + B.catSel);
        B.catSel = std::min(B.catSel, (int)gCats.size() - 1);
        SaveCats();
    }
    if (B.catSel > 0 && ImGui::Button("위로")) { std::swap(gCats[B.catSel], gCats[B.catSel - 1]); B.catSel--; SaveCats(); }
    ImGui::SameLine();
    if (B.catSel >= 0 && B.catSel + 1 < (int)gCats.size() && ImGui::Button("아래로")) { std::swap(gCats[B.catSel], gCats[B.catSel + 1]); B.catSel++; SaveCats(); }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("catedit", ImVec2(0, 0), true);
    auto base = JoyBaseCollections();
    if (B.catSel >= 0 && B.catSel < (int)gCats.size()) {
        CatDef &c = gCats[B.catSel];
        bool ch = false;
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("이름 (화면에 보임)", &c.name);
        ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("그림 이름 (영문)", &c.image);
        ch |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("테마·내 그림 폴더에서 이 이름의 배경·로고·아이콘을 찾음. 없으면 묶은 첫 기종의 그림");
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("장르로 모으기", "예: RPG, 슈팅 (비우면 장르 상관없음)", &c.genre);
        ch |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::Checkbox("묶은 원래 카테고리는 목록에서 숨김", &c.hideIncluded);
        const char *posName = c.position == "bottom" ? "맨 아래" : c.position.rfind("after:", 0) == 0 ? c.position.c_str() : "맨 위";
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("목록 자리", posName)) {
            if (ImGui::Selectable("맨 위")) { c.position = "top"; ch = true; }
            if (ImGui::Selectable("맨 아래")) { c.position = "bottom"; ch = true; }
            for (const auto &b : base)
                if (ImGui::Selectable(("after:" + b.name).c_str())) { c.position = "after:" + b.name; ch = true; }
            ImGui::EndCombo();
        }
        ImGui::SeparatorText("묶을 카테고리");
        auto inc = SplitComma(c.include);
        bool all = std::find(inc.begin(), inc.end(), "*") != inc.end();
        if (ImGui::Checkbox("모든 카테고리 (*)", &all)) { c.include = all ? "*" : ""; ch = true; inc = SplitComma(c.include); }
        if (!all) {
            if (ImGui::BeginTable("inc", 3)) {
                for (const auto &b : base) {
                    ImGui::TableNextColumn();
                    bool on = std::find(inc.begin(), inc.end(), b.name) != inc.end();
                    std::string lbl = b.name + " (" + std::to_string(b.games) + ")";
                    if (ImGui::Checkbox(lbl.c_str(), &on)) {
                        if (on) inc.push_back(b.name);
                        else inc.erase(std::remove(inc.begin(), inc.end(), b.name), inc.end());
                        c.include = JoinComma(inc);
                        ch = true;
                    }
                }
                ImGui::EndTable();
            }
        }
        if (ch) SaveCats();
    } else {
        ImGui::TextDisabled("왼쪽에서 고르거나 + 추가");
    }
    ImGui::SeparatorText("목록에서 숨길 카테고리 ([hide])");
    if (ImGui::BeginTable("hide", 3)) {
        for (const auto &b : base) {
            ImGui::TableNextColumn();
            bool on = std::find(gCatHide.begin(), gCatHide.end(), b.name) != gCatHide.end();
            if (ImGui::Checkbox(("##h" + b.name).c_str(), &on)) {
                if (on) gCatHide.push_back(b.name);
                else gCatHide.erase(std::remove(gCatHide.begin(), gCatHide.end(), b.name), gCatHide.end());
                SaveCats();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(b.name.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- 스위치로 보내기 (FTP)
struct FtpState {
    std::string host = "192.168.0.10", user, pass, base = "/JoyLauncher";
    int port = 5000;
    bool onlyChanged = true, withCats = true;
    std::mutex m;
    std::vector<std::string> log;
    std::atomic<int> done{0}, total{0};
    std::atomic<bool> running{false};
    std::vector<std::string> remote;  // 스위치에 있는 테마 (목록 읽은 결과)
    bool remoteRead = false;
    std::string delAsk;                // 지울지 묻는 테마
};
static FtpState gFtp;
static void FtpLog(const std::string &s) {
    std::lock_guard<std::mutex> l(gFtp.m);
    gFtp.log.push_back(s);
    if (gFtp.log.size() > 300) gFtp.log.erase(gFtp.log.begin());
}
static void LoadFtpSettings() {
    std::string t;
    if (!ReadFile(B.home + "/builder_ftp.txt", t)) return;
    ThemeTxt f;
    f.Parse(t);
    if (!f.Get("host").empty()) gFtp.host = f.Get("host");
    if (!f.Get("port").empty()) gFtp.port = atoi(f.Get("port").c_str());
    gFtp.user = f.Get("user");
    gFtp.pass = f.Get("pass");
    if (!f.Get("base").empty()) gFtp.base = f.Get("base");
}
static void SaveFtpSettings() {
    WriteFile(B.home + "/builder_ftp.txt", "host=" + gFtp.host + "\nport=" + std::to_string(gFtp.port) + "\nuser=" + gFtp.user +
                                               "\npass=" + gFtp.pass + "\nbase=" + gFtp.base + "\n");
}
// (로컬 경로, 원격 경로) 목록
static void FtpCollect(const std::string &dir, const std::string &remote, std::vector<std::pair<std::string, std::string>> &out) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = U8(fs::relative(it->path(), P(dir), ec));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (rel == "layout_errors.txt" || rel == "builder_fill.txt") continue;
        out.push_back({U8(it->path()), remote + "/" + rel});
    }
}
static void FtpSendTheme() {
    if (gFtp.running || B.theme.empty()) return;
    SaveFiles();
    SaveFtpSettings();
    std::vector<std::pair<std::string, std::string>> files;
    std::string base = gFtp.base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    FtpCollect(ThemeDir(), base + "/themes/" + B.theme, files);
    if (gFtp.withCats) {
        if (Exists(CatPath())) files.push_back({CatPath(), base + "/categories.txt"});
        if (IsDir(B.home + "/categories")) FtpCollect(B.home + "/categories", base + "/categories", files);
    }
    gFtp.total = (int)files.size();
    gFtp.done = 0;
    gFtp.running = true;
    {
        std::lock_guard<std::mutex> l(gFtp.m);
        gFtp.log.clear();
    }
    std::string host = gFtp.host, user = gFtp.user, pass = gFtp.pass;
    int port = gFtp.port;
    bool onlyChanged = gFtp.onlyChanged;
    std::thread([files, host, port, user, pass, onlyChanged]() {
        Ftp f;
        std::string err;
        FtpLog("연결: " + host + ":" + std::to_string(port));
        if (!f.Login(host, port, user, pass, err)) {
            FtpLog("실패: " + err);
            gFtp.running = false;
            return;
        }
        std::set<std::string> made;
        int sent = 0, skipped = 0, failed = 0;
        for (const auto &fp : files) {
            // 폴더 만들기 (있으면 무시)
            std::string r = fp.second;
            for (size_t p = r.find('/', 1); p != std::string::npos; p = r.find('/', p + 1)) {
                std::string d = r.substr(0, p);
                if (made.insert(d).second) f.Mkd(d);
            }
            std::string data;
            if (!ReadFile(fp.first, data)) { failed++; FtpLog("읽기 실패: " + fp.first); gFtp.done++; continue; }
            if (onlyChanged && f.Size(fp.second) == (long long)data.size()) { skipped++; gFtp.done++; continue; }
            if (f.Stor(fp.second, data, err)) {
                sent++;
                FtpLog("보냄: " + fp.second);
            } else {
                failed++;
                FtpLog("실패: " + err);
                if (f.c == INVALID_SOCKET || err.find("PASV") != std::string::npos) break;
            }
            gFtp.done++;
        }
        f.Cmd("QUIT");
        FtpLog("끝: 보냄 " + std::to_string(sent) + ", 같아서 건너뜀 " + std::to_string(skipped) + ", 실패 " + std::to_string(failed) +
               "  →  스위치에서 Y 옵션 → 테마 설정 → 이 테마 → A");
        gFtp.running = false;
    }).detach();
}
static std::string FtpBase() {
    std::string base = gFtp.base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}
// 스위치의 테마 목록 읽기 / 테마 지우기 (del 이 비어 있으면 목록만)
static void FtpRemoteJob(const std::string &del) {
    if (gFtp.running) return;
    SaveFtpSettings();
    gFtp.running = true;
    gFtp.total = 0;
    gFtp.done = 0;
    std::string host = gFtp.host, user = gFtp.user, pass = gFtp.pass, base = FtpBase();
    int port = gFtp.port;
    std::thread([host, port, user, pass, base, del]() {
        Ftp f;
        std::string err;
        FtpLog("연결: " + host + ":" + std::to_string(port));
        if (!f.Login(host, port, user, pass, err)) {
            FtpLog("실패: " + err);
            gFtp.running = false;
            return;
        }
        if (!del.empty()) {
            int n = f.RemoveTree(base + "/themes/" + del, err);
            if (n < 0) FtpLog("지우기 실패: " + err);
            else FtpLog("스위치에서 지움: " + del + " (" + std::to_string(n) + "개)");
        }
        std::vector<std::pair<std::string, bool>> ents;
        std::vector<std::string> names;
        if (f.List(base + "/themes", ents, err)) {
            for (const auto &e : ents)
                if (e.second) names.push_back(e.first);
            std::sort(names.begin(), names.end());
            FtpLog("스위치 테마 " + std::to_string(names.size()) + "개");
        } else
            FtpLog("목록 실패: " + err);
        f.Cmd("QUIT");
        {
            std::lock_guard<std::mutex> l(gFtp.m);
            gFtp.remote = names;
            gFtp.remoteRead = true;
        }
        gFtp.running = false;
    }).detach();
}
static void FtpWindow(bool open) {
    if (open) ImGui::OpenPopup("스위치로 보내기");
    ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("스위치로 보내기")) {
        ImGui::TextWrapped("스위치에서 ftpd(또는 sys-ftpd)를 켜고 화면에 보이는 주소·포트를 적으세요. 테마 폴더만 /JoyLauncher/themes/%s 로 보냅니다 (런처·설정은 건드리지 않음).",
                           B.theme.c_str());
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("주소", &gFtp.host);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("포트", &gFtp.port, 0);
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("사용자", "비우면 익명", &gFtp.user);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::InputText("비밀번호", &gFtp.pass, ImGuiInputTextFlags_Password);
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("Joy Launcher 폴더", &gFtp.base);
        ImGui::Checkbox("바뀐 파일만 (크기 비교)", &gFtp.onlyChanged);
        ImGui::SameLine();
        ImGui::Checkbox("내 카테고리도 (categories.txt + categories 폴더)", &gFtp.withCats);
        ImGui::BeginDisabled(gFtp.running);
        if (ImGui::Button("보내기")) FtpSendTheme();
        ImGui::EndDisabled();
        ImGui::SameLine();
        int tot = gFtp.total, dn = gFtp.done;
        ImGui::ProgressBar(tot ? dn / (float)tot : 0.0f, ImVec2(-90, 0), (std::to_string(dn) + " / " + std::to_string(tot)).c_str());
        ImGui::SameLine();
        if (ImGui::Button("닫기")) ImGui::CloseCurrentPopup();
        // 스위치에 있는 테마 (목록·지우기)
        ImGui::SeparatorText("스위치에 있는 테마");
        ImGui::BeginDisabled(gFtp.running);
        if (ImGui::Button("목록 읽기")) FtpRemoteJob("");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("테마가 너무 많으면 여기서 지움 (스위치 테마 선택 창의 [X] 와 같음)");
        {
            std::vector<std::string> rem;
            bool rd;
            {
                std::lock_guard<std::mutex> l(gFtp.m);
                rem = gFtp.remote;
                rd = gFtp.remoteRead;
            }
            ImGui::BeginChild("ftpremote", ImVec2(0, 130), true);
            if (!rd) ImGui::TextDisabled("[목록 읽기] 를 누르면 스위치의 /JoyLauncher/themes 를 보여 줌");
            else if (rem.empty()) ImGui::TextDisabled("(테마 없음)");
            for (const auto &n : rem) {
                ImGui::PushID(n.c_str());
                ImGui::BeginDisabled(gFtp.running);
                if (ImGui::SmallButton("지우기")) gFtp.delAsk = n;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted(n.c_str());
                if (n == B.theme) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(지금 만드는 테마)");
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if (!gFtp.delAsk.empty()) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "스위치에서 '%s' 테마를 지울까요? (되돌릴 수 없음)", gFtp.delAsk.c_str());
            ImGui::SameLine();
            if (ImGui::Button("지우기##yes")) {
                FtpRemoteJob(gFtp.delAsk);
                gFtp.delAsk.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("취소##no")) gFtp.delAsk.clear();
        }
        ImGui::BeginChild("ftplog", ImVec2(0, 0), true);
        {
            std::lock_guard<std::mutex> l(gFtp.m);
            for (const auto &s : gFtp.log) ImGui::TextUnformatted(s.c_str());
            if (gFtp.running) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- 그림 창
static void AssetsPanel() {
    if (B.theme.empty()) return;
    if (ImGui::RadioButton("이 테마에 넣기", !B.assetUser)) B.assetUser = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("내 그림 폴더 (모든 테마 공통)", B.assetUser)) B.assetUser = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("스위치의 /JoyLauncher/categories/<종류>/ — 테마에 그림이 없을 때 씀 (나만의 카테고리는 기종 그림보다 먼저)");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    std::vector<std::string> kinds = {"backgrounds", "logos", "icons"};
    if (!B.assetUser)
    for (const auto &d : ListDir(ThemeDir(), true))
        if (std::find(kinds.begin(), kinds.end(), d) == kinds.end() && d != "fonts" && d != "credits") kinds.push_back(d);
    for (const auto &k : kinds) {
        ImGui::SameLine();
        if (ImGui::RadioButton(kKindKo(k), B.assetKind == k)) B.assetKind = k;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ 폴더")) ImGui::OpenPopup("newfolder");
    if (ImGui::BeginPopup("newfolder")) {
        static std::string nf = "controllers";
        ImGui::InputText("폴더 이름 (영문)", &nf);
        if (ImGui::Button("만들기")) {
            std::error_code ec;
            fs::create_directories(P(ThemeDir() + "/" + nf), ec);
            B.assetKind = nf;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // 한꺼번에 채우기
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("##set", B.bulkSet.c_str())) {
        for (const auto &s : B.libSets)
            if (ImGui::Selectable(s.c_str(), s == B.bulkSet)) B.bulkSet = s;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    static bool coreOnly = true;
    auto bulk = [&](bool onlyMissing) {
        int n = FillKind(B.bulkSet, B.assetKind, coreOnly, onlyMissing);
        AssetsChanged();
        Status(std::to_string(n) + "개 넣음 (" + B.bulkSet + " → " + kKindKo(B.assetKind) + ")");
    };
    // 한 기종만 보는 중(지금 카테고리)이면: 이 세트의 그림을 그 기종에만 넣기
    bool single = !B.assetFilter.empty() && B.assetFilter == CurrentSystemId();
    if (single) {
        static bool allKinds = true;
        std::string id = B.assetFilter;
        std::string lbl = "이 세트를 '" + id + "' 에 적용";
        if (ImGui::Button(lbl.c_str())) {
            PushUndo();
            int n = 0;
            std::string kinds;
            for (const auto &kv : B.lib[B.bulkSet]) {
                if (kv.first == "fonts" || kv.first == "ui") continue;
                if (!allKinds && kv.first != B.assetKind) continue;
                auto it = kv.second.find(id);
                if (it == kv.second.end()) continue;
                if (AssignAsset(kv.first, id, it->second)) {
                    n++;
                    kinds += std::string(kinds.empty() ? "" : "·") + kKindKo(kv.first);
                }
            }
            AssetsChanged();
            Status(n ? id + " ← " + B.bulkSet + " (" + kinds + ")" : B.bulkSet + " 세트에 " + id + " 그림이 없음");
        }
        ImGui::SameLine();
        ImGui::Checkbox("배경·로고·아이콘 모두", &allKinds);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("끄면 지금 보는 종류(%s)만", kKindKo(B.assetKind));
    } else {
        if (ImGui::Button("이 세트로 전부 채우기")) bulk(false);
        ImGui::SameLine();
        if (ImGui::Button("없는 것만 채우기")) bulk(true);
        ImGui::SameLine();
        ImGui::Checkbox("주요 기종만", &coreOnly);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("스위치 런처에서 쓰는 35개 기종만 (용량 절약). 끄면 세트의 모든 기종");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##flt", "기종 찾기", &B.assetFilter);
    ImGui::SameLine();
    ImGui::TextDisabled("테마 용량 %s", HumanSize(B.themeSize).c_str());

    auto have = ThemeImages(B.assetKind);
    // 행: 기종 표 + 테마에만 있는 이름
    std::vector<std::pair<std::string, std::string>> rows;
    std::set<std::string> listed;
    for (const auto &c : gCats)
        if (listed.insert(CatImageId(c)).second) rows.push_back({CatImageId(c), "내 카테고리: " + c.name});
    for (const auto &s : EsdeSystems())
        if (listed.insert(s.id).second) rows.push_back({s.id, s.ko});
    for (const auto &kv : have)
        if (!listed.count(kv.first)) { rows.push_back({kv.first, ""}); listed.insert(kv.first); }
    for (const auto &set : B.libSets)
        for (const auto &kv : B.lib[set][B.assetKind])
            if (!listed.count(kv.first)) { rows.push_back({kv.first, ""}); listed.insert(kv.first); }

    float th = 54;
    if (ImGui::BeginTable("assets", 3 + (int)B.libSets.size(), ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("기종", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("테마 그림", ImGuiTableColumnFlags_WidthFixed, th + 70);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 96);
        for (const auto &s : B.libSets) ImGui::TableSetupColumn(s.c_str(), ImGuiTableColumnFlags_WidthFixed, th + 8);
        ImGui::TableHeadersRow();
        for (const auto &row : rows) {
            const std::string &id = row.first;
            if (!B.assetFilter.empty() && Lower(id + row.second).find(Lower(B.assetFilter)) == std::string::npos &&
                (id + row.second).find(B.assetFilter) == std::string::npos)
                continue;
            bool any = have.count(id);
            for (const auto &set : B.libSets) any = any || B.lib[set][B.assetKind].count(id);
            if (!any && B.assetFilter.empty() && row.second.rfind("내 카테고리", 0) != 0) continue;  // 어디에도 그림이 없는 기종은 숨김 (찾으면 보임)
            ImGui::TableNextRow(0, th + 4);
            ImGui::PushID(id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(id.c_str());
            if (!row.second.empty()) ImGui::TextDisabled("%s", row.second.c_str());
            ImGui::TableNextColumn();
            auto it = have.find(id);
            if (it != have.end()) {
                Thumb(it->second, th);
                if (ImGui::IsItemHovered()) {
                    TexEntry *t = Tex(it->second);
                    ImGui::SetTooltip("%s\n%dx%d", it->second.c_str(), t ? t->w : 0, t ? t->h : 0);
                }
            } else {
                ImGui::TextDisabled("없음");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("파일...")) {
                std::string kind = B.assetKind;
                B.pendKind = kind;
                B.pendStem = id;
                Browse("png jpg jpeg", [kind, id](const std::string &path) {
                    PushUndo();
                    if (AssignAsset(kind, id, path)) { AssetsChanged(); Status(id + " 넣음"); }
                    B.pendKind.clear();
                });
            }
            if (it != have.end() && ImGui::SmallButton("지우기")) {
                std::error_code ec;
                fs::remove(P(it->second), ec);
                AssetsChanged();
            }
            for (const auto &set : B.libSets) {
                ImGui::TableNextColumn();
                auto li = B.lib[set][B.assetKind].find(id);
                if (li == B.lib[set][B.assetKind].end()) continue;
                ImGui::PushID(set.c_str());
                ImVec2 c = ImGui::GetCursorScreenPos();
                Thumb(li->second, th);
                if (ImGui::IsItemHovered()) {
                    ImGui::GetWindowDrawList()->AddRect(c, ImVec2(c.x + th, c.y + th), IM_COL32(255, 200, 60, 255), 0, 0, 2);
                    ImGui::SetTooltip("누르면 이 그림을 넣음\n%s", li->second.c_str());
                }
                if (ImGui::IsItemClicked()) {
                    if (AssignAsset(B.assetKind, id, li->second)) { AssetsChanged(); Status(id + " ← " + set); }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- 테마 설정 창
static void ThemePanel() {
    if (B.theme.empty()) return;
    ImGui::SeparatorText("이름 (theme.txt)");
    for (auto [k, lbl] : std::initializer_list<std::pair<const char *, const char *>>{{"name", "테마 이름"}, {"name_en", "영어 이름"}}) {
        std::string v = B.ttxt.Get(k);
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText(lbl, &v, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
            if (v != B.ttxt.Get(k)) { PushUndo(); B.ttxt.Set(k, v); SaveFiles(); }
        }
    }
    {
        std::string dv = B.ttxt.Get("dim");
        int d = dv.empty() ? 150 : atoi(dv.c_str());
        ImGui::SetNextItemWidth(300);
        if (ImGui::SliderInt("배경 어둡게 (dim)", &d, 0, 255)) { B.ttxt.Set("dim", std::to_string(d)); SaveFiles(); AssetsChanged(); }
    }
    ImGui::SeparatorText("글꼴·색 ([theme])");
    auto fontRow = [&](const char *key, const char *lbl) {
        std::string v = B.doc.Get("theme", key);
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo(lbl, v.empty() ? "(내장 나눔고딕)" : v.c_str())) {
            if (ImGui::Selectable("(내장 나눔고딕)", v.empty())) { PushUndo(); B.doc.Erase("theme", key); Changed(); }
            for (const auto &f : ListDir(ThemeDir() + "/fonts", false)) {
                std::string e = Ext(f);
                if (e != "ttf" && e != "otf") continue;
                std::string rel = "fonts/" + f;
                if (ImGui::Selectable(rel.c_str(), rel == v)) { PushUndo(); B.doc.Set("theme", key, rel); Changed(); }
            }
            for (const auto &set : B.libSets)
                for (const auto &kv : B.lib[set]["fonts"])
                    if (ImGui::Selectable(("라이브러리: " + set + "/" + U8(P(kv.second).filename())).c_str())) {
                        std::string fn = U8(P(kv.second).filename());
                        std::error_code ec;
                        fs::create_directories(P(ThemeDir() + "/fonts"), ec);
                        fs::copy_file(P(kv.second), P(ThemeDir() + "/fonts/" + fn), fs::copy_options::overwrite_existing, ec);
                        PushUndo();
                        B.doc.Set("theme", key, "fonts/" + fn);
                        Changed();
                    }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::PushID(key);
        if (ImGui::SmallButton("가져오기")) {
            std::string k2 = key;
            Browse("ttf otf", [k2](const std::string &path) {
                std::string fn = U8(P(path).filename());
                for (auto &ch : fn) if ((unsigned char)ch >= 0x80 || ch == ' ') ch = '_';
                std::error_code ec;
                fs::create_directories(P(ThemeDir() + "/fonts"), ec);
                fs::copy_file(P(path), P(ThemeDir() + "/fonts/" + fn), fs::copy_options::overwrite_existing, ec);
                PushUndo();
                B.doc.Set("theme", k2, "fonts/" + fn);
                Changed();
            });
        }
        ImGui::PopID();
    };
    fontRow("font", "글꼴");
    fontRow("font_bold", "굵은 글꼴");
    auto colorRow = [&](const std::string &key, const char *lbl, const char *def) {
        std::string v = B.doc.Get("theme", key, def);
        ImVec4 c = ResolveColor(v);
        float col[4] = {c.x, c.y, c.z, c.w};
        ImGui::PushID(key.c_str());
        if (ImGui::ColorEdit4(lbl, col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            static Uint32 at = 0;
            if (SDL_GetTicks() - at > 800) PushUndo();
            at = SDL_GetTicks();
            B.doc.Set("theme", key, ColorHex(ImVec4(col[0], col[1], col[2], col[3])));
            Changed();
        }
        if (B.doc.Has("theme", key)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) { PushUndo(); B.doc.Erase("theme", key); Changed(); }
        }
        ImGui::PopID();
    };
    colorRow("background_color", "바탕색 (배경 그림 없을 때)", "#1e1e26");
    colorRow("color.text", "text  (기본 글자)", "#ebebf0");
    colorRow("color.dim", "dim  (흐린 글자)", "#9696a5");
    colorRow("color.accent", "accent  (제목)", "#ffc440");
    colorRow("color.selection", "selection  (선택 막대)", "#46466ed2");
    colorRow("color.bar", "bar  (위·아래 막대)", "#14141ac8");
    if (IniSec *ts = B.doc.Find("theme"))
        for (const auto &kv : std::vector<IniKV>(ts->kv))
            if (!kv.raw && kv.key.rfind("color.", 0) == 0 && kv.key != "color.text" && kv.key != "color.dim" && kv.key != "color.accent" &&
                kv.key != "color.selection" && kv.key != "color.bar")
                colorRow(kv.key, kv.key.substr(6).c_str(), "#ffffff");
    static std::string newColor = "navy";
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##nc", &newColor);
    ImGui::SameLine();
    if (ImGui::Button("색 이름 추가") && !newColor.empty()) { PushUndo(); B.doc.Set("theme", "color." + Lower(newColor), "#3b4191"); Changed(); }
    ImGui::SeparatorText("화면 효과 (움직임·전환)");
    auto msRow = [&](const char *key, const char *lbl, int def) {
        std::string v = B.doc.Get("theme", key);
        int x = v.empty() ? def : atoi(v.c_str());
        ImGui::SetNextItemWidth(300);
        if (ImGui::SliderInt(lbl, &x, 0, 1000, "%d ms")) {
            static Uint32 at = 0;
            if (SDL_GetTicks() - at > 800) PushUndo();
            at = SDL_GetTicks();
            if (x == 0) B.doc.Erase("theme", key);
            else B.doc.Set("theme", key, std::to_string(x));
            Changed();
        }
    };
    msRow("anim_ms", "선택·스크롤 움직임", 0);
    msRow("bg_fade_ms", "배경 바뀜 (서서히)", 0);
    {
        std::string tr = B.doc.Get("theme", "transition", "none");
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("화면 전환", tr == "fade" ? "fade (겹쳐 바뀜)" : tr == "slide" ? "slide (밀어냄)" : "none (없음)")) {
            for (const char *o : {"none", "fade", "slide"})
                if (ImGui::Selectable(o, tr == o)) {
                    PushUndo();
                    if (std::string(o) == "none") B.doc.Erase("theme", "transition");
                    else B.doc.Set("theme", "transition", o);
                    Changed();
                }
            ImGui::EndCombo();
        }
    }
    msRow("transition_ms", "화면 전환 시간", 220);
    ImGui::TextDisabled("스위치 옵션의 \"화면 효과\" 를 끄면 모두 바로 바뀜. 미리보기에서 ◀▶·A·B 로 확인");
    ImGui::SeparatorText("최소 버전");
    std::string req = B.doc.Get("theme", "requires");
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputText("requires", &req, ImGuiInputTextFlags_EnterReturnsTrue)) {
        PushUndo();
        if (req.empty()) B.doc.Erase("theme", "requires");
        else B.doc.Set("theme", "requires", req);
        Changed();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("확장 문법을 쓰면 내보낼 때 자동으로 1.1.0");
}

// [strings] / [names] 표
static void TablePanel(const char *section, const char *colA, const char *colB, std::initializer_list<std::pair<const char *, const char *>> presets) {
    if (B.theme.empty()) return;
    ImGui::TextWrapped(strcmp(section, "names") == 0 ? "카테고리 이름(또는 shortname·롬 폴더 이름)을 화면에 보일 이름으로 바꿉니다."
                                                     : "화면 문구 바꾸기. 원래 한국어 문구(또는 아래 hint.* 키) = 바꿀 말. 영어 화면용은 [strings.en].");
    for (auto [k, d] : presets) {
        std::string v = B.doc.Get(section, k);
        ImGui::PushID(k);
        ImGui::SetNextItemWidth(-200);
        if (ImGui::InputTextWithHint(k, d, &v, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
            if (v != B.doc.Get(section, k)) {
                PushUndo();
                if (v.empty()) B.doc.Erase(section, k);
                else B.doc.Set(section, k, v);
                Changed();
            }
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::BeginTable("kv", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(colA);
        ImGui::TableSetupColumn(colB);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableHeadersRow();
        IniSec *s = B.doc.Find(section);
        if (s) {
            for (size_t i = 0; i < s->kv.size(); i++) {
                IniKV &kv = s->kv[i];
                if (kv.raw) continue;
                bool preset = false;
                for (auto [k, d] : presets) preset = preset || kv.key == k;
                if (preset) continue;
                ImGui::TableNextRow();
                ImGui::PushID((int)i);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kv.key.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                std::string v = kv.val;
                if (ImGui::InputText("##v", &v, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
                    if (v != kv.val) { PushUndo(); B.doc.Set(section, kv.key, v); Changed(); ImGui::PopID(); break; }
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("x")) { PushUndo(); B.doc.Erase(section, kv.key); Changed(); ImGui::PopID(); break; }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    static std::string na, nb;
    ImGui::PushID(section);
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##a", colA, &na);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##b", colB, &nb);
    ImGui::SameLine();
    if (ImGui::Button("추가") && !na.empty()) {
        PushUndo();
        B.doc.Set(section, na, nb);
        Changed();
        na.clear();
        nb.clear();
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------- 새 테마·내보내기
static bool ValidName(const std::string &n) {
    if (n.empty()) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
static bool UsesExtensions() {
    for (const auto &s : B.doc.secs) {
        if (s.name == "strings" || s.name == "strings.en") return true;
        if (std::count(s.name.begin(), s.name.end(), '.') == 2) return true;
        for (const auto &kv : s.kv) {
            if (kv.raw) continue;
            if (kv.key == "visible_if" || kv.key == "hidden_if" || kv.key == "state") return true;
            if (kv.key == "text" && (kv.val.find('|') != std::string::npos || kv.val.find('?') != std::string::npos)) return true;
            for (const char *ph : {"{name}", "{short}", "{system}", "{num}", "{num_desc}", "{fav}", "{playcount}", "{lastplayed}", "{selected}", "{date}", "{file}"})
                if (kv.val.find(ph) != std::string::npos) return true;
        }
    }
    return false;
}
static void ExportTheme(const std::string &target, bool zip) {
    if (UsesExtensions() && !B.doc.Has("theme", "requires")) {
        B.doc.Set("theme", "requires", "1.1.0");
        Changed();
    }
    SaveFiles();
    std::string err;
    if (zip) {
        std::string out = target + "/" + B.theme + ".zip";
        if (WriteZip(out, ThemeDir(), B.theme, err)) Status("zip 저장: " + out);
        else Status("실패: " + err);
    } else {
        std::string out = target + "/" + B.theme;
        if (CopyTree(ThemeDir(), out, err)) {
            std::error_code ec;
            fs::remove(P(out + "/layout_errors.txt"), ec);
            Status("폴더로 내보냄: " + out);
        } else {
            Status("실패: " + err);
        }
    }
}

static void CreateTheme() {

    std::string err, dst = B.home + "/themes/" + B.newName;
    if (B.newEmpty || B.newTemplate == "(빈 테마)") {
        // 완전 빈 테마: 스타일의 [theme] 글꼴·색만, 기본 요소는 모두 숨김
        IniDoc src, d, def;
        std::string txt;
        ReadFile(B.templates + "/" + B.newTemplate + "/layout.txt", txt);
        src.Parse(txt);
        d.head = {"# " + B.newName + " — 빈 테마에서 시작 (Joy Launcher 테마 빌더)"};
        if (const IniSec *t = src.Find("theme"))
            for (const auto &kv : t->kv)
                if (!kv.raw && (kv.key == "font" || kv.key == "font_bold" || kv.key == "background_color" || kv.key.rfind("color.", 0) == 0))
                    d.Set("theme", kv.key, kv.val);
        if (!d.Has("theme", "background_color")) d.Set("theme", "background_color", "#1e1e26");
        def.Parse(LayoutDefaultText());
        for (const auto &sec : def.secs) d.Set(sec.name, "visible", "off");
        WriteFile(dst + "/layout.txt", d.Str());
        WriteFile(dst + "/theme.txt", "name=" + B.newName + "\nname_en=" + B.newName + "\ndim=0\n");
        if (IsDir(B.templates + "/" + B.newTemplate + "/fonts")) CopyTree(B.templates + "/" + B.newTemplate + "/fonts", dst + "/fonts", err);
    } else {
        CopyTree(B.templates + "/" + B.newTemplate, dst, err);
        ThemeTxt t;
        std::string txt;
        ReadFile(dst + "/theme.txt", txt);
        t.Parse(txt);
        t.Set("name", B.newName);
        t.Set("name_en", B.newName);
        WriteFile(dst + "/theme.txt", t.Str());
        // builder_fill.txt: 그림은 라이브러리에서 채움 (종류=세트)
        std::string fill;
        if (ReadFile(dst + "/builder_fill.txt", fill)) {
            B.theme = B.newName;
            ThemeTxt f;
            f.Parse(fill);
            int n = 0;
            for (const auto &kv : f.Maps()) n += FillKind(kv.second, kv.first, B.newCore, false);
            std::error_code ec2;
            fs::remove(P(dst + "/builder_fill.txt"), ec2);
            Status("그림 " + std::to_string(n) + "개 채움");
        }
    }
    OpenTheme(B.newName);
}

// ---------------------------------------------------------------- 데모 창: 런처를 따로 띄워 키보드로 실제처럼 조작
static void RunDemo() {
    if (B.theme.empty()) { Status("테마를 먼저 여세요"); return; }
    SaveFiles();
    std::vector<std::string> args = {"--demo", "--root", B.roms, "--home", B.home, "--theme", B.theme, "--lang", B.en ? "en" : "ko"};
#ifdef _WIN32
    std::string cmd = "\"JoyDemo.exe\"";
    for (const auto &a : args) cmd += " \"" + a + "\"";
    int n = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &w[0], n);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &w[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Status("데모 창을 띄웠습니다 — Enter/Space=A  Esc=B  방향키  Q/W=L/R  Z/X=ZL/ZR  F=즐겨찾기  Y=옵션");
    } else {
        Status("데모를 띄우지 못함 (JoyDemo.exe 가 빌더 폴더에 있는지 확인)");
    }
#else
    if (!Exists("joydemo")) { Status("데모를 띄우지 못함 (joydemo 가 빌더 폴더에 없음)"); return; }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        std::vector<char *> av;
        av.push_back((char *)"./joydemo");
        for (auto &a : args) av.push_back((char *)a.c_str());
        av.push_back(nullptr);
        execv("./joydemo", av.data());
        _exit(127);
    }
    Status(pid > 0 ? "데모 창을 띄웠습니다 — Enter/Space=A  Esc=B  방향키  Q/W=L/R  Z/X=ZL/ZR  F=즐겨찾기  Y=옵션" : "데모를 띄우지 못함");
#endif
}

// ---------------------------------------------------------------- 테마 지우기·관리 (함께 든 견본과 같은 이름은 지울 수 없음)
static bool Protected(const std::string &name) { return IsDir(B.templates + "/" + name); }
static void CreateTheme();
static void DeleteTheme(const std::string &name) {
    if (name.empty() || Protected(name)) return;
    std::error_code ec;
    uintmax_t n = fs::remove_all(P(B.home + "/themes/" + name), ec);
    if (ec) { Status("지우지 못함: " + ec.message()); return; }
    Status("테마를 지웠습니다: " + name + " (파일 " + std::to_string(n) + "개)");
    if (B.theme == name) {
        B.theme.clear();
        B.sel.clear();
        auto ts = ListDir(B.home + "/themes", true);
        if (!ts.empty()) OpenTheme(ts[0]);
        else JoyLoadTheme("");
    }
}
// 지우기 확인 창 (마법사·관리 창 어디서나)
static void DeleteConfirm() {
    if (B.delAsk.empty()) return;
    ImGui::OpenPopup("테마 지우기");
    if (ImGui::BeginPopupModal("테마 지우기", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string title;
        {
            std::string t;
            ThemeTxt tt;
            if (ReadFile(B.home + "/themes/" + B.delAsk + "/theme.txt", t)) tt.Parse(t);
            title = tt.Get("name");
        }
        ImGui::Text("'%s'%s 테마를 지울까요?", B.delAsk.c_str(), title.empty() ? "" : (" (" + title + ")").c_str());
        ImGui::TextDisabled("work/themes/%s 폴더가 삭제됩니다 (%s). 되돌릴 수 없습니다.", B.delAsk.c_str(),
                            HumanSize(DirSize(B.home + "/themes/" + B.delAsk)).c_str());
        ImGui::TextDisabled("스위치에 보낸 테마는 스위치의 테마 선택 창에서 [X] 로 따로 지웁니다.");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.6f, .2f, .2f, 1));
        if (ImGui::Button(B.delThenCreate ? "지우고 새로 만들기" : "지우기", ImVec2(180, 0))) {
            DeleteTheme(B.delAsk);
            if (B.delThenCreate) {
                CreateTheme();
                B.wizard = 0;
            }
            B.delAsk.clear();
            B.delThenCreate = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("취소", ImVec2(120, 0))) {
            B.delAsk.clear();
            B.delThenCreate = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
static void ManageWindow() {
    if (B.openManage) { ImGui::OpenPopup("테마 관리"); B.openManage = false; }
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("테마 관리")) {
        ImGui::TextDisabled("%s/themes — 함께 든 견본(잠금 표시)은 지울 수 없음", B.home.c_str());
        ImGui::BeginChild("tl", ImVec2(0, -40), true);
        if (ImGui::BeginTable("themes", 4, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("폴더");
            ImGui::TableSetupColumn("이름");
            ImGui::TableSetupColumn("용량", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130);
            for (const auto &t : ListDir(B.home + "/themes", true)) {
                ImGui::TableNextRow();
                ImGui::PushID(t.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s%s", t.c_str(), t == B.theme ? "  (편집 중)" : "");
                ImGui::TableNextColumn();
                std::string txt;
                ThemeTxt tt;
                if (ReadFile(B.home + "/themes/" + t + "/theme.txt", txt)) tt.Parse(txt);
                ImGui::TextUnformatted(tt.Get("name").c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(HumanSize(DirSize(B.home + "/themes/" + t)).c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("열기")) { OpenTheme(t); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (Protected(t)) ImGui::TextDisabled("잠금");
                else if (ImGui::SmallButton("지우기")) { B.delAsk = t; B.delThenCreate = false; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        if (ImGui::Button("닫기")) ImGui::CloseCurrentPopup();
        DeleteConfirm();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- 시작 마법사: ① 스타일 ② 자동 매칭 / 빈 테마
struct StyleInfo { std::string id, name, desc, preview; int order = 100; };
static std::vector<StyleInfo> Styles() {
    std::vector<StyleInfo> v;
    for (const auto &t : ListDir(B.templates, true)) {
        StyleInfo s;
        s.id = s.name = t;
        std::string txt;
        if (ReadFile(B.templates + "/" + t + "/builder_info.txt", txt)) {
            ThemeTxt f;
            f.Parse(txt);
            if (!f.Get("name").empty()) s.name = f.Get("name");
            s.desc = f.Get("desc");
            if (!f.Get("order").empty()) s.order = atoi(f.Get("order").c_str());
        }
        s.preview = B.templates + "/" + t + "/builder_preview.png";
        v.push_back(s);
    }
    std::stable_sort(v.begin(), v.end(), [](const StyleInfo &a, const StyleInfo &b) { return a.order < b.order; });
    return v;
}
static void Picture(const std::string &path, float w, float h) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(40, 40, 48, 255));
    if (TexEntry *t = Tex(path, 640)) {
        float sc = std::min(w / t->w, h / t->h);
        ImVec2 a(p.x + (w - t->w * sc) / 2, p.y + (h - t->h * sc) / 2);
        dl->AddImage((ImTextureID)(intptr_t)t->t, a, ImVec2(a.x + t->w * sc, a.y + t->h * sc));
    }
}
static bool Card(const char *id, bool selected, float w, float h, const std::function<void()> &body) {
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(1, .78f, .25f, 1) : ImVec4(.35f, .35f, .4f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, selected ? 3.0f : 1.0f);
    ImGui::BeginChild(id, ImVec2(w, h), true, ImGuiWindowFlags_NoScrollbar);
    body();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    bool clicked = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}
static void WizardWindow() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("wizard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    auto styles = Styles();
    if (B.newTemplate.empty() || std::none_of(styles.begin(), styles.end(), [](const StyleInfo &s) { return s.id == B.newTemplate; }))
        B.newTemplate = styles.empty() ? "" : styles[0].id;
    const StyleInfo *cur = nullptr;
    for (const auto &s : styles)
        if (s.id == B.newTemplate) cur = &s;
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text(B.wizard == 1 ? "1단계 · 테마 스타일을 고르세요" : "2단계 · 그림과 요소를 어떻게 시작할까요?");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled(B.wizard == 1 ? "고른 스타일의 화면 구성·글꼴·색으로 시작합니다. 나중에 모두 바꿀 수 있습니다."
                                      : "스타일: %s", cur ? cur->name.c_str() : "");
    ImGui::Separator();
    float footer = 64;
    ImGui::BeginChild("body", ImVec2(0, -footer), false);
    if (B.wizard == 1) {
        float cw = 380, ch = 310, gap = ImGui::GetStyle().ItemSpacing.x;
        int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cw + gap)));
        int i = 0;
        for (const auto &s : styles) {
            if (i % cols) ImGui::SameLine();
            ImGui::PushID(s.id.c_str());
            bool click = Card("card", s.id == B.newTemplate, cw, ch, [&]() {
                Picture(s.preview, cw - 16, (cw - 16) * 9 / 16);
                ImGui::SetWindowFontScale(1.15f);
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - 20);
                ImGui::TextDisabled("%s", s.desc.c_str());
                ImGui::PopTextWrapPos();
            });
            if (click) {
                if (B.newTemplate == s.id && ImGui::IsMouseDoubleClicked(0)) B.wizard = 2;
                B.newTemplate = s.id;
            }
            ImGui::PopID();
            i++;
        }
    } else {
        float cw = std::min(620.0f, (ImGui::GetContentRegionAvail().x - 20) / 2), ch = cw * 9 / 16 + 230;
        bool a = Card("auto", !B.newEmpty, cw, ch, [&]() {
            Picture(cur ? cur->preview : "", cw - 16, (cw - 16) * 9 / 16);
            ImGui::SetWindowFontScale(1.2f);
            ImGui::TextUnformatted("자동 매칭");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - 20);
            ImGui::TextWrapped("고른 스타일의 화면 구성 그대로, 내 롬 모음의 카테고리에 맞는 배경·로고·아이콘을 라이브러리에서 알아서 넣습니다.");
            ImGui::TextDisabled("예) 롬 폴더 'FC'·'패미컴' → nes 배경·로고, 'PS1' → psx 게임기 그림, '액션-아케이드' → 아케이드 그림");
            ImGui::TextDisabled("바로 스위치에서 쓸 수 있는 상태로 시작 → 바꾸고 싶은 곳만 고치면 됨");
            ImGui::PopTextWrapPos();
        });
        if (a) B.newEmpty = false;
        ImGui::SameLine(0, 20);
        bool e = Card("empty", B.newEmpty, cw, ch, [&]() {
            Picture("builder_ui/empty_example.png", cw - 16, (cw - 16) * 9 / 16);
            ImGui::SetWindowFontScale(1.2f);
            ImGui::TextUnformatted("완전 빈 테마");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - 20);
            ImGui::TextWrapped("그림도 요소도 없는 빈 화면에서 시작합니다. 왼쪽 [요소 넣기] 에서 목록·제목·배경 등을 하나씩 넣고, 고른 요소의 디자인을 오른쪽에서 정합니다.");
            ImGui::TextDisabled("예) 배경 그림 → 가로 캐러셀 → 카테고리 이름 → 게임 수 → 조작 도움말 순으로 넣어 나만의 화면 만들기");
            ImGui::TextDisabled("고른 스타일에서는 글꼴과 색만 가져옴");
            ImGui::PopTextWrapPos();
        });
        if (e) B.newEmpty = true;
        ImGui::Spacing();
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("테마 폴더 이름 (영문·숫자)", &B.newName);
        if (!B.newEmpty) {
            ImGui::SameLine();
            ImGui::Checkbox("주요 기종 그림만 (용량 절약)", &B.newCore);
        }
        bool exists = IsDir(B.home + "/themes/" + B.newName);
        bool okName = !B.newName.empty() && std::all_of(B.newName.begin(), B.newName.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
        if (!okName) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "영문·숫자·_·- 만 (스위치 SD 에서 안전)");
        else if (exists && Protected(B.newName)) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "같은 이름의 견본 테마가 있음 — 다른 이름으로");
        else if (exists) {
            ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "같은 이름의 테마가 있음 — 다른 이름으로 하거나");
            ImGui::SameLine();
            if (ImGui::SmallButton("기존 테마 지우고 새로 만들기")) { B.delAsk = B.newName; B.delThenCreate = true; }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (B.wizard == 1) {
        auto ts = ListDir(B.home + "/themes", true);
        if (!ts.empty()) {
            static std::string pick;
            if (pick.empty() || std::find(ts.begin(), ts.end(), pick) == ts.end()) pick = ts[0];
            ImGui::TextUnformatted("기존 테마 이어서 편집:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            if (ImGui::BeginCombo("##resume", pick.c_str())) {
                for (const auto &t : ts)
                    if (ImGui::Selectable(t.c_str(), t == pick)) pick = t;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("열기")) { OpenTheme(pick); B.wizard = 0; }
            ImGui::SameLine();
            ImGui::BeginDisabled(Protected(pick));
            if (ImGui::Button("지우기")) { B.delAsk = pick; B.delThenCreate = false; }
            ImGui::EndDisabled();
            if (Protected(pick) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("함께 든 견본 테마는 지울 수 없음");
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 140);
        if (ImGui::Button("다음 ▶", ImVec2(120, 40))) B.wizard = 2;
    } else {
        if (ImGui::Button("◀ 스타일 다시 고르기", ImVec2(200, 40))) B.wizard = 1;
        ImGui::SameLine(ImGui::GetWindowWidth() - 140);
        bool exists = IsDir(B.home + "/themes/" + B.newName);
        bool okName = ValidName(B.newName) && !exists;
        ImGui::BeginDisabled(!okName);
        if (ImGui::Button("만들기", ImVec2(120, 40))) {
            CreateTheme();
            B.wizard = 0;
            if (B.newEmpty) Status("빈 테마: 왼쪽 [요소 넣기] 에서 요소를 넣어 시작하세요. 요소를 고르면 오른쪽에 디자인 항목이 나타납니다");
        }
        ImGui::EndDisabled();
    }
    DeleteConfirm();
    ImGui::End();
}

// ---------------------------------------------------------------- 따로 뜨는 설정 창 (화면을 크게 쓰려고 아래 탭 대신)
static bool BeginTool(const char *title, bool *open, float w, float h) {
    if (!*open) return false;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(std::min(w, io.DisplaySize.x - 40), std::min(h, io.DisplaySize.y - 80)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    return true;
}
static void MapsPanel() {
    ImGui::TextWrapped("theme.txt: 카테고리·폴더 이름 = 그림 파일 이름. 내장 기종 표(FC→nes, 액션-아케이드→arcade_action …)로 찾지 못하는 이름만 적으면 됩니다.");
    auto maps = B.ttxt.Maps();
    if (ImGui::BeginTable("maps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, -34))) {
        ImGui::TableSetupColumn("카테고리·폴더 이름");
        ImGui::TableSetupColumn("그림 이름");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < maps.size(); i++) {
            ImGui::TableNextRow();
            ImGui::PushID((int)i);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(maps[i].first.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(maps[i].second.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) { PushUndo(); B.ttxt.Erase(maps[i].first); SaveFiles(); AssetsChanged(); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    static std::string ma, mb;
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##ma", "카테고리·폴더 이름", &ma);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##mb", "그림 이름 (확장자 없이)", &mb);
    ImGui::SameLine();
    if (ImGui::Button("추가##map") && !ma.empty() && !mb.empty()) { PushUndo(); B.ttxt.Set(ma, mb); SaveFiles(); AssetsChanged(); ma.clear(); mb.clear(); }
}
static void ToolWindows() {
    auto tf = [](const char *n) { return B.tabReq == n ? ImGuiTabItemFlags_SetSelected : 0; };
    if (BeginTool("카테고리·기종 설정###wcats", &B.winCats, 1100, 640)) {
        if (ImGui::Begin("카테고리·기종 설정###wcats", &B.winCats)) {
            ImGui::TextDisabled("지금 카테고리: %s", CurrentSystemId().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("이 기종만 보기")) B.assetFilter = CurrentSystemId();
            ImGui::SameLine();
            if (ImGui::SmallButton("모두 보기")) B.assetFilter.clear();
            if (ImGui::BeginTabBar("cattabs")) {
                if (ImGui::BeginTabItem("기종별 그림 (배경·로고·아이콘)", nullptr, tf("assets"))) { AssetsPanel(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("나만의 카테고리", nullptr, tf("cats"))) { CategoriesPanel(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("카테고리 이름")) { TablePanel("names", "카테고리·폴더 이름", "화면에 보일 이름", {}); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("기종 연결 (theme.txt)")) { MapsPanel(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("미리보기 롬")) {
                    ImGui::TextWrapped("미리보기에 쓸 롬 폴더 (페가수스 metadata 또는 롬 파일). 기본은 함께 든 sample/roms. SD 카드의 /roms 를 골라도 됩니다.");
                    ImGui::SetNextItemWidth(-120);
                    ImGui::InputText("##roms", &B.roms);
                    ImGui::SameLine();
                    if (ImGui::Button("다시 읽기")) { std::string log; JoySetRoms(B.roms, log); Status("게임 목록: " + std::to_string(JoyCollections().size()) + " 카테고리"); }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
    if (BeginTool("테마 설정###wtheme", &B.winTheme, 640, 720)) {
        if (ImGui::Begin("테마 설정###wtheme", &B.winTheme)) ThemePanel();
        ImGui::End();
    }
    if (BeginTool("화면 문구###wstr", &B.winStrings, 820, 480)) {
        if (ImGui::Begin("화면 문구###wstr", &B.winStrings))
            TablePanel("strings", "원래 문구", "바꿀 말",
                       {{"hint.categories", "카테고리 화면 도움말 ([A] 열기 …)"}, {"hint.categories_grid", "카테고리 화면 도움말 (격자)"},
                        {"hint.games", "게임 화면 도움말"}, {"hint.games_grid", "게임 화면 도움말 (격자)"}});
        ImGui::End();
    }
    if (BeginTool("layout.txt###wraw", &B.winRaw, 760, 640)) {
        if (ImGui::Begin("layout.txt###wraw", &B.winRaw)) {
            if (!B.rawDirty) B.rawText = B.doc.Str();
            if (ImGui::Button("적용")) {
                PushUndo();
                B.doc.Parse(B.rawText);
                Changed();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("직접 고친 뒤 [적용]");
            if (ImGui::InputTextMultiline("##raw", &B.rawText, ImVec2(-1, -1))) B.rawDirty = true;
        }
        ImGui::End();
    }
    if (BeginTool("문제###wprob", &B.winProblems, 600, 320)) {
        if (ImGui::Begin("문제###wprob", &B.winProblems)) {
            const auto &probs = JoyLayout().problems;
            if (probs.empty()) ImGui::TextDisabled("문제 없음");
            for (const auto &p : probs) ImGui::TextWrapped("%s", p.c_str());
        }
        ImGui::End();
    }
    B.tabReq.clear();
}

// ---------------------------------------------------------------- 메인
static void Frame() {
    ImGuiIO &io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float menuH = ImGui::GetFrameHeight(), statusH = 26;
    if (B.wizard) {
        WizardWindow();
        BrowseWindow();
        return;
    }
    bool openNew = false, openExport = false, openOpen = false, openFtp = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("파일")) {
            if (ImGui::MenuItem("새 테마...")) B.wizard = 1;
            if (ImGui::MenuItem("열기...")) openOpen = true;
            if (ImGui::MenuItem("테마 관리 (지우기)...")) B.openManage = true;
            if (ImGui::MenuItem("내보내기...", nullptr, false, !B.theme.empty())) openExport = true;
            if (ImGui::MenuItem("스위치로 보내기 (FTP)...", nullptr, false, !B.theme.empty())) openFtp = true;
            if (ImGui::MenuItem("테마 폴더 다시 읽기", nullptr, false, !B.theme.empty())) OpenTheme(B.theme);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("편집")) {
            if (ImGui::MenuItem("실행 취소", "Ctrl+Z", false, !B.undo.empty())) Undo();
            if (ImGui::MenuItem("다시 실행", "Ctrl+Y", false, !B.redo.empty())) Redo();
            if (ImGui::MenuItem("복제", "Ctrl+D", false, !B.sel.empty())) DuplicateSel();
            if (ImGui::MenuItem("삭제", "Del", false, !B.sel.empty())) DeleteSel();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("설정")) {
            ImGui::MenuItem("카테고리·기종 (그림·나만의 카테고리·이름)", "F2", &B.winCats);
            ImGui::MenuItem("테마 (이름·글꼴·색·화면 효과)", "F3", &B.winTheme);
            ImGui::MenuItem("화면 문구 (도움말 등)", nullptr, &B.winStrings);
            ImGui::MenuItem("layout.txt 직접 편집", nullptr, &B.winRaw);
            ImGui::MenuItem("문제 목록", nullptr, &B.winProblems);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("보기")) {
            ImGui::MenuItem("격자 맞춤", nullptr, &B.snap);
            ImGui::SliderInt("격자 간격", &B.grid, 1, 32);
            ImGui::MenuItem("자리 표시", nullptr, &B.showBounds);
            ImGui::EndMenu();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.2f, .55f, .3f, 1));
        if (ImGui::Button("▶ 데모 실행 (F5)")) RunDemo();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 테마를 실제 런처처럼 새 창에서 조작해 보기 (키보드: Enter=A Esc=B 방향키 Q/W=L/R Z/X=ZL/ZR F=즐겨찾기)");
        ImGui::TextDisabled("  |  %s  ·  Joy Launcher %s", B.theme.empty() ? "(테마 없음)" : (B.theme + " — " + B.ttxt.Get("name")).c_str(), JoyVersion());
        ImGui::EndMainMenuBar();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) RunDemo();
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        B.winCats = !B.winCats;
        if (B.winCats) { B.assetFilter = CurrentSystemId(); B.tabReq = "assets"; }  // 지금 카테고리만 보며 열기
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) B.winTheme = !B.winTheme;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) B.sel.clear();
    if (!io.WantTextInput && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
        if (ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSel();
    }
    ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    // 예전 탭 요청 → 따로 뜨는 창
    if (!B.tabReq.empty()) {
        if (B.tabReq == "assets" || B.tabReq == "cats") B.winCats = true;
        else if (B.tabReq == "theme") B.winTheme = true;
        else if (B.tabReq == "strings") B.winStrings = true;
        else if (B.tabReq == "raw") B.winRaw = true;
    }
    // 왼쪽: 고른 요소가 있으면 그 요소의 꾸미기만, 없으면 요소 넣기 + 레이어
    float left = B.sel.empty() ? 300 : 400;
    ImGui::SetNextWindowPos(ImVec2(0, menuH));
    ImGui::SetNextWindowSize(ImVec2(left, H - menuH - statusH));
    ImGui::Begin(B.sel.empty() ? "요소###side" : "고른 요소 꾸미기###side", nullptr, fixed);
    if (B.sel.empty()) {
        LayersPanel();
    } else {
        if (ImGui::Button("◀ 선택 해제 (Esc)")) B.sel.clear();
        ImGui::SameLine();
        ImGui::TextDisabled(B.games ? "게임 화면" : "카테고리 화면");
        ImGui::Separator();
        if (!B.sel.empty()) PropertiesPanel();
    }
    ImGui::End();
    // 가운데: 화면 (남은 자리 전부, 가운데 정렬)
    ImGui::SetNextWindowPos(ImVec2(left, menuH));
    ImGui::SetNextWindowSize(ImVec2(W - left, H - menuH - statusH));
    ImGui::Begin("화면###canvas", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
    Canvas();
    ImGui::End();
    ToolWindows();
    // 상태 줄
    ImGui::SetNextWindowPos(ImVec2(0, H - statusH));
    ImGui::SetNextWindowSize(ImVec2(W, statusH));
    ImGui::Begin("status", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    {
        size_t np = JoyLayout().problems.size();
        if (np) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.6f, .3f, .15f, 1));
            if (ImGui::SmallButton(("문제 " + std::to_string(np)).c_str())) B.winProblems = true;
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
    }
    if (SDL_GetTicks() < B.statusUntil) ImGui::TextUnformatted(B.status.c_str());
    else ImGui::TextDisabled("자동 저장: %s   ·   화면을 눌러 고르기, 끌어서 옮기기, 손잡이로 크기, 방향키 1점(Shift 10점), Alt+누르기 = 겹친 아래 요소",
                             B.theme.empty() ? "-" : ThemeDir().c_str());
    ImGui::End();

    // 새 테마
    if (openNew) ImGui::OpenPopup("새 테마");
    if (ImGui::BeginPopupModal("새 테마", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("폴더 이름 (영문·숫자)", &B.newName);
        auto tpls = ListDir(B.templates, true);
        tpls.insert(tpls.begin(), "(빈 테마)");
        if (ImGui::BeginCombo("시작 견본", B.newTemplate.c_str())) {
            for (const auto &t : tpls)
                if (ImGui::Selectable(t.c_str(), t == B.newTemplate)) B.newTemplate = t;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("주요 기종 그림만 (용량 절약)", &B.newCore);
        bool exists = IsDir(B.home + "/themes/" + B.newName);
        if (!ValidName(B.newName)) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "영문·숫자·_·- 만 (스위치 SD 에서 안전)");
        else if (exists) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "같은 이름의 테마가 있음");
        if (ImGui::Button("만들기") && ValidName(B.newName) && !exists) {
            CreateTheme();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (openOpen) ImGui::OpenPopup("열기");
    if (ImGui::BeginPopupModal("열기", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s/themes", B.home.c_str());
        for (const auto &t : ListDir(B.home + "/themes", true))
            if (ImGui::Selectable(t.c_str(), t == B.theme, 0, ImVec2(360, 0))) { OpenTheme(t); ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        if (ImGui::Button("다른 곳의 테마 폴더 가져오기 (theme.txt 고르기)")) {
            ImGui::CloseCurrentPopup();
            Browse("txt", [](const std::string &path) {
                fs::path dir = P(path).parent_path();
                std::string name = U8(dir.filename()), err;
                for (auto &ch : name) if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-')) ch = '_';
                if (CopyTree(U8(dir), B.home + "/themes/" + name, err)) OpenTheme(name);
                else Status("가져오기 실패: " + err);
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("닫기")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (openExport) {
        ImGui::OpenPopup("내보내기");
        if (B.exportDir.empty()) B.exportDir = "export";
    }
    if (ImGui::BeginPopupModal("내보내기", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("zip: 게시판 배포용. 폴더: SD 카드의 /JoyLauncher/themes 를 고르면 바로 스위치용으로 복사됩니다.");
        ImGui::SetNextItemWidth(480);
        ImGui::InputText("내보낼 곳", &B.exportDir);
        ImGui::SameLine();
        if (ImGui::Button("고르기")) {
            Browse("", [](const std::string &path) { B.exportDir = U8(P(path).parent_path()); });
        }
        if (UsesExtensions()) ImGui::TextColored(ImVec4(1, .85f, .4f, 1), "확장 문법 사용 → requires = 1.1.0 (이전 런처에서는 일부가 안 보임)");
        if (ImGui::Button("zip 만들기")) { ExportTheme(B.exportDir, true); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("폴더로 복사")) { ExportTheme(B.exportDir, false); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    FtpWindow(openFtp);
    ManageWindow();
    BrowseWindow();
}

static std::string ExeDir(const char *argv0) {
    char *base = SDL_GetBasePath();
    std::string d = base ? base : ".";
    if (base) SDL_free(base);
    (void)argv0;
    return d;
}

int main(int argc, char **argv) {
#ifndef _WIN32
    signal(SIGCHLD, SIG_IGN);  // 데모 창이 끝나면 좀비로 남지 않게
#endif
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);  // 패드로 미리보기 조작
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();
    // 실행 파일 폴더 기준 상대 경로로 일함 (윈도우 한글 경로 문제 피하기)
    std::error_code ec;
    fs::current_path(P(ExeDir(argv[0])), ec);
    std::string shot;
    std::string openName, selArg, newArg, ftpHost, ftpDel;
    int ftpPort = 0, wizArg = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];  // 시험: 빌더 화면을 그림으로 저장하고 끝
        else if (a == "--theme" && i + 1 < argc) openName = argv[++i];
        else if (a == "--select" && i + 1 < argc) selArg = argv[++i];
        else if (a == "--games") B.games = true;
        else if (a == "--tab" && i + 1 < argc) B.assetKind = argv[++i];
        else if (a == "--panel" && i + 1 < argc) B.tabReq = argv[++i];
        else if (a == "--wizard" && i + 1 < argc) wizArg = atoi(argv[++i]);  // 시험: 마법사 단계
        else if (a == "--empty") B.newEmpty = true;
        else if (a == "--ftp-test" && i + 2 < argc) { ftpHost = argv[++i]; ftpPort = atoi(argv[++i]); }  // 시험: 테마를 FTP 로 보내고 끝
        else if (a == "--ftp-del" && i + 1 < argc) ftpDel = argv[++i];  // 시험: --ftp-test 와 함께, 스위치 테마 지우고 목록
        else if (a == "--new" && i + 2 < argc) { newArg = argv[++i]; B.newTemplate = argv[++i]; }  // 시험: --new 이름 견본
    }
    fs::create_directories(P(B.home + "/themes"), ec);
    B.win = SDL_CreateWindow("Joy Launcher 테마 빌더", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (shot.empty() ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN));
    if (shot.empty()) SDL_MaximizeWindow(B.win);
    B.r = SDL_CreateRenderer(B.win, -1, shot.empty() ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE
                                                     : SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!B.r) B.r = SDL_CreateRenderer(B.win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    SDL_SetRenderDrawBlendMode(B.r, SDL_BLENDMODE_BLEND);
    B.preview = SDL_CreateTexture(B.r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SW, SH);
    SDL_SetTextureBlendMode(B.preview, SDL_BLENDMODE_NONE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0;
    ImGui::GetStyle().FrameRounding = 3;
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2000, 0x206F, 0x2190, 0x21FF, 0x2460, 0x24FF, 0x2500, 0x27BF, 0x3000, 0x30FF,
                                     0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0};
    io.Fonts->AddFontFromFileTTF("romfs/font.ttf", 17.0f, nullptr, ranges);
    ImGui_ImplSDL2_InitForSDLRenderer(B.win, B.r);
    ImGui_ImplSDLRenderer2_Init(B.r);

    std::string log;
    if (!JoyInit(B.r, B.home, B.roms, log)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Joy Theme Builder", log.c_str(), B.win);
        return 1;
    }
    ScanLibrary();
    LoadCats();
    LoadFtpSettings();
    if (!openName.empty() && IsDir(B.home + "/themes/" + openName)) OpenTheme(openName);
    else {
        auto ts = ListDir(B.home + "/themes", true);
        if (!ts.empty()) OpenTheme(ts[0]);
    }

    if (!newArg.empty()) { B.newName = newArg; CreateTheme(); }
    if (!ftpHost.empty()) {
        gFtp.host = ftpHost;
        gFtp.port = ftpPort;
        if (ftpDel.empty()) FtpSendTheme();
        else FtpRemoteJob(ftpDel == "-" ? "" : ftpDel);
        while (gFtp.running) SDL_Delay(50);
        for (const auto &n : gFtp.remote) printf("remote: %s\n", n.c_str());
        for (const auto &l : gFtp.log) printf("%s\n", l.c_str());
        return 0;
    }
    if (!selArg.empty()) B.sel = selArg;
    // 패드: 미리보기(편집 중인 테마)를 스위치처럼 조작. 스위치 버튼 번호와 같은 뜻으로 바꿔 씀
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
    int padHeld = -1, padStick = -1;
    Uint32 padNext = 0;
    bool padZL = false, padZR = false;
    auto padAct = [&](int b) {
        if (B.wizard) return;
        switch (b) {
        case 0: if (!B.games) { JoyNav(0); B.games = true; } break;   // A: 카테고리 열기
        case 1: if (B.games) { JoyNav(1); B.games = false; } break;   // B: 카테고리 화면으로
        case 6: JoyNav(20); break;                                     // L / R: 카테고리 이동
        case 7: JoyNav(21); break;
        case 8: B.games = true; JoyNav(22); break;                     // ZL / ZR: 보기
        case 9: B.games = true; JoyNav(23); break;
        case 11: if (B.games) JoyNav(30); break;                       // −: 즐겨찾기
        case 12: case 13: case 14: case 15: JoyNav(b); break;
        }
    };
    if (openName.empty() && newArg.empty()) B.wizard = 1;
    if (wizArg) B.wizard = wizArg;
    bool running = true;
    int frames = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_GameController *gc = SDL_GameControllerOpen(ev.cdevice.which))
                    Status(std::string("패드 연결됨: ") + (SDL_GameControllerName(gc) ? SDL_GameControllerName(gc) : "?") +
                           " — A 열기 · B 뒤로 · 십자키·스틱 이동 · L/R 카테고리 · ZL/ZR 보기 · − 즐겨찾기");
            }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
                int b = -1;
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: b = 0; break;
                case SDL_CONTROLLER_BUTTON_B: b = 1; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: b = 6; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: b = 7; break;
                case SDL_CONTROLLER_BUTTON_BACK: b = 11; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: b = 12; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: b = 13; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: b = 14; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: b = 15; break;
                }
                if (b >= 0 && ev.type == SDL_CONTROLLERBUTTONDOWN) {
                    padAct(b);
                    padHeld = b;
                    padNext = SDL_GetTicks() + 400;
                } else if (b >= 0 && padHeld == b) {
                    padHeld = -1;
                }
            }
            if (ev.type == SDL_CONTROLLERAXISMOTION) {
                int v = ev.caxis.value;
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    bool &st = ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? padZL : padZR;
                    bool on = v > 16000;
                    if (on && !st) padAct(ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 8 : 9);
                    st = on;
                } else if (SDL_GameController *gc = SDL_GameControllerFromInstanceID(ev.caxis.which)) {
                    int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
                    int dir = -1;
                    if (std::abs(y) >= std::abs(x) && std::abs(y) > 16000) dir = y < 0 ? 13 : 15;
                    else if (std::abs(x) > 16000) dir = x < 0 ? 12 : 14;
                    if (dir != padStick) {
                        padStick = dir;
                        if (dir >= 0) {
                            padAct(dir);
                            padHeld = dir;
                            padNext = SDL_GetTicks() + 400;
                        } else if (padHeld >= 12 && padHeld <= 15) {
                            padHeld = -1;
                        }
                    }
                }
            }
            if (ev.type == SDL_DROPFILE) {
                std::string path = ev.drop.file;
                SDL_free(ev.drop.file);
                std::string e = Ext(path);
                if (B.browseOpen && B.browseDone) {
                    B.browseOpen = false;
                    B.browseDone(path);
                } else if (!B.theme.empty() && (e == "ttf" || e == "otf")) {
                    std::string fn = U8(P(path).filename());
                    for (auto &ch : fn) if ((unsigned char)ch >= 0x80 || ch == ' ') ch = '_';
                    fs::create_directories(P(ThemeDir() + "/fonts"), ec);
                    fs::copy_file(P(path), P(ThemeDir() + "/fonts/" + fn), fs::copy_options::overwrite_existing, ec);
                    PushUndo();
                    B.doc.Set("theme", "font", "fonts/" + fn);
                    Changed();
                    Status("글꼴 넣음: " + fn);
                } else if (!B.theme.empty() && (e == "png" || e == "jpg" || e == "jpeg")) {
                    // 화면에 놓으면 새 그림 요소
                    std::string err, stem = Stem(U8(P(path).filename()));
                    for (auto &ch : stem) if ((unsigned char)ch >= 0x80 || ch == ' ') ch = '_';
                    std::string out = ImportImage(path, ThemeDir() + "/ui", stem, "ui", err);
                    if (!out.empty()) {
                        PushUndo();
                        std::string name = UniqueName("image", ScreenPrefix()), sec = ScreenPrefix() + name;
                        B.doc.Set(sec, "type", "image");
                        B.doc.Set(sec, "file", "ui/" + U8(P(out).filename()));
                        B.doc.Set(sec, "rect", "440,210,400,300");
                        B.sel = sec;
                        Changed();
                        AssetsChanged();
                        Status("그림 요소 추가: " + name);
                    }
                }
            }
        }
        // 패드 방향 누르고 있으면 반복
        if (padHeld >= 12 && padHeld <= 15 && SDL_GetTicks() >= padNext) {
            padAct(padHeld);
            padNext = SDL_GetTicks() + 70;
        }
        // 미리보기 그리기 (지난 프레임에 바꾼 화면 구성을 여기서 반영)
        FlushApply();
        SDL_SetRenderTarget(B.r, B.preview);
        JoyRender(B.games);
        SDL_SetRenderTarget(B.r, nullptr);
        if (SDL_GetTicks() > B.sizeAt) {
            B.themeSize = B.theme.empty() ? 0 : DirSize(ThemeDir());
            B.sizeAt = SDL_GetTicks() + 3000;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();
        SDL_RenderSetScale(B.r, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(B.r, 24, 24, 28, 255);
        SDL_RenderClear(B.r);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), B.r);
        if (!shot.empty() && ++frames >= 3) {
            int w, h;
            SDL_GetRendererOutputSize(B.r, &w, &h);
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(B.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            IMG_SavePNG(s, shot.c_str());
            SDL_FreeSurface(s);
            running = false;
        }
        SDL_RenderPresent(B.r);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    return 0;
}
