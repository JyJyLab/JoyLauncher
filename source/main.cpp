// Joy Launcher: /roms 의 롬 폴더(페가수스 metadata.pegasus.txt 또는 롬 파일만)를 읽어 카테고리(플랫폼)별로
// 보여주고 RetroArch 코어 / PPSSPP 등 nro 를 롬 경로 인자와 함께 실행한다.
// /hbmenu.nro 자리에 두면 게임 종료 후 hbloader 가 다시 이 런처를 띄운다.
// 설정·테마는 SD 루트의 /JoyLauncher (themes/<테마>/ 에 카테고리별 배경·로고)
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "hbmenu.h"
#include "pegasus.h"
#include "updater.h"
#include "videoframe.h"

#ifdef __SWITCH__
#include <switch.h>
extern "C" {
// 1: 종료 시 애플릿을 닫고 HOME 으로, 0: hbloader 로 돌아가 다음 nro 로드
u32 __nx_applet_exit_mode = 0;
}
static const char *kRoot = "/roms";  // 기본 롬·목록 폴더 (추가 폴더는 roots.txt)
static const char *kFont = "romfs:/font.ttf";
static const char *kFontBold = "romfs:/font_bold.ttf";
static std::string gHome = "/JoyLauncher";  // 설정·상태·테마 폴더
#else
static std::string gRoot = "roms";
static const char *kFont = "romfs/font.ttf";
static const char *kFontBold = "romfs/font_bold.ttf";
static std::string gHome = "JoyLauncher";
#endif
static const char *kOldStateDir = "/config/templauncher";  // 이전 이름(Temp Launcher) 시절 설정

static const int W = 1280, H = 720;
static const SDL_Color kWhite = {235, 235, 240, 255};
static const SDL_Color kGrey = {150, 150, 165, 255};
static const SDL_Color kAccent = {255, 196, 64, 255};
static const SDL_Color kDark = {20, 20, 26, 255};
static const SDL_Color kSel = {70, 70, 96, 255};

// 카테고리 화면 아래쪽 메뉴 (게임 외 항목). 실제로 있는 프로그램만 표시
enum class MenuAct { Launch, Theme, Frames, Refresh, Update, InstallHb, RestoreHb, Exit };
struct MenuItem {
    std::string label;
    std::string nro;
    MenuAct act;
};
static std::vector<MenuItem> gMenu;
// 앨범(애플릿)으로 실행됐는지. 애플릿 모드는 쓸 수 있는 메모리가 적어 큰 게임이 느리거나 실행되지 않을 수 있음
static bool gAppletMode = false;
static std::string gSelfNro = "/switch/joylauncher/joylauncher.nro";
static std::string gThemeName;  // 현재 테마 ("" = 기본)
static int gFramesPending = 0;   // 영상 장면을 아직 뽑지 않은 게임 수
static std::string gVideoFrames = "auto";
static std::string gAutoUpdate = "on";      // on: 시작할 때 깃헙에서 새 버전 확인 (설치는 옵션 창에서)
static bool gUpdateFound = false;           // 새 버전을 찾았는지
static UpdateInfo gUpdate;
static bool gLaunchedAsHbmenu = false;      // /hbmenu.nro 로 실행됐는지 (업데이트 후 다시 시작할 경로)  // auto: 게임을 볼 때 자동 추출, manual: 메뉴의 일괄 추출만

static void BuildMenu() {
    gMenu.clear();
    if (SdExists("/switch/sphaira/sphaira.nro"))
        gMenu.push_back({"홈브루 메뉴 (sphaira)", "/switch/sphaira/sphaira.nro", MenuAct::Launch});
    if (SdExists("/switch/hbmenu.nro") && NroTitle("/switch/hbmenu.nro") == "nx-hbmenu")
        gMenu.push_back({"홈브루 메뉴 (nx-hbmenu)", "/switch/hbmenu.nro", MenuAct::Launch});
    if (SdExists("/switch/retroarch_switch.nro"))
        gMenu.push_back({"RetroArch", "/switch/retroarch_switch.nro", MenuAct::Launch});
    if (SdExists("/switch/ppsspp/PPSSPP.nro"))
        gMenu.push_back({"PPSSPP", "/switch/ppsspp/PPSSPP.nro", MenuAct::Launch});
    if (gFramesPending > 0)
        gMenu.push_back({"영상 장면 일괄 추출 (남은 게임 " + std::to_string(gFramesPending) + "개)", "", MenuAct::Frames});
    gMenu.push_back({"게임 목록 새로 고침 (롬·그림·영상을 넣거나 뺐을 때)", "", MenuAct::Refresh});
    gMenu.push_back({gUpdateFound ? "업데이트 설치: v" + gUpdate.version + " (현재 v" + std::string(CurrentVersion()) + ")"
                                  : "업데이트 확인 (현재 v" + std::string(CurrentVersion()) + ")",
                     "", MenuAct::Update});
    gMenu.push_back({"테마 설정 (현재: " + (gThemeName.empty() ? std::string("기본") : gThemeName) + ")", "",
                     MenuAct::Theme});
    HbKind hb = CurrentHbmenu();
    if (hb == HbKind::Self && NroTitle("/hbmenu.nro") == "Joy Launcher")
        gMenu.push_back({"원래 홈브루 메뉴로 되돌리기", "", MenuAct::RestoreHb});
    else if (hb == HbKind::Self)
        gMenu.push_back({"기본 홈브루 메뉴를 Joy Launcher 로 업데이트 (현재: 이전 버전)", "", MenuAct::InstallHb});
    else
        gMenu.push_back({std::string("Joy Launcher 를 기본 홈브루 메뉴로 설정 (현재: ") + HbKindName(hb) + ")", "",
                         MenuAct::InstallHb});
    gMenu.push_back({"종료 (HOME)", "", MenuAct::Exit});
}

struct TextCache {
    SDL_Renderer *r = nullptr;
    std::map<std::string, SDL_Texture *> map;
    void Clear() {
        for (auto &kv : map) SDL_DestroyTexture(kv.second);
        map.clear();
    }
    SDL_Texture *Get(TTF_Font *f, const std::string &s, SDL_Color c, int wrap = 0) {
        if (s.empty()) return nullptr;
        char key[64];
        snprintf(key, sizeof(key), "%p|%02x%02x%02x|%d|", (void *)f, c.r, c.g, c.b, wrap);
        std::string k = key + s;
        auto it = map.find(k);
        if (it != map.end()) return it->second;
        if (map.size() > 800) Clear();
        SDL_Surface *surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), c, wrap)
                                     : TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return nullptr;
        SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        map[k] = t;
        return t;
    }
};

// 박스아트 텍스처 LRU 캐시
struct ImageCache {
    SDL_Renderer *r = nullptr;
    std::map<std::string, SDL_Texture *> map;
    std::list<std::string> order;
    size_t cap = 40;
    SDL_Texture *Find(const std::string &p) {
        auto it = map.find(p);
        if (it == map.end()) return nullptr;
        order.remove(p);
        order.push_front(p);
        return it->second;
    }
    SDL_Texture *Load(const std::string &p) {
        if (p.empty()) return nullptr;
        if (SDL_Texture *t = Find(p)) return t;
        SDL_Surface *s = IMG_Load(p.c_str());
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(r, s) : nullptr;
        if (s) SDL_FreeSurface(s);
        map[p] = t;  // 실패도 기록해 재시도 방지
        order.push_front(p);
        while (order.size() > cap) {
            std::string old = order.back();
            order.pop_back();
            if (map[old]) SDL_DestroyTexture(map[old]);
            map.erase(old);
        }
        return t;
    }
    void Clear() {
        for (auto &kv : map)
            if (kv.second) SDL_DestroyTexture(kv.second);
        map.clear();
        order.clear();
    }
};

