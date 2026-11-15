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
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "bgm.h"
#include "debuglog.h"
#include "hbmenu.h"
#include "i18n.h"
#include "installed.h"
#include "layout.h"
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
enum class MenuAct { Launch, Theme, Frames, Refresh, Update, Language, BgmMenu, Bgm, BgmVolume, BgmOrder, BgmNext, BgmNone, HbFav, InstallHb, RestoreHb, Exit };
struct MenuItem {
    std::string label;
    std::string nro;
    MenuAct act;
    bool on = false;  // 켜져 있는 상태 (BGM 재생 중): 초록색 글자
    bool hb = false;  // 홈브루 메뉴 (X 버튼으로 바로 실행)
};
static const SDL_Color kOn = {90, 220, 120, 255};
static std::vector<MenuItem> gMenu;
static std::vector<MenuItem> gBgmMenu;  // 옵션 창의 "BGM 설정" 에서 여는 BGM 창
// 앨범(애플릿)으로 실행됐는지. 애플릿 모드는 쓸 수 있는 메모리가 적어 큰 게임이 느리거나 실행되지 않을 수 있음
static bool gAppletMode = false;
static std::string gSelfNro = "/switch/joylauncher/joylauncher.nro";
static std::string gThemeName;  // 현재 테마 폴더 이름 ("" = 기본)
static std::string ThemeTitle(const std::string &folder);  // 테마의 화면 이름
static int gFramesPending = 0;   // 영상 장면을 아직 뽑지 않은 게임 수
static std::string gVideoFrames = "auto";
static std::string gAutoUpdate = "on";      // on: 시작할 때 깃헙에서 새 버전 확인 (설치는 옵션 창에서)
static bool gUpdateFound = false;           // 새 버전을 찾았는지
static UpdateInfo gUpdate;
static bool gLaunchedAsHbmenu = false;      // /hbmenu.nro 로 실행됐는지 (업데이트 후 다시 시작할 경로)
static std::string gLanguage = "auto";      // auto: 스위치 본체 언어, ko, en
static Lang gSystemLang = Lang::Ko;         // 본체 언어 (시작할 때 한 번 읽음)
static bool gBgmOn = false;                 // BGM (/JoyLauncher/bgm 의 mp3·ogg)
static int gBgmVolume = 60;                 // 0~100
static bool gBgmShuffle = false;

// 즐겨찾기: /JoyLauncher/favorites.txt (롬 파일 경로 한 줄씩)
static std::set<std::string> gFavSet;

// 홈브루 즐겨찾기: 옵션에서 고른 nro (포팅 게임 등) — /JoyLauncher/homebrew_favorites.txt (nro 의 SD 경로 한 줄씩)
static std::vector<std::string> gHbFav;

static void SaveHbFav() {
    std::ofstream o(gHome + "/homebrew_favorites.txt");
    o << "# Joy Launcher homebrew favorites (nro paths on the SD card)\n";
    for (const auto &p : gHbFav) o << p << "\n";
}

static void LoadHbFav() {
    gHbFav.clear();
    std::string path = gHome + "/homebrew_favorites.txt", old = gHome + "/homebrew_games.txt";
    bool migrate = !std::ifstream(path).good() && std::ifstream(old).good();  // 0.9.1 시험판의 "홈브루 게임즈" 기록
    std::ifstream in(migrate ? old : path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#' && std::find(gHbFav.begin(), gHbFav.end(), line) == gHbFav.end())
            gHbFav.push_back(line);
    }
    in.close();
    if (migrate) {
        SaveHbFav();
        remove(old.c_str());
    }
}

static void LoadFavorites() {
    gFavSet.clear();
    std::ifstream in(gHome + "/favorites.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') gFavSet.insert(line);
    }
}

static void SaveFavorites() {
    std::ofstream o(gHome + "/favorites.txt");
    o << "# Joy Launcher favorites (rom paths)\n";
    for (const auto &f : gFavSet) o << f << "\n";
}

static void ApplyLanguage() {
    SetLang(gLanguage == "ko" ? Lang::Ko : gLanguage == "en" ? Lang::En : gSystemLang);
}

static void ApplyBgm() { BgmConfigure(gBgmOn, gBgmVolume, gBgmShuffle); }

static std::string LanguageLabel() {
    std::string sys = gSystemLang == Lang::Ko ? "한국어" : "English";
    std::string v = gLanguage == "ko" ? "한국어" : gLanguage == "en" ? "English" : Fmt(T("자동 (%s)"), sys.c_str());
    return Fmt(T("언어 / Language: %s"), v.c_str());
}

static void BuildMenu() {
    gMenu.clear();
    HbKind hb = CurrentHbmenu();
    // Joy 를 기본 메뉴로 설정해 뒀는데 다른 메뉴로 바뀌었으면 (Atmosphere 업데이트 등) 맨 위에 다시 설정
    bool hbReplaced = hb != HbKind::Self && HbInstalledRecord();
    if (hbReplaced)
        gMenu.push_back({Fmt(T("Joy Launcher 를 기본 홈브루 메뉴로 다시 설정 (현재: %s)"), HbKindName(hb)), "", MenuAct::InstallHb});
    for (const auto &m : HomebrewMenus()) gMenu.push_back({m.label, m.nro, MenuAct::Launch, false, true});
    if (SdExists("/switch/retroarch_switch.nro"))
        gMenu.push_back({"RetroArch", "/switch/retroarch_switch.nro", MenuAct::Launch});
    if (SdExists("/switch/ppsspp/PPSSPP.nro"))
        gMenu.push_back({"PPSSPP", "/switch/ppsspp/PPSSPP.nro", MenuAct::Launch});
    if (gFramesPending > 0)
        gMenu.push_back({Fmt(T("영상 장면 일괄 추출 (남은 게임 %d개)"), gFramesPending), "", MenuAct::Frames});
    gMenu.push_back({T("게임 목록 새로 고침 (롬·그림·영상을 넣거나 뺐을 때)"), "", MenuAct::Refresh});
    gMenu.push_back({gUpdateFound ? Fmt(T("업데이트 설치: v%s (현재 v%s)"), gUpdate.version.c_str(), CurrentVersion())
                                  : Fmt(T("업데이트 확인 (현재 v%s)"), CurrentVersion()),
                     "", MenuAct::Update});
    gMenu.push_back({Fmt(T("테마 설정 (현재: %s)"), gThemeName.empty() ? T("기본") : ThemeTitle(gThemeName).c_str()), "",
                     MenuAct::Theme});
    if (BgmCount() == 0)
        gMenu.push_back({T("BGM: 곡 없음  (/JoyLauncher/bgm 에 mp3·ogg 넣기)"), "", MenuAct::BgmNone});
    else if (gBgmOn)
        gMenu.push_back({Fmt(T("BGM 설정 (켜짐 (On) · 볼륨 %d%% · %s)"), gBgmVolume, gBgmShuffle ? T("무작위") : T("차례대로")), "",
                         MenuAct::BgmMenu, true});
    else
        gMenu.push_back({T("BGM 설정 (꺼짐 (Off))"), "", MenuAct::BgmMenu});
    gBgmMenu.clear();
    gBgmMenu.push_back({Fmt(gBgmOn ? T("BGM: 켜짐 (On) (%d곡)") : T("BGM: 꺼짐 (Off) (%d곡)"), BgmCount()), "", MenuAct::Bgm, gBgmOn});
    gBgmMenu.push_back({Fmt(T("볼륨: %d%%"), gBgmVolume), "", MenuAct::BgmVolume});
    gBgmMenu.push_back({gBgmShuffle ? T("순서: 무작위") : T("순서: 차례대로"), "", MenuAct::BgmOrder});
    if (gBgmOn) gBgmMenu.push_back({T("다음 곡"), "", MenuAct::BgmNext});
    gMenu.push_back({LanguageLabel(), "", MenuAct::Language});
    gMenu.push_back({Fmt(T("홈브루 즐겨찾기 추가·빼기 (%d개)"), (int)gHbFav.size()), "", MenuAct::HbFav, !gHbFav.empty()});
    if (hbReplaced) {
    } else if (hb == HbKind::Self && NroTitle("/hbmenu.nro") == "Joy Launcher")
        gMenu.push_back({T("원래 홈브루 메뉴로 되돌리기"), "", MenuAct::RestoreHb});
    else if (hb == HbKind::Self)
        gMenu.push_back({T("기본 홈브루 메뉴를 Joy Launcher 로 업데이트 (현재: 이전 버전)"), "", MenuAct::InstallHb});
    else
        gMenu.push_back({Fmt(T("Joy Launcher 를 기본 홈브루 메뉴로 설정 (현재: %s)"), HbKindName(hb)), "", MenuAct::InstallHb});
    gMenu.push_back({T("종료 (HOME)"), "", MenuAct::Exit});
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
        if (map.count(p)) return nullptr;  // 읽기에 실패했던 그림: 다시 읽지 않음
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

// 큰 그림을 정수 배율로 줄인다 (면적 평균). 표시 크기의 2배 이상일 때만. 원본은 해제하고 새 것을 돌려준다
static SDL_Surface *ShrinkSurface(SDL_Surface *src, int k) {
    if (k < 2) return src;
    SDL_Surface *c = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!c) return src;
    int nw = c->w / k, nh = c->h / k;
    SDL_Surface *d = nw > 0 && nh > 0 ? SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (!d) { SDL_FreeSurface(c); return src; }
    for (int y = 0; y < nh; y++) {
        Uint8 *out = (Uint8 *)d->pixels + y * d->pitch;
        for (int x = 0; x < nw; x++) {
            unsigned r = 0, g = 0, b = 0, al = 0;
            for (int j = 0; j < k; j++) {
                const Uint8 *in = (const Uint8 *)c->pixels + (y * k + j) * c->pitch + x * k * 4;
                for (int i = 0; i < k; i++, in += 4) {
                    r += in[0] * in[3]; g += in[1] * in[3]; b += in[2] * in[3]; al += in[3];  // 투명한 점의 색이 섞이지 않게
                }
            }
            out[x * 4 + 0] = al ? r / al : 0;
            out[x * 4 + 1] = al ? g / al : 0;
            out[x * 4 + 2] = al ? b / al : 0;
            out[x * 4 + 3] = al / (k * k);
        }
    }
    SDL_FreeSurface(c);
    SDL_FreeSurface(src);
    return d;
}

// 표시 크기에 맞춰 줄여 올리는 그림 캐시 (테마 아이콘·칸 바탕·격자의 박스 사진).
// 큰 그림을 그대로 올리면 메모리를 많이 쓰고 줄여 그릴 때 거칠게 보인다.
struct ThumbCache {
    struct Item { SDL_Texture *t; Uint32 used; };
    SDL_Renderer *r = nullptr;
    std::map<std::string, Item> map;
    size_t cap = 240;
    Uint32 tick = 0;  // 프레임마다 +1 (가장 오래 안 쓴 것부터 버림)
    static std::string Key(const std::string &p, int w, int h) { return p + "|" + std::to_string(w) + "x" + std::to_string(h); }
    // known: 읽은 적이 있는지 (읽기에 실패한 그림도 true, 다시 읽지 않음)
    SDL_Texture *Find(const std::string &p, int w, int h, bool &known) {
        auto it = map.find(Key(p, w, h));
        known = it != map.end();
        if (!known) return nullptr;
        it->second.used = tick;
        return it->second.t;
    }
    // w x h 칸에 그릴 그림. cover: 칸을 가득 채우는 용도 (작은 쪽 배율 기준)
    SDL_Texture *Load(const std::string &p, int w, int h, bool cover) {
        SDL_Surface *s = IMG_Load(p.c_str());
        if (s && w > 0 && h > 0) {
            float kx = s->w / (float)w, ky = s->h / (float)h;
            s = ShrinkSurface(s, (int)(cover ? std::min(kx, ky) : std::max(kx, ky)));
        }
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(r, s) : nullptr;
        if (s) SDL_FreeSurface(s);
        if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);  // 줄이거나 늘릴 때 부드럽게
        if (map.size() >= cap) {
            auto old = map.begin();
            for (auto it = map.begin(); it != map.end(); ++it)
                if (it->second.used < old->second.used) old = it;
            if (old->second.t) SDL_DestroyTexture(old->second.t);
            map.erase(old);
        }
        map[Key(p, w, h)] = {t, tick};
        return t;
    }
    void Clear() {
        for (auto &kv : map)
            if (kv.second.t) SDL_DestroyTexture(kv.second.t);
        map.clear();
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
    std::string base;                             // 테마 폴더 ("" = 테마 없음)
    Layout layout;                                // 화면 구성 (layout.txt, 없으면 기본)
    std::map<std::string, std::map<std::string, std::string>> folders;  // 그 밖의 그림 폴더 (icons 등): 처음 쓸 때 읽음
    // alias 만 있는 테마는 의미 없음
    bool Active() const { return !bg.empty() || !logo.empty() || layout.custom; }
};
static Theme gTheme;
static Uint32 gThemeSerial = 0;  // 테마를 바꿀 때마다 +1 (글꼴·그림 캐시 비우기용)

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

// 런처와 함께 배포하는 테마 팩 (릴리스의 <폴더>.zip, 테마 선택 창에서 내려받기)
struct ThemePack {
    const char *folder, *ko, *en;
    const char *oldKo, *oldEn;  // 1.0.0 의 이름: SD 에 남은 예전 theme.txt 의 name 을 지금 이름으로 보여 준다
};
static const ThemePack kPacks[] = {
    {"JoyBasic", "기본테마", "Basic Theme", "기본팩", "Basic Pack"},
    {"RetroPack", "레트로테마", "Retro Theme", "레트로팩", "Retro Pack"},
    {"ByeokgolKim", "벽골김씨테마", "WallStreetKim's Theme", "벽골김씨팩", "Byeokgol Kim Pack"},
};