// ---------------------------------------------------------------- 테마
// /JoyLauncher/themes/<테마>/
//   backgrounds/<롬 폴더>.jpg|png    /roms/<롬 폴더> 와 이름이 같은 그림을 그 카테고리 배경으로 (대소문자 무시)
//                                    (없으면 collection 의 shortname, collection 이름 순으로 찾음)
//   backgrounds/default.jpg          카테고리 화면·배경이 없는 카테고리용
//   logos/<롬 폴더>.png              게임 목록 제목 대신 표시할 로고 (선택, 이름 규칙은 배경과 같음)
//   theme.txt                        dim=0~255 (배경 어둡게, 기본 150)
//                                    <카테고리 또는 롬 폴더>=<그림 파일 이름>  (한글 이름 대신 영문 파일 쓰기용)
//                                      예) 액션-아케이드=arcade_action   (backgrounds/arcade_action.jpg, logos/arcade_action.png)
// 페가수스 테마의 Resource 폴더(Background_image1, Logo1)를 그대로 넣어도 인식한다.
struct Theme {
    std::string name;
    std::map<std::string, std::string> bg, logo;  // 소문자 이름 -> 파일 경로
    std::map<std::string, std::string> alias;     // 소문자 카테고리/폴더 이름 -> 그림 파일 이름 (theme.txt)
    int dim = 150;
    bool Active() const { return !bg.empty() || !logo.empty(); }  // alias 만 있는 테마는 의미 없음
};
static Theme gTheme;

static std::string LowerAscii(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return s;
}

static bool IsDirPath(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::vector<std::string> ListSubdirs(const std::string &dir) {
    std::vector<std::string> out;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.') continue;
            if (IsDirPath(dir + "/" + n)) out.push_back(n);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static void IndexImages(const std::string &dir, std::map<std::string, std::string> &out) {
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    while (dirent *e = readdir(d)) {
        std::string n = e->d_name;
        size_t dot = n.find_last_of('.');
        if (n.empty() || n[0] == '.' || dot == std::string::npos) continue;
        std::string ext = LowerAscii(n.substr(dot + 1));
        if (ext != "jpg" && ext != "jpeg" && ext != "png") continue;
        std::string key = LowerAscii(n.substr(0, dot));
        if (!out.count(key)) out[key] = dir + "/" + n;
    }
    closedir(d);
}

static std::vector<std::string> ThemeNames() { return ListSubdirs(gHome + "/themes"); }

static void LoadTheme(const std::string &name) {
    gTheme = Theme();
    gThemeName.clear();
    if (name.empty()) return;
    std::string base = gHome + "/themes/" + name;
    if (!IsDirPath(base)) return;
    gTheme.name = gThemeName = name;
    for (const char *d : {"backgrounds", "Background_image1", "Resource/Background_image1"}) IndexImages(base + "/" + d, gTheme.bg);
    for (const char *d : {"logos", "Logo1", "Resource/Logo1"}) IndexImages(base + "/" + d, gTheme.logo);
    std::ifstream in(base + "/theme.txt");
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line = line.substr(3);  // UTF-8 BOM (메모장 저장)
        first = false;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t lead = line.find_first_not_of(" \t");
        if (lead == std::string::npos || line[lead] == '#' || line[lead] == ';' || line[lead] == '[') continue;
        line = line.substr(lead);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
        if (k == "dim") gTheme.dim = std::max(0, std::min(255, atoi(v.c_str())));
        else if (!k.empty() && !v.empty()) {
            // 확장자를 적었으면 떼고 이름만 (backgrounds 는 jpg/png, logos 는 png 를 각각 찾음)
            size_t dot = v.find_last_of('.');
            if (dot != std::string::npos) {
                std::string ext = LowerAscii(v.substr(dot + 1));
                if (ext == "jpg" || ext == "jpeg" || ext == "png") v = v.substr(0, dot);
            }
            gTheme.alias[LowerAscii(k)] = LowerAscii(v);
        }
    }
}

static std::string ThemeFind(const std::map<std::string, std::string> &m, const Collection *c) {
    if (m.empty()) return "";
    auto alias = [&](const std::string &k) -> std::string {
        auto al = gTheme.alias.find(LowerAscii(k));
        if (al == gTheme.alias.end()) return "";
        auto it = m.find(al->second);
        return it != m.end() ? it->second : "";
    };
    auto direct = [&](const std::string &k) -> std::string {
        auto it = m.find(LowerAscii(k));
        return it != m.end() ? it->second : "";
    };
    auto first = [](std::initializer_list<std::string> v) {
        for (const auto &s : v)
            if (!s.empty()) return s;
        return std::string();
    };
    if (!c) {
        for (const char *k : {"default", "main", "전체게임", "모든게임", "all"}) {
            std::string r = first({alias(k), direct(k)});
            if (!r.empty()) return r;
        }
        return "";
    }
    // 1) theme.txt 에 카테고리 이름(또는 shortname)으로 지정한 그림 (직접 지정이 항상 우선)
    std::string r = first({alias(c->name), c->shortName.empty() ? "" : alias(c->shortName)});
    if (!r.empty()) return r;
    // 2) 롬 폴더가 하나인 카테고리: 폴더 이름의 그림
    if (c->folders.size() == 1) {
        r = first({alias(c->folders[0]), direct(c->folders[0])});
        if (!r.empty()) return r;
    }
    // 3) 카테고리 shortname / 이름과 같은 그림
    r = first({c->shortName.empty() ? "" : direct(c->shortName), direct(c->name)});
    if (!r.empty()) return r;
    // 4) 여러 폴더를 합친 카테고리: 그림이 있는 첫 폴더 (예: psx+psp+ps2 를 합친 "Play Station")
    for (const auto &f : c->folders) {
        r = first({alias(f), direct(f)});
        if (!r.empty()) return r;
    }
    return "";
}

// 숨길 코어 설정 (settings.txt 의 hide_cores=, 쉼표 구분). 비우면 모두 표시
static std::string gHideCores = "flycast,mupen64plus_next,melonds,desmume,yabause,kronos,yabasanshiro,citra,dolphin";

static void ApplyHideCores() {
    std::vector<std::string> v;
    std::string cur;
    for (char ch : gHideCores + ",") {
        if (ch == ',') {
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            size_t a = cur.find_first_not_of(' ');
            if (a != std::string::npos) v.push_back(cur.substr(a));
            cur.clear();
        } else {
            cur += ch;
        }
    }
    SetHiddenCores(v);
}

static void SaveSettings() {
    mkdir(gHome.c_str(), 0777);
    std::ofstream o(gHome + "/settings.txt");
    o << "theme=" << gThemeName << "\n";
    o << "# 숨길 코어 (스위치 성능 범위 밖 기종). 비우면 모든 기종 표시\n";
    o << "hide_cores=" << gHideCores << "\n";
    o << "# 영상 장면: auto = 게임을 볼 때 자동 추출, manual = 메뉴의 일괄 추출만\n";
    o << "video_frames=" << gVideoFrames << "\n";
    o << "# 자동 업데이트 확인: on = 시작할 때 새 버전 확인 (설치는 옵션 창에서), off = 끔\n";
    o << "auto_update=" << gAutoUpdate << "\n";
}

static void LoadSettings() {
    std::ifstream in(gHome + "/settings.txt");
    std::string line, theme;
    bool found = false;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("theme=", 0) == 0) { theme = line.substr(6); found = true; }
        if (line.rfind("hide_cores=", 0) == 0) gHideCores = line.substr(11);
        if (line.rfind("video_frames=", 0) == 0) gVideoFrames = line.substr(13) == "manual" ? "manual" : "auto";
        if (line.rfind("auto_update=", 0) == 0) gAutoUpdate = line.substr(12) == "off" ? "off" : "on";
    }
    // 설정이 없으면 첫 번째 테마를 기본으로
    if (!found) {
        auto names = ThemeNames();
        if (!names.empty()) theme = names[0];
    }
    LoadTheme(theme);
    ApplyHideCores();
    if (!found) SaveSettings();  // 처음 실행: 기본 설정 파일 만들기
}

enum class Mode { Categories, Games };

struct App {
    SDL_Renderer *r = nullptr;
    TTF_Font *fTitle = nullptr, *fBig = nullptr, *fList = nullptr, *fInfo = nullptr, *fDesc = nullptr;
    TextCache text;
    ImageCache images;
    ImageCache backs;            // 테마 배경 (작은 캐시)
    std::string lastBack;        // 새 배경을 읽기 전까지 보여줄 직전 배경
    bool themePick = false;      // 테마 선택 창
    bool optPick = false;        // 옵션 창 (Y): 홈브루 메뉴·RetroArch·PPSSPP·새로 고침·테마 등
    int optSel = 0;
    std::vector<std::string> themeList;  // [0] = 기본
    int themeSel = 0;
    std::vector<Collection> cols;
    Mode mode = Mode::Categories;
    int catSel = 0, catTop = 0;  // 카테고리 화면 선택 (cols + 메뉴)
    int col = 0, sel = 0, top = 0;
    Uint32 selChanged = 0;       // 선택 변경 시각 (박스아트 지연 로드)
    std::string message;
    Uint32 messageUntil = 0;
    bool messageModal = false;  // B 로 닫을 때까지 떠 있는 알림 (그동안 다른 입력 무시)
    SDL_Color messageColor = {120, 40, 40, 240};
    int confirmIdx = -1;
    Uint32 confirmUntil = 0;
};

static std::vector<char> gFontReg, gFontBold;

static bool ReadAll(const char *path, std::vector<char> &buf) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf.resize(n > 0 ? n : 0);
    size_t got = n > 0 ? fread(buf.data(), 1, n, fp) : 0;
    fclose(fp);
    return got == (size_t)n && n > 0;
}

static TTF_Font *OpenFontMem(std::vector<char> &buf, int size) {
    return TTF_OpenFontRW(SDL_RWFromConstMem(buf.data(), (int)buf.size()), 1, size);
}

static void DrawText(App &a, TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxW = 0,
                     int align = 0, int wrap = 0, int clipH = 0) {
    SDL_Texture *t = a.text.Get(f, s, c, wrap);
    if (!t) return;
    int tw, th;
    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    SDL_Rect src = {0, 0, tw, th};
    if (maxW > 0 && tw > maxW) src.w = maxW;
    if (clipH > 0 && th > clipH) {
        int ls = TTF_FontLineSkip(f);
        src.h = (wrap > 0 && ls > 0) ? clipH / ls * ls : clipH;  // 여러 줄이면 줄 단위로 자름
    }
    int dx = x;
    if (align == 1) dx = x - src.w / 2;
    if (align == 2) dx = x - src.w;
    SDL_Rect dst = {dx, y, src.w, src.h};
    SDL_RenderCopy(a.r, t, &src, &dst);
}

static void Fill(App &a, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(a.r, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(a.r, &rc);
}

static SDL_Rect DrawImage(App &a, SDL_Texture *t, int x, int y, int w, int h) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    float s = std::min(w / (float)bw, h / (float)bh);
    SDL_Rect d = {x + (int)(w - bw * s) / 2, y + (int)(h - bh * s) / 2, (int)(bw * s), (int)(bh * s)};
    SDL_RenderCopy(a.r, t, nullptr, &d);
    return d;
}

static const int kListX = 40, kListY = 96, kRowH = 42, kRows = 13;

// 화면 가득 채우기 (비율 유지, 넘치는 부분은 잘라냄)
static void DrawCover(App &a, SDL_Texture *t) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    float s = std::max(W / (float)bw, H / (float)bh);
    int sw = (int)(W / s), sh = (int)(H / s);
    SDL_Rect src = {(bw - sw) / 2, (bh - sh) / 2, sw, sh};
    SDL_RenderCopy(a.r, t, &src, nullptr);
}

// 지금 화면에 테마 배경이 깔렸는지 (배경이 있을 때만 반투명 스타일)
static bool gBackDrawn = false;
static bool Themed() { return gBackDrawn; }

// col: 배경·로고를 고를 카테고리 (nullptr = 기본 배경)
static void Frame(App &a, const std::string &title, const std::string &hint, const Collection *col = nullptr,
                  bool logoTitle = false) {
    SDL_SetRenderDrawColor(a.r, 30, 30, 38, 255);
    SDL_RenderClear(a.r);
    bool themed = gTheme.Active();
    if (themed) {
        // 카테고리(플랫폼)별 배경. 그 카테고리 배경이 없으면 배경 없이 기본 화면
        // (카테고리가 아닌 메뉴 항목에서는 테마의 default 배경)
        std::string bg = ThemeFind(gTheme.bg, col);
        SDL_Texture *t = nullptr;
        if (!bg.empty()) {
            t = a.backs.Find(bg);
            // 빠르게 넘길 때는 디스크에서 읽지 않고 직전 배경 유지
            if (!t && SDL_GetTicks() - a.selChanged > 150) t = a.backs.Load(bg);
            if (t) a.lastBack = bg;
            else if (!a.lastBack.empty()) t = a.backs.Find(a.lastBack);
        } else {
            a.lastBack.clear();
        }
        if (t) {
            DrawCover(a, t);
            Fill(a, 0, 0, W, H, {0, 0, 0, (Uint8)gTheme.dim});
        }
        gBackDrawn = t != nullptr;
    } else {
        gBackDrawn = false;
    }
    Uint8 barA = gBackDrawn ? 200 : 255;
    Fill(a, 0, 0, W, 76, {kDark.r, kDark.g, kDark.b, barA});
    Fill(a, 0, H - 52, W, 52, {kDark.r, kDark.g, kDark.b, barA});
    std::string logo = col && logoTitle ? ThemeFind(gTheme.logo, col) : "";
    SDL_Texture *lt = logo.empty() ? nullptr : a.images.Load(logo);
    if (lt) DrawImage(a, lt, W / 2 - 220, 8, 440, 60);
    else DrawText(a, a.fTitle, title, W / 2, 18, kAccent, 900, 1);
    if (gAppletMode) DrawText(a, a.fInfo, "애플릿 모드", 100, 26, {235, 60, 60, 255});
    DrawText(a, a.fInfo, hint, W / 2, H - 40, kWhite, 0, 1);
}

// ---------------------------------------------------------------- 영상 장면 캐시
// 화면 사진이 없고 미리보기 영상만 있는 게임: 처음 볼 때 한 장면을 뽑아 /JoyLauncher/cache 에 저장
static bool gShotMode = false;  // 데스크톱 스냅샷: 바로 추출
static std::map<std::string, std::string> gFramePath;  // 롬 파일 -> 캐시 jpg
static std::map<std::string, bool> gFrameReady;

// 영상 장면 저장 위치: 롬 파일 경로로 이름을 만든다 (영상과 무관).
// 그래서 장면을 뽑은 뒤에는 영상 파일을 지워도 그대로 보인다.
static std::string FramePath(const Game &g) {
    auto it = gFramePath.find(g.file);
    if (it != gFramePath.end()) return it->second;
    unsigned long long h = 1469598103934665603ULL;
    for (char c : g.file) { h ^= (unsigned char)c; h *= 1099511628211ULL; }
    char name[40];
    snprintf(name, sizeof(name), "%016llx.jpg", h);
    std::string p = gHome + "/cache/" + name;
    gFramePath[g.file] = p;
    return p;
}

// 이미 뽑아 둔 장면이 있는지 (결과를 기억)
static bool FrameFileReady(const std::string &p) {
    auto it = gFrameReady.find(p);
    if (it != gFrameReady.end()) return it->second;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    gFrameReady[p] = true;
    return true;
}

// 게임의 큰 사진: 화면 사진, 없으면 영상 장면(뽑아 둔 것 또는 준비됐을 때). settled: 이동이 멈췄는지
static std::string GameShot(const Game &g, bool settled) {
    if (!g.screenshot.empty()) return g.screenshot;
    std::string p = FramePath(g);
    if (FrameFileReady(p)) return p;
    if (g.video.empty() || gFrameReady.count(p)) return "";  // 영상이 없거나 이미 실패
    if (gShotMode) {
        bool ok = ExtractVideoFrame(g.video, p);
        gFrameReady[p] = ok;
        return ok ? p : "";
    }
    int state = FrameWorkerState(p);
    if (state != 0) { gFrameReady[p] = state > 0; return state > 0 ? p : ""; }
    if (settled && gVideoFrames == "auto") FrameWorkerRequest(g.video, p);
    return "";
}