// theme.txt 한 줄을 key / value 로 (설명·빈 줄이면 false)
static bool ThemeTxtLine(std::string line, bool first, std::string &k, std::string &v) {
    if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        line = line.substr(3);  // UTF-8 BOM (메모장 저장)
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t lead = line.find_first_not_of(" \t");
    if (lead == std::string::npos || line[lead] == '#' || line[lead] == ';' || line[lead] == '[') return false;
    line = line.substr(lead);
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    k = line.substr(0, eq);
    v = line.substr(eq + 1);
    // 줄 끝 설명: 공백 뒤의 # 또는 ; 부터 끝까지 버림 (예: snes=SFC   # 슈퍼패미컴)
    for (size_t i = 1; i < v.size(); i++)
        if ((v[i] == '#' || v[i] == ';') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v.erase(i);
            break;
        }
    while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
    while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    return !k.empty();
}

// 테마의 화면 이름: theme.txt 의 name (영어 화면이면 name_en 우선) → 함께 배포하는 팩의 이름 → 폴더 이름
static std::string ThemeTitle(const std::string &folder) {
    bool en = CurrentLang() == Lang::En;
    std::string name, nameEn, line, k, v;
    std::ifstream in(gHome + "/themes/" + folder + "/theme.txt");
    for (int n = 0; n < 200 && std::getline(in, line); n++) {
        if (!ThemeTxtLine(line, n == 0, k, v)) continue;
        std::string lk = LowerAscii(k);
        if (lk == "name") name = v;
        else if (lk == "name_en") nameEn = v;
    }
    const ThemePack *pack = nullptr;
    for (const auto &p : kPacks)
        if (folder == p.folder) pack = &p;
    if (pack && name == pack->oldKo) name = pack->ko;
    if (pack && nameEn == pack->oldEn) nameEn = pack->en;
    if (en && !nameEn.empty()) return nameEn;
    if (!name.empty()) return name;
    if (pack) return en ? pack->en : pack->ko;
    return folder;
}

static void LoadTheme(const std::string &name) {
    gTheme = Theme();
    LayoutDefault(gTheme.layout);
    gThemeSerial++;
    gThemeName.clear();
    if (name.empty()) return;
    std::string base = gHome + "/themes/" + name;
    if (!IsDirPath(base)) return;
    gTheme.name = gThemeName = name;
    gTheme.base = base;
    LayoutLoad(gTheme.layout, base + "/layout.txt");
    // layout.txt 의 문제는 테마 폴더의 layout_errors.txt 에 적어 둔다 (문제가 없으면 지움)
    if (gTheme.layout.custom) {
        std::string ep = base + "/layout_errors.txt";
        if (gTheme.layout.problems.empty()) {
            remove(ep.c_str());
        } else {
            std::ofstream eo(ep, std::ios::binary);
            eo << "layout.txt problems (line: what)\r\n";
            for (const auto &pr : gTheme.layout.problems) eo << pr << "\r\n";
        }
    }
    for (const char *d : {"backgrounds", "Background_image1", "Resource/Background_image1"}) IndexImages(base + "/" + d, gTheme.bg);
    for (const char *d : {"logos", "Logo1", "Resource/Logo1"}) IndexImages(base + "/" + d, gTheme.logo);
    std::ifstream in(base + "/theme.txt");
    std::string line, k, v;
    bool first = true;
    while (std::getline(in, line)) {
        bool ok = ThemeTxtLine(line, first, k, v);
        first = false;
        if (!ok) continue;
        std::string lk = LowerAscii(k);
        if (lk == "dim") gTheme.dim = std::max(0, std::min(255, atoi(v.c_str())));
        else if (lk == "name" || lk == "name_en") continue;  // 화면 이름 (ThemeTitle)
        else if (!v.empty()) {
            // 확장자를 적었으면 떼고 이름만 (backgrounds 는 jpg/png, logos 는 png 를 각각 찾음)
            size_t dot = v.find_last_of('.');
            if (dot != std::string::npos) {
                std::string ext = LowerAscii(v.substr(dot + 1));
                if (ext == "jpg" || ext == "jpeg" || ext == "png") v = v.substr(0, dot);
            }
            gTheme.alias[lk] = LowerAscii(v);
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
    o << T("# 숨길 코어 (스위치 성능 범위 밖 기종). 비우면 모든 기종 표시") << "\n";
    o << "hide_cores=" << gHideCores << "\n";
    o << T("# 영상 장면: auto = 게임을 볼 때 자동 추출, manual = 메뉴의 일괄 추출만") << "\n";
    o << "video_frames=" << gVideoFrames << "\n";
    o << T("# 자동 업데이트 확인: on = 시작할 때 새 버전 확인 (설치는 옵션 창에서), off = 끔") << "\n";
    o << "auto_update=" << gAutoUpdate << "\n";
    o << T("# 언어: auto = 스위치 본체 언어, ko = 한국어, en = English") << "\n";
    o << "language=" << gLanguage << "\n";
    o << T("# BGM: /JoyLauncher/bgm 의 mp3·ogg. bgm = on/off, bgm_volume = 0~100, bgm_shuffle = on/off") << "\n";
    o << "bgm=" << (gBgmOn ? "on" : "off") << "\n";
    o << "bgm_volume=" << gBgmVolume << "\n";
    o << "bgm_shuffle=" << (gBgmShuffle ? "on" : "off") << "\n";
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
        if (line.rfind("language=", 0) == 0) {
            std::string v = line.substr(9);
            gLanguage = v == "ko" || v == "en" ? v : "auto";
        }
        if (line.rfind("bgm=", 0) == 0) gBgmOn = line.substr(4) == "on";
        if (line.rfind("bgm_volume=", 0) == 0) gBgmVolume = std::max(0, std::min(100, atoi(line.c_str() + 11)));
        if (line.rfind("bgm_shuffle=", 0) == 0) gBgmShuffle = line.substr(12) == "on";
    }
    ApplyLanguage();
    // 설정이 없으면 기본 테마 JoyBasic (없으면 첫 번째 테마)
    if (!found) {
        auto names = ThemeNames();
        if (std::find(names.begin(), names.end(), "JoyBasic") != names.end()) theme = "JoyBasic";
        else if (!names.empty()) theme = names[0];
    }
    LoadTheme(theme);
    ApplyHideCores();
    if (!found) SaveSettings();  // 처음 실행: 기본 설정 파일 만들기
}

// 사용자가 파일을 넣는 폴더와 안내 파일. 안내 파일이 없으면 만들고,
// 런처가 만든 옛 안내(첫 줄이 같음)면 새 내용으로 바꾼다. 사용자가 첫 줄을 고친 파일은 그대로 둔다.
static void WriteGuide(const std::string &dir, const char *text) {
    mkdir(dir.c_str(), 0777);
    std::string p = dir + "/README.txt";
    {
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::string cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string want(text);
            if (cur == want) return;
            size_t nl = want.find('\n');
            if (nl == std::string::npos || cur.compare(0, nl + 1, want, 0, nl + 1) != 0) return;
        }
    }
    std::ofstream o(p, std::ios::binary);
    o << text;
}

static void MakeUserFolders() {
    mkdir(gHome.c_str(), 0777);
    mkdir((gHome + "/cache").c_str(), 0777);
    WriteGuide(gHome + "/themes",
               "Joy Launcher 테마 폴더 / Theme folder\r\n"
               "\r\n"
               "[한국어]\r\n"
               "테마 하나 = 이 안의 폴더 하나입니다. 예) themes/MyTheme/\r\n"
               "  backgrounds/<롬 폴더 이름>.jpg|png   카테고리 배경 (예: backgrounds/GBA.jpg -> /roms/GBA)\r\n"
               "  backgrounds/favorites.jpg            즐겨찾기 카테고리 배경 (홈브루 즐겨찾기는 hbfav)\r\n"
               "  backgrounds/default.jpg              \"게임 목록이 없습니다\" 화면 배경\r\n"
               "  logos/<롬 폴더 이름>.png             게임 목록 제목 대신 표시할 로고 (선택)\r\n"
               "  theme.txt                            dim=0~255 (배경 어둡게), <카테고리 이름>=<그림 파일 이름>\r\n"
               "배경이 없는 카테고리는 테마를 끈 화면으로 보입니다.\r\n"
               "폴더·파일 이름은 영문(ASCII)을 권장합니다. Y 옵션 > 테마 설정에서 고릅니다.\r\n"
               "  layout.txt                           화면 구성 바꾸기 (격자, 위치, 글꼴·색): 테마_레이아웃.md\r\n"
               "자세한 방법: 테마_만들기.md (릴리스 zip) / github.com/JyJyLab/JoyLauncher/blob/main/docs/테마_만들기.md\r\n"
               "\r\n"
               "[English]\r\n"
               "One theme = one folder in here, e.g. themes/MyTheme/\r\n"
               "  backgrounds/<rom folder>.jpg|png   category background (e.g. backgrounds/GBA.jpg -> /roms/GBA)\r\n"
               "  backgrounds/favorites.jpg          Favorites category background (Homebrew Favorites: hbfav)\r\n"
               "  backgrounds/default.jpg            background of the \"No games found\" screen\r\n"
               "  logos/<rom folder>.png             logo shown instead of the game list title (optional)\r\n"
               "  theme.txt                          dim=0-255 (darken backgrounds), <category name>=<image file name>\r\n"
               "Categories without a background look like the no-theme screen.\r\n"
               "Use ASCII folder and file names. Pick a theme in Y Options > Theme.\r\n"
               "  layout.txt                         change the screen layout (grids, positions, fonts, colors): theme-layout-en.md\r\n"
               "Details: theme-guide-en.md (release zip) / github.com/JyJyLab/JoyLauncher/blob/main/docs/theme-guide-en.md\r\n");
    WriteGuide(gHome + "/bgm",
               "Joy Launcher BGM 폴더 / BGM folder\r\n"
               "\r\n"
               "[한국어]\r\n"
               "이 폴더에 mp3 또는 ogg 파일을 넣으면 런처 화면에서 배경 음악으로 재생합니다.\r\n"
               "Y 옵션에서 켜기/끄기, 볼륨, 순서(차례대로/무작위)를 바꿀 수 있습니다. (기본: 꺼짐)\r\n"
               "파일 이름은 영문(ASCII)을 권장합니다. 런처에는 음악이 들어 있지 않습니다.\r\n"
               "\r\n"
               "[English]\r\n"
               "Put mp3 or ogg files in this folder to play them as background music in the launcher.\r\n"
               "Turn it on/off and change volume and order (in order/shuffle) in Y Options. (Default: off)\r\n"
               "Use ASCII file names. No music is included with the launcher.\r\n");
}

enum class Mode { Categories, Games };

struct App {
    SDL_Renderer *r = nullptr;
    TTF_Font *fTitle = nullptr, *fBig = nullptr, *fList = nullptr, *fInfo = nullptr, *fDesc = nullptr;
    TextCache text;
    ImageCache images;
    ImageCache backs;            // 테마 배경 (작은 캐시)
    ThumbCache thumbs;           // 표시 크기로 줄인 그림 (테마 아이콘·격자 칸)
    std::string lastBack;        // 새 배경을 읽기 전까지 보여줄 직전 배경
    bool themePick = false;      // 테마 선택 창
    bool optPick = false;        // 옵션 창 (Y): 홈브루 메뉴·RetroArch·PPSSPP·새로 고침·테마 등
    int optSel = 0;
    bool bgmPick = false;        // BGM 창 (옵션 창 위에 한 겹 더)
    int bgmSel = 0;
    bool hbPick = false;         // 홈브루 즐겨찾기 창 (옵션 창 위에 한 겹 더): SD 의 nro 를 골라 넣고 빼기
    int hbSel = 0;
    std::vector<Game> hbList;    // 고를 수 있는 nro (/switch 아래 두 단계)
    bool exitAsk = false;        // 맨 위 화면에서 B: 종료 확인
    std::vector<std::string> themeList;  // [0] = 기본, 그다음 테마 폴더 이름, 끝에 내려받기 항목
    std::vector<std::string> themeTitles;  // themeList 와 같은 순서의 화면 이름
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

// 긴 글자 가로 스크롤: 처음 그린 때부터 0.8초 멈춤 → 천천히 왼쪽으로 → 끝에서 1초 멈춤 → 처음부터 반복.
// 글자·위치별로 시작 시각을 기억하고, 한 프레임이라도 안 그려지면(선택이 바뀌면) 지워서 처음부터 다시
struct Marquee {
    Uint32 start;
    Uint32 frame;
};
static std::map<std::string, Marquee> gMarquee;
static Uint32 gFrameNo = 0;
static Uint32 gMarqueeShift = 0;  // 데스크톱 스냅샷: 스크롤 중간 모습 (JOY_MARQUEE_MS)

static void MarqueeNextFrame() {
    gFrameNo++;
    for (auto it = gMarquee.begin(); it != gMarquee.end();)
        it = it->second.frame + 1 < gFrameNo ? gMarquee.erase(it) : std::next(it);
}

static int MarqueeOffset(const std::string &key, int over, int pxPerSec, Uint32 kWait = 800, Uint32 kEnd = 1000) {
    auto it = gMarquee.find(key);
    if (it == gMarquee.end()) it = gMarquee.insert({key, {SDL_GetTicks() - gMarqueeShift, gFrameNo}}).first;
    it->second.frame = gFrameNo;
    Uint32 run = (Uint32)over * 1000 / pxPerSec;
    Uint32 t = (SDL_GetTicks() - it->second.start) % (kWait + run + kEnd);
    if (t < kWait) return 0;
    if (t < kWait + run) return (int)((t - kWait) * pxPerSec / 1000);
    return over;
}

// maxW 를 넘는 한 줄 글자: scroll 이면 가로로 흐르고(선택된 줄·알림), 아니면 끝을 «…» 로
static void DrawText(App &a, TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxW = 0,
                     int align = 0, int wrap = 0, int clipH = 0, int scroll = 0) {
    SDL_Texture *t = a.text.Get(f, s, c, wrap);
    if (!t) return;
    int tw, th;
    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    SDL_Rect src = {0, 0, tw, th};
    SDL_Texture *ell = nullptr;
    int ellW = 0, boxW = tw;
    if (maxW > 0 && tw > maxW) {
        boxW = src.w = maxW;
        if (wrap == 0 && scroll > 0) {
            src.x = MarqueeOffset(s + "@" + std::to_string(x) + "," + std::to_string(y), tw - maxW, scroll);
        } else if (wrap == 0 && (ell = a.text.Get(f, "…", c))) {
            SDL_QueryTexture(ell, nullptr, nullptr, &ellW, nullptr);
            src.w = std::max(0, maxW - ellW);
        }
    }
    if (clipH > 0 && th > clipH) {
        int ls = TTF_FontLineSkip(f);
        src.h = (wrap > 0 && ls > 0) ? clipH / ls * ls : clipH;  // 여러 줄이면 줄 단위로 자름
        // 긴 설명: 3초 읽을 시간 → 천천히 위로 → 끝에서 3초 → 처음부터
        if (wrap > 0 && scroll > 0)
            src.y = MarqueeOffset(s + "@" + std::to_string(x) + "," + std::to_string(y), th - src.h, scroll, 3000, 3000);
    }
    int dx = x;
    if (align == 1) dx = x - boxW / 2;
    if (align == 2) dx = x - boxW;
    SDL_Rect dst = {dx, y, src.w, src.h};
    SDL_RenderCopy(a.r, t, &src, &dst);
    if (ell) {
        int eh;
        SDL_QueryTexture(ell, nullptr, nullptr, nullptr, &eh);
        SDL_Rect ed = {dx + src.w, y, ellW, eh};
        SDL_RenderCopy(a.r, ell, nullptr, &ed);
    }
}

// 스크롤 속도 (px/초): 목록·제목은 천천히, 알림 상자는 조금 빠르게 (긴 오류도 끝까지 읽을 수 있게)
static const int kScroll = 40, kScrollMsg = 80, kScrollDesc = 14;

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

static const int kRowH = 42, kRows = 13;

// 화면 가득 채우기 (비율 유지, 넘치는 부분은 잘라냄)
static void DrawCover(App &a, SDL_Texture *t) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    float s = std::max(W / (float)bw, H / (float)bh);
    int sw = (int)(W / s), sh = (int)(H / s);
    SDL_Rect src = {(bw - sw) / 2, (bh - sh) / 2, sw, sh};
    SDL_RenderCopy(a.r, t, &src, nullptr);
}