static bool FrameCached(const Game &g) {
    std::string p = FramePath(g);
    if (gFrameReady.count(p)) return true;  // 성공 또는 실패로 이미 끝남
    return FrameFileReady(p);
}

static void CountFramesPending(const std::vector<Collection> &cols) {
    gFramesPending = 0;
    for (const auto &c : cols)
        for (const auto &g : c.games)
            if (g.screenshot.empty() && !g.video.empty() && !FrameCached(g)) gFramesPending++;
}

static SDL_Color SelColor() { return Themed() ? SDL_Color{70, 70, 110, 210} : kSel; }

static void RenderCategories(App &a) {
    const Collection *cc = a.catSel < (int)a.cols.size() ? &a.cols[a.catSel] : nullptr;
    Frame(a, "Joy Launcher", "[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼ 선택", cc);
    int total = (int)a.cols.size();
    for (int i = 0; i < kRows && a.catTop + i < total; i++) {
        int idx = a.catTop + i;
        int y = kListY + i * kRowH;
        bool sel = idx == a.catSel;
        if (sel) Fill(a, kListX - 12, y - 4, 720, kRowH, SelColor());
        if (idx < (int)a.cols.size()) {
            const Collection &c = a.cols[idx];
            DrawText(a, a.fList, c.name, kListX, y + 4, sel ? kWhite : kGrey, 560);
            char n[32];
            snprintf(n, sizeof(n), "%d", (int)c.games.size());
            DrawText(a, a.fInfo, n, kListX + 690, y + 8, kGrey, 0, 2);
        }
    }
    // 오른쪽: 선택 카테고리의 게임 박스아트 미리보기 (2x2)
    if (a.catSel < (int)a.cols.size()) {
        const Collection &c = a.cols[a.catSel];
        int px = 800, k = 0;
        for (size_t g = 0; g < c.games.size() && k < 4; g++) {
            if (c.games[g].boxart.empty()) continue;
            SDL_Texture *t = (SDL_GetTicks() - a.selChanged > 120) ? a.images.Load(c.games[g].boxart)
                                                                   : a.images.Find(c.games[g].boxart);
            if (!t) continue;
            DrawImage(a, t, px + (k % 2) * 225, 96 + (k / 2) * 260, 215, 250);
            k++;
        }
    }
}

static void RenderGames(App &a) {
    const Collection &c = a.cols[a.col];
    Frame(a, c.name, "[A] 실행   [B] 카테고리   [L][R] 카테고리 이동   ◀ ▶ 페이지   [X] 홈브루 메뉴   [Y] 옵션", &c, true);
    char idx[64];
    snprintf(idx, sizeof(idx), "%d / %d", a.col + 1, (int)a.cols.size());
    DrawText(a, a.fInfo, "◀ L", 30, 26, kGrey);
    DrawText(a, a.fInfo, "R ▶", W - 30, 26, kGrey, 0, 2);
    DrawText(a, a.fInfo, idx, W - 110, 26, kGrey, 0, 2);

    int n = (int)c.games.size();
    for (int i = 0; i < kRows && a.top + i < n; i++) {
        int gi = a.top + i;
        int y = kListY + i * kRowH;
        if (gi == a.sel) Fill(a, kListX - 12, y - 4, 720, kRowH, SelColor());
        DrawText(a, a.fList, c.games[gi].title, kListX, y + 4, gi == a.sel ? kWhite : kGrey, 690);
    }
    char cnt[64];
    snprintf(cnt, sizeof(cnt), "%d / %d", a.sel + 1, n);
    DrawText(a, a.fInfo, cnt, kListX, H - 40, kGrey);

    const Game &g = c.games[a.sel];
    int px = 800, pw = 440;
    // 이동이 잠시 멈춘 뒤에만 디스크에서 읽고, 캐시에 있으면 바로 표시
    bool settled = SDL_GetTicks() - a.selChanged > 120;
    auto img = [&](const std::string &p) -> SDL_Texture * {
        if (p.empty()) return nullptr;
        SDL_Texture *t = a.images.Find(p);
        if (!t && settled) t = a.images.Load(p);
        return t;
    };
    std::string shotPath = GameShot(g, SDL_GetTicks() - a.selChanged > 250);
    if (!shotPath.empty()) {
        // 게임 화면 사진(또는 영상 장면)을 크게, 박스 사진은 오른쪽 아래에 작게 겹쳐서
        SDL_Texture *shot = img(shotPath);
        SDL_Rect area = {px, 92, pw, 300};
        if (shot) area = DrawImage(a, shot, px, 92, pw, 300);
        else if (!Themed()) Fill(a, px, 92, pw, 300, {44, 44, 56, 255});
        SDL_Texture *box = g.boxart != shotPath ? img(g.boxart) : nullptr;
        if (box) {
            int bw, bh;
            SDL_QueryTexture(box, nullptr, nullptr, &bw, &bh);
            float sc = std::min(130.0f / bw, 150.0f / bh);
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            SDL_Rect d = {area.x + area.w - dw - 8, area.y + area.h - dh - 8, dw, dh};
            Fill(a, d.x - 3, d.y - 3, dw + 6, dh + 6, {0, 0, 0, 160});
            SDL_RenderCopy(a.r, box, nullptr, &d);
        }
    } else {
        SDL_Texture *box = img(g.boxart);
        if (box) DrawImage(a, box, px, 92, pw, 300);
        else if (!Themed()) Fill(a, px, 92, pw, 300, {44, 44, 56, 255});
    }

    int y = 404;
    std::string meta;
    if (!g.developer.empty()) meta += g.developer;
    if (!g.release.empty()) meta += (meta.empty() ? "" : " · ") + g.release.substr(0, 4);
    if (!g.players.empty()) meta += (meta.empty() ? "" : " · ") + g.players + "인";
    if (!meta.empty()) { DrawText(a, a.fInfo, meta, px, y, kWhite, pw); y += 32; }
    if (!g.genre.empty()) { DrawText(a, a.fInfo, g.genre, px, y, kGrey, pw); y += 32; }
    if (!g.description.empty() && SDL_GetTicks() - a.selChanged > 120)
        DrawText(a, a.fDesc, g.description.substr(0, 900), px, y + 4, kGrey, pw, 0, pw, H - 60 - (y + 4));
}

static void RenderThemePick(App &a) {
    int n = (int)a.themeList.size();
    int rows = std::min(n, 10);
    int bw = 560, bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, bx, by, bw, bh, {28, 28, 40, 245});
    DrawText(a, a.fBig, "테마 선택", W / 2, by + 16, kAccent, 0, 1);
    int top = std::max(0, std::min(a.themeSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 66 + i * kRowH;
        bool sel = idx == a.themeSel;
        if (sel) Fill(a, bx + 16, y - 4, bw - 32, kRowH, kSel);
        std::string label = idx == 0 ? std::string("기본 (배경 없음)") : a.themeList[idx];
        bool cur = (idx == 0 && gThemeName.empty()) || (idx > 0 && a.themeList[idx] == gThemeName);
        if (cur) label += "  ✓";
        DrawText(a, a.fList, label, bx + 32, y + 4, sel ? kWhite : kGrey, bw - 64);
    }
    DrawText(a, a.fInfo, "[A] 적용   [B] 닫기   (테마 폴더: /JoyLauncher/themes)", W / 2, by + bh - 34, kGrey, 0, 1);
}

// 옵션 창 (Y)
static void RenderOptions(App &a) {
    int n = (int)gMenu.size();
    int bw = 820, bh = 110 + n * kRowH;
    int bx = (W - bw) / 2, by = std::max(20, (H - bh) / 2);
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, bx, by, bw, bh, {28, 28, 40, 245});
    DrawText(a, a.fBig, "옵션", W / 2, by + 16, kAccent, 0, 1);
    for (int i = 0; i < n; i++) {
        int y = by + 66 + i * kRowH;
        bool sel = i == a.optSel;
        if (sel) Fill(a, bx + 16, y - 4, bw - 32, kRowH, kSel);
        DrawText(a, a.fList, gMenu[i].label, bx + 32, y + 4, sel ? kWhite : kGrey, bw - 64);
    }
    DrawText(a, a.fInfo, "[A] 선택   [B] 닫기", W / 2, by + bh - 34, kGrey, 0, 1);
}

static void Render(App &a);

// 메뉴의 "영상 장면 일괄 추출": 메인 스레드에서 차례로 추출하며 진행 상황 표시, B 로 중지
static void RunFrameBatch(App &a) {
    std::vector<const Game *> todo;
    for (const auto &c : a.cols)
        for (const auto &g : c.games)
            if (g.screenshot.empty() && !g.video.empty() && !FrameCached(g)) todo.push_back(&g);
    FrameWorkerStop();
    int done = 0, failed = 0;
    bool cancel = false;
    for (size_t i = 0; i < todo.size() && !cancel; i++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) cancel = true;
            if (e.type == SDL_JOYBUTTONDOWN && e.jbutton.button == 1) cancel = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) cancel = true;
        }
#ifdef __SWITCH__
        if (!appletMainLoop()) cancel = true;
#endif
        Render(a);
        Fill(a, 240, 280, 800, 150, {28, 28, 40, 245});
        char buf[128];
        snprintf(buf, sizeof(buf), "영상 장면 추출 중  %d / %d", (int)i + 1, (int)todo.size());
        DrawText(a, a.fList, buf, W / 2, 300, kWhite, 760, 1);
        Fill(a, 280, 350, 720, 14, {60, 60, 80, 255});
        Fill(a, 280, 350, (int)(720.0 * i / std::max<size_t>(1, todo.size())), 14, kAccent);
        DrawText(a, a.fInfo, "[B] 중지  (추출한 장면은 /JoyLauncher/cache 에 저장)", W / 2, 380, kGrey, 760, 1);
#ifndef __SWITCH__
        // 데스크톱: 설명서용 진행 화면 캡처 (JOY_SHOT_PROGRESS=파일)
        if (const char *sp = getenv("JOY_SHOT_PROGRESS"))
            if (i == todo.size() / 2) {
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(a.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
                IMG_SavePNG(s, sp);
                SDL_FreeSurface(s);
            }
#endif
        SDL_RenderPresent(a.r);
        const Game &g = *todo[i];
        std::string p = FramePath(g);
        bool ok = ExtractVideoFrame(g.video, p);
        gFrameReady[p] = ok;
        if (ok) done++;
        else failed++;
    }
    if (gVideoFrames == "auto") FrameWorkerStart();
    CountFramesPending(a.cols);
    BuildMenu();
    char msg[128];
    snprintf(msg, sizeof(msg), "영상 장면 %d개 추출%s%s", done, failed ? ", 실패 " : "",
             failed ? std::to_string(failed).c_str() : "");
    a.message = std::string(msg) + (cancel ? " (중지)" : "");
    a.messageColor = {40, 100, 60, 240};
    a.messageModal = true;
}

// 게임 목록: 저장해 둔 목록(library.cache)이 유효하면 그대로 쓰고, 아니면(또는 force) 롬 폴더를 다시 읽어 저장
static std::vector<std::string> gRoots;

static void LoadGames(App &a, bool force, std::string &log) {
    std::string cache = gHome + "/library.cache";
    std::string sig = LibrarySignature(gRoots);
    if (!force && LoadLibraryCache(cache, sig, a.cols)) {
        log += "[cache] " + cache + "\n";
    } else {
        if (a.r && !gShotMode) {
            SDL_SetRenderDrawColor(a.r, 16, 16, 22, 255);
            SDL_RenderClear(a.r);
            DrawText(a, a.fBig, "게임 목록을 읽는 중...", W / 2, 300, kWhite, 0, 1);
            DrawText(a, a.fInfo, "처음 한 번만 오래 걸립니다. 이후에는 저장된 목록을 바로 씁니다.", W / 2, 360, kGrey, 0, 1);
            SDL_RenderPresent(a.r);
        }
        Uint32 t0 = SDL_GetTicks();
        a.cols = LoadLibrary(gRoots, log);
        SaveLibraryCache(cache, sig, a.cols);
        log += "[scan] " + std::to_string(SDL_GetTicks() - t0) + " ms\n";
    }
    CountFramesPending(a.cols);
    BuildMenu();
}

static void Render(App &a) {
    if (a.cols.empty()) {
        Frame(a, "Joy Launcher", "[B] 종료");
        DrawText(a, a.fBig, "게임 목록이 없습니다", W / 2, 300, kWhite, 0, 1);
        DrawText(a, a.fInfo, "SD 카드의 /roms 폴더(또는 /JoyLauncher/roots.txt)를 확인하세요", W / 2, 360, kGrey, 0, 1);
    } else if (a.mode == Mode::Categories) {
        RenderCategories(a);
    } else {
        RenderGames(a);
    }
    if (a.optPick) RenderOptions(a);
    if (a.themePick) RenderThemePick(a);
    if (a.messageModal) {
        Fill(a, 0, 0, W, H, {0, 0, 0, 120});
        Fill(a, 240, 280, 800, 140, a.messageColor);
        DrawText(a, a.fList, a.message, W / 2, 310, kWhite, 780, 1);
        DrawText(a, a.fInfo, "[B] 닫기", W / 2, 370, kWhite, 0, 1);
    } else if (!a.message.empty() && SDL_GetTicks() < a.messageUntil) {
        Fill(a, 240, 300, 800, 90, a.messageColor);
        DrawText(a, a.fList, a.message, W / 2, 330, kWhite, 780, 1);
    }
}

static void Clamp(App &a) {
    if (a.cols.empty()) return;
    int total = (int)a.cols.size();
    a.catSel = std::max(0, std::min(a.catSel, total - 1));
    a.optSel = std::max(0, std::min(a.optSel, (int)gMenu.size() - 1));
    if (a.catSel < a.catTop) a.catTop = a.catSel;
    if (a.catSel >= a.catTop + kRows) a.catTop = a.catSel - kRows + 1;
    int n = (int)a.cols[a.col].games.size();
    a.sel = std::max(0, std::min(a.sel, n - 1));
    if (a.sel < a.top) a.top = a.sel;
    if (a.sel >= a.top + kRows) a.top = a.sel - kRows + 1;
}

// 화면 위치 저장. resume: 게임(또는 메뉴의 프로그램)을 실행하러 나갈 때만 true
//   -> 돌아왔을 때 그 위치로 복귀. 종료(HOME) 후 다시 켜면 맨 처음 화면(카테고리 목록 맨 위)
static void SaveState(App &a, bool resume = false) {
    if (a.cols.empty()) return;
    mkdir(gHome.c_str(), 0777);
    std::ofstream o(gHome + "/state.txt");
    o << a.cols[a.col].name << "\n"
      << a.cols[a.col].games[a.sel].file << "\n"
      << (a.mode == Mode::Games ? "games" : "categories") << "\n"
      << a.catSel << "\n"
      << (resume ? "resume" : "") << "\n";
}