// ---------------------------------------------------------------- 배터리 (HOME 화면과 같은 오른쪽 위)
struct Battery {
    int percent = -1;  // -1: 모름 (표시 안 함)
    bool charging = false;
    Uint32 nextRead = 0;
};
static Battery gBattery;

static void ReadBattery() {
    if (SDL_GetTicks() < gBattery.nextRead) return;
    gBattery.nextRead = SDL_GetTicks() + 5000;
#ifdef __SWITCH__
    u32 pct = 0;
    PsmChargerType ct = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) gBattery.percent = (int)pct;
    if (R_SUCCEEDED(psmGetChargerType(&ct))) gBattery.charging = ct != PsmChargerType_Unconnected;
#else
    // 데스크톱: JOY_BATTERY=76 또는 76,c (충전 중)
    if (const char *b = getenv("JOY_BATTERY")) {
        gBattery.percent = atoi(b);
        gBattery.charging = strchr(b, 'c') != nullptr;
    }
#endif
}

// 오른쪽 끝을 right 에 맞춰 «76% [■■■ ]» 를 그리고, 차지한 왼쪽 x 를 돌려준다
static int DrawBattery(App &a, int right, int y, SDL_Color base = kWhite) {
    ReadBattery();
    if (gBattery.percent < 0) return right;
    const int bw = 40, bh = 20, nub = 4;
    int bx = right - nub - bw, by = y;
    bool low = gBattery.percent <= 15 && !gBattery.charging;
    SDL_Color fill = low ? SDL_Color{235, 60, 60, 255} : gBattery.charging ? SDL_Color{80, 220, 120, 255} : base;
    // 테두리 (모서리를 깎아 둥글게)
    SDL_SetRenderDrawColor(a.r, base.r, base.g, base.b, 255);
    SDL_Rect edges[] = {{bx + 2, by, bw - 4, 2}, {bx + 2, by + bh - 2, bw - 4, 2}, {bx, by + 2, 2, bh - 4},
                        {bx + bw - 2, by + 2, 2, bh - 4}, {bx + 1, by + 1, 1, 1}, {bx + bw - 2, by + 1, 1, 1},
                        {bx + 1, by + bh - 2, 1, 1}, {bx + bw - 2, by + bh - 2, 1, 1},
                        {bx + bw, by + 6, nub - 1, bh - 12}};
    SDL_RenderFillRects(a.r, edges, (int)(sizeof edges / sizeof edges[0]));
    int inner = bw - 8;
    int fw = std::max(gBattery.percent > 0 ? 2 : 0, inner * std::min(100, gBattery.percent) / 100);
    Fill(a, bx + 4, by + 4, fw, bh - 8, fill);
    if (gBattery.charging) {
        // 번개 (가운데)
        float cx = bx + bw / 2.0f, cy = by + bh / 2.0f;
        SDL_Color k = {20, 20, 26, 255};
        SDL_Vertex v[6] = {
            {{cx + 2, cy - 8}, k, {0, 0}}, {{cx - 5, cy + 1}, k, {0, 0}}, {{cx + 1, cy + 1}, k, {0, 0}},
            {{cx - 2, cy + 8}, k, {0, 0}}, {{cx + 5, cy - 1}, k, {0, 0}}, {{cx - 1, cy - 1}, k, {0, 0}},
        };
        SDL_RenderGeometry(a.r, nullptr, v, 6, nullptr, 0);
    }
    char txt[16];
    snprintf(txt, sizeof txt, "%d%%", gBattery.percent);
    SDL_Texture *t = a.text.Get(a.fInfo, txt, low ? fill : base);
    int tw = 0, th = 0;
    if (t) SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    int tx = bx - 8 - tw;
    if (t) {
        SDL_Rect d = {tx, by + bh / 2 - th / 2, tw, th};
        SDL_RenderCopy(a.r, t, nullptr, &d);
    }
    return tx;
}

// 지금 화면에 테마 배경이 깔렸는지 (배경이 있을 때만 반투명 스타일)
static bool gBackDrawn = false;
static int gBatteryLeft = W;  // 배터리 표시가 시작되는 x (그 왼쪽에 다른 표시)
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
    else DrawText(a, a.fTitle, title, W / 2, 18, kAccent, 640, 1, 0, 0, kScroll);
    if (gAppletMode) DrawText(a, a.fInfo, T("애플릿 모드"), 100, 26, {235, 60, 60, 255});
    gBatteryLeft = DrawBattery(a, W - 24, 28);
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
        if (!c.virt)
            for (const auto &g : c.games)
                if (g.screenshot.empty() && !g.video.empty() && !FrameCached(g)) gFramesPending++;
}

static SDL_Color SelColor() { return Themed() ? SDL_Color{70, 70, 110, 210} : kSel; }

// ---------------------------------------------------------------- 화면 구성 그리기 (layout.txt)
// 화면은 요소(그림·글자·목록 …)의 모음이다. 기본 구성은 layout.cpp 에 있고, 테마의 layout.txt 가 덮어쓴다.

// 색 이름 → 실제 색. bar·selection 은 배경 그림이 깔렸을 때 반투명 (테마가 값을 정하면 그 값)
static SDL_Color LC(const LColor &c) {
    if (c.name.empty()) return {c.r, c.g, c.b, c.a};
    auto it = gTheme.layout.colors.find(c.name);
    if (it != gTheme.layout.colors.end()) return {it->second.r, it->second.g, it->second.b, it->second.a};
    if (c.name == "dim") return kGrey;
    if (c.name == "accent") return kAccent;
    if (c.name == "selection") return SelColor();
    if (c.name == "bar") return {kDark.r, kDark.g, kDark.b, (Uint8)(gBackDrawn ? 200 : 255)};
    return kWhite;  // text, selected, 모르는 이름
}

// 테마 폴더 기준 파일 경로 (폴더 밖으로 나가는 경로는 무시)
static std::string ThemeFile(const std::string &rel) {
    if (rel.empty() || gTheme.base.empty() || rel[0] == '/' || rel[0] == '\\' || rel.find("..") != std::string::npos ||
        rel.find(':') != std::string::npos)
        return "";
    return gTheme.base + "/" + rel;
}

// backgrounds·logos 밖의 그림 폴더 (icons 등): 처음 쓸 때 읽어 둔다
static const std::map<std::string, std::string> &ThemeFolder(const std::string &folder) {
    std::string key = LowerAscii(folder);
    auto it = gTheme.folders.find(key);
    if (it != gTheme.folders.end()) return it->second;
    auto &m = gTheme.folders[key];
    std::string dir = ThemeFile(folder);
    if (!dir.empty()) IndexImages(dir, m);
    return m;
}

// 글꼴: 내장 나눔고딕 + 테마 글꼴(layout.txt 의 font / font_bold). 크기별로 열어 둔다.
struct FontSet {
    std::vector<char> reg, bold;       // 테마 글꼴 파일 내용 (없으면 빔)
    std::map<int, TTF_Font *> fonts;   // 크기*4 + 굵게*2 + 테마
    std::map<std::string, bool> covers[2];  // 테마 글꼴에 그 글의 글자가 다 있는지 (일반 / 굵게)
    Uint32 serial = 0xffffffffu;
};
static FontSet gFonts;

// 테마가 바뀌었으면 글꼴·글자·작은 그림 캐시를 비우고 테마 글꼴을 읽는다
static void FontsSync(App &a) {
    if (gFonts.serial == gThemeSerial) return;
    gFonts.serial = gThemeSerial;
    a.text.Clear();
    a.thumbs.Clear();
    for (auto &kv : gFonts.fonts)
        if (kv.second) TTF_CloseFont(kv.second);
    gFonts.fonts.clear();
    gFonts.covers[0].clear();
    gFonts.covers[1].clear();
    gFonts.reg.clear();
    gFonts.bold.clear();
    std::string fr = ThemeFile(gTheme.layout.font), fb = ThemeFile(gTheme.layout.fontBold);
    if (!fr.empty() && !ReadAll(fr.c_str(), gFonts.reg)) gFonts.reg.clear();
    if (!fb.empty() && !ReadAll(fb.c_str(), gFonts.bold)) gFonts.bold.clear();
    if (gFonts.bold.empty()) gFonts.bold = gFonts.reg;  // 굵은 글꼴을 안 정했으면 일반 글꼴로
}

static TTF_Font *FontRaw(bool bold, int size, bool theme) {
    int key = size * 4 + (bold ? 2 : 0) + (theme ? 1 : 0);
    auto it = gFonts.fonts.find(key);
    if (it != gFonts.fonts.end()) return it->second;
    std::vector<char> &buf = theme ? (bold ? gFonts.bold : gFonts.reg) : (bold ? gFontBold : gFontReg);
    TTF_Font *f = buf.empty() ? nullptr : OpenFontMem(buf, size);
    gFonts.fonts[key] = f;
    return f;
}

// s 를 그릴 글꼴. 테마 글꼴에 없는 글자(한글 등)가 섞인 글은 내장 글꼴로 그린다.
static TTF_Font *FontFor(bool bold, int size, const std::string &s) {
    if (!(bold ? gFonts.bold : gFonts.reg).empty()) {
        if (TTF_Font *f = FontRaw(bold, size, true)) {
            auto &memo = gFonts.covers[bold ? 1 : 0];
            auto it = memo.find(s);
            bool ok;
            if (it != memo.end()) {
                ok = it->second;
            } else {
                ok = true;
                const unsigned char *p = (const unsigned char *)s.c_str();
                while (*p && ok) {
                    Uint32 cp = *p;
                    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
                    if (extra) cp &= 0x3F >> extra;
                    p++;
                    for (int i = 0; i < extra && *p; i++) cp = (cp << 6) | (*p++ & 0x3F);
                    if (cp > 0x20 && !TTF_GlyphIsProvided32(f, cp)) ok = false;
                }
                if (memo.size() > 4000) memo.clear();
                memo[s] = ok;
            }
            if (ok) return f;
        }
    }
    return FontRaw(bold, size, false);
}

// 이번 프레임에 디스크에서 새로 읽을 수 있는 작은 그림 수 (격자를 처음 그릴 때 끊기지 않게 나눠 읽음)
static int gLoadBudget = 1;

static SDL_Texture *Thumb(App &a, const std::string &path, int w, int h, bool cover, bool now) {
    if (path.empty()) return nullptr;
    bool known = false;
    SDL_Texture *t = a.thumbs.Find(path, w, h, known);
    if (known) return t;
    if (!now && gLoadBudget <= 0) return nullptr;
    if (!now) gLoadBudget--;
    return a.thumbs.Load(path, w, h, cover);
}

// 칸에 맞춰 그리기. fit: 0 비율 유지(안에 맞춤) / 1 비율 유지(가득, 넘치면 자름) / 2 늘림
static void DrawFit(App &a, SDL_Texture *t, int x, int y, int w, int h, int fit, int align = 1, int valign = 1) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    if (w <= 0 || h <= 0) {  // 크기를 안 정했으면 그림 크기 그대로
        SDL_Rect d = {x, y, bw, bh};
        SDL_RenderCopy(a.r, t, nullptr, &d);
        return;
    }
    if (fit == 2) {
        SDL_Rect d = {x, y, w, h};
        SDL_RenderCopy(a.r, t, nullptr, &d);
    } else if (fit == 1) {
        float s = std::max(w / (float)bw, h / (float)bh);
        int sw = (int)(w / s), sh = (int)(h / s);
        SDL_Rect src = {(bw - sw) / 2, (bh - sh) / 2, sw, sh};
        SDL_Rect d = {x, y, w, h};
        SDL_RenderCopy(a.r, t, &src, &d);
    } else {
        float s = std::min(w / (float)bw, h / (float)bh);
        int dw = (int)(bw * s), dh = (int)(bh * s);
        SDL_Rect d = {x + (align == 0 ? 0 : align == 2 ? w - dw : (int)(w - bw * s) / 2),
                      y + (valign == 0 ? 0 : valign == 2 ? h - dh : (int)(h - bh * s) / 2), dw, dh};
        SDL_RenderCopy(a.r, t, nullptr, &d);
    }
}