// always: 실행 후 복귀가 아니어도 위치 복원 (목록 새로 고침 등)
static bool LoadState(App &a, bool always = false) {
    std::ifstream in(gHome + "/state.txt");
    std::string cn, file, mode, cat, resume;
    if (!std::getline(in, cn) || !std::getline(in, file)) return false;
    std::getline(in, mode);
    std::getline(in, cat);
    std::getline(in, resume);
    if (!always && resume != "resume") return false;
    for (size_t i = 0; i < a.cols.size(); i++) {
        if (a.cols[i].name != cn) continue;
        a.col = a.catSel = (int)i;
        for (size_t j = 0; j < a.cols[i].games.size(); j++)
            if (a.cols[i].games[j].file == file) a.sel = (int)j;
        a.mode = mode == "games" ? Mode::Games : Mode::Categories;
    }
    // 카테고리 화면에서 나갔으면(옵션의 RetroArch 등) 그 카테고리 줄로
    if (a.mode == Mode::Categories && !cat.empty()) {
        int cs = atoi(cat.c_str());
        if (cs >= 0 && cs < (int)a.cols.size()) a.catSel = cs;
    }
    a.top = std::max(0, a.sel - kRows / 2);
    a.catTop = std::max(0, a.catSel - kRows / 2);
    return true;
}

// 다음에 로드할 nro 를 예약. 스위치에서는 이후 런처가 종료되면 hbloader 가 실행한다.
static bool ChainLoad(App &a, std::string nro, const std::vector<std::string> &args) {
#ifdef __SWITCH__
    struct stat st;
    if (stat(nro.c_str(), &st) != 0) {
        a.message = "프로그램이 없습니다: " + nro;
        a.messageUntil = SDL_GetTicks() + 3000;
        return false;
    }
    if (nro.rfind("sdmc:", 0) != 0) nro = "sdmc:" + nro;
    std::string argv = "\"" + nro + "\"";
    for (const auto &s : args) argv += " \"" + s + "\"";
    if (R_FAILED(envSetNextLoad(nro.c_str(), argv.c_str()))) {
        a.message = "실행 예약 실패";
        a.messageUntil = SDL_GetTicks() + 3000;
        return false;
    }
    __nx_applet_exit_mode = 0;  // hbloader 로 돌아가 예약한 nro 실행
#else
    printf("LAUNCH nro=%s\n", nro.c_str());
    for (const auto &s : args) printf("ARG %s\n", s.c_str());
#endif
    return true;
}

// 명시적 종료: hbloader 로 돌아가지 않고 HOME 으로 (hbmenu 자리에서 무한 재실행 방지)
static void ExitHome() {
#ifdef __SWITCH__
    __nx_applet_exit_mode = 1;
#endif
}

// 실행 기록: /JoyLauncher/launch_log.txt 에 최근 kLogMax 개만 (최신이 위)
static const size_t kLogMax = 20;

static void LogLaunch(const App &a, const Game &g, const std::string &nro, const std::string &result) {
    std::string path = gHome + "/launch_log.txt";
    std::vector<std::string> entries;
    {
        std::ifstream in(path, std::ios::binary);
        std::string line, cur;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("[", 0) == 0 && !cur.empty()) {
                entries.push_back(cur);
                cur.clear();
            }
            if (line.rfind("[", 0) == 0 || !cur.empty()) {
                if (!line.empty()) cur += line + "\n";
            }
        }
        if (!cur.empty()) entries.push_back(cur);
    }
    char when[32] = "?";
    time_t now = time(nullptr);
    if (struct tm *t = localtime(&now)) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", t);
    std::string e = std::string("[") + when + "] " + g.title + "\n";
    e += "  카테고리: " + a.cols[a.col].name + "\n";
    e += "  롬 파일: " + g.file + "\n";
    e += "  원래 코어: " + (g.metaCore.empty() ? std::string("(메타데이터에 없음)") : g.metaCore) + "\n";
    e += "  선택 방법: " + (g.how.empty() ? std::string("?") : g.how) + "\n";
    e += "  실행: " + (nro.empty() ? std::string("(없음)") : nro) + "\n";
    e += "  결과: " + result + "\n";
    entries.insert(entries.begin(), e);
    if (entries.size() > kLogMax) entries.resize(kLogMax);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << "# Joy Launcher 실행 기록 (최근 " << kLogMax << "개, 최신이 위)\n\n";
    for (const auto &s : entries) o << s << "\n";
}

static bool LaunchGame(App &a) {
    const Game &g = a.cols[a.col].games[a.sel];
    std::string nro, args;
    if (!BuildLaunch(g, nro, args)) {
        a.message = "실행 설정(launch)이 없습니다";
        a.messageUntil = SDL_GetTicks() + 2500;
        LogLaunch(a, g, nro, "실패: 실행 설정 없음 (" + g.launch + ")");
        return false;
    }
    SaveState(a, true);
    bool ok = ChainLoad(a, nro, {g.file});
    LogLaunch(a, g, nro, ok ? "실행 예약됨" : "실패: " + a.message);
    return ok;
}