// 화면에 보일 카테고리 이름: layout.txt 의 [names] (카테고리 이름 → shortname → 롬 폴더 순), 없으면 원래 이름
static std::string CatName(const Collection &c) {
    const auto &names = gTheme.layout.names;
    if (!names.empty()) {
        auto it = names.find(LowerAscii(c.name));
        if (it == names.end() && !c.shortName.empty()) it = names.find(LowerAscii(c.shortName));
        if (it == names.end() && c.folders.size() == 1) it = names.find(LowerAscii(c.folders[0]));
        if (it != names.end()) return it->second;
    }
    return T(c.name);
}

struct DrawCtx {
    bool games = false;
    const Collection *col = nullptr;  // 커서가 있는(또는 열린) 카테고리
    const Game *game = nullptr;       // 선택된 게임 (게임 화면)
    int index = 0, count = 0;         // 목록의 선택 위치(1부터) / 전체
    std::string hint;
};

static std::string GameLongTitle(const Game &g) {
    // 롬 모음에 따라 developer 칸에 긴 제목을 적어 두기도 한다 (목록 제목으로 시작하면 긴 제목으로 보고 그것을 표시)
    bool devIsTitle = !g.developer.empty() && g.developer.rfind(g.title, 0) == 0;
    return devIsTitle ? g.developer : g.title;
}

static std::string GameMeta(const Game &g) {
    bool devIsTitle = !g.developer.empty() && g.developer.rfind(g.title, 0) == 0;
    std::string meta;
    if (!g.developer.empty() && !devIsTitle) meta += g.developer;
    if (!g.release.empty()) meta += (meta.empty() ? "" : " · ") + g.release.substr(0, 4);
    if (!g.players.empty()) meta += (meta.empty() ? "" : " · ") + Fmt(T("%s인"), g.players.c_str());
    return meta;
}

static std::string GameDescription(App &a, const Game &g) {
    if (g.description.empty() || SDL_GetTicks() - a.selChanged <= 120) return "";
    // 너무 긴 설명은 잘라 둔다 (UTF-8 글자 중간에서 자르지 않게)
    size_t cut = std::min<size_t>(g.description.size(), 3000);
    while (cut < g.description.size() && cut > 0 && ((unsigned char)g.description[cut] & 0xC0) == 0x80) cut--;
    return g.description.substr(0, cut);
}

// 글자 틀의 {자리표} 채우기. 모르는 자리표는 그대로 둔다
static std::string Expand(App &a, const std::string &tpl, const DrawCtx &c) {
    if (tpl.find('{') == std::string::npos) return tpl;
    std::string out;
    for (size_t i = 0; i < tpl.size();) {
        size_t close = tpl[i] == '{' ? tpl.find('}', i) : std::string::npos;
        if (close == std::string::npos) { out += tpl[i++]; continue; }
        std::string k = tpl.substr(i + 1, close - i - 1), v;
        bool known = true;
        const Game *g = c.game;
        if (k == "app") v = "Joy Launcher";
        else if (k == "version") v = CurrentVersion();
        else if (k == "applet") v = gAppletMode ? T("애플릿 모드") : "";
        else if (k == "hint") v = c.hint;
        else if (k == "clock") {
            time_t now = time(nullptr);
            char buf[16];
            strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
            v = buf;
        }
        else if (k == "category") v = c.col ? CatName(*c.col) : "";
        else if (k == "games") v = c.col ? std::to_string(c.col->games.size()) : "";
        else if (k == "index") v = std::to_string(c.index);
        else if (k == "count") v = std::to_string(c.count);
        else if (k == "category_index") v = std::to_string((c.games ? a.col : a.catSel) + 1);
        else if (k == "category_count") v = std::to_string(a.cols.size());
        else if (k == "title") v = g ? g->title : "";
        else if (k == "longtitle") v = g ? GameLongTitle(*g) : "";
        else if (k == "developer") v = g ? g->developer : "";
        else if (k == "release") v = g ? g->release : "";
        else if (k == "year") v = g ? g->release.substr(0, std::min<size_t>(4, g->release.size())) : "";
        else if (k == "players") v = g ? g->players : "";
        else if (k == "genre") v = g ? g->genre : "";
        else if (k == "meta") v = g ? GameMeta(*g) : "";
        else if (k == "description") v = g ? GameDescription(a, *g) : "";
        else known = false;
        if (known) out += v;
        else out += tpl.substr(i, close - i + 1);
        i = close + 1;
    }
    // 자리표가 모두 비어 글자가 공백뿐이면 그리지 않는다
    if (out.find_first_not_of(" \t") == std::string::npos) return "";
    return out;
}

static void DrawElemImage(App &a, const Elem &e, const DrawCtx &c) {
    if (e.source == "background") {
        // 카테고리(플랫폼)별 배경. 그 카테고리 배경이 없으면 fallback 그림, 그것도 없으면 배경 없이
        std::string bg = ThemeFind(gTheme.bg, c.col);
        if (bg.empty()) bg = ThemeFile(e.fallback);
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
            DrawFit(a, t, e.x, e.y, e.w, e.h, e.fit);
            int dim = e.dim >= 0 ? e.dim : gTheme.dim;
            if (dim > 0) Fill(a, e.x, e.y, e.w, e.h, {0, 0, 0, (Uint8)dim});
        }
        gBackDrawn = t != nullptr;
        return;
    }
    std::string path;
    bool gameImg = false;
    if (e.source == "logo") path = c.col ? ThemeFind(gTheme.logo, c.col) : "";
    else if (e.source == "icon") path = c.col ? ThemeFind(ThemeFolder(e.folder.empty() ? "icons" : e.folder), c.col) : "";
    else if (e.source == "boxart") { path = c.game ? c.game->boxart : ""; gameImg = true; }
    else if (e.source == "screenshot") { path = c.game ? GameShot(*c.game, SDL_GetTicks() - a.selChanged > 250) : ""; gameImg = true; }
    else path = ThemeFile(e.file);
    if (path.empty()) { path = ThemeFile(e.fallback); gameImg = false; }
    if (path.empty()) return;
    SDL_Texture *t;
    if (gameImg) {
        // 이동이 잠시 멈춘 뒤에만 디스크에서 읽고, 캐시에 있으면 바로 표시
        t = a.images.Find(path);
        if (!t && SDL_GetTicks() - a.selChanged > 120) t = a.images.Load(path);
    } else {
        t = Thumb(a, path, e.w, e.h, e.fit != 0, true);
    }
    if (!t) return;
    if (e.alpha < 255) SDL_SetTextureAlphaMod(t, (Uint8)e.alpha);
    if (e.hasTint) {
        SDL_Color tc = LC(e.tint);
        SDL_SetTextureColorMod(t, tc.r, tc.g, tc.b);
    }
    DrawFit(a, t, e.x, e.y, e.w, e.h, e.fit, e.align, e.valign);
    if (e.alpha < 255) SDL_SetTextureAlphaMod(t, 255);
    if (e.hasTint) SDL_SetTextureColorMod(t, 255, 255, 255);
    if (e.dim > 0) Fill(a, e.x, e.y, e.w, e.h, {0, 0, 0, (Uint8)e.dim});
}

static void DrawElemText(App &a, const Elem &e, const DrawCtx &c) {
    if (e.logo && c.col) {
        std::string logo = ThemeFind(gTheme.logo, c.col);
        SDL_Texture *lt = logo.empty() ? nullptr : a.images.Load(logo);
        if (lt) { DrawImage(a, lt, e.lx, e.ly, e.lw, e.lh); return; }
    }
    std::string s = Expand(a, e.text, c);
    if (s.empty()) return;
    TTF_Font *f = FontFor(e.bold, e.size, s);
    if (!f) return;
    SDL_Color col = LC(e.color);
    if (e.wrap && e.w > 0) {
        DrawText(a, f, s, e.x, e.y, col, e.w, 0, e.w, e.h, e.scroll ? kScrollDesc : 0);
        return;
    }
    int ax = e.x + (e.align == 1 ? e.w / 2 : e.align == 2 ? e.w : 0), y = e.y;
    if (e.h > 0 && e.valign) {
        int th = TTF_FontHeight(f);
        y += e.valign == 1 ? (e.h - th) / 2 : e.h - th;
    }
    DrawText(a, f, s, ax, y, col, e.w, e.align, 0, 0, e.scroll ? kScroll : 0);
}

// 목록의 열·줄 수
static void ListGeom(const Elem &e, int &cols, int &rows) {
    if (e.style == 1) {
        cols = std::max(1, e.columns);
        int cw = std::max(1, (e.w - e.gapX * (cols - 1)) / cols);
        rows = e.rows > 0 ? e.rows : std::max(1, (e.h + e.gapY) / (cw + e.gapY));  // 줄 수를 안 정했으면 정사각형 칸으로
    } else {
        cols = 1;
        rows = e.rows > 0 ? e.rows : std::max(1, (int)(e.h / std::max(1.0f, e.rowH) + 0.01f));
    }
}

// 카테고리·게임 화면의 목록 모양 (목록 요소가 없으면 기본 13줄)
static void ScreenGeom(bool games, int &cols, int &rows) {
    const Elem *l = (games ? gTheme.layout.games : gTheme.layout.categories).List();
    if (!l) { cols = 1; rows = kRows; return; }
    ListGeom(*l, cols, rows);
}

static void DrawElemList(App &a, const Elem &e, const DrawCtx &c) {
    int cols, rows;
    ListGeom(e, cols, rows);
    int n = c.games ? (c.col ? (int)c.col->games.size() : 0) : (int)a.cols.size();
    int top = c.games ? a.top : a.catTop, sel = c.games ? a.sel : a.catSel;
    SDL_Color colN = LC(e.color), colS = LC(e.selColor);
    auto starred = [&](int idx) {
        return c.games && ((!c.col->virt && gFavSet.count(c.col->games[idx].file)) || c.col->shortName == "hbfav");
    };
    if (e.style == 0) {
        bool showCount = e.count && !c.games;
        bool themeFont = !(e.bold ? gFonts.bold : gFonts.reg).empty();
        for (int i = 0; i < rows && top + i < n; i++) {
            int idx = top + i, ry = e.y + (int)(i * e.rowH + 0.5f), rh = e.y + (int)((i + 1) * e.rowH + 0.5f) - ry;
            bool s = idx == sel;
            if (s) {
                SDL_Texture *st = e.selector.empty() ? nullptr : Thumb(a, ThemeFile(e.selector), e.w, (int)e.rowH, true, true);
                int g = e.selectorGrow;
                if (st) DrawFit(a, st, e.x - g, ry - g, e.w + g * 2, rh + g * 2, 2);
                else Fill(a, e.x, ry, e.w, rh, LC(e.selection));
            }
            std::string label = c.games ? c.col->games[idx].title : CatName(a.cols[idx]);
            int maxW = e.w - e.textX - (showCount ? 148 : 18), dx = 0;
            if (starred(idx)) {
                if (!themeFont) {
                    label = "★ " + label;
                } else {  // 테마 글꼴에 ★ 이 없어도 제목은 테마 글꼴로: 별만 따로 그린다
                    TTF_Font *sf = FontFor(e.bold, e.size, "★ ");
                    int sw = 0;
                    if (sf && e.align == 0) {
                        TTF_SizeUTF8(sf, "★ ", &sw, nullptr);
                        int sy = e.textY >= 0 ? e.textY : (rh - TTF_FontHeight(sf)) / 2;
                        DrawText(a, sf, "★ ", e.x + e.textX, ry + sy, s ? colS : colN);
                        dx = sw;
                    } else {
                        label = "★ " + label;
                    }
                }
            }
            TTF_Font *f = FontFor(e.bold, e.size, label);
            if (!f) continue;
            int ty = e.textY >= 0 ? e.textY : (rh - TTF_FontHeight(f)) / 2;
            int tx = e.align == 1 ? e.x + e.w / 2 : e.align == 2 ? e.x + e.w - e.textX : e.x + e.textX + dx;
            DrawText(a, f, label, tx, ry + ty, s ? colS : colN, maxW - dx, e.align, 0, 0, s ? kScroll : 0);
            if (showCount) {
                char num[32];
                snprintf(num, sizeof(num), "%d", (int)a.cols[idx].games.size());
                if (TTF_Font *fc = FontFor(false, e.countSize, num)) {
                    int cy = e.countY >= 0 ? e.countY : (rh - TTF_FontHeight(fc)) / 2;
                    DrawText(a, fc, num, e.x + e.w - e.countX, ry + cy, LC(e.countColor), 0, 2);
                }
            }
        }
        return;
    }
    // 격자
    int cw = std::max(1, (e.w - e.gapX * (cols - 1)) / cols), ch = std::max(1, (e.h - e.gapY * (rows - 1)) / rows);
    std::string kind = e.image.empty() ? (c.games ? "boxart" : "icon") : e.image;
    int iw = std::max(1, cw - e.pad[0] - e.pad[2]), ih = std::max(1, ch - e.pad[1] - e.pad[3] - (e.label == 1 ? e.labelH : 0));
    auto cellPath = [&](int idx) -> std::string {
        if (c.games) return kind == "boxart" ? c.col->games[idx].boxart : "";
        const Collection &cc = a.cols[idx];
        if (kind == "icon") return ThemeFind(ThemeFolder(e.folder.empty() ? "icons" : e.folder), &cc);
        if (kind == "logo") return ThemeFind(gTheme.logo, &cc);
        if (kind == "boxart")
            for (const auto &g : cc.games)
                if (!g.boxart.empty()) return g.boxart;
        return "";
    };
    auto cell = [&](int idx, bool s) {
        int k = idx - top;
        SDL_Rect rc = {e.x + (k % cols) * (cw + e.gapX), e.y + (k / cols) * (ch + e.gapY), cw, ch};
        float sc = s ? e.selScale : 1.0f;
        if (sc != 1.0f) {
            int nw = (int)(cw * sc), nh = (int)(ch * sc);
            rc = {rc.x - (nw - cw) / 2, rc.y - (nh - ch) / 2, nw, nh};
        }
        auto px = [&](int v) { return (int)(v * sc); };
        if (s && e.selector.empty()) Fill(a, rc.x - 4, rc.y - 4, rc.w + 8, rc.h + 8, LC(e.selection));
        if (e.hasCardColor) Fill(a, rc.x, rc.y, rc.w, rc.h, LC(e.cardColor));
        if (!e.card.empty())
            if (SDL_Texture *ct = Thumb(a, ThemeFile(e.card), cw + e.cardGrow * 2, ch + e.cardGrow * 2, true, true)) {
                int g = px(e.cardGrow);
                DrawFit(a, ct, rc.x - g, rc.y - g, rc.w + g * 2, rc.h + g * 2, 2);
            }
        std::string path = cellPath(idx), name;
        if (c.games) name = starred(idx) ? "★ " + c.col->games[idx].title : c.col->games[idx].title;
        else name = CatName(a.cols[idx]);
        SDL_Rect in = {rc.x + px(e.pad[0]), rc.y + px(e.pad[1]), rc.w - px(e.pad[0] + e.pad[2]), rc.h - px(e.pad[1] + e.pad[3])};
        int lh = e.label == 1 ? px(e.labelH) : 0;
        SDL_Texture *t = path.empty() ? nullptr : Thumb(a, path, iw, ih, false, false);
        if (t && in.w > 0 && in.h - lh > 0) DrawFit(a, t, in.x, in.y, in.w, in.h - lh, 0);
        if (e.label == 1 || (e.label == 0 && path.empty())) {
            if (TTF_Font *f = FontFor(e.bold, e.size, name)) {
                int th = TTF_FontHeight(f);
                int ty = e.label == 1 ? in.y + in.h - lh + (lh - th) / 2 : in.y + (in.h - th) / 2;
                DrawText(a, f, name, in.x + in.w / 2, ty, s ? colS : colN, in.w, 1, 0, 0, s ? kScroll : 0);
            }
        }
        if (s && !e.selector.empty())
            if (SDL_Texture *st = Thumb(a, ThemeFile(e.selector), cw + e.selectorGrow * 2, ch + e.selectorGrow * 2, true, true)) {
                int g = px(e.selectorGrow);
                DrawFit(a, st, rc.x - g, rc.y - g, rc.w + g * 2, rc.h + g * 2, 2);
            }
    };
    int end = std::min(n, top + cols * rows);
    for (int idx = top; idx < end; idx++)
        if (idx != sel) cell(idx, false);
    if (sel >= top && sel < end) cell(sel, true);  // 선택된 칸은 맨 위에 (확대했을 때 옆 칸을 덮음)
    // 보이는 칸을 다 읽었으면 다음·이전 쪽 그림을 미리 읽어 둔다 (넘길 때 바로 보이게)
    for (int idx = end; idx < std::min(n, end + cols * rows) && gLoadBudget > 0; idx++) Thumb(a, cellPath(idx), iw, ih, false, false);
    for (int idx = top - 1; idx >= std::max(0, top - cols * rows) && gLoadBudget > 0; idx--) Thumb(a, cellPath(idx), iw, ih, false, false);
}

// 카테고리 화면: 커서가 있는 카테고리의 게임 박스 사진 미리보기
static void DrawElemPreview(App &a, const Elem &e, const DrawCtx &c) {
    if (!c.col) return;
    int cols = std::max(1, e.columns), rows = std::max(1, e.rows);
    int cw = (e.w - e.gapX * (cols - 1)) / cols, ch = (e.h - e.gapY * (rows - 1)) / rows, k = 0;
    for (size_t g = 0; g < c.col->games.size() && k < cols * rows; g++) {
        const std::string &box = c.col->games[g].boxart;
        if (box.empty()) continue;
        SDL_Texture *t = (SDL_GetTicks() - a.selChanged > 120) ? a.images.Load(box) : a.images.Find(box);
        if (!t) continue;
        DrawImage(a, t, e.x + (k % cols) * (cw + e.gapX), e.y + (k / cols) * (ch + e.gapY), cw, ch);
        k++;
    }
}

// 게임 화면: 화면 사진(또는 영상 장면)을 크게, 박스 사진은 오른쪽 아래에 작게 겹쳐서. 화면 사진이 없으면 박스 사진만
static void DrawElemMedia(App &a, const Elem &e, const DrawCtx &c) {
    if (!c.game) return;
    const Game &g = *c.game;
    bool settled = SDL_GetTicks() - a.selChanged > 120;
    auto img = [&](const std::string &p) -> SDL_Texture * {
        if (p.empty()) return nullptr;
        SDL_Texture *t = a.images.Find(p);
        if (!t && settled) t = a.images.Load(p);
        return t;
    };
    std::string shotPath = GameShot(g, SDL_GetTicks() - a.selChanged > 250);
    if (!shotPath.empty()) {
        SDL_Texture *shot = img(shotPath);
        SDL_Rect area = {e.x, e.y, e.w, e.h};
        if (shot) area = DrawImage(a, shot, e.x, e.y, e.w, e.h);
        else if (!Themed()) Fill(a, e.x, e.y, e.w, e.h, {44, 44, 56, 255});
        SDL_Texture *box = e.overlay && g.boxart != shotPath ? img(g.boxart) : nullptr;
        if (box) {
            int bw, bh;
            SDL_QueryTexture(box, nullptr, nullptr, &bw, &bh);
            float sc = std::min(e.w * 130.0f / 440 / bw, e.h * 150.0f / 300 / bh);
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            SDL_Rect d = {area.x + area.w - dw - 8, area.y + area.h - dh - 8, dw, dh};
            Fill(a, d.x - 3, d.y - 3, dw + 6, dh + 6, {0, 0, 0, 160});
            SDL_RenderCopy(a.r, box, nullptr, &d);
        }
    } else {
        SDL_Texture *box = img(g.boxart);
        if (box) DrawImage(a, box, e.x, e.y, e.w, e.h);
        else if (!Themed()) Fill(a, e.x, e.y, e.w, e.h, {44, 44, 56, 255});
    }
}

// 게임 화면: 제목·제작사·장르·설명을 위에서부터 차례로
static void DrawElemInfo(App &a, const Elem &e, const DrawCtx &c) {
    if (!c.game) return;
    const Game &g = *c.game;
    SDL_Color body = LC(e.color);
    int y = e.y;
    std::string title = GameLongTitle(g);
    if (TTF_Font *f = FontFor(false, e.titleSize, title)) DrawText(a, f, title, e.x, y, LC(e.titleColor), e.w, 0, 0, 0, kScroll);
    y += e.titleSize + 11;
    std::string meta = GameMeta(g);
    if (!meta.empty()) {
        if (TTF_Font *f = FontFor(false, e.size, meta)) DrawText(a, f, meta, e.x, y, body, e.w);
        y += e.size + 10;
    }
    if (!g.genre.empty()) {
        if (TTF_Font *f = FontFor(false, e.size, g.genre)) DrawText(a, f, g.genre, e.x, y, body, e.w);
        y += e.size + 10;
    }
    std::string desc = GameDescription(a, g);
    if (!desc.empty() && e.y + e.h - (y + 4) > 0)
        if (TTF_Font *f = FontFor(false, e.descSize, desc))
            DrawText(a, f, desc, e.x, y + 4, body, e.w, 0, e.w, e.y + e.h - (y + 4), kScrollDesc);
}

// 게임 화면: «n / 13   R ▶» (자리를 안 정하면 배터리 표시 왼쪽)
static void DrawElemPager(App &a, const Elem &e, const DrawCtx &c) {
    char idx[64];
    snprintf(idx, sizeof(idx), "%d / %d", (c.games ? a.col : a.catSel) + 1, (int)a.cols.size());
    int rx = e.hasRect ? e.x + e.w : gBatteryLeft - 28, y = e.hasRect ? e.y : 26;
    SDL_Color col = LC(e.color);
    if (TTF_Font *f = FontFor(e.bold, e.size, "R ▶")) DrawText(a, f, "R ▶", rx, y, col, 0, 2);
    if (TTF_Font *f = FontFor(e.bold, e.size, idx)) DrawText(a, f, idx, rx - 80 * e.size / 22, y, col, 0, 2);
}

static void RenderScreen(App &a, const Screen &sc, const DrawCtx &c) {
    FontsSync(a);
    const Layout &l = gTheme.layout;
    if (l.hasClear) SDL_SetRenderDrawColor(a.r, l.clear.r, l.clear.g, l.clear.b, 255);
    else SDL_SetRenderDrawColor(a.r, 30, 30, 38, 255);
    SDL_RenderClear(a.r);
    gBackDrawn = false;
    gLoadBudget = gShotMode ? 100000 : 1;
    a.thumbs.tick++;
    for (const Elem &e : sc.elems) {
        if (!e.visible) continue;
        switch (e.type) {
        case ElemType::Rect: Fill(a, e.x, e.y, e.w, e.h, LC(e.color)); break;
        case ElemType::Image: DrawElemImage(a, e, c); break;
        case ElemType::Text: DrawElemText(a, e, c); break;
        case ElemType::List: DrawElemList(a, e, c); break;
        case ElemType::Preview: DrawElemPreview(a, e, c); break;
        case ElemType::Media: DrawElemMedia(a, e, c); break;
        case ElemType::Info: DrawElemInfo(a, e, c); break;
        case ElemType::Battery: gBatteryLeft = DrawBattery(a, e.x + e.w, e.y, LC(e.color)); break;
        case ElemType::Pager: DrawElemPager(a, e, c); break;
        }
    }
}

static void RenderCategories(App &a) {
    DrawCtx c;
    c.col = a.catSel < (int)a.cols.size() ? &a.cols[a.catSel] : nullptr;
    c.index = a.catSel + 1;
    c.count = (int)a.cols.size();
    int cols, rows;
    ScreenGeom(false, cols, rows);
    c.hint = cols > 1 ? T("[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼◀▶ 선택")
                      : T("[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼ 선택");
    RenderScreen(a, gTheme.layout.categories, c);
}

static void RenderGames(App &a) {
    const Collection &col = a.cols[a.col];
    DrawCtx c;
    c.games = true;
    c.col = &col;
    c.game = col.games.empty() ? nullptr : &col.games[a.sel];
    c.index = a.sel + 1;
    c.count = (int)col.games.size();
    int cols, rows;
    ScreenGeom(true, cols, rows);
    c.hint = cols > 1 ? T("[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ▲▼◀▶ 선택  [-] 즐겨찾기  [X] 홈브루  [Y] 옵션")
                      : T("[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ◀ ▶ 페이지  [-] 즐겨찾기  [X] 홈브루  [Y] 옵션");
    RenderScreen(a, gTheme.layout.games, c);
}

// 테마 목록 끝의 "<팩> 내려받기" 항목: 이 글 뒤에 팩 폴더 이름이 붙는다
static const char *kThemeDownload = "\x01download:";
static bool IsThemeDownload(const std::string &item) { return item.rfind(kThemeDownload, 0) == 0; }
static const ThemePack *ThemeDownloadPack(const std::string &item) {
    if (!IsThemeDownload(item)) return nullptr;
    std::string folder = item.substr(strlen(kThemeDownload));
    for (const auto &p : kPacks)
        if (folder == p.folder) return &p;
    return nullptr;
}
static const char *PackName(const ThemePack &p) { return CurrentLang() == Lang::En ? p.en : p.ko; }