int main(int argc, char **argv) {
    std::string shot;
    int wantCol = -1, wantSel = -1, wantMode = -1, wantCat = -1, wantOpt = -1;
    std::string wantTheme;
    bool wantPick = false;
    bool wantFrames = false;
#ifdef __SWITCH__
    romfsInit();
    std::string root = kRoot;
    // /hbmenu.nro 로 실행됐으면 종료 시 HOME 으로 (아니면 hbloader 로 복귀)
    gLaunchedAsHbmenu = argc > 0 && strcasecmp(argv[0], "sdmc:/hbmenu.nro") == 0;
    __nx_applet_exit_mode = gLaunchedAsHbmenu ? 1 : 0;
    {
        AppletType t = appletGetAppletType();
        gAppletMode = t != AppletType_Application && t != AppletType_SystemApplication;
    }
#else
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--root" && i + 1 < argc) gRoot = argv[++i];
        else if (s == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (s == "--col" && i + 1 < argc) wantCol = atoi(argv[++i]);
        else if (s == "--cat" && i + 1 < argc) wantCat = atoi(argv[++i]);  // 카테고리 화면 커서
        else if (s == "--opt" && i + 1 < argc) wantOpt = atoi(argv[++i]);
        else if (s == "--applet") gAppletMode = true;  // 애플릿 모드 표시 확인용
        else if (s == "--update-check") {  // 깃헙 최신 릴리스 조회 결과 출력
            UpdateInfo u;
            std::string err;
            bool ok = FetchLatestRelease(u, err);
            printf("current=%s ok=%d latest=%s newer=%d size=%lld sha256=%s url=%s err=%s\n", CurrentVersion(), ok,
                   u.version.c_str(), ok ? CompareVersions(u.version, CurrentVersion()) > 0 : 0, u.size,
                   u.sha256.c_str(), u.url.c_str(), err.c_str());
            return ok ? 0 : 1;
        }  // 옵션 창 열고 커서
        else if (s == "--sel" && i + 1 < argc) wantSel = atoi(argv[++i]);
        else if (s == "--mode" && i + 1 < argc) wantMode = atoi(argv[++i]);
        else if (s == "--home" && i + 1 < argc) gHome = argv[++i];
        else if (s == "--theme" && i + 1 < argc) wantTheme = argv[++i];
        else if (s == "--themepick") wantPick = true;
        else if (s == "--frames") wantFrames = true;
    }
    std::string root = gRoot;
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();
    SDL_Window *win = SDL_CreateWindow("Joy Launcher", 0, 0, W, H, shot.empty() ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    App a;
    a.r = SDL_CreateRenderer(win, -1, shot.empty() ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
                                                   : SDL_RENDERER_SOFTWARE);
    SDL_SetRenderDrawBlendMode(a.r, SDL_BLENDMODE_BLEND);
    a.text.r = a.r;
    a.images.r = a.r;
    a.backs.r = a.r;
    a.backs.cap = 4;
    if (!ReadAll(kFont, gFontReg) || !ReadAll(kFontBold, gFontBold)) { printf("font read failed\n"); return 1; }
    a.fTitle = OpenFontMem(gFontBold, 34);
    a.fBig = OpenFontMem(gFontBold, 30);
    a.fList = OpenFontMem(gFontReg, 27);
    a.fInfo = OpenFontMem(gFontReg, 22);
    a.fDesc = OpenFontMem(gFontReg, 20);

#ifdef __SWITCH__
    if (argc > 0) {
        std::string self = argv[0];
        if (self.rfind("sdmc:", 0) == 0) self = self.substr(5);
        if (strcasecmp(self.c_str(), "/hbmenu.nro") != 0) gSelfNro = self;
    }
#endif
    LoadSettings();
    mkdir(gHome.c_str(), 0777);
    mkdir((gHome + "/cache").c_str(), 0777);
    gShotMode = !shot.empty();
    if (!gShotMode && gVideoFrames == "auto") FrameWorkerStart();
    if (!gShotMode && gAutoUpdate == "on") UpdateCheckStart();
    // 이전에 중단된 임시 파일 정리
    if (DIR *d = opendir((gHome + "/cache").c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0) remove((gHome + "/cache/" + n).c_str());
        }
        closedir(d);
    }
    if (!wantTheme.empty()) LoadTheme(wantTheme == "-" ? "" : wantTheme);
    BuildMenu();
    std::string log;
    std::vector<std::string> roots = {root};
    for (const std::string &rfile : {gHome + "/roots.txt", std::string(kOldStateDir) + "/roots.txt"}) {
        std::ifstream rf(rfile);
        std::string line;
        while (std::getline(rf, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '/')) line.pop_back();
            if (!line.empty() && line[0] != '#' && std::find(roots.begin(), roots.end(), line) == roots.end())
                roots.push_back(line);
        }
    }
    LoadCoreConfig(gHome + "/cores.txt");
    gRoots = roots;
    LoadGames(a, false, log);
    printf("%s", log.c_str());
    for (auto &c : a.cols) printf("collection %s (%s): %d\n", c.name.c_str(), c.sortBy.c_str(), (int)c.games.size());
    if (LoadState(a)) SaveState(a);  // 복귀 표시는 한 번만 사용 (다음 새 실행은 맨 처음 화면)
    if (wantCol >= 0 && wantCol < (int)a.cols.size()) { a.col = a.catSel = wantCol; a.sel = 0; a.top = 0; }
    if (wantCat >= 0 && wantCat < (int)a.cols.size()) {
        a.catSel = wantCat;
        a.catTop = std::max(0, a.catSel - kRows / 2);
    }
    if (wantOpt >= 0) { a.optPick = true; a.optSel = wantOpt; }
    if (wantSel >= 0) a.sel = wantSel;
    if (wantMode >= 0) a.mode = wantMode ? Mode::Games : Mode::Categories;
    Clamp(a);
    auto openThemePick = [&]() {
        a.themeList = ThemeNames();
        a.themeList.insert(a.themeList.begin(), "");
        a.themeSel = 0;
        for (size_t i = 1; i < a.themeList.size(); i++)
            if (a.themeList[i] == gThemeName) a.themeSel = (int)i;
        a.themePick = true;
    };
    if (wantPick) openThemePick();
    if (wantFrames) { gShotMode = false; RunFrameBatch(a); gShotMode = !shot.empty(); }

    if (!shot.empty()) {
        a.selChanged = SDL_GetTicks() - 1000;
        Render(a);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(a.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        IMG_SavePNG(s, shot.c_str());
        if (wantSel >= 0 && a.mode == Mode::Games) LaunchGame(a);
        return 0;
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    bool running = true;
    int held = -1;
    Uint32 nextRepeat = 0;
    int stickDir = -1;  // 스틱이 가리키는 방향 버튼 (12~15) 또는 -1
    // 옵션 창의 항목 실행
    // 화면 가운데 상자에 한 줄 표시 (오래 걸리는 작업 전)
    auto showBusy = [&](const std::string &text) {
        Render(a);
        Fill(a, 240, 300, 800, 90, {28, 28, 40, 245});
        DrawText(a, a.fList, text, W / 2, 330, kWhite, 780, 1);
        SDL_RenderPresent(a.r);
    };
    auto modal = [&](const std::string &text, bool ok) {
        a.message = text;
        a.messageColor = ok ? SDL_Color{40, 100, 60, 240} : SDL_Color{120, 40, 40, 240};
        a.messageModal = true;
    };
    // 옵션 창의 "업데이트": 확인 → (새 버전) A 한 번 더 → 내려받아 설치 → 다시 시작
    auto runUpdate = [&](int idx) {
        if (!gUpdateFound) {
            showBusy("업데이트 확인 중...");
            UpdateCheckStop();
            UpdateInfo info;
            std::string err;
            if (!FetchLatestRelease(info, err)) { a.optPick = false; modal("업데이트 확인 실패: " + err, false); return; }
            if (CompareVersions(info.version, CurrentVersion()) <= 0) {
                a.optPick = false;
                modal("최신 버전입니다 (v" + std::string(CurrentVersion()) + ")", true);
                return;
            }
            gUpdate = info;
            gUpdateFound = true;
            BuildMenu();
        } else if (a.confirmIdx == idx && SDL_GetTicks() <= a.confirmUntil) {
            a.confirmIdx = -1;
            a.optPick = false;
            UpdateCheckStop();
            Uint32 lastDraw = 0;
            auto progress = [&](long long got, long long total) {
                bool cancel = false;
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) cancel = true;
                    if (e.type == SDL_JOYBUTTONDOWN && e.jbutton.button == 1) cancel = true;
                    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) cancel = true;
                }
                if (SDL_GetTicks() - lastDraw >= 100 || got == total) {
                    lastDraw = SDL_GetTicks();
                    Render(a);
                    Fill(a, 240, 280, 800, 150, {28, 28, 40, 245});
                    char buf[160];
                    snprintf(buf, sizeof(buf), "v%s 내려받는 중  %.1f / %.1f MB", gUpdate.version.c_str(), got / 1048576.0,
                             total / 1048576.0);
                    DrawText(a, a.fList, buf, W / 2, 300, kWhite, 760, 1);
                    Fill(a, 280, 350, 720, 14, {60, 60, 80, 255});
                    if (total > 0) Fill(a, 280, 350, (int)(720.0 * got / total), 14, kAccent);
                    DrawText(a, a.fInfo, "[B] 취소", W / 2, 380, kGrey, 760, 1);
                    SDL_RenderPresent(a.r);
                }
                return !cancel;
            };
            std::string err;
            bool hb = CurrentHbmenu() == HbKind::Self;
            if (!DownloadAndInstall(gUpdate, gSelfNro, hb, err, progress)) { modal("업데이트 실패: " + err, false); return; }
            showBusy("v" + gUpdate.version + " 설치 완료. 다시 시작합니다...");
            SDL_Delay(1200);
            SaveState(a, true);
            if (ChainLoad(a, gLaunchedAsHbmenu ? std::string("/hbmenu.nro") : gSelfNro, {})) running = false;
            else modal("설치는 끝났습니다. 런처를 다시 실행해 주세요", true);
            return;
        }
        a.confirmIdx = idx;
        a.confirmUntil = SDL_GetTicks() + 5000;
        a.message = "새 버전 v" + gUpdate.version + ": A 를 한 번 더 누르면 내려받아 설치합니다";
        a.messageColor = {60, 70, 120, 240};
        a.messageUntil = a.confirmUntil;
    };
    auto runMenu = [&](int idx) {
        if (idx < 0 || idx >= (int)gMenu.size()) return;
        const MenuItem m = gMenu[idx];
        if (m.act == MenuAct::Theme) {
            a.optPick = false;
            openThemePick();
        } else if (m.act == MenuAct::Frames) {
            a.optPick = false;
            RunFrameBatch(a);
        } else if (m.act == MenuAct::Refresh) {
            a.optPick = false;
            SaveState(a);
            std::string log;
            LoadGames(a, true, log);
            LoadState(a, true);
            int n = 0;
            for (const auto &col : a.cols) n += (int)col.games.size();
            a.message = "게임 목록을 새로 읽었습니다 (" + std::to_string(n) + "개)";
            a.messageColor = {40, 100, 60, 240};
            a.messageModal = true;
            a.optPick = false;
        } else if (m.act == MenuAct::Update) {
            runUpdate(idx);
        } else if (m.act == MenuAct::Launch) {
            SaveState(a, true);
            if (ChainLoad(a, m.nro, {})) running = false;
        } else if (m.act == MenuAct::Exit) {
            SaveState(a);
            ExitHome();
            running = false;
        } else if (a.confirmIdx != idx || SDL_GetTicks() > a.confirmUntil) {
            a.confirmIdx = idx;
            a.confirmUntil = SDL_GetTicks() + 3000;
            a.message = "확인: A 를 한 번 더 누르면 실행합니다";
            a.messageColor = {60, 70, 120, 240};
            a.messageUntil = a.confirmUntil;
        } else {
            std::string msg;
            bool ok = m.act == MenuAct::InstallHb ? InstallAsHbmenu(gSelfNro, msg) : RestoreHbmenu(msg);
            a.message = msg;
            a.messageColor = ok ? SDL_Color{40, 100, 60, 240} : SDL_Color{120, 40, 40, 240};
            a.messageUntil = SDL_GetTicks() + 3500;
            a.confirmIdx = -1;
            BuildMenu();
        }
    };
    auto act = [&](int b) {
        if (a.messageModal) {  // 알림: B(또는 A)로 닫고, 그 외 입력은 무시
            if (b == 1 || b == 0) {
                a.messageModal = false;
                a.message.clear();
            }
            return;
        }
        if (a.themePick) {
            int n = (int)a.themeList.size();
            if (b == 13) a.themeSel = (a.themeSel + n - 1) % n;
            else if (b == 15) a.themeSel = (a.themeSel + 1) % n;
            else if (b == 0) {
                LoadTheme(a.themeList[a.themeSel]);
                SaveSettings();
                a.backs.Clear();
                a.lastBack.clear();
                a.themePick = false;
                BuildMenu();
            } else if (b == 1) a.themePick = false;
            a.selChanged = SDL_GetTicks() - 1000;
            return;
        }
        if (a.optPick) {
            int n = (int)gMenu.size();
            if (b == 13) a.optSel = (a.optSel + n - 1) % n;
            else if (b == 15) a.optSel = (a.optSel + 1) % n;
            else if (b == 0) runMenu(a.optSel);
            else if (b == 1 || b == 3) a.optPick = false;
            a.selChanged = SDL_GetTicks() - 1000;
            return;
        }
        if (b == 3) {  // Y: 옵션
            a.optPick = true;
            a.optSel = 0;
            a.confirmIdx = -1;
            return;
        }
        if (b == 2) {  // X: 홈브루 메뉴 (sphaira / nx-hbmenu)
            for (const auto &m : gMenu)
                if (m.act == MenuAct::Launch && m.label.rfind("홈브루 메뉴", 0) == 0) {
                    SaveState(a, true);
                    if (ChainLoad(a, m.nro, {})) running = false;
                    return;
                }
            a.message = "홈브루 메뉴(sphaira / nx-hbmenu)를 찾을 수 없습니다";
            a.messageColor = {120, 40, 40, 240};
            a.messageUntil = SDL_GetTicks() + 3000;
            return;
        }
        if (a.cols.empty()) { if (b == 1 || b == 10) running = false; return; }
        int ncol = (int)a.cols.size();
        if (a.mode == Mode::Categories) {
            switch (b) {
            case 13: a.catSel--; break;
            case 15: a.catSel++; break;
            case 12: a.catSel -= kRows; break;
            case 14: a.catSel += kRows; break;
            case 0:
                if (a.catSel < ncol) {
                    if (a.col != a.catSel) { a.col = a.catSel; a.sel = a.top = 0; }
                    a.mode = Mode::Games;
                }
                break;
            case 1: case 10: SaveState(a); ExitHome(); running = false; break;
            }
        } else {
            switch (b) {
            case 13: a.sel--; break;
            case 15: a.sel++; break;
            case 12: a.sel -= kRows; break;
            case 14: a.sel += kRows; break;
            case 6: a.col = (a.col + ncol - 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 7: a.col = (a.col + 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 0: if (LaunchGame(a)) running = false; break;
            case 1: a.mode = Mode::Categories; a.catSel = a.col; break;
            case 10: SaveState(a); ExitHome(); running = false; break;
            }
        }
        a.selChanged = SDL_GetTicks();
        Clamp(a);
    };
    bool updateNotified = gAutoUpdate != "on" || gShotMode;
    while (running) {
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
#endif
        if (!updateNotified) {  // 시작 시 백그라운드 확인 결과 (새 버전이면 한 번 알림)
            UpdateInfo info;
            std::string err;
            int st = UpdateCheckState(info, err);
            if (st != 0) {
                updateNotified = true;
                if (st == 1 && !gUpdateFound) {
                    gUpdate = info;
                    gUpdateFound = true;
                    BuildMenu();
                    if (!a.messageModal) {
                        a.message = "새 버전 v" + info.version + " 이 있습니다  ·  Y 옵션에서 설치";
                        a.messageColor = {60, 70, 120, 240};
                        a.messageUntil = SDL_GetTicks() + 5000;
                    }
                }
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            // 스위치 SDL 은 스틱을 축 이벤트와 함께 버튼 16~23 으로도 보낸다. 스틱은 아래 축 처리로만 다루고
            // 버튼 쪽은 무시 (그러지 않으면 누르고 있는 방향(held)을 덮어써 연속 이동이 끊김)
            if ((e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) && e.jbutton.button >= 16) continue;
            if (e.type == SDL_JOYBUTTONDOWN) {
                act(e.jbutton.button);
                held = e.jbutton.button;
                nextRepeat = SDL_GetTicks() + 400;
            }
            if (e.type == SDL_JOYBUTTONUP && e.jbutton.button == held) held = -1;
            if (e.type == SDL_JOYAXISMOTION) {
                // 축 0/2: 좌우, 1/3: 상하 (왼쪽·오른쪽 스틱)
                SDL_Joystick *js = SDL_JoystickFromInstanceID(e.jaxis.which);
                int dir = -1;
                if (js) {
                    for (int ax = 0; ax < 4 && ax < SDL_JoystickNumAxes(js); ax += 2) {
                        int x = SDL_JoystickGetAxis(js, ax), y = SDL_JoystickGetAxis(js, ax + 1);
                        const int dead = 16000;
                        if (std::abs(y) >= std::abs(x) && std::abs(y) > dead) dir = y < 0 ? 13 : 15;
                        else if (std::abs(x) > dead) dir = x < 0 ? 12 : 14;
                        if (dir >= 0) break;
                    }
                }
                if (dir != stickDir) {
                    stickDir = dir;
                    if (dir >= 0) {
                        act(dir);
                        held = dir;
                        nextRepeat = SDL_GetTicks() + 400;
                    } else if (held >= 12 && held <= 15) {
                        held = -1;
                    }
                }
            }
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: act(13); break;
                case SDLK_DOWN: act(15); break;
                case SDLK_LEFT: act(12); break;
                case SDLK_RIGHT: act(14); break;
                case SDLK_q: act(6); break;
                case SDLK_w: act(7); break;
                case SDLK_RETURN: act(0); break;
                case SDLK_ESCAPE: act(1); break;
                case SDLK_t: act(2); break;
                }
            }
        }
        if (held >= 12 && held <= 15 && SDL_GetTicks() >= nextRepeat) {
            act(held);
            nextRepeat = SDL_GetTicks() + 70;
        }
        Render(a);
        SDL_RenderPresent(a.r);
    }
    FrameWorkerStop();
    a.text.Clear();
    a.images.Clear();
    a.backs.Clear();
    for (TTF_Font *f : {a.fTitle, a.fBig, a.fList, a.fInfo, a.fDesc})
        if (f) TTF_CloseFont(f);
    SDL_DestroyRenderer(a.r);
    SDL_DestroyWindow(win);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    fflush(nullptr);
#ifdef __SWITCH__
    // exFAT 손상 방지: 종료 전에 SD 파일시스템 커밋
    if (FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc")) fsFsCommit(fs);
    romfsExit();
#endif
    return 0;
}