static void RenderThemePick(App &a) {
    int n = (int)a.themeList.size();
    int rows = std::min(n, 10);
    int bw = 640, bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, bx, by, bw, bh, {28, 28, 40, 245});
    DrawText(a, a.fBig, T("테마 선택"), W / 2, by + 16, kAccent, 0, 1);
    int top = std::max(0, std::min(a.themeSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 66 + i * kRowH;
        bool sel = idx == a.themeSel;
        if (sel) Fill(a, bx + 16, y - 4, bw - 32, kRowH, kSel);
        const ThemePack *pack = ThemeDownloadPack(a.themeList[idx]);
        bool dl = pack != nullptr;
        std::string label = idx == 0 ? std::string(T("기본 (배경 없음)")) : idx < (int)a.themeTitles.size() ? a.themeTitles[idx] : a.themeList[idx];
        if (dl)
            label = Fmt(IsDirPath(gHome + "/themes/" + pack->folder) ? T("%s 다시 받기 (최신으로)") : T("%s 내려받기"), PackName(*pack));
        bool cur = !dl && ((idx == 0 && gThemeName.empty()) || (idx > 0 && a.themeList[idx] == gThemeName));
        if (cur) label += T("  (사용 중)");
        SDL_Color c = dl ? kAccent : sel ? kWhite : kGrey;
        DrawText(a, a.fList, label, bx + 32, y + 4, c, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    DrawText(a, a.fInfo, T("[A] 적용   [B] 닫기   (테마 폴더: /JoyLauncher/themes)"), W / 2, by + bh - 34, kGrey, 0, 1);
}

// 홈브루 즐겨찾기 창: ★ = 넣음, ☆ = 안 넣음
static void RenderHbPick(App &a) {
    int n = (int)a.hbList.size();
    int rows = std::min(n, (H - 16 - 110) / kRowH);
    int bw = 980, bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = std::max(8, (H - bh) / 2);
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, bx, by, bw, bh, {28, 28, 40, 245});
    DrawText(a, a.fBig, T("홈브루 즐겨찾기"), W / 2, by + 16, kAccent, 0, 1);
    int top = std::max(0, std::min(a.hbSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 66 + i * kRowH;
        bool sel = idx == a.hbSel;
        if (sel) Fill(a, bx + 16, y - 4, bw - 32, kRowH, kSel);
        const Game &g = a.hbList[idx];
        bool in = std::find(gHbFav.begin(), gHbFav.end(), g.file) != gHbFav.end();
        std::string label = std::string(in ? "★ " : "☆ ") + g.title + "   " + g.file;
        DrawText(a, a.fList, label, bx + 32, y + 4, in ? kOn : sel ? kWhite : kGrey, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    DrawText(a, a.fInfo, Fmt(T("[A] 또는 [-] 넣기 / 빼기   [B] 닫기   (%d개)"), (int)gHbFav.size()), W / 2, by + bh - 34,
             kGrey, 0, 1);
}

static bool Adjustable(MenuAct m) {
    return m == MenuAct::Language || m == MenuAct::Bgm || m == MenuAct::BgmVolume || m == MenuAct::BgmOrder;
}

// 항목을 고르는 창 (옵션 창, BGM 창). bw: 창 너비
static void RenderPopup(App &a, const char *title, const std::vector<MenuItem> &items, int selIdx, int bw) {
    int n = (int)items.size();
    int rows = std::min(n, (H - 16 - 110) / kRowH);  // 화면에 다 안 들어가면 커서를 따라 넘김
    int top = std::max(0, std::min(selIdx - rows / 2, n - rows));
    int bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = std::max(8, (H - bh) / 2);
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, bx, by, bw, bh, {28, 28, 40, 245});
    DrawText(a, a.fBig, title, W / 2, by + 16, kAccent, 0, 1);
    for (int i = top; i < top + rows; i++) {
        int y = by + 66 + (i - top) * kRowH;
        bool sel = i == selIdx;
        if (sel) Fill(a, bx + 16, y - 4, bw - 32, kRowH, kSel);
        SDL_Color c = items[i].act == MenuAct::BgmNone ? SDL_Color{110, 110, 125, 255} : items[i].on ? kOn : sel ? kWhite : kGrey;
        DrawText(a, a.fList, items[i].label, bx + 32, y + 4, c, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    bool adjustable = selIdx >= 0 && selIdx < n && Adjustable(items[selIdx].act);
    DrawText(a, a.fInfo, adjustable ? T("[A] 선택   ◀ ▶ 조절   [B] 닫기") : T("[A] 선택   [B] 닫기"), W / 2, by + bh - 34, kGrey,
             0, 1);
}

// 옵션 창 (Y)
static void RenderOptions(App &a) { RenderPopup(a, T("옵션"), gMenu, a.optSel, 820); }

// 종료 확인
static void RenderExitAsk(App &a) {
    Fill(a, 0, 0, W, H, {0, 0, 0, 120});
    Fill(a, 340, 270, 600, 160, {28, 28, 40, 245});
    DrawText(a, a.fBig, T("런처를 종료합니다"), W / 2, 300, kWhite, 560, 1);
    DrawText(a, a.fInfo, T("[A] 종료   [B] 돌아가기"), W / 2, 372, kGrey, 560, 1);
}

static void Render(App &a);

// 메뉴의 "영상 장면 일괄 추출": 메인 스레드에서 차례로 추출하며 진행 상황 표시, B 로 중지
static void RunFrameBatch(App &a) {
    std::vector<const Game *> todo;
    for (const auto &c : a.cols)
        if (!c.virt)
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
        snprintf(buf, sizeof(buf), T("영상 장면 추출 중  %d / %d"), (int)i + 1, (int)todo.size());
        DrawText(a, a.fList, buf, W / 2, 300, kWhite, 760, 1);
        Fill(a, 280, 350, 720, 14, {60, 60, 80, 255});
        Fill(a, 280, 350, (int)(720.0 * i / std::max<size_t>(1, todo.size())), 14, kAccent);
        DrawText(a, a.fInfo, T("[B] 중지  (추출한 장면은 /JoyLauncher/cache 에 저장)"), W / 2, 380, kGrey, 760, 1);
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
    a.message = Fmt(T("영상 장면 %d개 추출"), done) + (failed ? Fmt(T(", 실패 %d"), failed) : "") + (cancel ? T(" (중지)") : "");
    a.messageColor = {40, 100, 60, 240};
    a.messageModal = true;
}

// 게임 목록: 저장해 둔 목록(library.cache)이 유효하면 그대로 쓰고, 아니면(또는 force) 롬 폴더를 다시 읽어 저장
static std::vector<std::string> gRoots;

// 카테고리 맨 앞에 "즐겨찾기" (즐겨찾기가 있을 때만). keepPos: 지금 보고 있는 카테고리 번호를 맞춰 줌
static void RebuildFavorites(App &a, bool keepPos) {
    bool had = !a.cols.empty() && a.cols[0].virt && a.cols[0].shortName == "favorites";
    if (had) a.cols.erase(a.cols.begin());
    Collection fav;
    fav.name = "즐겨찾기";
    fav.sortBy = "000";
    fav.shortName = "favorites";
    fav.virt = true;
    std::set<std::string> seen;
    for (const auto &c : a.cols)
        for (const auto &g : c.games)
            if (gFavSet.count(g.file) && seen.insert(g.file).second) fav.games.push_back(g);
    bool has = !fav.games.empty();
    if (has) a.cols.insert(a.cols.begin(), fav);
    if (!keepPos) return;
    int shift = (has ? 1 : 0) - (had ? 1 : 0);
    if (had && a.col == 0 && !has) {  // 즐겨찾기를 모두 빼서 카테고리가 사라짐
        a.mode = Mode::Categories;
        a.col = a.catSel = 0;
        a.sel = a.top = 0;
    } else if (!(had && a.col == 0)) {
        a.col = std::max(0, a.col + shift);
    }
    a.catSel = std::max(0, a.catSel + shift);
}

// "홈브루 즐겨찾기": 옵션에서 고른 nro. 카테고리 목록 맨 위(즐겨찾기 바로 아래), 하나라도 있을 때만.
// keepPos: 보던 카테고리·커서를 그대로 (그 카테고리가 사라졌으면 카테고리 목록으로)
static void RebuildHbFavorites(App &a, bool keepPos) {
    auto key = [&](int i) {
        return i >= 0 && i < (int)a.cols.size() ? a.cols[i].shortName + "\x1f" + a.cols[i].name : std::string();
    };
    std::string colKey = key(a.col), catKey = key(a.catSel);
    a.cols.erase(std::remove_if(a.cols.begin(), a.cols.end(),
                                [](const Collection &c) { return c.virt && c.shortName == "hbfav"; }),
                 a.cols.end());
    if (!gHbFav.empty()) {
        Collection hb = BuildHomebrewFavorites(gHbFav, gHome + "/cache");
        if (!hb.games.empty()) {
            int pos = !a.cols.empty() && a.cols[0].virt && a.cols[0].shortName == "favorites" ? 1 : 0;
            a.cols.insert(a.cols.begin() + pos, hb);
        }
    }
    if (!keepPos) return;
    if (a.cols.empty()) {
        a.mode = Mode::Categories;
        a.col = a.catSel = a.sel = a.top = 0;
        return;
    }
    auto find = [&](const std::string &k) {
        for (int i = 0; i < (int)a.cols.size(); i++)
            if (key(i) == k) return i;
        return -1;
    };
    int c = find(colKey), s = find(catKey);
    if (c < 0) {
        a.mode = Mode::Categories;
        a.col = std::min(a.col, (int)a.cols.size() - 1);
        a.sel = a.top = 0;
    } else {
        a.col = c;
    }
    a.catSel = s >= 0 ? s : std::min(a.catSel, (int)a.cols.size() - 1);
    a.sel = std::min(a.sel, std::max(0, (int)a.cols[a.col].games.size() - 1));
}

static void LoadGames(App &a, bool force, std::string &log) {
    std::string cache = gHome + "/library.cache";
    std::string sig = LibrarySignature(gRoots);
    if (!force && LoadLibraryCache(cache, sig, a.cols)) {
        log += "[cache] " + cache + "\n";
    } else {
        if (a.r && !gShotMode) {
            SDL_SetRenderDrawColor(a.r, 16, 16, 22, 255);
            SDL_RenderClear(a.r);
            DrawText(a, a.fBig, T("게임 목록을 읽는 중..."), W / 2, 300, kWhite, 0, 1);
            DrawText(a, a.fInfo, T("처음 한 번만 오래 걸립니다. 이후에는 저장된 목록을 바로 씁니다."), W / 2, 360, kGrey, 0, 1);
            SDL_RenderPresent(a.r);
        }
        // 그림 캐시 비우기: 읽기에 실패했던 그림(새로 넣은 파일일 수 있음)도 다시 읽게
        a.images.Clear();
        a.thumbs.Clear();
        Uint32 t0 = SDL_GetTicks();
        a.cols = LoadLibrary(gRoots, log);
        SaveLibraryCache(cache, sig, a.cols);
        log += "[scan] " + std::to_string(SDL_GetTicks() - t0) + " ms\n";
    }
    CountFramesPending(a.cols);
    BuildMenu();
    RebuildHbFavorites(a, false);
    RebuildFavorites(a, false);
}

static void Render(App &a) {
    MarqueeNextFrame();
    if (a.cols.empty()) {
        Frame(a, "Joy Launcher", T("[B] 종료"));
        DrawText(a, a.fBig, T("게임 목록이 없습니다"), W / 2, 300, kWhite, 0, 1);
        DrawText(a, a.fInfo, T("SD 카드의 /roms 폴더(또는 /JoyLauncher/roots.txt)를 확인하세요"), W / 2, 360, kGrey, 0, 1);
    } else if (a.mode == Mode::Categories) {
        RenderCategories(a);
    } else {
        RenderGames(a);
    }
    if (a.optPick) RenderOptions(a);
    if (a.bgmPick) RenderPopup(a, T("BGM 설정"), gBgmMenu, a.bgmSel, 640);
    if (a.themePick) RenderThemePick(a);
    if (a.hbPick) RenderHbPick(a);
    if (a.exitAsk) RenderExitAsk(a);
    if (a.messageModal) {
        Fill(a, 0, 0, W, H, {0, 0, 0, 120});
        Fill(a, 240, 280, 800, 140, a.messageColor);
        DrawText(a, a.fList, a.message, W / 2, 310, kWhite, 760, 1, 0, 0, kScrollMsg);
        DrawText(a, a.fInfo, T("[B] 닫기"), W / 2, 370, kWhite, 0, 1);
    } else if (!a.message.empty() && SDL_GetTicks() < a.messageUntil) {
        // 여러 줄 알림 ("\n" 으로 나눔): 줄마다 가운데 정렬, 한 번에 보이게
        std::vector<std::string> lines;
        std::stringstream ss(a.message);
        for (std::string l; std::getline(ss, l, '\n');) lines.push_back(l);
        int n = std::max(1, (int)lines.size()), lh = 40;
        int boxH = 50 + n * lh, top = 345 - boxH / 2;
        Fill(a, 240, top, 800, boxH, a.messageColor);
        for (int i = 0; i < n; i++)
            DrawText(a, a.fList, lines[i], W / 2, top + 30 + i * lh - (n > 1 ? 5 : 0), kWhite, 760, 1, 0, 0, kScrollMsg);
    }
}

static void Clamp(App &a) {
    if (a.cols.empty()) return;
    int total = (int)a.cols.size();
    a.catSel = std::max(0, std::min(a.catSel, total - 1));
    a.optSel = std::max(0, std::min(a.optSel, (int)gMenu.size() - 1));
    // 목록 맨 위 항목(top)은 열 수의 배수 (격자는 줄 단위로 넘김)
    auto keep = [](int sel, int &top, bool games) {
        int cols, rows;
        ScreenGeom(games, cols, rows);
        top = std::max(0, top) / cols * cols;
        if (sel < top) top = sel / cols * cols;
        if (sel >= top + cols * rows) top = (sel / cols - rows + 1) * cols;
    };
    keep(a.catSel, a.catTop, false);
    int n = (int)a.cols[a.col].games.size();
    a.sel = std::max(0, std::min(a.sel, n - 1));
    keep(a.sel, a.top, true);
}

// 테마의 layout.txt 에 문제가 있으면 알림 (자세한 내용은 테마 폴더의 layout_errors.txt)
static void LayoutNotice(App &a) {
    if (gTheme.layout.problems.empty()) return;
    a.message = Fmt(T("테마 layout.txt 에 문제가 %d건 있습니다\n테마 폴더의 layout_errors.txt 를 확인하세요"), (int)gTheme.layout.problems.size());
    a.messageColor = {120, 40, 40, 240};
    a.messageUntil = SDL_GetTicks() + 5000;
}

// 선택이 화면 가운데쯤 오도록 하는 맨 위 항목
static int CenterTop(int sel, bool games) {
    int cols, rows;
    ScreenGeom(games, cols, rows);
    return std::max(0, sel / cols - rows / 2) * cols;
}

// 방향키로 옮긴 선택. 한 줄 목록: ▲▼ 한 칸, ◀▶ 한 쪽. 격자: ▲▼ 한 줄, ◀▶ 한 칸 (끝에서는 그대로)
static int MoveSel(int sel, int n, bool games, int b) {
    int cols, rows;
    ScreenGeom(games, cols, rows);
    if (cols <= 1) return sel + (b == 13 ? -1 : b == 15 ? 1 : b == 12 ? -rows : rows);
    if (b == 12) return sel - 1;
    if (b == 14) return sel + 1;
    if (b == 13) return sel >= cols ? sel - cols : sel;
    if (sel + cols < n) return sel + cols;
    return sel / cols < (n - 1) / cols ? n - 1 : sel;  // 아랫줄이 짧으면 그 줄의 마지막 칸으로
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
    a.top = CenterTop(a.sel, true);
    a.catTop = CenterTop(a.catSel, false);
    return true;
}

// 다음에 로드할 nro 를 예약. 스위치에서는 이후 런처가 종료되면 hbloader 가 실행한다.
static bool ChainLoad(App &a, std::string nro, const std::vector<std::string> &args) {
#ifdef __SWITCH__
    struct stat st;
    if (stat(nro.c_str(), &st) != 0) {
        a.message = T("프로그램이 없습니다: ") + nro;
        a.messageUntil = SDL_GetTicks() + 3000;
        return false;
    }
    if (nro.rfind("sdmc:", 0) != 0) nro = "sdmc:" + nro;
    std::string argv = "\"" + nro + "\"";
    for (const auto &s : args) argv += " \"" + s + "\"";
    if (R_FAILED(envSetNextLoad(nro.c_str(), argv.c_str()))) {
        a.message = T("실행 예약 실패");
        a.messageUntil = SDL_GetTicks() + 3000;
        return false;
    }
    __nx_applet_exit_mode = 0;  // hbloader 로 돌아가 예약한 nro 실행
    DebugLogLaunch(nro.c_str(), argv.c_str());
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
    e += T("  카테고리: ") + T(a.cols[a.col].name) + "\n";
    e += T("  롬 파일: ") + g.file + "\n";
    e += T("  원래 코어: ") + (g.metaCore.empty() ? std::string(T("(메타데이터에 없음)")) : g.metaCore) + "\n";
    e += T("  선택 방법: ") + (g.how.empty() ? std::string("?") : T(g.how)) + "\n";
    e += T("  실행: ") + (nro.empty() ? std::string(T("(없음)")) : nro) + "\n";
    e += T("  결과: ") + result + "\n";
    entries.insert(entries.begin(), e);
    if (entries.size() > kLogMax) entries.resize(kLogMax);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << Fmt(T("# Joy Launcher 실행 기록 (최근 %d개, 최신이 위)"), (int)kLogMax) << "\n\n";
    for (const auto &s : entries) o << s << "\n";
}

// 앨범(애플릿) 모드에서 게임·에뮬레이터를 실행하려 할 때 (두 줄 경고)
static void ShowAppletBlock(App &a) {
    a.message = T("앨범(애플릿) 모드에서는 게임을 실행할 수 없습니다") + std::string("\n") + T("R 버튼을 누른 채 게임을 실행해 런처를 여세요");
    a.messageColor = {120, 40, 40, 240};
    a.messageUntil = SDL_GetTicks() + 6000;
}

// 홈브루 즐겨찾기의 nro: 게임처럼 애플릿 모드에서는 막고, 아니면 그대로 이어 실행
static bool LaunchNro(App &a, const Game &g) {
    if (gAppletMode) {  // 게임처럼 애플릿 모드에서는 메모리 부족·복귀 실패가 생길 수 있어 막음
        ShowAppletBlock(a);
        LogLaunch(a, g, g.file, T("실행 안 함: 앨범(애플릿) 모드"));
        return false;
    }
    SaveState(a, true);
    bool ok = ChainLoad(a, g.file, {});
    LogLaunch(a, g, g.file, ok ? std::string(T("실행 예약됨")) : T("실패: ") + a.message);
    return ok;
}

static bool LaunchGame(App &a) {
    const Game &g = a.cols[a.col].games[a.sel];
    if (g.launch == kLaunchNro) return LaunchNro(a, g);
    std::string nro, args;
    if (!BuildLaunch(g, nro, args)) {
        a.message = T("실행 설정(launch)이 없습니다");
        a.messageUntil = SDL_GetTicks() + 2500;
        LogLaunch(a, g, nro, T("실패: 실행 설정 없음 (") + g.launch + ")");
        return false;
    }
    // 앨범(애플릿) 모드에서는 RetroArch·PPSSPP 가 메모리 부족으로 오류가 나고(PS1·PSP·FBNeo·GBA 실기 확인),
    // 끝낼 때 hbloader 가 런처를 다시 올리지 못해(2347-0018) HOME 메뉴까지 오류(2144-0001)로 재부팅이 필요 → 실행 막음
    if (gAppletMode) {
        ShowAppletBlock(a);
        LogLaunch(a, g, nro, T("실행 안 함: 앨범(애플릿) 모드"));
        return false;
    }
    SaveState(a, true);
    bool ok = ChainLoad(a, nro, {g.file});
    LogLaunch(a, g, nro, ok ? std::string(T("실행 예약됨")) : T("실패: ") + a.message);
    return ok;
}

int main(int argc, char **argv) {
    std::string shot;
    int wantCol = -1, wantSel = -1, wantMode = -1, wantCat = -1, wantOpt = -1;
    std::string wantTheme, wantLang;
    bool wantPick = false;
    bool wantHbPick = false;  // 데스크톱 확인용: 홈브루 게임즈 창
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
    bool psmOk = R_SUCCEEDED(psmInitialize());
#else
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--root" && i + 1 < argc) gRoot = argv[++i];
        else if (s == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (s == "--col" && i + 1 < argc) wantCol = atoi(argv[++i]);
        else if (s == "--cat" && i + 1 < argc) wantCat = atoi(argv[++i]);  // 카테고리 화면 커서
        else if (s == "--opt" && i + 1 < argc) wantOpt = atoi(argv[++i]);
        else if (s == "--applet") gAppletMode = true;  // 애플릿 모드 표시 확인용
        else if (s == "--lang" && i + 1 < argc) wantLang = argv[++i];  // ko / en (설정보다 우선)
        else if (s == "--update-install") {  // JOY_SDROOT 의 가짜 SD 에 최신 릴리스 설치 시험
            UpdateInfo u;
            std::string err;
            bool ok = FetchLatestRelease(u, err) &&
                      DownloadAndInstall(u, gSelfNro, CurrentHbmenu() == HbKind::Self, err,
                                         [](long long g, long long t) { return true; });
            printf("install ok=%d version=%s hbmenu=%d err=%s\n", ok, u.version.c_str(), CurrentHbmenu() == HbKind::Self,
                   err.c_str());
            return ok ? 0 : 1;
        }
#ifndef __SWITCH__
        else if (s == "--theme-unzip" && i + 3 < argc) {  // 테마 zip 풀기 시험: --theme-unzip <zip> <zip 안 폴더/> <풀 곳>
            int files = 0;
            std::string err;
            bool ok = ExtractThemeZip(argv[i + 1], argv[i + 2], argv[i + 3], files, err);
            printf("unzip ok=%d files=%d err=%s\n", ok, files, err.c_str());
            return ok ? 0 : 1;
        }
#endif
        else if (s == "--theme-download") {  // --home 의 themes 에 테마 팩 받기 시험: --theme-download [폴더] (기본 JoyBasic)
            int files = 0;
            std::string err, pack = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "JoyBasic";
            bool ok = DownloadTheme(gHome + "/themes", pack, files, err, [](long long, long long) { return true; });
            printf("theme ok=%d files=%d err=%s\n", ok, files, err.c_str());
            UpdaterShutdown();
            return ok ? 0 : 1;
        }
        else if (s == "--update-check") {  // 깃헙 최신 릴리스 조회 결과 출력
            UpdateInfo u;
            std::string err;
            bool ok = FetchLatestRelease(u, err);
            printf("current=%s ok=%d latest=%s newer=%d size=%lld sha256=%s url=%s err=%s\n", CurrentVersion(), ok,
                   u.version.c_str(), ok ? CompareVersions(u.version, CurrentVersion()) > 0 : 0, u.size,
                   u.sha256.c_str(), u.url.c_str(), err.c_str());
            return ok ? 0 : 1;
        } else if (s == "--hb") {  // JOY_SDROOT 의 가짜 SD 에서 기본 메뉴 처리 시험: --hb install|restore|check|list
            std::string what = i + 1 < argc ? argv[++i] : "list";
            std::string msg;
            bool ok = true;
            if (what == "install") ok = InstallAsHbmenu(gSelfNro, msg);
            else if (what == "restore") ok = RestoreHbmenu(msg);
            else if (what == "check") msg = HbCheckOnStart();
            printf("hb %s ok=%d current=%s record=%d msg=%s\n", what.c_str(), ok, HbKindName(CurrentHbmenu()),
                   HbInstalledRecord(), msg.c_str());
            for (const auto &m : HomebrewMenus()) printf("  menu: %s  %s\n", m.label.c_str(), m.nro.c_str());
            return ok ? 0 : 1;
        }  // 옵션 창 열고 커서
        else if (s == "--sel" && i + 1 < argc) wantSel = atoi(argv[++i]);
        else if (s == "--mode" && i + 1 < argc) wantMode = atoi(argv[++i]);
        else if (s == "--home" && i + 1 < argc) gHome = argv[++i];
        else if (s == "--theme" && i + 1 < argc) wantTheme = argv[++i];
        else if (s == "--themepick") wantPick = true;
        else if (s == "--hbpick") wantHbPick = true;
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
    a.thumbs.r = a.r;
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
    gSystemLang = SystemLang();
    LoadSettings();
    if (!wantLang.empty()) { gLanguage = wantLang; ApplyLanguage(); }
#ifndef __SWITCH__
    if (const char *m = getenv("JOY_MARQUEE_MS")) gMarqueeShift = (Uint32)atoi(m);
#endif
    MakeUserFolders();
    BgmScan(gHome + "/bgm");
    gShotMode = !shot.empty();
    if (!gShotMode) ApplyBgm();
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
            if (line.rfind("esde=", 0) == 0) { SetEsdeHint(line.substr(5)); continue; }  // ES-DE 폴더 위치
            if (!line.empty() && line[0] != '#' && std::find(roots.begin(), roots.end(), line) == roots.end())
                roots.push_back(line);
        }
    }
    LoadCoreConfig(gHome + "/cores.txt");
    LoadFavorites();
    LoadHbFav();
    gRoots = roots;
    LoadGames(a, false, log);
    printf("%s", log.c_str());
    for (auto &c : a.cols) printf("collection %s (%s): %d\n", c.name.c_str(), c.sortBy.c_str(), (int)c.games.size());
    if (LoadState(a)) SaveState(a);  // 복귀 표시는 한 번만 사용 (다음 새 실행은 맨 처음 화면)
    if (wantCol >= 0 && wantCol < (int)a.cols.size()) { a.col = a.catSel = wantCol; a.sel = 0; a.top = 0; }
    if (wantCat >= 0 && wantCat < (int)a.cols.size()) {
        a.catSel = wantCat;
        a.catTop = CenterTop(a.catSel, false);
    }
    if (wantOpt >= 0) { a.optPick = true; a.optSel = wantOpt; }
    if (wantSel >= 0) a.sel = wantSel;
    if (wantMode >= 0) a.mode = wantMode ? Mode::Games : Mode::Categories;
    Clamp(a);
    LayoutNotice(a);
    auto openThemePick = [&]() {
        a.themeList = ThemeNames();
        // 함께 배포하는 팩(기본테마·레트로테마·벽골김씨테마)을 앞에, 나머지는 이름순
        std::stable_sort(a.themeList.begin(), a.themeList.end(), [](const std::string &x, const std::string &y) {
            auto rank = [](const std::string &f) {
                int i = 0;
                for (const auto &p : kPacks) {
                    if (f == p.folder) return i;
                    i++;
                }
                return i;
            };
            return rank(x) < rank(y);
        });
        a.themeList.insert(a.themeList.begin(), "");
        a.themeTitles.clear();
        for (const auto &f : a.themeList) a.themeTitles.push_back(f.empty() ? "" : ThemeTitle(f));
        for (const auto &p : kPacks) {
            a.themeList.push_back(std::string(kThemeDownload) + p.folder);
            a.themeTitles.push_back("");
        }
        a.themeSel = 0;
        for (size_t i = 1; i < a.themeList.size(); i++)
            if (a.themeList[i] == gThemeName) a.themeSel = (int)i;
        a.themePick = true;
    };
    if (wantPick) openThemePick();
    if (wantHbPick) {
        a.hbList = ScanHomebrew(gHome + "/cache", gSelfNro).games;
        a.hbPick = !a.hbList.empty();
        if (wantSel >= 0) a.hbSel = std::min(wantSel, (int)a.hbList.size() - 1);
    }
    if (wantFrames) { gShotMode = false; RunFrameBatch(a); gShotMode = !shot.empty(); }

    if (!shot.empty()) {
#ifndef __SWITCH__
        // 실행을 먼저 시도한 뒤의 화면 (애플릿 모드 경고 등 확인용)
        if (getenv("JOY_SHOT_LAUNCH") && wantSel >= 0 && a.mode == Mode::Games) { LaunchGame(a); wantSel = -1; }
        // 방향키 이동 시험: JOY_SHOT_NAV="15,14,0" (12 ◀ 13 ▲ 14 ▶ 15 ▼, 0 열기, 1 뒤로) — 이동한 뒤의 화면을 찍음
        if (const char *nav = getenv("JOY_SHOT_NAV")) {
            std::stringstream ns(nav);
            for (std::string tok; std::getline(ns, tok, ',');) {
                int b = atoi(tok.c_str());
                if (b >= 12 && b <= 15) {
                    if (a.mode == Mode::Categories) a.catSel = MoveSel(a.catSel, (int)a.cols.size(), false, b);
                    else a.sel = MoveSel(a.sel, (int)a.cols[a.col].games.size(), true, b);
                } else if (b == 0 && a.mode == Mode::Categories) {
                    if (a.col != a.catSel) { a.col = a.catSel; a.sel = a.top = 0; }
                    a.mode = Mode::Games;
                } else if (b == 1) {
                    a.mode = Mode::Categories;
                    a.catSel = a.col;
                }
                Clamp(a);
                printf("NAV %d -> catSel=%d catTop=%d sel=%d top=%d\n", b, a.catSel, a.catTop, a.sel, a.top);
            }
        }
#endif
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
    // 기본 홈브루 메뉴 확인: Joy 로 설정해 뒀는데 Atmosphere 업데이트 등으로 바뀌었는지, sphaira 자동 덮어쓰기가 켜졌는지
    std::string hbNote = HbCheckOnStart();
    // cores.txt [user] 에 적은 코어가 스위치에 없으면 알림 (그 게임은 기본 코어로 실행)
    std::string coreNote = UserCoreProblems();
    if (!coreNote.empty())
        hbNote += (hbNote.empty() ? "" : "\n") +
                  Fmt(T("cores.txt [user] 에 적은 코어가 없어 기본 코어로 실행합니다: %s · /retroarch/cores 의 파일 이름을 확인하세요"),
                      coreNote.c_str());
    if (!hbNote.empty()) {
        BuildMenu();
        a.message = hbNote;
        a.messageColor = {60, 70, 120, 240};
        a.messageModal = true;
    }
    // 옵션 창의 항목 실행
    // 화면 가운데 상자에 한 줄 표시 (오래 걸리는 작업 전)
    auto showBusy = [&](const std::string &text) {
        Render(a);
        Fill(a, 240, 300, 800, 90, {28, 28, 40, 245});
        DrawText(a, a.fList, text, W / 2, 330, kWhite, 760, 1, 0, 0, kScrollMsg);
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
            showBusy(T("업데이트 확인 중..."));
            UpdateCheckStop();
            UpdateInfo info;
            std::string err;
            if (!FetchLatestRelease(info, err)) { a.optPick = false; modal(T("업데이트 확인 실패: ") + err, false); return; }
            if (CompareVersions(info.version, CurrentVersion()) <= 0) {
                a.optPick = false;
                modal(Fmt(T("최신 버전입니다 (v%s)"), CurrentVersion()), true);
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
                    snprintf(buf, sizeof(buf), T("v%s 내려받는 중  %.1f / %.1f MB"), gUpdate.version.c_str(), got / 1048576.0,
                             total / 1048576.0);
                    DrawText(a, a.fList, buf, W / 2, 300, kWhite, 760, 1);
                    Fill(a, 280, 350, 720, 14, {60, 60, 80, 255});
                    if (total > 0) Fill(a, 280, 350, (int)(720.0 * got / total), 14, kAccent);
                    DrawText(a, a.fInfo, T("[B] 취소"), W / 2, 380, kGrey, 760, 1);
                    SDL_RenderPresent(a.r);
                }
                return !cancel;
            };
            std::string err;
            bool hb = CurrentHbmenu() == HbKind::Self;
            if (!DownloadAndInstall(gUpdate, gSelfNro, hb, err, progress)) { modal(T("업데이트 실패: ") + err, false); return; }
            showBusy(Fmt(T("v%s 설치 완료. 다시 시작합니다..."), gUpdate.version.c_str()));
            SDL_Delay(1200);
            SaveState(a, true);
            if (ChainLoad(a, gLaunchedAsHbmenu ? std::string("/hbmenu.nro") : gSelfNro, {})) running = false;
            else modal(T("설치는 끝났습니다. 런처를 다시 실행해 주세요"), true);
            return;
        }
        a.confirmIdx = idx;
        a.confirmUntil = SDL_GetTicks() + 5000;
        a.message = Fmt(T("새 버전 v%s: A 를 한 번 더 누르면 내려받아 설치합니다"), gUpdate.version.c_str());
        a.messageColor = {60, 70, 120, 240};
        a.messageUntil = a.confirmUntil;
    };
    // 테마 선택 창의 "<팩> 내려받기": 최신 릴리스에서 받아 /JoyLauncher/themes/<팩 폴더> 에 풀기
    auto runThemeDownload = [&](const ThemePack &pack) {
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
                DrawText(a, a.fList, Fmt(T("%s 내려받는 중  %.1f / %.1f MB"), PackName(pack), got / 1048576.0, total / 1048576.0),
                         W / 2, 300, kWhite, 760, 1);
                Fill(a, 280, 350, 720, 14, {60, 60, 80, 255});
                if (total > 0) Fill(a, 280, 350, (int)(720.0 * got / total), 14, kAccent);
                DrawText(a, a.fInfo, T("[B] 취소"), W / 2, 380, kGrey, 760, 1);
                SDL_RenderPresent(a.r);
            }
            return !cancel;
        };
        showBusy(T("최신 릴리스 확인 중..."));
        int files = 0;
        std::string err;
        bool ok = DownloadTheme(gHome + "/themes", pack.folder, files, err, progress);
        if (!ok) {
            modal(T("테마 내려받기 실패: ") + err, false);
            return;
        }
        // 새 그림으로 다시 읽기 (지금 그 테마를 쓰고 있었으면 바로 반영)
        a.backs.Clear();
        a.images.Clear();
        a.lastBack.clear();
        if (gThemeName == pack.folder) {
            LoadTheme(pack.folder);
            a.top = CenterTop(a.sel, true);
            a.catTop = CenterTop(a.catSel, false);
            Clamp(a);
        }
        openThemePick();
        for (size_t i = 0; i < a.themeList.size(); i++)
            if (a.themeList[i] == pack.folder) a.themeSel = (int)i;
        a.message = Fmt(T("%s: 받았습니다 (파일 %d개) · A 를 누르면 적용합니다"), PackName(pack), files);
        a.messageColor = {40, 100, 60, 240};
        a.messageUntil = SDL_GetTicks() + 4000;
    };
    // 옵션 창에서 값을 바꾸는 항목 (A 또는 ◀ ▶). dir: +1 / -1
    auto adjust = [&](MenuAct act, int dir) {
        if (act == MenuAct::Language) {
            static const char *kLangs[] = {"auto", "ko", "en"};
            int cur = gLanguage == "ko" ? 1 : gLanguage == "en" ? 2 : 0;
            gLanguage = kLangs[(cur + 3 + dir) % 3];
            ApplyLanguage();
        } else if (act == MenuAct::Bgm) {
            gBgmOn = !gBgmOn;
        } else if (act == MenuAct::BgmVolume) {
            gBgmVolume = std::max(0, std::min(100, gBgmVolume + dir * 10));
        } else if (act == MenuAct::BgmOrder) {
            gBgmShuffle = !gBgmShuffle;
        } else if (act == MenuAct::BgmNext) {
            BgmSkip();
            return;
        } else {
            return;
        }
        ApplyBgm();
        SaveSettings();
        BuildMenu();
        a.bgmSel = std::min(a.bgmSel, (int)gBgmMenu.size() - 1);  // BGM 을 끄면 "다음 곡" 이 빠짐
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
            for (const auto &col : a.cols)
                if (!col.virt) n += (int)col.games.size();
            a.message = Fmt(T("게임 목록을 새로 읽었습니다 (%d개)"), n);
            a.messageColor = {40, 100, 60, 240};
            a.messageModal = true;
            a.optPick = false;
        } else if (m.act == MenuAct::Update) {
            runUpdate(idx);
        } else if (m.act == MenuAct::Language) {
            adjust(m.act, 1);
        } else if (m.act == MenuAct::HbFav) {
            a.hbList = ScanHomebrew(gHome + "/cache", gSelfNro).games;
            if (a.hbList.empty()) {
                a.message = T("SD 의 /switch 에서 nro 를 찾지 못했습니다");
                a.messageColor = {60, 70, 120, 240};
                a.messageUntil = SDL_GetTicks() + 3500;
            } else {
                a.hbPick = true;
                a.hbSel = 0;
            }
        } else if (m.act == MenuAct::BgmMenu) {
            a.bgmPick = true;
            a.bgmSel = 0;
        } else if (m.act == MenuAct::BgmNone) {
            a.message = T("/JoyLauncher/bgm 폴더에 mp3 또는 ogg 파일을 넣고 다시 실행하세요");
            a.messageColor = {60, 70, 120, 240};
            a.messageUntil = SDL_GetTicks() + 3500;
        } else if (m.act == MenuAct::Launch && gAppletMode && !m.hb) {
            a.optPick = false;
            ShowAppletBlock(a);  // RetroArch·PPSSPP 단독 실행도 애플릿 모드에서는 오류
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
            a.message = T("확인: A 를 한 번 더 누르면 실행합니다");
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
        if (a.exitAsk) {
            if (b == 0) { SaveState(a); ExitHome(); running = false; }
            if (b == 0 || b == 1) a.exitAsk = false;
            return;
        }
        if (a.hbPick) {
            int n = (int)a.hbList.size();
            if (b == 13) a.hbSel = (a.hbSel + n - 1) % n;
            else if (b == 15) a.hbSel = (a.hbSel + 1) % n;
            else if (b == 0 || b == 11) {  // A 또는 − (즐겨찾기 버튼)
                const std::string &f = a.hbList[a.hbSel].file;
                auto it = std::find(gHbFav.begin(), gHbFav.end(), f);
                if (it != gHbFav.end()) gHbFav.erase(it);
                else gHbFav.push_back(f);
                SaveHbFav();
            } else if (b == 1) {  // 닫을 때 카테고리를 다시 만든다
                a.hbPick = false;
                RebuildHbFavorites(a, true);
                BuildMenu();
            }
            a.selChanged = SDL_GetTicks() - 1000;
            return;
        }
        if (a.bgmPick) {
            int n = (int)gBgmMenu.size();
            if (b == 13) a.bgmSel = (a.bgmSel + n - 1) % n;
            else if (b == 15) a.bgmSel = (a.bgmSel + 1) % n;
            else if (b == 0) adjust(gBgmMenu[a.bgmSel].act, 1);
            else if ((b == 12 || b == 14) && Adjustable(gBgmMenu[a.bgmSel].act)) adjust(gBgmMenu[a.bgmSel].act, b == 14 ? 1 : -1);
            else if (b == 1) a.bgmPick = false;
            return;
        }
        if (a.themePick) {
            int n = (int)a.themeList.size();
            if (b == 13) a.themeSel = (a.themeSel + n - 1) % n;
            else if (b == 15) a.themeSel = (a.themeSel + 1) % n;
            else if (b == 0 && IsThemeDownload(a.themeList[a.themeSel])) {
                if (const ThemePack *p = ThemeDownloadPack(a.themeList[a.themeSel])) runThemeDownload(*p);
            } else if (b == 0) {
                LoadTheme(a.themeList[a.themeSel]);
                SaveSettings();
                a.backs.Clear();
                a.lastBack.clear();
                a.themePick = false;
                BuildMenu();
                a.top = CenterTop(a.sel, true);
                a.catTop = CenterTop(a.catSel, false);
                Clamp(a);
                LayoutNotice(a);
            } else if (b == 1) a.themePick = false;
            a.selChanged = SDL_GetTicks() - 1000;
            return;
        }
        if (a.optPick) {
            int n = (int)gMenu.size();
            if (b == 13) a.optSel = (a.optSel + n - 1) % n;
            else if (b == 15) a.optSel = (a.optSel + 1) % n;
            else if (b == 0) runMenu(a.optSel);
            else if ((b == 12 || b == 14) && Adjustable(gMenu[a.optSel].act)) adjust(gMenu[a.optSel].act, b == 14 ? 1 : -1);
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
        if (b == 2) {  // X: 홈브루 메뉴 (이전 기본 메뉴 → sphaira → nx-hbmenu → 기타 백업)
            for (const auto &m : gMenu)
                if (m.hb) {
                    SaveState(a, true);
                    if (ChainLoad(a, m.nro, {})) running = false;
                    return;
                }
            a.message = T("홈브루 메뉴(sphaira / nx-hbmenu 등)를 찾을 수 없습니다");
            a.messageColor = {120, 40, 40, 240};
            a.messageUntil = SDL_GetTicks() + 3000;
            return;
        }
        if (a.cols.empty()) { if (b == 1 || b == 10) running = false; return; }
        int ncol = (int)a.cols.size();
        if (a.mode == Mode::Categories) {
            switch (b) {
            case 12: case 13: case 14: case 15: a.catSel = MoveSel(a.catSel, ncol, false, b); break;
            case 0:
                if (a.catSel < ncol) {
                    if (a.col != a.catSel) { a.col = a.catSel; a.sel = a.top = 0; }
                    a.mode = Mode::Games;
                }
                break;
            case 1: a.exitAsk = true; return;  // 맨 위 화면의 B: 한 번 더 확인
            case 10: SaveState(a); ExitHome(); running = false; break;
            }
        } else {
            switch (b) {
            case 12: case 13: case 14: case 15:
                a.sel = MoveSel(a.sel, (int)a.cols[a.col].games.size(), true, b);
                break;
            case 6: a.col = (a.col + ncol - 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 7: a.col = (a.col + 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 0: if (LaunchGame(a)) running = false; break;
            case 1: a.mode = Mode::Categories; a.catSel = a.col; break;
            case 11: {  // −: 즐겨찾기 넣기/빼기
                std::string file = a.cols[a.col].games[a.sel].file;
                if (a.cols[a.col].shortName == "hbfav") {  // 홈브루 즐겨찾기에서는 빼기만 (넣기는 옵션 창)
                    gHbFav.erase(std::remove(gHbFav.begin(), gHbFav.end(), file), gHbFav.end());
                    SaveHbFav();
                    RebuildHbFavorites(a, true);
                    BuildMenu();
                    a.message = T("홈브루 즐겨찾기에서 뺐습니다");
                    a.messageColor = {60, 70, 120, 240};
                    a.messageUntil = SDL_GetTicks() + 1500;
                    break;
                }
                bool add = !gFavSet.count(file);
                if (add) gFavSet.insert(file);
                else gFavSet.erase(file);
                SaveFavorites();
                RebuildFavorites(a, true);
                a.message = add ? T("즐겨찾기에 넣었습니다") : T("즐겨찾기에서 뺐습니다");
                a.messageColor = {60, 70, 120, 240};
                a.messageUntil = SDL_GetTicks() + 1500;
                break;
            }
            case 10: SaveState(a); ExitHome(); running = false; break;
            }
        }
        a.selChanged = SDL_GetTicks();
        Clamp(a);
    };
    bool updateNotified = gAutoUpdate != "on" || gShotMode;
#ifdef __SWITCH__
    // 슬립·HOME: 런처가 멈추면 BGM 도 멈추고, 돌아오면 약 1초 동안 소리를 키우며 이어서 재생
    AppletHookCookie hookCookie;
    appletHook(
        &hookCookie,
        [](AppletHookType hook, void *) {
            if (hook == AppletHookType_OnFocusState) {
                if (appletGetFocusState() == AppletFocusState_InFocus) BgmResume();
                else BgmPause();
            } else if (hook == AppletHookType_OnResume) {
                BgmResume();
            }
        },
        nullptr);
#endif
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
                        a.message = Fmt(T("새 버전 v%s 이 있습니다  ·  Y 옵션에서 설치"), info.version.c_str());
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
                case SDLK_f: act(11); break;
                }
            }
        }
        if (held >= 12 && held <= 15 && SDL_GetTicks() >= nextRepeat) {
            act(held);
            nextRepeat = SDL_GetTicks() + 70;
        }
        BgmUpdate();
        Render(a);
        SDL_RenderPresent(a.r);
    }
    // 다음 프로그램(게임·홈브루)으로 넘어가기 전에 소리를 줄이고 오디오·스레드·서비스를 모두 닫는다
    BgmShutdown(400);
#ifdef __SWITCH__
    appletUnhook(&hookCookie);
    if (psmOk) psmExit();
#endif
    FrameWorkerStop();
    UpdaterShutdown();
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
