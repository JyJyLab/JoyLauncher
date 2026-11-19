// Joy Launcher (안드로이드): 롬 폴더(페가수스 metadata.pegasus.txt · ES-DE gamelist.xml · 롬 파일만)를 읽어
// 카테고리(플랫폼)별로 보여 주고, 에뮬레이터 앱을 게임 파일과 함께 실행한다 (emulators.cpp).
// 설정·테마는 공용 저장소 루트의 /JoyLauncher (themes/<테마>/ 에 카테고리별 배경·로고):
// 스위치판(JyJyLab/JoyLauncher)과 폴더 구성·테마 문법이 같아 테마를 그대로 서로 쓸 수 있다.
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <fstream>
#include <iterator>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <vector>

#include "apps.h"
#include "bgm.h"
#include "emulators.h"
#include "esde.h"
#include "ftpserver.h"
#include "i18n.h"
#include "layout.h"
#include "overlay.h"
#include "pegasus.h"
#include "platform.h"
#include "uistyle.h"
#include "updater.h"
#include "video.h"

static std::string gRoot = "roms";         // 데스크톱 시험판: --root (안드로이드는 저장소에서 롬 폴더를 찾음)
static std::string gHome = "JoyLauncher";  // 설정·상태·테마 폴더 (안드로이드: <공용 저장소>/JoyLauncher)

static const int W = 1280, H = 720;
static const SDL_Color kWhite = {235, 235, 240, 255};
static const SDL_Color kGrey = {150, 150, 165, 255};
static const SDL_Color kAccent = {255, 196, 64, 255};
static const SDL_Color kDark = {20, 20, 26, 255};
static const SDL_Color kSel = {70, 70, 96, 255};

// 옵션 창(Y)의 항목
enum class MenuAct { Theme, FtpRecv, Video, Refresh, Update, Language, Effects, BgmMenu, SoundCommon, SoundTheme, SoundVideo, SoundOff, BgmVolume, BgmOrder, BgmNext, AppFav, EmuPick, SwapAB, Overlay, Exit };
struct MenuItem {
    std::string label;
    MenuAct act;
    bool on = false;  // 켜져 있는 상태 (BGM 재생 중 등): 초록색 글자
};
// 켜짐 초록색은 메뉴 창 꾸밈(uistyle.cpp MenuStyle::on)
static std::vector<MenuItem> gMenu;
static std::vector<MenuItem> gBgmMenu;  // BGM · 소리 설정 창 (X 버튼, 또는 옵션 창에서)
static bool gDemo = false;  // 데스크톱 시험판의 데모 창 (--demo): 게임·앱 실행 대신 알림, 업데이트 확인 안 함
static std::string gThemeName;  // 현재 테마 폴더 이름 ("" = 기본)
static std::string ThemeTitle(const std::string &folder);  // 테마의 화면 이름
static bool gVideoOn = true;                // 설정: 고른 게임의 미리보기 영상 재생 (video)
static std::string gAutoUpdate = "on";      // on: 시작할 때 깃헙에서 새 버전 확인 (받기는 옵션 창의 링크로)
static bool gUpdateFound = false;           // 새 버전을 찾았는지
static UpdateInfo gUpdate;
static std::string gLanguage = "auto";      // auto: 기기 언어, ko, en
static Lang gSystemLang = Lang::Ko;         // 기기 언어 (시작할 때 한 번 읽음)
static bool gSwapAB = false;                // 설정: A·B 의 선택·취소를 서로 바꿈 (swap_ab)
static std::string gOverlayMode = "auto";   // 설정: 화면 컨트롤러 auto(컨트롤러가 없을 때) / on(항상) / off(끔)
static bool gPadStartSelect = false;        // 지금 쓰는 컨트롤러가 닌텐도 계열이 아님: [+] [-] 를 START·SELECT 로 표시
// 설정: 소리 선택 (sound). 넷 중 하나만 난다 — common 공통 BGM(/JoyLauncher/bgm) / theme 테마 BGM(지금 테마 폴더의 bgm)
// / video 미리보기 영상의 소리 / off 무음
static std::string gSound = "video";
static bool gBgmLive = false;               // 시작 준비가 끝남 (그 전과 데스크톱 스냅샷에서는 오디오를 열지 않음)
static int gBgmVolume = 60;                 // 0~100
static bool gBgmShuffle = false;
static bool gEffects = true;      // 설정: 화면 효과 (effects=on/off)
static bool gAnimAllowed = true;  // 데스크톱 스냅샷에서는 끔
static bool AnimOn() { return gEffects && gAnimAllowed; }
static float Ease(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
static float Approach(float cur, float target, float dt, int ms) {
    if (ms <= 0) return target;
    cur += (target - cur) * (1 - expf(-dt / (ms / 3.0f)));
    return fabsf(target - cur) < 0.002f ? target : cur;
}

// 즐겨찾기: /JoyLauncher/favorites.txt (롬 파일 경로 한 줄씩)
static std::set<std::string> gFavSet;

// 앱 즐겨찾기: 앱 목록(X)에서 고른 안드로이드 앱 — /JoyLauncher/app_favorites.txt ("패키지/액티비티" 한 줄씩)
// (스위치판의 홈브루 즐겨찾기 자리. 카테고리의 테마 그림 이름은 그대로 hbfav)
static std::vector<std::string> gAppFav;

static void SaveAppFav() {
    std::ofstream o(gHome + "/app_favorites.txt");
    o << "# Joy Launcher app favorites (package/activity)\n";
    for (const auto &p : gAppFav) o << p << "\n";
}

static void LoadAppFav() {
    gAppFav.clear();
    std::ifstream in(gHome + "/app_favorites.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#' && std::find(gAppFav.begin(), gAppFav.end(), line) == gAppFav.end())
            gAppFav.push_back(line);
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

// 실행 횟수: /JoyLauncher/playcount.txt (횟수<TAB>마지막 실행 시각(유닉스 초)<TAB>롬 경로) — 테마의 {playcount} {lastplayed}
static unsigned gViewVersion = 1;  // 즐겨찾기·실행 기록이 바뀔 때 +1 (보기 목록 다시 만들기)
struct PlayStat { int count = 0; long long last = 0; };
static std::map<std::string, PlayStat> gPlay;
static void LoadPlayCounts() {
    gPlay.clear();
    std::ifstream in(gHome + "/playcount.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        PlayStat &p = gPlay[line.substr(t2 + 1)];
        p.count = atoi(line.substr(0, t1).c_str());
        p.last = atoll(line.substr(t1 + 1, t2 - t1 - 1).c_str());
    }
}
static void CountPlay(const std::string &file) {
    PlayStat &p = gPlay[file];
    gViewVersion++;
    p.count++;
    p.last = (long long)time(nullptr);
    std::ofstream o(gHome + "/playcount.txt");
    for (const auto &kv : gPlay) o << kv.second.count << '\t' << kv.second.last << '\t' << kv.first << "\n";
}

static void ApplyLanguage() {
    SetLang(gLanguage == "ko" ? Lang::Ko : gLanguage == "en" ? Lang::En : gSystemLang);
}

// 소리 선택에 따른 BGM 곡 폴더 ("" = 없음): 공통 = /JoyLauncher/bgm, 테마 = 지금 테마 폴더의 bgm
static std::string BgmDir(const std::string &mode) {
    if (mode == "common") return gHome + "/bgm";
    if (mode == "theme" && !gThemeName.empty()) return gHome + "/themes/" + gThemeName + "/bgm";
    return "";
}

// 소리 선택·테마가 바뀌었을 때: 곡 폴더를 다시 읽고(달라졌으면 그 곡으로) BGM 을 켜거나 끈다
static void ApplyBgm() {
    if (!gBgmLive) return;
    bool bgm = gSound == "common" || gSound == "theme";
    BgmScan(bgm ? BgmDir(gSound) : "");
    BgmConfigure(bgm, gBgmVolume, gBgmShuffle);
}

static const char *SoundLabel() {
    return gSound == "common" ? T("공통 BGM") : gSound == "theme" ? T("테마 BGM") : gSound == "video" ? T("영상 소리") : T("무음");
}

static std::string LanguageLabel() {
    std::string sys = gSystemLang == Lang::Ko ? "한국어" : "English";
    std::string v = gLanguage == "ko" ? "한국어" : gLanguage == "en" ? "English" : Fmt(T("자동 (%s)"), sys.c_str());
    return Fmt(T("언어 / Language: %s"), v.c_str());
}

static const char *OverlayLabel() {
    return gOverlayMode == "on" ? T("항상 표시") : gOverlayMode == "off" ? T("끔") : T("자동 (컨트롤러가 없을 때)");
}

static void BuildMenu() {
    gMenu.clear();
    gMenu.push_back({T("게임 목록 새로 고침 (롬·그림·영상을 넣거나 뺐을 때)"), MenuAct::Refresh});
    if (UpdaterEnabled())
        gMenu.push_back({gUpdateFound ? Fmt(T("새 버전 v%s 받기 (현재 v%s)"), gUpdate.version.c_str(), CurrentVersion())
                                      : Fmt(T("업데이트 확인 (현재 v%s)"), CurrentVersion()),
                         MenuAct::Update});
    gMenu.push_back({Fmt(T("테마 설정 (현재: %s)"), gThemeName.empty() ? T("기본") : ThemeTitle(gThemeName).c_str()), MenuAct::Theme});
    gMenu.push_back({T("PC에서 테마 받기 (FTP)"), MenuAct::FtpRecv});
    // BGM · 소리 설정 창: 소리는 넷 중 하나 (● = 지금 고른 것), 아래는 BGM 볼륨·순서
    bool bgmMode = gSound == "common" || gSound == "theme";
    gMenu.push_back({bgmMode ? Fmt(T("BGM · 소리 설정 (%s · 볼륨 %d%%)"), SoundLabel(), gBgmVolume) : Fmt(T("BGM · 소리 설정 (%s)"), SoundLabel()),
                     MenuAct::BgmMenu, bgmMode});
    gBgmMenu.clear();
    auto mode = [](const char *id, const std::string &label, MenuAct act) {
        bool cur = gSound == id;
        gBgmMenu.push_back({(cur ? "● " : "○ ") + label, act, cur});
    };
    mode("common", Fmt(T("공통 BGM (%d곡)  /JoyLauncher/bgm"), BgmCountIn(BgmDir("common"))), MenuAct::SoundCommon);
    mode("theme", gThemeName.empty() ? std::string(T("테마 BGM (테마 없음)"))
                                     : Fmt(T("테마 BGM (%d곡)  themes/%s/bgm"), BgmCountIn(BgmDir("theme")), gThemeName.c_str()),
         MenuAct::SoundTheme);
    mode("video", T("영상 소리 (미리보기 영상)"), MenuAct::SoundVideo);
    mode("off", T("무음"), MenuAct::SoundOff);
    gBgmMenu.push_back({Fmt(T("BGM 볼륨: %d%%"), gBgmVolume), MenuAct::BgmVolume});
    gBgmMenu.push_back({gBgmShuffle ? T("BGM 순서: 무작위") : T("BGM 순서: 차례대로"), MenuAct::BgmOrder});
    if (bgmMode && BgmCountIn(BgmDir(gSound)) > 0) gBgmMenu.push_back({T("다음 곡"), MenuAct::BgmNext});
    gMenu.push_back({LanguageLabel(), MenuAct::Language});
    gMenu.push_back({Fmt(T("화면 효과 (움직임·전환): %s"), gEffects ? T("켜짐 (On)") : T("꺼짐 (Off)")), MenuAct::Effects, gEffects});
    gMenu.push_back({Fmt(T("미리보기 영상 재생: %s"), gVideoOn ? T("켜짐 (On)") : T("꺼짐 (Off)")), MenuAct::Video, gVideoOn});
    gMenu.push_back({Fmt(T("앱 목록 · 앱 즐겨찾기 추가·빼기 (%d개)"), (int)gAppFav.size()), MenuAct::AppFav, !gAppFav.empty()});
    gMenu.push_back({T("에뮬레이터 선택 (지금 카테고리)"), MenuAct::EmuPick});
    gMenu.push_back({Fmt(T("A·B 버튼 바꾸기 (선택 ↔ 취소): %s"), gSwapAB ? T("켜짐 (On)") : T("꺼짐 (Off)")), MenuAct::SwapAB, gSwapAB});
    gMenu.push_back({Fmt(T("화면 컨트롤러 (터치): %s"), OverlayLabel()), MenuAct::Overlay, gOverlayMode == "on"});
    gMenu.push_back({T("종료"), MenuAct::Exit});
}

// ---------------------------------------------------------------- 기기 화면 배율
// 화면은 늘 1280x720 좌표로 그린다 (테마의 좌표 그대로, SDL 이 기기 화면에 맞춰 늘림).
// 다만 글자와 줄여 올리는 그림은 기기의 실제 해상도로 만들어 늘린 화면에서도 또렷하게 한다.
// gScale = 실제 점 / 화면 좌표. 1.0 (1280x720 화면) 이면 예전과 점 하나까지 같다.
static float gScale = 1.0f;
static int ToPx(int v) { return gScale == 1.0f ? v : (int)lroundf(v * gScale); }      // 화면 좌표 → 실제 점
static int FromPx(int px) { return gScale == 1.0f ? px : (int)lroundf(px / gScale); }  // 실제 점 → 화면 좌표
static int FontH(TTF_Font *f) { return FromPx(TTF_FontHeight(f)); }
static int FontAsc(TTF_Font *f) { return FromPx(TTF_FontAscent(f)); }
// 실제 해상도로 만든 그림(글자·버튼)을 화면 좌표 (x, y) 에, 실제 점에 딱 맞춰 그린다 (반 점에 걸려 흐려지지 않게).
// src: 그림 안에서 그릴 자리 (실제 점, nullptr = 전체)
static void CopyPx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, int x, int y) {
    int pw, ph;
    if (src) { pw = src->w; ph = src->h; }
    else SDL_QueryTexture(t, nullptr, nullptr, &pw, &ph);
    if (gScale == 1.0f) {
        SDL_Rect d = {x, y, pw, ph};
        SDL_RenderCopy(r, t, src, &d);
        return;
    }
    SDL_FRect d = {roundf(x * gScale) / gScale, roundf(y * gScale) / gScale, pw / gScale, ph / gScale};
    SDL_RenderCopyF(r, t, src, &d);
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
        SDL_Surface *surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), c, ToPx(wrap))
                                     : TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return nullptr;
        SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        map[k] = t;
        return t;
    }
};

// ---------------------------------------------------------------- 그림 파일 읽기
// SDL2_image 로 읽고, 못 읽는 형식(webp·avif 등 — 페가수스 JoiPlay 목록의 그림)은 기기의 그림 해석기로 (안드로이드)
static SDL_Surface *LoadImageFile(const std::string &path) {
    SDL_Surface *s = IMG_Load(path.c_str());
    if (!s) s = plat::DecodeImage(path, 2048);
    return s;
}

// ---------------------------------------------------------------- 그림 읽기 스레드
// 파일 읽기·풀기·줄이기는 스레드에서, 텍스처 만들기는 메인에서 (SDL 렌더러는 메인 스레드 전용).
// 큰 그림을 넘길 때 화면이 멈칫하지 않게 한다. 데스크톱 스냅샷·테마 빌더는 바로 읽음 (gAsyncImg = false)
struct ImgJob {
    std::string key, path;
    int w = 0, h = 0;
    bool cover = false, thumb = false;
    void *cache = nullptr;
    Uint32 tick = 0, seq = 0;  // 요청한 프레임 (최근 프레임 것부터), 요청 순서
};
struct ImgDone { ImgJob job; SDL_Surface *s = nullptr; };
static bool gAsyncImg = false;
static Uint32 gFrameTick = 0;
static void ImgRequest(const ImgJob &j);

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
        if (gAsyncImg) {
            ImgJob j;
            j.key = j.path = p;
            j.cache = this;
            ImgRequest(j);
            return nullptr;
        }
        SDL_Surface *s = LoadImageFile(p);
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(r, s) : nullptr;
        if (s) SDL_FreeSurface(s);
        return Insert(p, t);
    }
    SDL_Texture *Insert(const std::string &p, SDL_Texture *t) {
        if (map.count(p)) {  // 이미 있음 (같은 그림을 두 번 받음)
            if (t) SDL_DestroyTexture(t);
            return Find(p);
        }
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
        if (gAsyncImg) {
            ImgJob j;
            j.key = Key(p, w, h);
            j.path = p;
            j.w = w;
            j.h = h;
            j.cover = cover;
            j.thumb = true;
            j.cache = this;
            ImgRequest(j);
            return nullptr;
        }
        SDL_Surface *s = LoadImageFile(p);
        if (s && w > 0 && h > 0) {
            float kx = s->w / (w * gScale), ky = s->h / (h * gScale);
            s = ShrinkSurface(s, (int)(cover ? std::min(kx, ky) : std::max(kx, ky)));
        }
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(r, s) : nullptr;
        if (s) SDL_FreeSurface(s);
        return Insert(Key(p, w, h), t);
    }
    SDL_Texture *Insert(const std::string &key, SDL_Texture *t) {
        if (map.count(key)) {
            if (t) SDL_DestroyTexture(t);
            return map[key].t;
        }
        if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);  // 줄이거나 늘릴 때 부드럽게
        if (map.size() >= cap) {
            auto old = map.begin();
            for (auto it = map.begin(); it != map.end(); ++it)
                if (it->second.used < old->second.used) old = it;
            if (old->second.t) SDL_DestroyTexture(old->second.t);
            map.erase(old);
        }
        map[key] = {t, tick};
        return t;
    }
    void Clear() {
        for (auto &kv : map)
            if (kv.second.t) SDL_DestroyTexture(kv.second.t);
        map.clear();
    }
};

static SDL_mutex *gImgMx = nullptr;
static SDL_cond *gImgCv = nullptr;
static SDL_Thread *gImgTh = nullptr;
static std::deque<ImgJob> gImgQ;
static std::vector<ImgDone> gImgDone;
static std::set<std::string> gImgPending;
static bool gImgQuit = false;
static Uint32 gImgSeq = 0;

static int ImgWorker(void *) {
    SDL_LockMutex(gImgMx);
    while (!gImgQuit) {
        if (gImgQ.empty()) {
            SDL_CondWait(gImgCv, gImgMx);
            continue;
        }
        // 가장 최근 프레임의 요청부터, 같은 프레임 안에서는 요청 순서대로 (보이는 칸 → 미리 읽기)
        size_t pick = 0;
        for (size_t i = 1; i < gImgQ.size(); i++)
            if (gImgQ[i].tick > gImgQ[pick].tick || (gImgQ[i].tick == gImgQ[pick].tick && gImgQ[i].seq < gImgQ[pick].seq)) pick = i;
        ImgJob j = gImgQ[pick];
        gImgQ.erase(gImgQ.begin() + pick);
        SDL_UnlockMutex(gImgMx);
        SDL_Surface *s = LoadImageFile(j.path);
        if (s && j.thumb && j.w > 0 && j.h > 0) {
            float kx = s->w / (j.w * gScale), ky = s->h / (j.h * gScale);
            s = ShrinkSurface(s, (int)(j.cover ? std::min(kx, ky) : std::max(kx, ky)));
        }
        SDL_LockMutex(gImgMx);
        gImgDone.push_back({j, s});
    }
    SDL_UnlockMutex(gImgMx);
    return 0;
}

static void ImgLoaderStart() {
    if (gImgTh) return;
    gImgMx = SDL_CreateMutex();
    gImgCv = SDL_CreateCond();
    gImgQuit = false;
    gImgTh = SDL_CreateThread(ImgWorker, "imgload", nullptr);
    gAsyncImg = gImgTh != nullptr;
}

static void ImgLoaderStop() {
    if (!gImgTh) return;
    SDL_LockMutex(gImgMx);
    gImgQuit = true;
    gImgQ.clear();
    SDL_CondSignal(gImgCv);
    SDL_UnlockMutex(gImgMx);
    SDL_WaitThread(gImgTh, nullptr);
    gImgTh = nullptr;
    gAsyncImg = false;
    for (auto &d : gImgDone)
        if (d.s) SDL_FreeSurface(d.s);
    gImgDone.clear();
    gImgPending.clear();
}

static void ImgRequest(const ImgJob &job) {
    if (!gImgTh) return;
    SDL_LockMutex(gImgMx);
    if (!gImgPending.count(job.key)) {
        ImgJob j = job;
        j.tick = gFrameTick;
        j.seq = ++gImgSeq;
        gImgPending.insert(j.key);
        gImgQ.push_back(j);
        // 너무 쌓이면 가장 오래된 요청을 버림 (빠르게 넘긴 동안의 칸)
        while (gImgQ.size() > 48) {
            size_t old = 0;
            for (size_t i = 1; i < gImgQ.size(); i++)
                if (gImgQ[i].tick < gImgQ[old].tick || (gImgQ[i].tick == gImgQ[old].tick && gImgQ[i].seq < gImgQ[old].seq)) old = i;
            gImgPending.erase(gImgQ[old].key);
            gImgQ.erase(gImgQ.begin() + old);
        }
        SDL_CondSignal(gImgCv);
    }
    SDL_UnlockMutex(gImgMx);
}

// 매 프레임: 다 읽은 그림을 텍스처로 올려 캐시에 넣는다 (한 프레임에 너무 많이 올리지 않게 최대 6장)
static void ImgPump(SDL_Renderer *r) {
    gFrameTick++;
    if (!gImgTh) return;
    std::vector<ImgDone> done;
    SDL_LockMutex(gImgMx);
    size_t n = std::min<size_t>(gImgDone.size(), 6);
    done.assign(gImgDone.begin(), gImgDone.begin() + n);
    gImgDone.erase(gImgDone.begin(), gImgDone.begin() + n);
    for (const auto &d : done) gImgPending.erase(d.job.key);
    SDL_UnlockMutex(gImgMx);
    for (auto &d : done) {
        SDL_Texture *t = d.s ? SDL_CreateTextureFromSurface(r, d.s) : nullptr;
        if (d.s) SDL_FreeSurface(d.s);
        if (d.job.thumb) ((ThumbCache *)d.job.cache)->Insert(d.job.key, t);
        else ((ImageCache *)d.job.cache)->Insert(d.job.path, t);
    }
}

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
static Uint32 gThemeSerial = 0;
static bool ButtonIconsOn() { return gTheme.layout.buttonIcons; }  // 테마를 바꿀 때마다 +1 (글꼴·그림 캐시 비우기용)

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

// 폴더 통째로 지우기 (테마 삭제). 성공하면 true
static bool RemoveTree(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return remove(path.c_str()) == 0;
    bool ok = true;
    if (DIR *d = opendir(path.c_str())) {
        std::vector<std::string> names;
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != "..") names.push_back(n);
        }
        closedir(d);
        for (const auto &n : names) ok = RemoveTree(path + "/" + n) && ok;
    }
    return rmdir(path.c_str()) == 0 && ok;
}

// 런처와 함께 배포하는 테마 팩 (릴리스의 <폴더>.zip, 테마 선택 창에서 내려받기)
struct ThemePack {
    const char *folder, *ko, *en;
    const char *oldKo, *oldEn;  // 1.0.0 의 이름: SD 에 남은 예전 theme.txt 의 name 을 지금 이름으로 보여 준다
};
static const ThemePack kPacks[] = {
    {"JoyBasic", "기본테마", "Basic Theme", "기본팩", "Basic Pack"},
    {"RetroPack", "레트로테마", "Retro Theme", "레트로팩", "Retro Pack"},
    {"ByeokgolKim", "벽골김씨테마", "WallStreetKim's Theme", "벽골김씨팩", "Byeokgol Kim Pack"},
    {"DcGall", "레트로게임갤러리테마", "Retro Game Gallery Theme", "디시 갤러리 (시험)", "DC Gallery (test)"},
};
static const int kPackCount = (int)(sizeof(kPacks) / sizeof(kPacks[0]));

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
    SetStringOverrides(nullptr, nullptr);  // gTheme 를 바꾸기 전에 (문구 표가 사라짐)
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
    if (!gTheme.layout.minVersion.empty() && CompareVersions(gTheme.layout.minVersion, std::string(CurrentVersion()).substr(0, std::string(CurrentVersion()).find('-'))) > 0)
        gTheme.layout.problems.insert(gTheme.layout.problems.begin(),
                                      Fmt(T("이 테마는 Joy Launcher v%s 이상이 필요합니다 (지금 v%s)"), gTheme.layout.minVersion.c_str(), CurrentVersion()));
    SetStringOverrides(&gTheme.layout.strings, &gTheme.layout.stringsEn);
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
    ApplyBgm();  // 소리 선택이 테마 BGM 이면 이 테마의 곡으로
}

// 내 그림 폴더: /JoyLauncher/categories/{backgrounds,logos,icons}/<이름>.png|jpg — 테마를 바꿔도 쓰는 카테고리 그림
// (폴더 바로 안의 그림은 아이콘으로). 테마에 그림이 없을 때 찾는다 (나만의 카테고리는 기종 그림보다 먼저)
static std::map<std::string, std::map<std::string, std::string>> gUserImg;
static void LoadUserImages() {
    gUserImg.clear();
    for (const char *k : {"backgrounds", "logos", "icons"}) IndexImages(gHome + "/categories/" + k, gUserImg[k]);
    IndexImages(gHome + "/categories", gUserImg["icons"]);
}
static std::string UserFind(const char *kind, const Collection *c) {
    if (!kind || !c) return "";
    auto it = gUserImg.find(kind);
    if (it == gUserImg.end()) return "";
    for (const std::string &k : {c->shortName, c->name}) {
        if (k.empty()) continue;
        auto f = it->second.find(LowerAscii(k));
        if (f != it->second.end()) return f->second;
    }
    return "";
}

// kind: backgrounds / logos / icons … (내 그림 폴더에서도 찾을 때)
static std::string ThemeFind(const std::map<std::string, std::string> &m, const Collection *c, const char *kind = nullptr) {
    if (m.empty()) return UserFind(kind, c);
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
    // 앱 즐겨찾기: appfav 그림이 있으면 그것 (없으면 아래에서 스위치판 테마의 hbfav 그림을 그대로 씀)
    if (c->virt && c->shortName == "hbfav") {
        std::string af = first({alias("appfav"), direct("appfav"), alias("홈브루 즐겨찾기")});
        if (!af.empty()) return af;
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
    // 나만의 카테고리 (categories.txt): 내 그림 폴더가 포함한 기종 그림보다 먼저
    if (c->custom) {
        r = UserFind(kind, c);
        if (!r.empty()) return r;
    }
    // 내장 기종 이름 표 (FC -> nes, 액션-아케이드 -> arcade_action …): ES-DE 이름의 그림을 theme.txt 없이 찾음
    auto esde = [&](const std::string &k) -> std::string {
        if (k.empty()) return "";
        std::string id = EsdeSystemName(LowerAscii(k));
        // 에뮬레이터 표의 기종: 롬 폴더 이름이 "PS2 한글"·"Nintendo 3DS" 같은 꼴이어도 그 기종 그림을 찾는다
        if (id.empty())
            if (const EmuSystem *s = EmuSystemForFolder(k)) id = s->theme.empty() ? s->name : s->theme;
        if (id.empty()) return "";
        std::string r2 = first({alias(id), direct(id)});
        // megadrivejp → megadrive 처럼 지역판 기종은 본래 기종의 그림으로
        if (r2.empty()) {
            std::string base = EsdeArtFallback(id);
            if (!base.empty()) r2 = first({alias(base), direct(base)});
        }
        return r2;
    };
    // 4) 여러 폴더를 합친 카테고리: 그림이 있는 첫 폴더 (예: psx+psp+ps2 를 합친 "Play Station"), 폴더 이름의 기종 이름까지
    for (const auto &f : c->folders) {
        r = first({alias(f), direct(f), c->custom ? esde(f) : std::string()});
        if (!r.empty()) return r;
    }
    // 5) 내장 기종 이름 표 (FC -> nes, 액션-아케이드 -> arcade_action …): ES-DE 이름의 그림을 theme.txt 없이 찾음
    r = first({esde(c->name), esde(c->shortName)});
    if (!r.empty()) return r;
    for (const auto &f : c->folders) {
        r = esde(f);
        if (!r.empty()) return r;
    }
    return UserFind(kind, c);
}

static void SaveSettings() {
    mkdir(gHome.c_str(), 0777);
    std::ofstream o(gHome + "/settings.txt");
    o << "theme=" << gThemeName << "\n";
    o << T("# 미리보기 영상: on = 고른 게임에 영상이 있으면 화면 사진 자리에서 재생, off = 끔") << "\n";
    o << "video=" << (gVideoOn ? "on" : "off") << "\n";
    o << T("# 자동 업데이트 확인: on = 시작할 때 새 버전 확인 (설치는 옵션 창에서), off = 끔") << "\n";
    o << "auto_update=" << gAutoUpdate << "\n";
    o << T("# 언어: auto = 기기 언어, ko = 한국어, en = English") << "\n";
    o << "language=" << gLanguage << "\n";
    o << T("# 소리 (하나만 남): common = 공통 BGM (/JoyLauncher/bgm 의 mp3·ogg), theme = 테마 BGM (지금 테마 폴더의 bgm), video = 미리보기 영상의 소리, off = 무음") << "\n";
    o << "sound=" << gSound << "\n";
    o << T("# BGM: bgm_volume = 0~100, bgm_shuffle = on/off (무작위)") << "\n";
    o << "bgm_volume=" << gBgmVolume << "\n";
    o << "bgm_shuffle=" << (gBgmShuffle ? "on" : "off") << "\n";
    o << T("# 화면 효과: on = 테마의 움직임·화면 전환 사용, off = 끔") << "\n";
    o << "effects=" << (gEffects ? "on" : "off") << "\n";
    o << T("# A·B 버튼 바꾸기: off = A 선택·B 취소, on = B 선택·A 취소") << "\n";
    o << "swap_ab=" << (gSwapAB ? "on" : "off") << "\n";
    o << T("# 화면 컨트롤러(터치): auto = 컨트롤러가 없을 때, on = 항상, off = 끔") << "\n";
    o << "overlay=" << gOverlayMode << "\n";
}

static void LoadSettings() {
    std::ifstream in(gHome + "/settings.txt");
    std::string line, theme;
    bool found = false, haveSound = false;
    int oldBgm = -1, oldVideoSound = -1;  // 1.1 까지의 설정 (bgm=on/off, video_sound=on/off)
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("theme=", 0) == 0) { theme = line.substr(6); found = true; }
        if (line.rfind("sound=", 0) == 0) {
            std::string v = line.substr(6);
            if (v == "common" || v == "theme" || v == "video" || v == "off") { gSound = v; haveSound = true; }
        }
        if (line.rfind("video=", 0) == 0) gVideoOn = line.substr(6) != "off";
        if (line.rfind("video_sound=", 0) == 0) oldVideoSound = line.substr(12) != "off";
        if (line.rfind("auto_update=", 0) == 0) gAutoUpdate = line.substr(12) == "off" ? "off" : "on";
        if (line.rfind("language=", 0) == 0) {
            std::string v = line.substr(9);
            gLanguage = v == "ko" || v == "en" ? v : "auto";
        }
        if (line.rfind("bgm=", 0) == 0) oldBgm = line.substr(4) == "on";
        if (line.rfind("bgm_volume=", 0) == 0) gBgmVolume = std::max(0, std::min(100, atoi(line.c_str() + 11)));
        if (line.rfind("bgm_shuffle=", 0) == 0) gBgmShuffle = line.substr(12) == "on";
        if (line.rfind("effects=", 0) == 0) gEffects = line.substr(8) != "off";
        if (line.rfind("swap_ab=", 0) == 0) gSwapAB = line.substr(8) == "on";
        if (line.rfind("overlay=", 0) == 0) {
            std::string v = line.substr(8);
            gOverlayMode = v == "on" || v == "off" ? v : "auto";
        }
    }
    // 예전 설정을 소리 선택으로: BGM 을 켜 두었으면 공통 BGM, 영상 소리를 꺼 두었으면 무음
    if (!haveSound && (oldBgm >= 0 || oldVideoSound >= 0)) gSound = oldBgm == 1 ? "common" : oldVideoSound == 0 ? "off" : "video";
    ApplyLanguage();
    // 설정이 없으면 기본 테마 JoyBasic (없으면 첫 번째 테마)
    if (!found) {
        auto names = ThemeNames();
        if (std::find(names.begin(), names.end(), "JoyBasic") != names.end()) theme = "JoyBasic";
        else if (!names.empty()) theme = names[0];
    }
    LoadTheme(theme);
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
               "  backgrounds/<롬 폴더 이름>.jpg|png   카테고리 배경 (예: backgrounds/GBA.jpg -> 롬 폴더 GBA)\r\n"
               "  backgrounds/favorites.jpg            즐겨찾기 카테고리 배경 (앱 즐겨찾기는 hbfav 또는 appfav)\r\n"
               "  backgrounds/default.jpg              \"게임 목록이 없습니다\" 화면 배경\r\n"
               "  logos/<롬 폴더 이름>.png             게임 목록 제목 대신 표시할 로고 (선택)\r\n"
               "  theme.txt                            dim=0~255 (배경 어둡게), <카테고리 이름>=<그림 파일 이름>\r\n"
               "  bgm/<곡>.mp3|ogg                     테마 BGM (선택): BGM · 소리 설정에서 \"테마 BGM\" 을 고르면 이 테마를 쓰는 동안 재생\r\n"
               "배경이 없는 카테고리는 테마를 끈 화면으로 보입니다.\r\n"
               "폴더·파일 이름은 영문(ASCII)을 권장합니다. Y 옵션 > 테마 설정에서 고릅니다.\r\n"
               "  layout.txt                           화면 구성 바꾸기 (격자, 위치, 글꼴·색): 테마_레이아웃.md\r\n"
               "스위치판 Joy Launcher 의 테마를 그대로 넣어 쓸 수 있습니다 (문법이 같습니다).\r\n"
               "자세한 방법: github.com/JyJyLab/JoyLauncher/blob/main/docs/테마_만들기.md\r\n"
               "\r\n"
               "[English]\r\n"
               "One theme = one folder in here, e.g. themes/MyTheme/\r\n"
               "  backgrounds/<rom folder>.jpg|png   category background (e.g. backgrounds/GBA.jpg -> ROM folder GBA)\r\n"
               "  backgrounds/favorites.jpg          Favorites category background (App Favorites: hbfav or appfav)\r\n"
               "  backgrounds/default.jpg            background of the \"No games found\" screen\r\n"
               "  logos/<rom folder>.png             logo shown instead of the game list title (optional)\r\n"
               "  theme.txt                          dim=0-255 (darken backgrounds), <category name>=<image file name>\r\n"
               "  bgm/<song>.mp3|ogg                 theme BGM (optional): plays while this theme is in use when \"Theme BGM\" is chosen in BGM / sound\r\n"
               "Categories without a background look like the no-theme screen.\r\n"
               "Use ASCII folder and file names. Pick a theme in Y Options > Theme.\r\n"
               "  layout.txt                         change the screen layout (grids, positions, fonts, colors): theme-layout-en.md\r\n"
               "Themes made for the Switch version of Joy Launcher work as they are (same syntax).\r\n"
               "Details: github.com/JyJyLab/JoyLauncher/blob/main/docs/theme-guide-en.md\r\n");
    WriteGuide(gHome + "/bgm",
               "Joy Launcher BGM 폴더 / BGM folder\r\n"
               "\r\n"
               "[한국어]\r\n"
               "이 폴더에 mp3 또는 ogg 파일을 넣으면 런처 화면에서 배경 음악(공통 BGM)으로 재생합니다.\r\n"
               "X 버튼(또는 Y 옵션)의 \"BGM · 소리 설정\" 에서 소리를 고릅니다: 공통 BGM / 테마 BGM / 영상 소리 / 무음.\r\n"
               "  공통 BGM = 이 폴더의 곡,  테마 BGM = 테마 폴더 안 bgm 폴더의 곡 (themes/<테마>/bgm)\r\n"
               "볼륨과 순서(차례대로/무작위)도 같은 창에서 바꿉니다.\r\n"
               "파일 이름은 영문(ASCII)을 권장합니다. 런처에는 음악이 들어 있지 않습니다.\r\n"
               "\r\n"
               "[English]\r\n"
               "Put mp3 or ogg files in this folder to play them as background music (Common BGM) in the launcher.\r\n"
               "Choose the sound in \"BGM / sound\" (X button, or Y Options): Common BGM / Theme BGM / Video sound / Silent.\r\n"
               "  Common BGM = the songs in this folder,  Theme BGM = the songs in the bgm folder of the theme (themes/<theme>/bgm)\r\n"
               "Volume and order (in order / shuffle) are in the same window.\r\n"
               "Use ASCII file names. No music is included with the launcher.\r\n");
    // 롬 폴더 목록: 없을 때만 설명이 든 빈 파일을 만든다 (사용자가 고친 파일은 건드리지 않음)
    std::string roots = gHome + "/roots.txt";
    if (!std::ifstream(roots).good()) {
        std::ofstream o(roots, std::ios::binary);
        o << "# Joy Launcher 롬 폴더 / ROM folders\r\n"
             "#\r\n"
             "# [한국어] 한 줄에 폴더 하나 (예: /storage/emulated/0/Roms). 적지 않아도 저장소마다 ROMs·Roms·roms 폴더,\r\n"
             "#   페가수스의 game_dirs.txt, ES-DE 의 롬 폴더를 자동으로 찾습니다.\r\n"
             "#   auto=off                              자동으로 찾지 않고 여기 적은 폴더만 씀\r\n"
             "#   esde=/storage/emulated/0/ES-DE        ES-DE 폴더 (gamelists · downloaded_media) 위치\r\n"
             "# [English] One folder per line. Without any line the launcher finds ROMs / Roms / roms on every storage,\r\n"
             "#   the folders in Pegasus' game_dirs.txt and the ES-DE ROM folder by itself.\r\n"
             "#   auto=off   use only the folders listed here      esde=<path>   where the ES-DE folder is\r\n";
    }
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
    bool optPick = false;        // 옵션 창 (Y): 새로 고침·테마·BGM·언어·앱 목록·버튼 설정 등
    int optSel = 0;
    bool bgmPick = false;        // BGM 창 (옵션 창 위에 한 겹 더)
    int bgmSel = 0;
    bool appPick = false;        // 앱 목록 창 (X, 또는 옵션 창에서): 설치된 앱 실행, 앱 즐겨찾기에 넣고 빼기
    int appSel = 0;
    std::vector<Game> appList;   // 실행할 수 있는 앱 전부
    bool emuPick = false;        // 에뮬레이터 선택 창 (옵션 창 위에 한 겹 더)
    int emuSel = 0;
    std::vector<EmuChoice> emuList;
    std::string emuKey, emuSystem;  // 고른 것을 적어 둘 이름(롬 폴더·카테고리), 그 기종
    bool exitAsk = false;        // 맨 위 화면에서 B: 종료 확인
    bool updLink = false;        // 업데이트 창: 새 버전 알림 + 릴리스 페이지 주소 (A = 브라우저로 열기)
    std::string updErr;          // 업데이트 확인 실패 이유 (있으면 창에 표시)
    std::vector<std::string> themeList;  // [0] = 기본, 그다음 테마 폴더 이름, 끝에 내려받기 항목
    std::vector<std::string> themeTitles;  // themeList 와 같은 순서의 화면 이름
    int themeSel = 0;
    int themeDelAsk = -1;        // 테마 선택 창: 지울지 묻는 중인 줄 (-1 = 아님)
    bool ftpOpen = false;        // PC에서 테마 받기 (FTP) 화면
    std::vector<Collection> cols;
    Mode mode = Mode::Categories;
    int catSel = 0, catTop = 0;  // 카테고리 화면 선택 (cols + 메뉴)
    int col = 0, sel = 0, top = 0;
    Uint32 selChanged = 0;       // 선택 변경 시각 (박스아트 지연 로드)
    std::string message;
    Uint32 messageUntil = 0;
    bool messageModal = false;  // B 로 닫을 때까지 떠 있는 알림 (그동안 다른 입력 무시)
    SDL_Color messageColor = {120, 40, 40, 240};
    // 보기 탭 (게임 화면, ZL/ZR): 0 전체 / 1 즐겨찾기 / 2 최근 실행
    int view = 0, viewFor = -1;
    Collection viewCol;
    unsigned viewSig = 0;
};

// 게임 화면이 보는 목록: 보기가 "전체"면 카테고리 그대로, 아니면 걸러 만든 목록
static const Collection &CurCol(App &a) {
    static Collection empty;
    if (a.cols.empty()) return empty;
    if (a.viewFor != a.col) { a.view = 0; a.viewFor = a.col; }
    const Collection &src = a.cols[a.col];
    if (a.view == 0) return src;
    unsigned sig = gViewVersion * 131u + (unsigned)a.col * 7u + (unsigned)a.view + (unsigned)src.games.size() * 1009u;
    if (a.viewSig != sig || a.viewCol.name != src.name) {
        Collection &v = a.viewCol;
        v.name = src.name;
        v.sortBy = src.sortBy;
        v.shortName = src.shortName;
        v.virt = src.virt;
        v.custom = src.custom;
        v.folders = src.folders;
        v.games.clear();
        if (a.view == 1) {
            for (const auto &g : src.games)
                if (gFavSet.count(g.file) || src.shortName == "hbfav" || (src.virt && src.shortName == "favorites")) v.games.push_back(g);
        } else {
            std::vector<std::pair<long long, const Game *>> played;
            for (const auto &g : src.games) {
                auto it = gPlay.find(g.file);
                if (it != gPlay.end() && it->second.count > 0) played.push_back({it->second.last, &g});
            }
            std::stable_sort(played.begin(), played.end(), [](const std::pair<long long, const Game *> &x, const std::pair<long long, const Game *> &y) { return x.first > y.first; });
            for (const auto &p : played) v.games.push_back(*p.second);
        }
        a.viewSig = sig;
    }
    return a.viewCol;
}
static const char *ViewName(int v) { return v == 1 ? T("★ 즐겨찾기") : v == 2 ? T("최근 실행") : ""; }

// ---------------------------------------------------------------- 나만의 카테고리 (/JoyLauncher/categories.txt)
//   [이름]                    화면에 보일 카테고리 이름
//   image = playstation       그림 이름 (테마 → 내 그림 폴더 /JoyLauncher/categories → 포함한 기종 그림 순)
//   include = PS1, PS2, PSP   롬 폴더·카테고리 이름 (쉼표, * = 모두)
//   exclude = …               뺄 롬 폴더·카테고리
//   genre = RPG, 액션         장르에 이 글이 들어간 게임만 (쉼표)
//   games = mypick.txt        롬 경로 목록 파일 (/JoyLauncher 기준, 즐겨찾기와 같은 형식)
//   hide_included = on        묶은 원래 카테고리를 목록에서 숨김 (기본 on, genre 를 쓰면 숨기지 않음)
//   position = top | bottom | after:<카테고리 이름>
//   [hide]                    목록에서 숨길 카테고리 (한 줄에 하나)
struct CustomCat {
    std::string name, image, gamesFile, position = "top";
    std::vector<std::string> include, exclude, genres;
    bool hideIncluded = true;
};
static std::vector<CustomCat> gCustomCats;
static std::vector<std::string> gHideCats;
static std::vector<std::string> SplitList(const std::string &v) {
    std::vector<std::string> out;
    std::stringstream ss(v);
    for (std::string p; std::getline(ss, p, ',');) {
        size_t a = p.find_first_not_of(" \t"), b = p.find_last_not_of(" \t\r");
        if (a != std::string::npos) out.push_back(p.substr(a, b - a + 1));
    }
    return out;
}
static void LoadCustomCategories() {
    gCustomCats.clear();
    gHideCats.clear();
    std::ifstream in(gHome + "/categories.txt");
    std::string line, section;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        first = false;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t lead = line.find_first_not_of(" \t");
        if (lead == std::string::npos) continue;
        line = line.substr(lead);
        if (line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1);
            if (LowerAscii(section) != "hide") {
                gCustomCats.push_back(CustomCat());
                gCustomCats.back().name = section;
            }
            continue;
        }
        if (LowerAscii(section) == "hide") {
            for (const auto &h : SplitList(line)) gHideCats.push_back(h);
            continue;
        }
        if (gCustomCats.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = LowerAscii(line.substr(0, line.find_last_not_of(" \t", eq - 1) + 1)), v = line.substr(eq + 1);
        size_t vs = v.find_first_not_of(" \t");
        v = vs == std::string::npos ? "" : v.substr(vs);
        CustomCat &cc = gCustomCats.back();
        if (k == "image") cc.image = v;
        else if (k == "include") cc.include = SplitList(v);
        else if (k == "exclude") cc.exclude = SplitList(v);
        else if (k == "genre") cc.genres = SplitList(v);
        else if (k == "games") cc.gamesFile = v;
        else if (k == "hide_included") cc.hideIncluded = LowerAscii(v) != "off" && v != "0" && LowerAscii(v) != "no";
        else if (k == "position") cc.position = v;
    }
}
static bool CatMatch(const Collection &c, const std::string &key) {
    if (key == "*") return true;
    std::string k = LowerAscii(key);
    if (LowerAscii(c.name) == k || LowerAscii(T(c.name)) == k || (!c.shortName.empty() && LowerAscii(c.shortName) == k)) return true;
    for (const auto &f : c.folders)
        if (LowerAscii(f) == k) return true;
    return false;
}
static void ApplyCustomCategories(std::vector<Collection> &cols) {
    if (gCustomCats.empty() && gHideCats.empty()) return;
    std::set<size_t> hide;
    std::vector<Collection> made;
    std::map<std::string, const Game *> byFile;
    for (const auto &cc : gCustomCats) {
        Collection n;
        n.name = cc.name;
        n.sortBy = cc.name;
        n.shortName = cc.image.empty() ? cc.name : cc.image;
        n.custom = true;
        std::set<std::string> seen;
        bool all = std::find(cc.include.begin(), cc.include.end(), "*") != cc.include.end();
        // include 에 적은 순서대로 (그림을 대신할 첫 기종 = 처음 적은 것)
        std::vector<std::pair<size_t, size_t>> picks;
        for (size_t i = 0; i < cols.size(); i++) {
            const Collection &c = cols[i];
            if (c.virt || c.custom) continue;
            size_t at = cc.include.size();
            bool out = false;
            for (size_t k = 0; k < cc.include.size(); k++)
                if (at == cc.include.size() && CatMatch(c, cc.include[k])) at = k;
            for (const auto &k : cc.exclude) out = out || CatMatch(c, k);
            if (at < cc.include.size() && !out) picks.push_back({at, i});
        }
        std::stable_sort(picks.begin(), picks.end());
        for (const auto &pk : picks) {
            size_t i = pk.second;
            const Collection &c = cols[i];
            for (const auto &f : c.folders)
                if (std::find(n.folders.begin(), n.folders.end(), f) == n.folders.end()) n.folders.push_back(f);
            for (const auto &g : c.games) {
                if (!cc.genres.empty()) {
                    bool ok = false;
                    for (const auto &ge : cc.genres) ok = ok || LowerAscii(g.genre).find(LowerAscii(ge)) != std::string::npos;
                    if (!ok) continue;
                }
                if (seen.insert(g.file).second) n.games.push_back(g);
            }
            if (cc.hideIncluded && cc.genres.empty() && !all) hide.insert(i);
        }
        if (!cc.gamesFile.empty()) {
            if (byFile.empty())
                for (const auto &c : cols)
                    for (const auto &g : c.games) byFile[g.file] = &g;
            std::ifstream gl(gHome + "/" + cc.gamesFile);
            for (std::string l; std::getline(gl, l);) {
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                auto it = byFile.find(l);
                if (it != byFile.end() && seen.insert(l).second) {
                    n.games.push_back(*it->second);
                    for (const auto &c : cols)
                        if (&c.games.front() <= it->second && it->second <= &c.games.back())
                            for (const auto &f : c.folders)
                                if (std::find(n.folders.begin(), n.folders.end(), f) == n.folders.end()) n.folders.push_back(f);
                }
            }
        }
        std::stable_sort(n.games.begin(), n.games.end(), [](const Game &x, const Game &y) {
            return LowerAscii(x.sortKey.empty() ? x.title : x.sortKey) < LowerAscii(y.sortKey.empty() ? y.title : y.sortKey);
        });
        made.push_back(std::move(n));
    }
    for (size_t i = 0; i < cols.size(); i++)
        for (const auto &h : gHideCats)
            if (CatMatch(cols[i], h)) hide.insert(i);
    std::vector<Collection> rest;
    for (size_t i = 0; i < cols.size(); i++)
        if (!hide.count(i)) rest.push_back(std::move(cols[i]));
    int topAt = 0;
    for (size_t k = 0; k < made.size(); k++) {
        const std::string &pos = gCustomCats[k].position;
        if (made[k].games.empty()) continue;  // 게임이 없는 카테고리는 만들지 않음
        if (LowerAscii(pos) == "bottom") {
            rest.push_back(std::move(made[k]));
        } else if (LowerAscii(pos).rfind("after:", 0) == 0) {
            std::string target = pos.substr(6);
            size_t at = rest.size();
            for (size_t i = 0; i < rest.size(); i++)
                if (CatMatch(rest[i], target)) at = i + 1;
            rest.insert(rest.begin() + at, std::move(made[k]));
        } else {
            rest.insert(rest.begin() + topAt++, std::move(made[k]));
        }
    }
    cols = std::move(rest);
}
#ifdef JOY_BUILDER
static std::vector<Collection> gBaseCols;  // 테마 빌더: 나만의 카테고리 다시 적용용 원래 목록
#endif

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

// size: 1280x720 화면 기준 글자 크기 (실제로는 기기 배율만큼 크게 연다)
static TTF_Font *OpenFontMem(std::vector<char> &buf, int size) {
    return TTF_OpenFontRW(SDL_RWFromConstMem(buf.data(), (int)buf.size()), 1, ToPx(size));
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
// 지금 그리는 것 전체의 불투명도 (캐러셀 양옆 칸 흐리게): 글자·네모·그림에 곱함
static int gAlpha = 255;
static void AlphaMod(SDL_Texture *t, bool on) {
    if (gAlpha >= 255 || !t) return;
    static Uint8 saved = 255;
    if (on) {
        SDL_GetTextureAlphaMod(t, &saved);
        SDL_SetTextureAlphaMod(t, (Uint8)(saved * gAlpha / 255));
    } else {
        SDL_SetTextureAlphaMod(t, saved);
    }
}

// 테마 빌더: 요소마다 실제로 그린 자리 모으기 (선택·끌기용)
static bool gTrackBox = false;
static SDL_Rect gDrawBox = {0, 0, 0, 0};
static void TrackBox(const SDL_Rect &r) {
    if (!gTrackBox || r.w <= 0 || r.h <= 0) return;
    if (gDrawBox.w <= 0 || gDrawBox.h <= 0) { gDrawBox = r; return; }
    SDL_Rect u;
    SDL_UnionRect(&gDrawBox, &r, &u);
    gDrawBox = u;
}

// ---- 도움말의 버튼 표시: "[A]" "[ZL]" "[-]" "[L/R]" … 를 스위치 버튼 모양 그림으로
// A·B·X·Y·+·− 는 동그라미, L·R·ZL·ZR 은 둥근 막대. 글자 부분은 비워(투명) 어느 바탕에서도 읽힌다
static TTF_Font *FontRaw(bool bold, int size, bool theme);
static bool ButtonIconsOn();
static bool IsButtonLabel(const std::string &b) {
    static const char *k[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "-", "−", "START", "SELECT"};
    for (const char *x : k)
        if (b == x) return true;
    return false;
}
// 도움말의 버튼 이름을 지금 상황에 맞게: 닌텐도 계열이 아닌 컨트롤러면 [+] [-] → [START] [SELECT],
// A·B 바꾸기를 켰으면 [A] ↔ [B]. 테마가 적은 도움말(스위치판과 같은 글)에도 그대로 적용된다
static std::string PadHint(const std::string &s) {
    if ((!gPadStartSelect && !gSwapAB) || s.find('[') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t e = s[i] == '[' ? s.find(']', i) : std::string::npos;
        if (e == std::string::npos || e - i > 8) { out += s[i++]; continue; }
        std::stringstream in(s.substr(i + 1, e - i - 1));
        std::string rep, part;
        bool ok = true, first = true;
        while (std::getline(in, part, '/')) {
            if (!IsButtonLabel(part)) { ok = false; break; }
            if (gPadStartSelect && part == "+") part = "START";
            else if (gPadStartSelect && (part == "-" || part == "−")) part = "SELECT";
            else if (gSwapAB && part == "A") part = "B";
            else if (gSwapAB && part == "B") part = "A";
            rep += (first ? "" : "/") + part;
            first = false;
        }
        if (!ok || first) { out += s[i++]; continue; }
        out += "[" + rep + "]";
        i = e + 1;
    }
    return out;
}
// 글자를 [버튼] 과 나머지로 나눔. 버튼이 하나도 없으면 false
struct HintPart { std::string text; bool button; };
static bool SplitButtons(const std::string &s, std::vector<HintPart> &out) {
    out.clear();
    bool any = false;
    size_t i = 0;
    std::string cur;
    while (i < s.size()) {
        size_t e = s[i] == '[' ? s.find(']', i) : std::string::npos;
        if (e != std::string::npos && e - i <= 8) {
            std::string in = s.substr(i + 1, e - i - 1);
            std::vector<std::string> keys;
            size_t sl = in.find('/');
            if (sl != std::string::npos) keys = {in.substr(0, sl), in.substr(sl + 1)};
            else keys = {in};
            bool ok = true;
            for (const auto &k : keys) ok = ok && IsButtonLabel(k);
            if (ok) {
                if (!cur.empty()) out.push_back({cur, false});
                cur.clear();
                for (const auto &k : keys) out.push_back({k == "-" ? "−" : k, true});
                any = true;
                i = e + 1;
                continue;
            }
        }
        cur += s[i++];
    }
    if (!cur.empty()) out.push_back({cur, false});
    return any;
}
// 버튼 그림 (색 c, 높이 lh: 화면 좌표). 그림은 실제 점으로 만든다. TextCache 에 함께 둠
static SDL_Texture *ButtonTexture(App &a, const std::string &label, int lh, SDL_Color c) {
    char key[64];
    snprintf(key, sizeof(key), "\x01btn|%d|%02x%02x%02x|", lh, c.r, c.g, c.b);
    int h = ToPx(lh);
    std::string k = key + label;
    auto it = a.text.map.find(k);
    if (it != a.text.map.end()) return it->second;
    bool round = label == "A" || label == "B" || label == "X" || label == "Y" || label == "+" || label == "−";
    bool word = label == "START" || label == "SELECT";  // 글자가 긴 버튼: 글자 폭에 맞춘 둥근 막대
    int w = round ? h : (int)(h * (label.size() > 1 ? 1.8f : 1.5f));
    int fs = std::max(8, (int)(lh * (round ? 0.78f : word ? 0.56f : 0.7f)));  // FontRaw 가 배율을 곱해 연다
    if (word)
        if (TTF_Font *wf = FontRaw(true, fs, false)) {
            int tw = 0;
            if (TTF_SizeUTF8(wf, label.c_str(), &tw, nullptr) == 0) w = tw + (int)(h * 0.72f);
        }
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!sf) return nullptr;
    // 모양: 동그라미 / 둥근 막대 (가장자리 부드럽게, 4x4 표본)
    float r = h / 2.0f;
    for (int y = 0; y < h; y++) {
        Uint8 *row = (Uint8 *)sf->pixels + y * sf->pitch;
        for (int x = 0; x < w; x++) {
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    float cx = std::min(std::max(px, r), w - r);  // 막대: 가운데 부분은 곧게
                    float dx = px - cx, dy = py - r;
                    if (dx * dx + dy * dy <= r * r) in++;
                }
            row[x * 4 + 0] = c.r;
            row[x * 4 + 1] = c.g;
            row[x * 4 + 2] = c.b;
            row[x * 4 + 3] = (Uint8)(c.a * in / 16);
        }
    }
    // + · − 는 직접 그림 (글꼴마다 모양·높이가 달라서)
    if (label == "+" || label == "−") {
        int len = (int)(h * 0.56f), th = std::max(2, (int)(h * 0.14f + 0.5f));
        auto cut = [&](int x0, int y0, int ww, int hh) {
            for (int y = y0; y < y0 + hh; y++)
                for (int x = x0; x < x0 + ww; x++)
                    if (x >= 0 && y >= 0 && x < w && y < h) ((Uint8 *)sf->pixels)[y * sf->pitch + x * 4 + 3] = 0;
        };
        cut((w - len) / 2, (h - th) / 2, len, th);
        if (label == "+") cut((w - th) / 2, (h - len) / 2, th, len);
    }
    // 글자 자리는 비움 (내장 굵은 글꼴)
    if (label != "+" && label != "−")
    if (TTF_Font *lf = FontRaw(true, fs, false)) {
        SDL_Surface *ls = TTF_RenderUTF8_Blended(lf, label.c_str(), SDL_Color{255, 255, 255, 255});
        if (ls) {
            SDL_Surface *lc = SDL_ConvertSurfaceFormat(ls, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(ls);
            if (lc) {
                int minx, maxx, miny, maxy, adv;
                int ox = (w - lc->w) / 2, oy = (h - lc->h) / 2;
                // 대문자 높이 기준으로 세로 가운데 (글꼴의 아래 여백 보정)
                Uint32 ch = (unsigned char)label[0];
                if (TTF_GlyphMetrics32(lf, ch, &minx, &maxx, &miny, &maxy, &adv) == 0)
                    oy = (h - (maxy - miny)) / 2 - (TTF_FontAscent(lf) - maxy);
                for (int y = 0; y < lc->h; y++) {
                    int ty = y + oy;
                    if (ty < 0 || ty >= h) continue;
                    const Uint8 *src = (const Uint8 *)lc->pixels + y * lc->pitch;
                    Uint8 *dst = (Uint8 *)sf->pixels + ty * sf->pitch;
                    for (int x = 0; x < lc->w; x++) {
                        int tx = x + ox;
                        if (tx < 0 || tx >= w) continue;
                        dst[tx * 4 + 3] = (Uint8)(dst[tx * 4 + 3] * (255 - src[x * 4 + 3]) / 255);
                    }
                }
                SDL_FreeSurface(lc);
            }
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(a.r, sf);
    SDL_FreeSurface(sf);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    if (a.text.map.size() > 800) a.text.Clear();
    a.text.map[k] = t;
    return t;
}
// 버튼 그림이 섞인 한 줄. 다 들어가지 않으면 false (보통 글자로 그림)
static bool DrawTextButtons(App &a, TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxW, int align) {
    std::vector<HintPart> parts;
    if (!SplitButtons(s, parts)) return false;
    int fh = FontH(f), asc = FontAsc(f);
    int bh = std::max(12, asc * 100 / 100);             // 버튼 높이 ≈ 글자 윗선~바닥선
    int by = y + asc - bh + std::max(1, bh / 10);       // 글자 바닥선보다 조금 아래까지
    int gap = std::max(2, bh / 4);
    struct Piece { SDL_Texture *t; int w, h; bool button; };
    std::vector<Piece> ps;
    int total = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        SDL_Texture *t;
        if (parts[i].button) t = ButtonTexture(a, parts[i].text, bh, c);
        else {
            // 버튼 바로 뒤 공백 하나는 버튼 간격으로
            std::string tx = parts[i].text;
            if (i > 0 && parts[i - 1].button && !tx.empty() && tx[0] == ' ') tx.erase(0, 1);
            if (tx.empty()) continue;
            t = a.text.Get(f, tx, c);
        }
        if (!t) return false;
        int w, h;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        w = FromPx(w);
        h = FromPx(h);
        if (!ps.empty()) total += parts[i].button && ps.back().button ? gap / 2 : gap;
        ps.push_back({t, w, h, parts[i].button});
        total += w;
    }
    if (maxW > 0 && total > maxW) return false;
    int dx = align == 1 ? x - total / 2 : align == 2 ? x - total : x;
    int cx = dx;
    for (size_t i = 0; i < ps.size(); i++) {
        if (i > 0) cx += ps[i].button && ps[i - 1].button ? gap / 2 : gap;
        AlphaMod(ps[i].t, true);
        CopyPx(a.r, ps[i].t, nullptr, cx, ps[i].button ? by : y);
        AlphaMod(ps[i].t, false);
        cx += ps[i].w;
    }
    TrackBox({dx, y, total, fh});
    return true;
}

static void DrawText(App &a, TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxW = 0,
                     int align = 0, int wrap = 0, int clipH = 0, int scroll = 0) {
    if (wrap == 0 && clipH == 0 && s.find('[') != std::string::npos && ButtonIconsOn() &&
        DrawTextButtons(a, f, s, x, y, c, maxW, align))
        return;
    SDL_Texture *t = a.text.Get(f, s, c, wrap);
    if (!t) return;
    // pw·ph·src 는 실제 점, 그 밖은 화면 좌표
    int pw, ph;
    SDL_QueryTexture(t, nullptr, nullptr, &pw, &ph);
    int tw = FromPx(pw), th = FromPx(ph);
    SDL_Rect src = {0, 0, pw, ph};
    SDL_Texture *ell = nullptr;
    int ellW = 0, boxW = tw, drawW = tw, drawH = th;
    if (maxW > 0 && tw > maxW) {
        boxW = drawW = maxW;
        src.w = std::min(pw, ToPx(maxW));
        if (wrap == 0 && scroll > 0) {
            src.x = ToPx(MarqueeOffset(s + "@" + std::to_string(x) + "," + std::to_string(y), tw - maxW, scroll));
            src.x = std::max(0, std::min(src.x, pw - src.w));
        } else if (wrap == 0 && (ell = a.text.Get(f, "…", c))) {
            int epw;
            SDL_QueryTexture(ell, nullptr, nullptr, &epw, nullptr);
            ellW = FromPx(epw);
            drawW = std::max(0, maxW - ellW);
            src.w = std::min(pw, ToPx(drawW));
        }
    }
    if (clipH > 0 && th > clipH) {
        int ls = TTF_FontLineSkip(f), clipPx = ToPx(clipH);
        src.h = std::min(ph, (wrap > 0 && ls > 0) ? clipPx / ls * ls : clipPx);  // 여러 줄이면 줄 단위로 자름
        drawH = FromPx(src.h);
        // 긴 설명: 3초 읽을 시간 → 천천히 위로 → 끝에서 3초 → 처음부터
        if (wrap > 0 && scroll > 0) {
            src.y = ToPx(MarqueeOffset(s + "@" + std::to_string(x) + "," + std::to_string(y), th - drawH, scroll, 3000, 3000));
            src.y = std::max(0, std::min(src.y, ph - src.h));
        }
    }
    int dx = x;
    if (align == 1) dx = x - boxW / 2;
    if (align == 2) dx = x - boxW;
    AlphaMod(t, true);
    CopyPx(a.r, t, &src, dx, y);
    AlphaMod(t, false);
    TrackBox({dx, y, boxW, drawH});
    if (ell) {
        AlphaMod(ell, true);
        CopyPx(a.r, ell, nullptr, dx + drawW, y);
        AlphaMod(ell, false);
    }
}

// 스크롤 속도 (px/초): 목록·제목은 천천히, 알림 상자는 조금 빠르게 (긴 오류도 끝까지 읽을 수 있게)
static const int kScroll = 40, kScrollMsg = 80, kScrollDesc = 14;

static void Fill(App &a, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(a.r, c.r, c.g, c.b, (Uint8)(c.a * gAlpha / 255));
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(a.r, &rc);
    TrackBox(rc);
}

// ---- 메뉴 창 꾸밈 (uistyle.cpp, 스위치판과 같은 파일): 테마 색에 맞춘 판·테두리·글자 + 둥근 모서리. 테마 없음은 예전 색 그대로
static MenuStyle Menu() {
    MenuStyle m;
    MenuStyleFrom(gTheme.layout, !gTheme.base.empty(), m);
    return m;
}

// 둥근 네모: 귀퉁이 4개는 실제 해상도(gScale)로 만든 그림(부드럽게, TextCache 에 둠), 나머지는 서로 겹치지 않는 네모
static void DrawRound(App &a, int x, int y, int w, int h, int r, int bw, SDL_Color fill, SDL_Color border) {
    if (w <= 0 || h <= 0) return;
    if (bw <= 0) { bw = 0; border = fill; }
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    if (r == 0) {
        if (bw == 0) { Fill(a, x, y, w, h, fill); return; }
        bw = std::min(bw, std::min(w, h) / 2);
        Fill(a, x, y, w, bw, border);
        Fill(a, x, y + h - bw, w, bw, border);
        Fill(a, x, y + bw, bw, h - 2 * bw, border);
        Fill(a, x + w - bw, y + bw, bw, h - 2 * bw, border);
        Fill(a, x + bw, y + bw, w - 2 * bw, h - 2 * bw, fill);
        return;
    }
    bw = std::min(bw, r);
    int R = std::max(1, ToPx(r)), BW = bw ? std::max(1, ToPx(bw)) : 0;  // 그림은 실제 점 크기로
    char key[96];
    snprintf(key, sizeof(key), "\x02rnd|%d|%d|%02x%02x%02x%02x|%02x%02x%02x%02x", R, BW, fill.r, fill.g, fill.b, fill.a, border.r,
             border.g, border.b, border.a);
    SDL_Texture *t = nullptr;
    auto it = a.text.map.find(key);
    if (it != a.text.map.end()) {
        t = it->second;
    } else {
        if (a.text.map.size() > 800) a.text.Clear();
        std::vector<Uint8> px;
        RoundCornerPixels(R, BW, fill, border, px);
        if (SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), 2 * R, 2 * R, 32, 8 * R, SDL_PIXELFORMAT_RGBA32)) {
            t = SDL_CreateTextureFromSurface(a.r, sf);
            SDL_FreeSurface(sf);
        }
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            a.text.map[key] = t;
        }
    }
    if (t) {
        AlphaMod(t, true);
        SDL_Rect s[4] = {{0, 0, R, R}, {R, 0, R, R}, {0, R, R, R}, {R, R, R, R}};
        SDL_FRect d[4] = {{(float)x, (float)y, (float)r, (float)r},
                          {(float)(x + w - r), (float)y, (float)r, (float)r},
                          {(float)x, (float)(y + h - r), (float)r, (float)r},
                          {(float)(x + w - r), (float)(y + h - r), (float)r, (float)r}};
        for (int i = 0; i < 4; i++) SDL_RenderCopyF(a.r, t, &s[i], &d[i]);
        AlphaMod(t, false);
    }
    int mw = w - 2 * r, mh = h - 2 * r;
    if (mw > 0) {
        if (bw) {
            Fill(a, x + r, y, mw, bw, border);
            Fill(a, x + r, y + h - bw, mw, bw, border);
        }
        Fill(a, x + r, y + bw, mw, r - bw, fill);
        Fill(a, x + r, y + h - r, mw, r - bw, fill);
    }
    if (mh > 0) {
        if (bw) {
            Fill(a, x, y + r, bw, mh, border);
            Fill(a, x + w - bw, y + r, bw, mh, border);
        }
        Fill(a, x + bw, y + r, w - 2 * bw, mh, fill);
    }
}

// 메뉴 창: 화면을 어둡게 덮고 판. panelA: 테마 없음일 때 판 불투명도를 따로 (-1 = 꾸밈 그대로)
static void MenuPanel(App &a, const MenuStyle &m, int x, int y, int w, int h, int dimA = 120, int panelA = -1) {
    if (dimA > 0) Fill(a, 0, 0, W, H, {0, 0, 0, (Uint8)dimA});
    SDL_Color p = m.panel;
    if (panelA >= 0 && !m.themed) p.a = (Uint8)panelA;
    DrawRound(a, x, y, w, h, m.radius, m.borderW, p, m.border);
}
static void MenuSel(App &a, const MenuStyle &m, int x, int y, int w, int h) { DrawRound(a, x, y, w, h, m.selRadius, 0, m.sel, m.sel); }
// 뜻이 있는 색의 상자 (오류 빨강·완료 초록·삭제 확인): 색은 그대로, 둥근 모서리 + 테마면 밝은 같은 색 테두리
static void NoticeBox(App &a, const MenuStyle &m, int x, int y, int w, int h, SDL_Color c) {
    SDL_Color b = {(Uint8)std::min(255, c.r + 80), (Uint8)std::min(255, c.g + 80), (Uint8)std::min(255, c.b + 80), 255};
    DrawRound(a, x, y, w, h, m.radius, m.themed ? m.borderW : 0, c, b);
}
// 진행 막대 (frac 0~1)
static void ProgressBar(App &a, const MenuStyle &m, int x, int y, int w, int h, double frac) {
    DrawRound(a, x, y, w, h, h / 2, 0, m.track, m.track);
    int fw = (int)(w * std::max(0.0, std::min(1.0, frac)));
    if (fw > 0) DrawRound(a, x, y, fw, h, h / 2, 0, m.title, m.title);
}
// 도움말의 "[X] <이름>": 이 판의 X 버튼은 BGM · 소리 설정 → 스위치판 테마의 "[X] 홈브루" 등 어떤 이름이든 BGM 으로
static std::string FixX(const std::string &s) { return FixXLabel(s, "BGM", true); }

// k: 불투명도 0~1 (화면 사진과 미리보기 영상이 서로 바뀔 때)
static SDL_Rect DrawImage(App &a, SDL_Texture *t, int x, int y, int w, int h, float k = 1.0f) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    float s = std::min(w / (float)bw, h / (float)bh);
    SDL_Rect d = {x + (int)(w - bw * s) / 2, y + (int)(h - bh * s) / 2, (int)(bw * s), (int)(bh * s)};
    if (k < 1.0f) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(t, (Uint8)(255 * std::max(0.0f, k)));
    }
    AlphaMod(t, true);
    SDL_RenderCopy(a.r, t, nullptr, &d);
    AlphaMod(t, false);
    if (k < 1.0f) SDL_SetTextureAlphaMod(t, 255);
    TrackBox(d);
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
    bool charging = false;
    int pct = plat::Battery(charging);  // 데스크톱 시험판: JOY_BATTERY=76 또는 76,c (충전 중)
    if (pct >= 0) {
        gBattery.percent = pct;
        gBattery.charging = charging;
    }
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
    tw = FromPx(tw);
    th = FromPx(th);
    int tx = bx - 8 - tw;
    if (t) CopyPx(a.r, t, nullptr, tx, by + bh / 2 - th / 2);
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
        std::string bg = ThemeFind(gTheme.bg, col, "backgrounds");
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
    std::string logo = col && logoTitle ? ThemeFind(gTheme.logo, col, "logos") : "";
    SDL_Texture *lt = logo.empty() ? nullptr : a.images.Load(logo);
    if (lt) DrawImage(a, lt, W / 2 - 220, 8, 440, 60);
    else DrawText(a, a.fTitle, title, W / 2, 18, kAccent, 640, 1, 0, 0, kScroll);
    gBatteryLeft = DrawBattery(a, W - 24, 28);
    DrawText(a, a.fInfo, hint, W / 2, H - 40, kWhite, 0, 1);
}

// ---------------------------------------------------------------- 미리보기 영상
// 게임 화면에서 고른 게임에 영상이 있으면 화면 사진 자리(media 요소, source = screenshot 인 그림)에서 재생한다.
// 선택이 잠시 멈춘 뒤 시작하고, 첫 장면이 올 때까지는 화면 사진(없으면 박스 사진)을 그대로 보여 준다
static bool gShotMode = false;            // 데스크톱 스냅샷
static const Game *gVideoGame = nullptr;  // 게임 화면에서 지금 고른 게임 (이 게임의 영상만 재생)
// 게임·앱을 실행한 직후: 그 앱이 화면에 뜰 때까지(런처는 아직 돌고 있음) 영상을 다시 시작하지 않는다. 돌아오면 0
static Uint32 gVideoHoldUntil = 0;

// 데스크톱 스냅샷에서는 재생하지 않는다 (JOY_SHOT_VIDEO 를 주면 첫 장면을 기다려 찍음)
static void ApplyVideo() { VideoConfigure(gVideoOn && (!gShotMode || getenv("JOY_SHOT_VIDEO")), gSound == "video"); }

// 그 게임의 미리보기 영상 그림 (재생 중일 때만). k = 나타나는 정도 0~1.
// first = 영상을 보여 줄 게임 (영상이 있으면 영상, 없으면 사진. 영상을 읽는 동안은 사진을 보이다가 첫 장면이 나오면 겹치며 바뀜)
static SDL_Texture *GameVideo(App &a, const Game *g, float &k, bool &first) {
    k = 1.0f;
    first = false;
    if (!g || g != gVideoGame || g->video.empty()) return nullptr;
    first = VideoUsable(g->video);
    Uint32 ms = 0;
    SDL_Texture *t = VideoWant(a.r, g->video, &ms);
    const Uint32 kFadeMs = 250;
    if (t && AnimOn() && ms < kFadeMs) k = ms / (float)kFadeMs;
    return t;
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
    std::map<std::string, bool> appCovers[2];  // 런처 글꼴(나눔고딕)에 다 있는지 — 없으면 한중일 대체 글꼴로
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
    gFonts.appCovers[0].clear();
    gFonts.appCovers[1].clear();
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
// 글꼴에 그 글의 글자가 모두 있는지 (결과를 기억)
static bool FontCovers(TTF_Font *f, std::map<std::string, bool> &memo, const std::string &s) {
    auto it = memo.find(s);
    if (it != memo.end()) return it->second;
    bool ok = true;
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
    return ok;
}

// 한중일 대체 글꼴: 런처 글꼴(나눔고딕)에 없는 일본어 가나·한자 등이 든 글. 기기에 있는 글꼴을 쓴다 (앱에 넣지 않음)
static std::string FallbackFontPath() {
    static std::string path = [] {
        static const char *cands[] = {
#ifdef __ANDROID__
            "/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/NotoSansCJKjp-Regular.otf", "/system/fonts/NotoSansJP-Regular.otf",
            "/system/fonts/DroidSansFallbackFull.ttf", "/system/fonts/DroidSansFallback.ttf",
#else
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
#endif
        };
        for (const char *c : cands) {
            struct stat st;
            if (stat(c, &st) == 0 && S_ISREG(st.st_mode)) return std::string(c);
        }
        return std::string();
    }();
    return path;
}

static TTF_Font *FallbackFont(int size) {
    int key = 1000000 + size;
    auto it = gFonts.fonts.find(key);
    if (it != gFonts.fonts.end()) return it->second;
    std::string p = FallbackFontPath();
    TTF_Font *f = p.empty() ? nullptr : TTF_OpenFontIndex(p.c_str(), ToPx(size), 0);  // .ttc 의 0번 = 일본어 판(JP)
    gFonts.fonts[key] = f;
    return f;
}

// 글에 맞는 글꼴: 테마 글꼴(글자가 다 있으면) → 런처 글꼴 → 런처 글꼴에도 없는 글자(일본어 등)가 있으면 기기의 한중일 글꼴
static TTF_Font *FontFor(bool bold, int size, const std::string &s) {
    if (!(bold ? gFonts.bold : gFonts.reg).empty())
        if (TTF_Font *f = FontRaw(bold, size, true))
            if (FontCovers(f, gFonts.covers[bold ? 1 : 0], s)) return f;
    TTF_Font *app = FontRaw(bold, size, false);
    if (app && !FontCovers(app, gFonts.appCovers[bold ? 1 : 0], s))
        if (TTF_Font *c = FallbackFont(size)) return c;
    return app;
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
static void DrawFitRaw(App &a, SDL_Texture *t, int x, int y, int w, int h, int fit, int align, int valign) {
    int bw, bh;
    SDL_QueryTexture(t, nullptr, nullptr, &bw, &bh);
    if (w <= 0 || h <= 0) {  // 크기를 안 정했으면 그림 크기 그대로
        SDL_Rect d = {x, y, bw, bh};
        SDL_RenderCopy(a.r, t, nullptr, &d);
        TrackBox(d);
        return;
    }
    TrackBox({x, y, w, h});
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

static void DrawFit(App &a, SDL_Texture *t, int x, int y, int w, int h, int fit, int align = 1, int valign = 1) {
    AlphaMod(t, true);
    DrawFitRaw(a, t, x, y, w, h, fit, align, valign);
    AlphaMod(t, false);
}

// 화면에 보일 카테고리 이름: layout.txt 의 [names] (카테고리 이름 → shortname → 롬 폴더 순), 없으면 원래 이름
static std::string CatName(const Collection &c) {
    const auto &names = gTheme.layout.names;
    if (!names.empty()) {
        auto it = names.find(LowerAscii(c.name));
        if (it == names.end() && !c.shortName.empty()) it = names.find(LowerAscii(c.shortName));
        if (it == names.end() && c.folders.size() == 1) it = names.find(LowerAscii(c.folders[0]));
        // 앱 즐겨찾기: 스위치판 테마가 "홈브루 즐겨찾기" 에 붙여 둔 이름도 그대로 쓴다
        if (it == names.end() && c.virt && c.shortName == "hbfav") it = names.find("홈브루 즐겨찾기");
        if (it != names.end()) return it->second;
    }
    if (c.virt && c.shortName == "hbfav" && !StringOverride(c.name.c_str()))
        if (const char *ov = StringOverride("홈브루 즐겨찾기")) return ov;
    return T(c.name);
}

struct DrawCtx {
    bool games = false;
    const Collection *col = nullptr;  // 커서가 있는(또는 열린) 카테고리
    const Game *game = nullptr;       // 선택된 게임 (게임 화면)
    int index = 0, count = 0;         // 목록의 선택 위치(1부터) / 전체
    std::string hint;
    // 목록 칸 안 요소를 그릴 때: 그 칸의 항목 (col/game 은 그 항목으로 바뀜)
    bool item = false, itemSel = false;
    int itemIdx = 0, itemCount = 0;
    const Collection *screenCol = nullptr;  // 칸 그리기 전의 col (게임 화면의 열린 카테고리)
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

// 자리표 하나의 값. 모르는 이름이면 false
static bool PlaceValue(App &a, const std::string &k, const DrawCtx &c, std::string &v) {
    const Game *g = c.game;
    v.clear();
    if (k == "app") v = "Joy Launcher";
    else if (k == "version") v = CurrentVersion();
    else if (k == "applet") v = "";  // 스위치판의 애플릿 모드 표시 자리 (안드로이드에는 없음: 늘 빈 값)
    else if (k == "hint") v = c.hint;
    else if (k == "clock") {
        time_t now = time(nullptr);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
        v = buf;
    }
    else if (k == "date") {
        time_t now = time(nullptr);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&now));
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
    else if (k == "file") {  // 롬 파일 이름 (확장자 뺌)
        if (g) {
            size_t sl = g->file.find_last_of('/'), dot = g->file.find_last_of('.');
            v = g->file.substr(sl == std::string::npos ? 0 : sl + 1);
            if (dot != std::string::npos && dot > sl + 1) v = g->file.substr(sl + 1, dot - sl - 1);
        }
    }
    // 칸 안 요소용 (칸이 아니면 선택된 항목 기준)
    else if (k == "name") v = c.item ? (c.games ? (g ? g->title : "") : (c.col ? CatName(*c.col) : "")) : (c.games ? (g ? g->title : "") : (c.col ? CatName(*c.col) : ""));
    else if (k == "short") v = c.col ? (!c.col->shortName.empty() ? c.col->shortName : c.col->folders.size() == 1 ? c.col->folders[0] : "") : "";
    else if (k == "system") {  // ES-DE 기종 이름 (모르면 shortname·폴더 이름)
        if (c.col) {
            for (const std::string &n : {c.col->name, c.col->shortName, c.col->folders.empty() ? std::string() : c.col->folders[0]}) {
                v = EsdeSystemName(LowerAscii(n));
                if (!v.empty()) break;
            }
            if (v.empty()) v = c.col->shortName.empty() && !c.col->folders.empty() ? c.col->folders[0] : c.col->shortName;
        }
    }
    else if (k == "num") v = std::to_string(c.item ? c.itemIdx + 1 : c.index);
    else if (k == "num_desc") v = std::to_string(c.item ? c.itemCount - c.itemIdx : c.count - c.index + 1);
    else if (k == "selected") v = c.item ? (c.itemSel ? "1" : "") : "1";
    else if (k == "fav") v = g && (gFavSet.count(g->file) || (c.col && c.col->shortName == "hbfav")) ? "★" : "";
    else if (k == "playcount") {
        auto it = g ? gPlay.find(g->file) : gPlay.end();
        v = std::to_string(it == gPlay.end() ? 0 : it->second.count);
    }
    else if (k == "lastplayed") {
        auto it = g ? gPlay.find(g->file) : gPlay.end();
        if (it != gPlay.end() && it->second.last > 0) {
            time_t t = (time_t)it->second.last;
            char buf[16];
            strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
            v = buf;
        }
    }
    else if (k == "screen") v = c.games ? "games" : "categories";
    else if (k == "view") v = a.view == 1 ? "fav" : a.view == 2 ? "recent" : "all";
    else if (k == "view_name") v = c.games ? ViewName(a.view) : "";
    else if (k == "view_count") v = c.games && c.col ? std::to_string(c.col->games.size()) : "";
    else return false;
    return true;
}

// 조건 값: 비었거나 "0" 이면 거짓
static bool Truthy(const std::string &v) { return !v.empty() && v != "0"; }

// 글자 틀의 {자리표} 채우기. 모르는 자리표는 그대로 둔다.
//   {이름}                       값
//   {이름1,이름2,이름3| · }       비지 않은 값만 « · » 로 이어 붙임
//   {이름?참일 때:거짓일 때}       값이 있으면(0 아님) 앞, 없으면 뒤. {이름=값?…:…} 은 값이 같은지
static std::string Expand(App &a, const std::string &tpl, const DrawCtx &c) {
    if (tpl.find('{') == std::string::npos) return tpl;
    std::string out;
    for (size_t i = 0; i < tpl.size();) {
        size_t close = tpl[i] == '{' ? tpl.find('}', i) : std::string::npos;
        if (close == std::string::npos) { out += tpl[i++]; continue; }
        std::string k = tpl.substr(i + 1, close - i - 1), v;
        bool known = true;
        size_t q = k.find('?'), bar = k.find('|');
        if (q != std::string::npos) {
            std::string cond = k.substr(0, q), rest = k.substr(q + 1), want;
            size_t colon = rest.find(':');
            std::string yes = rest.substr(0, colon), no = colon == std::string::npos ? "" : rest.substr(colon + 1);
            size_t eq = cond.find('=');
            if (eq != std::string::npos) { want = cond.substr(eq + 1); cond = cond.substr(0, eq); }
            std::string cv;
            known = PlaceValue(a, cond, c, cv);
            if (known) v = (eq != std::string::npos ? cv == want : Truthy(cv)) ? yes : no;
        } else if (bar != std::string::npos) {
            std::string sep = k.substr(bar + 1), part;
            std::stringstream ks(k.substr(0, bar));
            while (known && std::getline(ks, part, ',')) {
                std::string pv;
                known = PlaceValue(a, part, c, pv);
                if (!pv.empty()) v += (v.empty() ? "" : sep) + pv;
            }
        } else {
            known = PlaceValue(a, k, c, v);
        }
        if (known) out += v;
        else out += tpl.substr(i, close - i + 1);
        i = close + 1;
    }
    // 자리표가 모두 비어 글자가 공백뿐이면 그리지 않는다
    if (out.find_first_not_of(" \t") == std::string::npos) return "";
    return out;
}

// visible_if / hidden_if
static bool ElemShown(App &a, const Elem &e, const DrawCtx &c) {
    if (!e.visible) return false;
    auto cond = [&](const std::string &t) { return Truthy(Expand(a, t.find('{') == std::string::npos ? "{" + t + "}" : t, c)); };
    if (!e.visibleIf.empty() && !cond(e.visibleIf)) return false;
    if (!e.hiddenIf.empty() && cond(e.hiddenIf)) return false;
    if (c.item && ((e.state == 1 && !c.itemSel) || (e.state == 2 && c.itemSel))) return false;
    return true;
}

static void DrawElemImage(App &a, const Elem &e, const DrawCtx &c) {
    if (e.source == "background") {
        // 카테고리(플랫폼)별 배경. 그 카테고리 배경이 없으면 fallback 그림, 그것도 없으면 배경 없이
        std::string bg = ThemeFind(gTheme.bg, c.col, "backgrounds");
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
        // 배경이 바뀌면 직전 배경 위로 천천히 (bg_fade_ms)
        static std::string shown, prev;
        static Uint32 at = 0;
        int fms = AnimOn() ? gTheme.layout.bgFadeMs : 0;
        std::string now = t ? a.lastBack : "";
        if (fms > 0 && now != shown) { prev = shown; shown = now; at = SDL_GetTicks(); }
        SDL_Texture *pt = fms > 0 && !prev.empty() && SDL_GetTicks() - at < (Uint32)fms ? a.backs.Find(prev) : nullptr;
        if (pt && t) {
            DrawFit(a, pt, e.x, e.y, e.w, e.h, e.fit);
            Uint8 k = (Uint8)(255 * Ease((SDL_GetTicks() - at) / (float)fms));
            SDL_SetTextureAlphaMod(t, k);
            DrawFit(a, t, e.x, e.y, e.w, e.h, e.fit);
            SDL_SetTextureAlphaMod(t, 255);
        } else if (t) {
            DrawFit(a, t, e.x, e.y, e.w, e.h, e.fit);
        }
        if (t) {
            int dim = e.dim >= 0 ? e.dim : gTheme.dim;
            if (dim > 0) Fill(a, e.x, e.y, e.w, e.h, {0, 0, 0, (Uint8)dim});
        }
        gBackDrawn = t != nullptr;
        return;
    }
    std::string path;
    bool gameImg = false;
    if (e.source == "logo") path = c.col ? ThemeFind(gTheme.logo, c.col, "logos") : "";
    else if (e.source == "icon") path = c.col ? ThemeFind(ThemeFolder(e.folder.empty() ? "icons" : e.folder), c.col, e.folder.empty() ? "icons" : e.folder.c_str()) : "";
    else if (e.source == "boxart") { path = c.game ? c.game->boxart : ""; gameImg = true; }
    else if (e.source == "screenshot") { path = c.game ? c.game->screenshot : ""; gameImg = true; }
    else path = ThemeFile(e.file);
    // 화면 사진 자리: 고른 게임의 미리보기 영상이 재생 중이면 영상을 (vk = 나타나는 정도)
    float vk = 1.0f;
    bool videoFirst = false;
    SDL_Texture *vid = e.source == "screenshot" ? GameVideo(a, c.game, vk, videoFirst) : nullptr;
    (void)videoFirst;  // 영상을 읽는 동안은 사진(없으면 fallback 그림), 영상이 나오면 겹치며 바뀜
    if (path.empty()) { path = ThemeFile(e.fallback); gameImg = false; }
    if (path.empty() && !vid) return;
    SDL_Texture *t = nullptr;
    if (vid && vk >= 1.0f) {
        // 영상이 다 나타났으면 사진은 그리지 않음
    } else if (path.empty()) {
    } else if (gameImg) {
        // 이동이 잠시 멈춘 뒤에만 디스크에서 읽고, 캐시에 있으면 바로 표시
        t = a.images.Find(path);
        if (!t && SDL_GetTicks() - a.selChanged > 120) t = a.images.Load(path);
    } else {
        t = Thumb(a, path, e.w, e.h, e.fit != 0, true);
    }
    if (!t && !vid) return;
    auto put = [&](SDL_Texture *x, float k) {
        int al = (int)(e.alpha * k);
        if (al < 255) {
            if (k < 1.0f) SDL_SetTextureBlendMode(x, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(x, (Uint8)std::max(0, al));
        }
        if (e.hasTint) {
            SDL_Color tc = LC(e.tint);
            SDL_SetTextureColorMod(x, tc.r, tc.g, tc.b);
        }
        DrawFit(a, x, e.x, e.y, e.w, e.h, e.fit, e.align, e.valign);
        if (al < 255) SDL_SetTextureAlphaMod(x, 255);
        if (e.hasTint) SDL_SetTextureColorMod(x, 255, 255, 255);
    };
    if (t) put(t, vid ? 1.0f - vk : 1.0f);
    if (vid) put(vid, vk);
    if (e.dim > 0) Fill(a, e.x, e.y, e.w, e.h, {0, 0, 0, (Uint8)e.dim});
}

static void DrawElemText(App &a, const Elem &e, const DrawCtx &c) {
    if (e.logo && c.col) {
        std::string logo = ThemeFind(gTheme.logo, c.col, "logos");
        SDL_Texture *lt = logo.empty() ? nullptr : a.images.Load(logo);
        if (lt) { DrawImage(a, lt, e.lx, e.ly, e.lw, e.lh); return; }
    }
    // [strings] 에 글 전체가 있으면 바꾼 말로 (영어 화면용 [strings.en] 으로 테마 글자 번역)
    const char *ov = StringOverride(e.text.c_str());
    std::string s = Expand(a, PadHint(FixX(ov ? std::string(ov) : e.text)), c);  // X 이름은 테마 글에서만 (게임 이름 등 자리표 값은 그대로)
    if (s.empty()) return;
    TTF_Font *f = FontFor(e.bold, e.size, s);
    if (!f) return;
    SDL_Color col = c.item && c.itemSel && e.hasSelColor ? LC(e.selColor) : LC(e.color);
    if (e.wrap && e.w > 0) {
        DrawText(a, f, s, e.x, e.y, col, e.w, 0, e.w, e.h, e.scroll ? kScrollDesc : 0);
        return;
    }
    int ax = e.x + (e.align == 1 ? e.w / 2 : e.align == 2 ? e.w : 0), y = e.y;
    if (e.h > 0 && e.valign) {
        int th = FontH(f);
        y += e.valign == 1 ? (e.h - th) / 2 : e.h - th;
    }
    DrawText(a, f, s, ax, y, col, e.w, e.align, 0, 0, e.scroll ? kScroll : 0);
}

// 목록의 열·줄 수
static void ListGeom(const Elem &e, int &cols, int &rows) {
    if (e.style == 2) {  // 캐러셀: 선택이 늘 가운데 (맨 위 항목 = 선택)
        cols = rows = 1;
        return;
    }
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

static void DrawElem(App &a, const Elem &e, const DrawCtx &c);
static std::map<std::string, SDL_Rect> gBounds;  // 테마 빌더: "화면.요소" -> 그린 자리

// 목록 칸(줄) 안 요소: rc = 칸 자리, sc = 선택 확대 배율, before = z 가 음수인 것(칸 내용 아래)만
static void DrawCellChildren(App &a, const Elem &e, const DrawCtx &c, int idx, int n, bool s, SDL_Rect rc, float sc, bool before) {
    for (const Elem &ch : e.children) {
        if ((ch.z < 0) != before || ch.type == ElemType::List || ch.type == ElemType::Preview) continue;
        DrawCtx ic = c;
        ic.item = true;
        ic.itemSel = s;
        ic.itemIdx = idx;
        ic.itemCount = n;
        ic.screenCol = c.col;
        if (c.games) ic.game = &c.col->games[idx];
        else { ic.col = &a.cols[idx]; ic.game = nullptr; }
        if (!ElemShown(a, ch, ic)) continue;
        Elem ce = ch;
        auto px = [&](int v) { return (int)(v * sc); };
        // 칸 기준 자리. 폭이 0 이하면 칸 오른쪽 끝까지(음수는 그만큼 덜), 높이도 같음 (글자는 높이 0 = 한 줄)
        ce.x = rc.x + px(ch.x);
        ce.y = rc.y + px(ch.y);
        ce.w = ch.w > 0 ? px(ch.w) : rc.w - px(ch.x) + px(ch.w);
        ce.h = ch.h > 0 ? px(ch.h) : (ch.type == ElemType::Text && ch.h == 0) ? 0 : rc.h - px(ch.y) + px(ch.h);
        ce.lx = rc.x + px(ch.lx); ce.ly = rc.y + px(ch.ly); ce.lw = px(ch.lw); ce.lh = px(ch.lh);
        if (sc != 1.0f) ce.size = std::max(6, px(ch.size));
        ce.hasRect = true;
        SDL_Rect outer = gDrawBox;
        if (gTrackBox && s) gDrawBox = {0, 0, 0, 0};
        DrawElem(a, ce, ic);
        if (gTrackBox && s) {
            SDL_Rect b = gDrawBox, u;
            if (ce.w > 0 && ce.h > 0) { SDL_Rect r = {ce.x, ce.y, ce.w, ce.h}; if (b.w > 0) { SDL_UnionRect(&b, &r, &u); b = u; } else b = r; }
            gBounds[(c.games ? "games." : "categories.") + e.name + "." + ch.name] = b;
            gDrawBox = outer;
            TrackBox(b);
        }
    }
}

// 목록 움직임 상태 (화면.요소 마다): 스크롤 위치·선택 위치를 시간에 따라 부드럽게
struct ListAnim {
    int sel = -1, prev = -1;
    Uint32 at = 0, last = 0;
    float pos = 0, selPos = 0;
    bool init = false;
    int id = -2;
};
static std::map<std::string, ListAnim> gListAnim;
static int AnimMs(const Elem &e) {
    if (!AnimOn()) return 0;
    return e.animMs >= 0 ? e.animMs : gTheme.layout.animMs;
}
// id: 목록이 바뀌면(다른 카테고리·보기) 움직임 없이 새로 시작
static ListAnim &AnimFor(const std::string &key, int id, int sel, float target, int ms) {
    ListAnim &st = gListAnim[key];
    Uint32 now = SDL_GetTicks();
    if (!st.init || st.id != id) {
        st = ListAnim();
        st.init = true;
        st.id = id;
        st.sel = sel;
        st.pos = target;
        st.selPos = (float)sel;
        st.last = now;
        st.at = now - 100000;
        return st;
    }
    if (sel != st.sel) {
        st.prev = st.sel;
        st.sel = sel;
        st.at = now;
    }
    float dt = (float)(now - st.last);
    st.last = now;
    st.pos = Approach(st.pos, target, dt, ms);
    st.selPos = Approach(st.selPos, (float)sel, dt, ms);
    return st;
}

static void DrawElemList(App &a, const Elem &e, const DrawCtx &c) {
    int cols, rows;
    ListGeom(e, cols, rows);
    int n = c.games ? (c.col ? (int)c.col->games.size() : 0) : (int)a.cols.size();
    int top = c.games ? a.top : a.catTop, sel = c.games ? a.sel : a.catSel;
    SDL_Color colN = LC(e.color), colS = LC(e.selColor);
    int ms = AnimMs(e);
    std::string akey = (c.games ? "g." : "c.") + e.name;
    int listId = c.games ? a.col * 4 + a.view : -1;
    auto starred = [&](int idx) {
        return c.games && ((!c.col->virt && gFavSet.count(c.col->games[idx].file)) || c.col->shortName == "hbfav");
    };
    if (e.style == 0) {
        ListAnim &st = AnimFor(akey, listId, sel, (float)top, ms);
        float view = st.pos;
        bool frac = fabsf(view - roundf(view)) > 0.001f;
        if (frac) {
            SDL_Rect clip = {e.x, e.y, e.w, (int)(rows * e.rowH + 0.5f)};
            SDL_RenderSetClipRect(a.r, &clip);
        }
        // 움직이는 선택 막대 (선택 위치를 따라 미끄러짐)
        bool slideBar = ms > 0 && fabsf(st.selPos - sel) > 0.002f;
        if (slideBar) {
            int ry = e.y + (int)lroundf((st.selPos - view) * e.rowH), rh = (int)(e.rowH + 0.5f);
            SDL_Texture *stx = e.selector.empty() ? nullptr : Thumb(a, ThemeFile(e.selector), e.w, (int)e.rowH, true, true);
            int g = e.selectorGrow;
            if (stx) DrawFit(a, stx, e.x - g, ry - g, e.w + g * 2, rh + g * 2, 2);
            else Fill(a, e.x, ry, e.w, rh, LC(e.selection));
        }
        bool showCount = e.count && !c.games;
        bool themeFont = !(e.bold ? gFonts.bold : gFonts.reg).empty();
        int first = (int)floorf(view + 0.0005f);
        for (int i = 0; i < rows + (frac ? 1 : 0); i++) {
            int idx = first + i;
            if (idx < 0 || idx >= n) continue;
            float rel = idx - view;
            int ry = e.y + (int)floorf(rel * e.rowH + 0.5f), rh = e.y + (int)floorf((rel + 1) * e.rowH + 0.5f) - ry;
            bool s = idx == sel;
            if (s && !slideBar) {
                SDL_Texture *stx = e.selector.empty() ? nullptr : Thumb(a, ThemeFile(e.selector), e.w, (int)e.rowH, true, true);
                int g = e.selectorGrow;
                if (stx) DrawFit(a, stx, e.x - g, ry - g, e.w + g * 2, rh + g * 2, 2);
                else Fill(a, e.x, ry, e.w, rh, LC(e.selection));
            }
            SDL_Rect rowRc = {e.x, ry, e.w, rh};
            if (!e.children.empty()) DrawCellChildren(a, e, c, idx, n, s, rowRc, 1.0f, true);
            if (e.label == 2) {  // label = off: 제목을 그리지 않음 (칸 안 요소로 직접 배치)
                if (!e.children.empty()) DrawCellChildren(a, e, c, idx, n, s, rowRc, 1.0f, false);
                continue;
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
                        sw = FromPx(sw);
                        int sy = e.textY >= 0 ? e.textY : (rh - FontH(sf)) / 2;
                        DrawText(a, sf, "★ ", e.x + e.textX, ry + sy, s ? colS : colN);
                        dx = sw;
                    } else {
                        label = "★ " + label;
                    }
                }
            }
            TTF_Font *f = FontFor(e.bold, e.size, label);
            if (!f) continue;
            int ty = e.textY >= 0 ? e.textY : (rh - FontH(f)) / 2;
            int tx = e.align == 1 ? e.x + e.w / 2 : e.align == 2 ? e.x + e.w - e.textX : e.x + e.textX + dx;
            DrawText(a, f, label, tx, ry + ty, s ? colS : colN, maxW - dx, e.align, 0, 0, s ? kScroll : 0);
            if (showCount) {
                char num[32];
                snprintf(num, sizeof(num), "%d", (int)a.cols[idx].games.size());
                if (TTF_Font *fc = FontFor(false, e.countSize, num)) {
                    int cy = e.countY >= 0 ? e.countY : (rh - FontH(fc)) / 2;
                    DrawText(a, fc, num, e.x + e.w - e.countX, ry + cy, LC(e.countColor), 0, 2);
                }
            }
            if (!e.children.empty()) DrawCellChildren(a, e, c, idx, n, s, rowRc, 1.0f, false);
        }
        if (frac) SDL_RenderSetClipRect(a.r, nullptr);
        return;
    }
    // 격자·캐러셀 칸
    bool carousel = e.style == 2;
    int cw, ch;
    if (carousel) {
        bool hz = e.direction == 0;
        cw = e.itemW > 0 ? e.itemW : hz ? (int)(e.h / std::max(1.0f, e.centerScale)) : (int)(e.w / std::max(1.0f, e.centerScale));
        ch = e.itemH > 0 ? e.itemH : hz ? (int)(e.h / std::max(1.0f, e.centerScale)) : std::max(8, cw / 3);
    } else {
        cw = std::max(1, (e.w - e.gapX * (cols - 1)) / cols);
        ch = std::max(1, (e.h - e.gapY * (rows - 1)) / rows);
    }
    std::string kind = e.image.empty() ? (c.games ? "boxart" : "icon") : e.image;
    float maxSc = carousel ? std::max(1.0f, std::max(e.centerScale, e.sideScale)) : 1.0f;
    int iw = std::max(1, cw - e.pad[0] - e.pad[2]), ih = std::max(1, ch - e.pad[1] - e.pad[3] - (e.label == 1 ? e.labelH : 0));
    int tw = (int)(iw * maxSc), th = (int)(ih * maxSc);  // 읽어 둘 그림 크기 (캐러셀은 가장 크게 보일 때 기준)
    auto cellPath = [&](int idx) -> std::string {
        if (c.games) return kind == "boxart" ? c.col->games[idx].boxart : kind == "screenshot" ? c.col->games[idx].screenshot : "";
        const Collection &cc = a.cols[idx];
        if (kind == "icon") return ThemeFind(ThemeFolder(e.folder.empty() ? "icons" : e.folder), &cc, e.folder.empty() ? "icons" : e.folder.c_str());
        if (kind == "logo") return ThemeFind(gTheme.logo, &cc, "logos");
        if (kind == "background") return ThemeFind(gTheme.bg, &cc, "backgrounds");
        if (kind == "boxart")
            for (const auto &g : cc.games)
                if (!g.boxart.empty()) return g.boxart;
        return "";
    };
    // base: 확대 전 칸 자리, sc: 확대 배율(가운데 기준), alpha: 흐리게
    auto cell = [&](int idx, bool s, SDL_Rect base, float sc, int alpha) {
        SDL_Rect rc = base;
        if (sc != 1.0f) {
            int nw = (int)(base.w * sc), nh = (int)(base.h * sc);
            rc = {base.x - (nw - base.w) / 2, base.y - (nh - base.h) / 2, nw, nh};
        }
        int oldAlpha = gAlpha;
        gAlpha = gAlpha * alpha / 255;
        auto px = [&](int v) { return (int)(v * sc); };
        if (s && e.selector.empty()) Fill(a, rc.x - 4, rc.y - 4, rc.w + 8, rc.h + 8, LC(e.selection));
        if (e.hasCardColor) Fill(a, rc.x, rc.y, rc.w, rc.h, LC(e.cardColor));
        if (!e.card.empty())
            if (SDL_Texture *ct = Thumb(a, ThemeFile(e.card), cw + e.cardGrow * 2, ch + e.cardGrow * 2, true, true)) {
                int g = px(e.cardGrow);
                DrawFit(a, ct, rc.x - g, rc.y - g, rc.w + g * 2, rc.h + g * 2, 2);
            }
        if (!e.children.empty()) DrawCellChildren(a, e, c, idx, n, s, rc, sc, true);
        std::string path = cellPath(idx), name;
        if (c.games) name = starred(idx) ? "★ " + c.col->games[idx].title : c.col->games[idx].title;
        else name = CatName(a.cols[idx]);
        SDL_Rect in = {rc.x + px(e.pad[0]), rc.y + px(e.pad[1]), rc.w - px(e.pad[0] + e.pad[2]), rc.h - px(e.pad[1] + e.pad[3])};
        int lh = e.label == 1 ? px(e.labelH) : 0;
        SDL_Texture *t = path.empty() ? nullptr : Thumb(a, path, carousel ? tw : iw, carousel ? th : ih, false, false);
        if (t && in.w > 0 && in.h - lh > 0) DrawFit(a, t, in.x, in.y, in.w, in.h - lh, 0);
        if (e.label == 1 || (e.label == 0 && path.empty())) {
            int fs = carousel ? std::max(6, (int)(e.size * sc)) : e.size;
            if (TTF_Font *f = FontFor(e.bold, fs, name)) {
                int fh = FontH(f);
                int ty = e.label == 1 ? in.y + in.h - lh + (lh - fh) / 2 : in.y + (in.h - fh) / 2;
                DrawText(a, f, name, in.x + in.w / 2, ty, s ? colS : colN, in.w, 1, 0, 0, s ? kScroll : 0);
            }
        }
        if (s && !e.selector.empty())
            if (SDL_Texture *stx = Thumb(a, ThemeFile(e.selector), cw + e.selectorGrow * 2, ch + e.selectorGrow * 2, true, true)) {
                int g = px(e.selectorGrow);
                DrawFit(a, stx, rc.x - g, rc.y - g, rc.w + g * 2, rc.h + g * 2, 2);
            }
        if (!e.children.empty()) DrawCellChildren(a, e, c, idx, n, s, rc, sc, false);
        gAlpha = oldAlpha;
    };
    if (carousel) {
        ListAnim &st = AnimFor(akey, listId, sel, (float)sel, ms);
        float pos = st.pos, half = e.visibleItems / 2.0f;
        bool hz = e.direction == 0;
        int spacing = e.spacing > 0 ? e.spacing : (hz ? cw + e.gapX : ch + e.gapY);
        int cx = e.x + e.w / 2, cy = e.y + e.h / 2;
        std::vector<std::pair<float, int>> order;  // 멀리 있는 것부터 그림
        for (int idx = std::max(0, (int)floorf(pos - half - 1)); idx <= std::min(n - 1, (int)ceilf(pos + half + 1)); idx++) {
            float d = idx - pos;
            if (fabsf(d) > half + 0.5f) continue;
            order.push_back({fabsf(d), idx});
        }
        std::sort(order.begin(), order.end(), [](const std::pair<float, int> &x, const std::pair<float, int> &y) { return x.first > y.first; });
        for (const auto &o : order) {
            float d = o.second - pos, f = std::min(1.0f, fabsf(d));
            float sc = e.centerScale + (e.sideScale - e.centerScale) * f;
            int alpha = (int)(255 + (e.sideAlpha - 255) * f);
            int ox = hz ? (int)lroundf(d * spacing) : 0, oy = hz ? 0 : (int)lroundf(d * spacing);
            SDL_Rect base = {cx + ox - cw / 2, cy + oy - ch / 2, cw, ch};
            cell(o.second, o.second == sel, base, sc, alpha);
        }
        // 다음·이전 그림 미리 읽기
        for (int k = 1; k <= (int)half + 2 && gLoadBudget > 0; k++)
            for (int idx : {sel + (int)half + k, sel - (int)half - k})
                if (idx >= 0 && idx < n) Thumb(a, cellPath(idx), tw, th, false, false);
        return;
    }
    ListAnim &st = AnimFor(akey, listId, sel, (float)(top / std::max(1, cols)), ms);
    float vrow = st.pos;
    bool frac = fabsf(vrow - roundf(vrow)) > 0.001f;
    float tsel = ms > 0 ? Ease((SDL_GetTicks() - st.at) / (float)ms) : 1.0f;
    auto scaleOf = [&](int idx) {
        if (idx == sel) return 1.0f + (e.selScale - 1.0f) * tsel;
        if (idx == st.prev && tsel < 1.0f) return e.selScale - (e.selScale - 1.0f) * tsel;
        return 1.0f;
    };
    if (frac) {
        int m = std::max(e.selectorGrow, e.cardGrow) + (int)((e.selScale - 1.0f) * std::max(cw, ch) / 2) + 6;
        SDL_Rect clip = {e.x - m, e.y - m, e.w + m * 2, e.h + m * 2};
        SDL_RenderSetClipRect(a.r, &clip);
    }
    int firstRow = (int)floorf(vrow + 0.0005f);
    int start = std::max(0, firstRow * cols), end = std::min(n, (firstRow + rows + (frac ? 1 : 0)) * cols);
    auto baseOf = [&](int idx) {
        float rowf = idx / cols - vrow;
        return SDL_Rect{e.x + (idx % cols) * (cw + e.gapX), e.y + (int)floorf(rowf * (ch + e.gapY) + 0.5f), cw, ch};
    };
    bool prevAnim = st.prev >= start && st.prev < end && st.prev != sel && tsel < 1.0f;
    for (int idx = start; idx < end; idx++)
        if (idx != sel && !(prevAnim && idx == st.prev)) cell(idx, false, baseOf(idx), 1.0f, 255);
    if (prevAnim) cell(st.prev, false, baseOf(st.prev), scaleOf(st.prev), 255);
    if (sel >= start && sel < end) cell(sel, true, baseOf(sel), scaleOf(sel), 255);  // 선택된 칸은 맨 위에 (확대했을 때 옆 칸을 덮음)
    if (frac) SDL_RenderSetClipRect(a.r, nullptr);
    // 보이는 칸을 다 읽었으면 다음·이전 쪽 그림을 미리 읽어 둔다 (넘길 때 바로 보이게)
    for (int idx = end; idx < std::min(n, end + cols * rows) && gLoadBudget > 0; idx++) Thumb(a, cellPath(idx), iw, ih, false, false);
    for (int idx = start - 1; idx >= std::max(0, start - cols * rows) && gLoadBudget > 0; idx--) Thumb(a, cellPath(idx), iw, ih, false, false);
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

// 게임 화면: 화면 사진(또는 재생 중인 미리보기 영상)을 크게, 박스 사진은 오른쪽 아래에 작게 겹쳐서. 둘 다 없으면 박스 사진만
// 그림을 읽는 동안 칸 자리 표시 (어두운 네모): 배경 그림도 배경 색도 없는 기본 화면에서만.
// 배경 색을 정한 테마(흰 바탕 갤러리테마 등)에서는 빠르게 넘길 때 검은 네모가 깜빡여 보이므로 그리지 않는다
static bool MediaPlaceholder() { return !Themed() && !gTheme.layout.hasClear; }

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
    const std::string &shotPath = g.screenshot;
    // 영상이 있으면 영상(vk = 나타나는 정도), 없으면 사진. 영상을 읽는 동안은 사진(없으면 박스 사진)을 보이다가 겹치며 바뀐다
    float vk = 1.0f;
    bool videoFirst = false;
    SDL_Texture *vid = GameVideo(a, &g, vk, videoFirst);
    float under = vid ? 1.0f - vk : 1.0f;
    if (!shotPath.empty() || vid || videoFirst) {
        SDL_Rect area = {e.x, e.y, e.w, e.h};
        if (under > 0.0f) {
            if (!shotPath.empty()) {
                SDL_Texture *shot = img(shotPath);
                if (shot) area = DrawImage(a, shot, e.x, e.y, e.w, e.h, under);
                else if (!vid && MediaPlaceholder()) Fill(a, e.x, e.y, e.w, e.h, {44, 44, 56, 255});
            } else if (SDL_Texture *box = img(g.boxart)) {
                DrawImage(a, box, e.x, e.y, e.w, e.h, under);  // 화면 사진이 없는 게임: 영상이 나오기 전에는 박스 사진이 크게
            }
        }
        if (vid) area = DrawImage(a, vid, e.x, e.y, e.w, e.h, vk);
        SDL_Texture *box = e.overlay && g.boxart != shotPath ? img(g.boxart) : nullptr;
        if (box) {
            int bw, bh;
            SDL_QueryTexture(box, nullptr, nullptr, &bw, &bh);
            float sc = std::min(e.w * 130.0f / 440 / bw, e.h * 150.0f / 300 / bh);
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            SDL_Rect d = {area.x + area.w - dw - 8, area.y + area.h - dh - 8, dw, dh};
            // 화면 사진이 없는 게임: 영상이 나오기 전에는 박스 사진이 크게, 작은 박스 사진은 영상과 함께 나타남
            Uint8 ba = shotPath.empty() ? (vid ? (Uint8)(255 * vk) : 0) : 255;
            Fill(a, d.x - 3, d.y - 3, dw + 6, dh + 6, {0, 0, 0, (Uint8)(160 * ba / 255)});
            if (ba < 255) {
                SDL_SetTextureBlendMode(box, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(box, ba);
            }
            SDL_RenderCopy(a.r, box, nullptr, &d);
            if (ba < 255) SDL_SetTextureAlphaMod(box, 255);
        }
    } else {
        SDL_Texture *box = img(g.boxart);
        if (box) DrawImage(a, box, e.x, e.y, e.w, e.h);
        else if (MediaPlaceholder()) Fill(a, e.x, e.y, e.w, e.h, {44, 44, 56, 255});
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
    gLoadBudget = gShotMode || gAsyncImg ? 100000 : 1;
    a.thumbs.tick++;
    for (const Elem &e : sc.elems) {
        if (!ElemShown(a, e, c)) continue;
        gDrawBox = {0, 0, 0, 0};
        DrawElem(a, e, c);
        if (gTrackBox) {
            SDL_Rect b = gDrawBox, u;
            if (e.w > 0 && e.h > 0 && e.type != ElemType::Battery) {
                SDL_Rect r = {e.x, e.y, e.w, e.h};
                if (b.w > 0) { SDL_UnionRect(&b, &r, &u); b = u; } else b = r;
            }
            gBounds[(c.games ? "games." : "categories.") + e.name] = b;
        }
    }
}

static void DrawElem(App &a, const Elem &e, const DrawCtx &c) {
    switch (e.type) {
    case ElemType::Rect: Fill(a, e.x, e.y, e.w, e.h, c.item && c.itemSel && e.hasSelColor ? LC(e.selColor) : LC(e.color)); break;
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

// 화면 아래 도움말. 테마가 [strings] 로 바꾼 글이 있으면 그것 (이름 hint.* → 스위치판의 원래 글 순으로 찾음).
// 스위치판 테마의 글에 남은 "[X] 홈브루 메뉴" 등은 이 런처의 X 버튼 기능인 "BGM" (BGM · 소리 설정) 으로 바꿔 보여 준다 (FixX: "[X]" 바로 뒤 이름만)
static std::string ScreenHint(bool games, bool grid) {
    static const char *kSwitchText[4] = {  // 스위치판 1.2 까지의 기본 문구
        "[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼ 선택",
        "[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼◀▶ 선택",
        "[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ◀ ▶ 페이지  [-] 즐겨찾기  [X] 홈브루  [Y] 옵션",
        "[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ▲▼◀▶ 선택  [-] 즐겨찾기  [X] 홈브루  [Y] 옵션",
    };
    const char *ov = StringOverride(games ? (grid ? "hint.games_grid" : "hint.games") : (grid ? "hint.categories_grid" : "hint.categories"));
    if (!ov && grid) ov = StringOverride(games ? "hint.games" : "hint.categories");
    if (!ov) ov = StringOverride(kSwitchText[(games ? 2 : 0) + (grid ? 1 : 0)]);
    if (!ov && !games)  // 스위치판 1.3 부터의 기본 문구 ("[X] 홈브루" 로 통일)
        ov = StringOverride(grid ? "[A] 열기     [B] 종료     [X] 홈브루     [Y] 옵션     ▲▼◀▶ 선택" : "[A] 열기     [B] 종료     [X] 홈브루     [Y] 옵션     ▲▼ 선택");
    if (ov) return PadHint(FixX(ov));
    if (!games)
        return PadHint(grid ? T("[A] 열기     [B] 종료     [X] BGM     [Y] 옵션     ▲▼◀▶ 선택") : T("[A] 열기     [B] 종료     [X] BGM     [Y] 옵션     ▲▼ 선택"));
    return PadHint(grid ? T("[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ▲▼◀▶ 선택  [-] 즐겨찾기  [X] BGM  [Y] 옵션")
                        : T("[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ◀ ▶ 페이지  [-] 즐겨찾기  [X] BGM  [Y] 옵션"));
}

static void RenderCategories(App &a) {
    DrawCtx c;
    c.col = a.catSel < (int)a.cols.size() ? &a.cols[a.catSel] : nullptr;
    c.index = a.catSel + 1;
    c.count = (int)a.cols.size();
    int cols, rows;
    ScreenGeom(false, cols, rows);
    if (const Elem *l = gTheme.layout.categories.List()) if (l->style == 2) cols = 2;  // 캐러셀: ▲▼◀▶ 모두 씀
    c.hint = ScreenHint(false, cols > 1);
    gVideoGame = nullptr;
    RenderScreen(a, gTheme.layout.categories, c);
}

static void RenderGames(App &a) {
    const Collection &col = CurCol(a);
    DrawCtx c;
    c.games = true;
    c.col = &col;
    c.game = col.games.empty() ? nullptr : &col.games[a.sel];
    c.index = a.sel + 1;
    c.count = (int)col.games.size();
    int cols, rows;
    ScreenGeom(true, cols, rows);
    if (const Elem *l = gTheme.layout.games.List()) if (l->style == 2) cols = 2;
    c.hint = ScreenHint(true, cols > 1);
    gVideoGame = c.game;
    RenderScreen(a, gTheme.layout.games, c);
    if (a.view != 0 && col.games.empty())
        DrawText(a, a.fList, a.view == 1 ? T("즐겨찾기한 게임이 없습니다") : T("실행한 게임이 없습니다"), W / 2, 330, kWhite, 0, 1);
}

// 테마 목록 끝의 "<팩> 내려받기" 항목: 이 글 뒤에 팩 폴더 이름이 붙는다
static const char *kThemeDownload = "\x01download:";
static bool IsThemeDownload(const std::string &item) { return item.rfind(kThemeDownload, 0) == 0; }
static const char *kThemeFtp = "\x01ftp";  // 테마 선택 창 맨 아래: PC에서 받기
static bool IsThemeSpecial(const std::string &item) { return !item.empty() && item[0] == '\x01'; }
static const char *PackName(const ThemePack &p) { return CurrentLang() == Lang::En ? p.en : p.ko; }

static void RenderThemePick(App &a) {
    int n = (int)a.themeList.size();
    int rows = std::min(n, 10);
    int bw = 640, bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    MenuStyle m = Menu();
    MenuPanel(a, m, bx, by, bw, bh);
    DrawText(a, a.fBig, T("테마 선택"), W / 2, by + 16, m.title, 0, 1);
    int top = std::max(0, std::min(a.themeSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 66 + i * kRowH;
        bool sel = idx == a.themeSel;
        if (sel) MenuSel(a, m, bx + 16, y - 4, bw - 32, kRowH);
        bool dl = IsThemeDownload(a.themeList[idx]);
        std::string label = idx == 0 ? std::string(T("기본 (배경 없음)")) : idx < (int)a.themeTitles.size() ? a.themeTitles[idx] : a.themeList[idx];
        if (dl) {
            bool all = true;
            for (const auto &p : kPacks) all = all && IsDirPath(gHome + "/themes/" + p.folder);
            label = Fmt(all ? T("기본 테마 %d종 다시 받기 (최신으로)") : T("기본 테마 %d종 내려받기"), kPackCount);
        }
        if (a.themeList[idx] == kThemeFtp) {
            label = T("PC에서 테마 받기 (FTP)");
            dl = true;
        }
        bool cur = !dl && ((idx == 0 && gThemeName.empty()) || (idx > 0 && a.themeList[idx] == gThemeName));
        if (cur) label += T("  (사용 중)");
        SDL_Color c = dl ? m.title : sel ? m.selText : m.dim;
        DrawText(a, a.fList, label, bx + 32, y + 4, c, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    DrawText(a, a.fInfo, PadHint(T("[A] 적용   [X] 삭제   [B] 닫기   (테마 폴더: /JoyLauncher/themes)")), W / 2, by + bh - 34, m.dim, 0, 1);
    if (a.themeDelAsk > 0 && a.themeDelAsk < n) {
        std::string name = a.themeDelAsk < (int)a.themeTitles.size() && !a.themeTitles[a.themeDelAsk].empty() ? a.themeTitles[a.themeDelAsk]
                                                                                                             : a.themeList[a.themeDelAsk];
        Fill(a, 0, 0, W, H, {0, 0, 0, 120});
        NoticeBox(a, m, 240, 270, 800, 170, {120, 40, 40, 245});
        DrawText(a, a.fList, Fmt(T("'%s' 테마를 지울까요?"), name.c_str()), W / 2, 296, kWhite, 760, 1, 0, 0, kScrollMsg);
        DrawText(a, a.fInfo, Fmt(T("/JoyLauncher/themes/%s 폴더가 삭제됩니다"), a.themeList[a.themeDelAsk].c_str()), W / 2, 344, kWhite, 760, 1);
        DrawText(a, a.fInfo, PadHint(T("[A] 삭제   [B] 취소")), W / 2, 392, kAccent, 0, 1);
    }
}

// 앱 목록 창: 설치된 앱 전부. ★ = 앱 즐겨찾기에 넣음, ☆ = 안 넣음
static void RenderAppPick(App &a) {
    int n = (int)a.appList.size();
    int rows = std::min(n, (H - 16 - 110) / kRowH);
    int bw = 980, bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = std::max(8, (H - bh) / 2);
    MenuStyle m = Menu();
    MenuPanel(a, m, bx, by, bw, bh);
    DrawText(a, a.fBig, T("앱"), W / 2, by + 16, m.title, 0, 1);
    int top = std::max(0, std::min(a.appSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 66 + i * kRowH;
        bool sel = idx == a.appSel;
        if (sel) MenuSel(a, m, bx + 16, y - 4, bw - 32, kRowH);
        const Game &g = a.appList[idx];
        bool in = std::find(gAppFav.begin(), gAppFav.end(), AppsKey(g.file)) != gAppFav.end();
        std::string label = std::string(in ? "★ " : "☆ ") + g.title + "   " + g.developer;
        DrawText(a, a.fList, label, bx + 32, y + 4, in ? m.on : sel ? m.selText : m.dim, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    DrawText(a, a.fInfo, PadHint(Fmt(T("[A] 실행   [-] 앱 즐겨찾기에 넣기 / 빼기   [B] 닫기   (즐겨찾기 %d개)"), (int)gAppFav.size())), W / 2,
             by + bh - 34, m.dim, 0, 1);
}

// 에뮬레이터 선택 창: 지금 카테고리(롬 폴더)의 기종에서 쓸 수 있는 에뮬레이터. 초록 = 설치됨
static void RenderEmuPick(App &a) {
    int n = (int)a.emuList.size();
    int rows = std::min(n, (H - 16 - 150) / kRowH);
    int bw = 900, bh = 150 + rows * kRowH;
    int bx = (W - bw) / 2, by = std::max(8, (H - bh) / 2);
    MenuStyle m = Menu();
    MenuPanel(a, m, bx, by, bw, bh);
    DrawText(a, a.fBig, T("에뮬레이터 선택"), W / 2, by + 16, m.title, 0, 1);
    DrawText(a, a.fInfo, Fmt(T("대상: %s  ·  기종: %s"), a.emuKey.c_str(), a.emuSystem.empty() ? "?" : a.emuSystem.c_str()), W / 2, by + 62, m.dim,
             bw - 60, 1);
    int top = std::max(0, std::min(a.emuSel - rows / 2, n - rows));
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        int y = by + 106 + i * kRowH;
        bool sel = idx == a.emuSel;
        if (sel) MenuSel(a, m, bx + 16, y - 4, bw - 32, kRowH);
        const EmuChoice &ch = a.emuList[idx];
        std::string label = ch.label.empty() ? std::string(T("자동 (롬 모음의 설정 → 설치된 에뮬레이터)")) : ch.label;
        if (!ch.label.empty() && !ch.installed) label += T("  (설치 안 됨)");
        if (ch.current) label = "● " + label;
        SDL_Color off = m.themed ? SDL_Color{(Uint8)((m.dim.r + m.panel.r) / 2), (Uint8)((m.dim.g + m.panel.g) / 2), (Uint8)((m.dim.b + m.panel.b) / 2), 255}
                                 : SDL_Color{110, 110, 125, 255};
        SDL_Color col = !ch.installed ? off : ch.current ? m.on : sel ? m.selText : m.dim;
        DrawText(a, a.fList, label, bx + 32, y + 4, col, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    DrawText(a, a.fInfo, PadHint(T("[A] 선택   [B] 닫기   (기록: /JoyLauncher/emulators.txt)")), W / 2, by + bh - 34, m.dim, 0, 1);
}

static bool Adjustable(MenuAct m) {
    return m == MenuAct::Language || m == MenuAct::Effects || m == MenuAct::BgmVolume || m == MenuAct::BgmOrder || m == MenuAct::SwapAB ||
           m == MenuAct::Overlay || m == MenuAct::Video;
}

// 항목을 고르는 창 (옵션 창, BGM 창). bw: 창 너비
static void RenderPopup(App &a, const char *title, const std::vector<MenuItem> &items, int selIdx, int bw) {
    int n = (int)items.size();
    int rows = std::min(n, (H - 16 - 110) / kRowH);  // 화면에 다 안 들어가면 커서를 따라 넘김
    int top = std::max(0, std::min(selIdx - rows / 2, n - rows));
    int bh = 110 + rows * kRowH;
    int bx = (W - bw) / 2, by = std::max(8, (H - bh) / 2);
    MenuStyle m = Menu();
    MenuPanel(a, m, bx, by, bw, bh);
    DrawText(a, a.fBig, title, W / 2, by + 16, m.title, 0, 1);
    for (int i = top; i < top + rows; i++) {
        int y = by + 66 + (i - top) * kRowH;
        bool sel = i == selIdx;
        if (sel) MenuSel(a, m, bx + 16, y - 4, bw - 32, kRowH);
        SDL_Color c = items[i].on ? m.on : sel ? m.selText : m.dim;
        DrawText(a, a.fList, items[i].label, bx + 32, y + 4, c, bw - 64, 0, 0, 0, sel ? kScroll : 0);
    }
    bool adjustable = selIdx >= 0 && selIdx < n && Adjustable(items[selIdx].act);
    DrawText(a, a.fInfo, PadHint(adjustable ? T("[A] 선택   ◀ ▶ 조절   [B] 닫기") : T("[A] 선택   [B] 닫기")), W / 2, by + bh - 34, m.dim,
             0, 1);
}

// 옵션 창 (Y)
static void RenderOptions(App &a) { RenderPopup(a, T("옵션"), gMenu, a.optSel, 820); }

// PC에서 테마 받기 (FTP): 주소·포트, 받은 것
static void RenderFtp(App &a) {
    FtpServerStatus st = FtpServerGet();
    int bw = 900, bh = 520, bx = (W - bw) / 2, by = (H - bh) / 2;
    MenuStyle m = Menu();
    MenuPanel(a, m, bx, by, bw, bh, 150, 248);
    DrawText(a, a.fBig, T("PC에서 테마 받기 (FTP)"), W / 2, by + 18, m.title, 0, 1);
    if (!st.running) {
        DrawText(a, a.fList, T("FTP 를 열지 못했습니다: ") + st.error, W / 2, by + 120, m.text, bw - 60, 1);
    } else {
        std::string addr = "ftp://" + st.ip + ":" + std::to_string(st.port);
        DrawText(a, a.fTitle, addr, W / 2, by + 78, m.text, bw - 40, 1);
        DrawText(a, a.fInfo, T("테마 빌더 → 파일 → 스위치로 보내기 에 위 주소·포트를 적으세요 (사용자·비밀번호 비움)"), W / 2, by + 132, m.dim, bw - 40, 1);
        DrawText(a, a.fInfo, st.client ? T("PC 연결됨") : T("PC 를 기다리는 중…"), bx + 30, by + 176, st.client ? m.on : m.dim);
        DrawText(a, a.fInfo, Fmt(T("받은 파일 %d개 · 지운 것 %d개"), st.files, st.removed), bx + bw - 30, by + 176, m.text, 0, 2);
        int y = by + 214, from = std::max(0, (int)st.log.size() - 9);
        for (int i = from; i < (int)st.log.size(); i++, y += 30) DrawText(a, a.fDesc, st.log[i], bx + 30, y, m.dim, bw - 60);
    }
    DrawText(a, a.fInfo, PadHint(T("[B] 닫기 (받은 테마를 다시 읽음) · 이 화면이 열린 동안만 FTP 가 열립니다")), W / 2, by + bh - 40, m.dim, bw - 40, 1);
}

// 종료 확인
static void RenderExitAsk(App &a) {
    MenuStyle m = Menu();
    MenuPanel(a, m, 340, 270, 600, 160);
    DrawText(a, a.fBig, T("런처를 종료합니다"), W / 2, 300, m.text, 560, 1);
    DrawText(a, a.fInfo, PadHint(T("[A] 종료   [B] 돌아가기")), W / 2, 372, m.dim, 560, 1);
}

// 업데이트 창: 앱이 apk 를 받아 설치하지 않는다 (다른 앱 설치 권한 문제) → 릴리스 페이지를 알려 주고 브라우저로 연다
static void RenderUpdateLink(App &a) {
    MenuStyle m = Menu();
    int bw = 960, bh = 350, bx = (W - bw) / 2, by = (H - bh) / 2;
    MenuPanel(a, m, bx, by, bw, bh);
    bool newer = gUpdateFound && !gUpdate.version.empty();
    std::string title = newer ? Fmt(T("새 버전 v%s 이 있습니다"), gUpdate.version.c_str())
                      : !a.updErr.empty() ? std::string(T("업데이트 확인 실패"))
                                          : Fmt(T("최신 버전입니다 (v%s)"), CurrentVersion());
    DrawText(a, a.fBig, title, W / 2, by + 18, m.title, bw - 40, 1);
    DrawText(a, a.fInfo, !a.updErr.empty() ? a.updErr : Fmt(T("지금 버전: v%s"), CurrentVersion()), W / 2, by + 70, m.dim, bw - 60, 1, 0, 0,
             kScrollMsg);
    DrawText(a, a.fInfo, T("내려받는 곳 (브라우저 주소)"), W / 2, by + 112, m.dim, bw - 60, 1);
    std::string url = ReleasePageUrl(newer ? gUpdate.version : std::string());
    DrawText(a, a.fList, url, W / 2, by + 146, m.text, bw - 60, 1, 0, 0, kScrollMsg);
    DrawText(a, a.fInfo, T("페이지의 JoyLauncher.apk 를 받아 직접 설치하세요"), W / 2, by + 196, m.text, bw - 60, 1, 0, 0, kScrollMsg);
    DrawText(a, a.fInfo, T("덮어 설치하면 /JoyLauncher 의 설정·테마는 그대로입니다"), W / 2, by + 228, m.text, bw - 60, 1, 0, 0, kScrollMsg);
    DrawText(a, a.fInfo, T("처음이면 브라우저(또는 파일 앱)에 '출처를 알 수 없는 앱 설치' 허용이 필요할 수 있습니다"), W / 2, by + 266, m.dim, bw - 60, 1, 0, 0,
             kScrollMsg);
    DrawText(a, a.fInfo, PadHint(T("[A] 브라우저로 열기   [B] 닫기")), W / 2, by + bh - 44, m.dim, bw - 40, 1);
}

static void Render(App &a);

// 오래 걸리는 작업(내려받기) 도중: 쌓인 입력을 비우며 취소 버튼(B, A·B 를 바꿨으면 A)·뒤로·Esc 가 있었는지
static bool PollCancel() {
    bool cancel = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) cancel = true;
        if (e.type == SDL_CONTROLLERBUTTONDOWN &&
            e.cbutton.button == (gSwapAB ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B))
            cancel = true;
        if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK)) cancel = true;
        if (e.type == SDL_FINGERDOWN) cancel = true;  // 컨트롤러 없이 터치만 쓰는 기기: 화면을 누르면 멈춤
    }
    return cancel;
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

// "앱 즐겨찾기": 앱 목록에서 고른 앱. 카테고리 목록 맨 위(즐겨찾기 바로 아래), 하나라도 있을 때만.
// keepPos: 보던 카테고리·커서를 그대로 (그 카테고리가 사라졌으면 카테고리 목록으로)
static void RebuildAppFavorites(App &a, bool keepPos) {
    auto key = [&](int i) {
        return i >= 0 && i < (int)a.cols.size() ? a.cols[i].shortName + "\x1f" + a.cols[i].name : std::string();
    };
    std::string colKey = key(a.col), catKey = key(a.catSel);
    a.cols.erase(std::remove_if(a.cols.begin(), a.cols.end(),
                                [](const Collection &c) { return c.virt && c.shortName == "hbfav"; }),
                 a.cols.end());
    if (!gAppFav.empty()) {
        Collection hb = AppsFavorites(gAppFav, gHome + "/cache");
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
    a.sel = std::min(a.sel, std::max(0, (int)CurCol(a).games.size() - 1));
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
#ifdef JOY_BUILDER
    gBaseCols = a.cols;
#endif
    LoadCustomCategories();
    ApplyCustomCategories(a.cols);
    EmuLearnCores(a.cols);
    BuildMenu();
    RebuildAppFavorites(a, false);
    RebuildFavorites(a, false);
}

static void Render(App &a) {
    ImgPump(a.r);
    MarqueeNextFrame();
    if (a.cols.empty()) {
        Frame(a, "Joy Launcher", PadHint(T("[B] 종료     [X] BGM     [Y] 옵션")));
        DrawText(a, a.fBig, T("게임 목록이 없습니다"), W / 2, 280, kWhite, 0, 1);
        DrawText(a, a.fInfo, T("저장소의 ROMs (또는 Roms·roms) 폴더에 게임을 넣거나, /JoyLauncher/roots.txt 에 롬 폴더를 적으세요"), W / 2, 340, kGrey,
                 1180, 1);
        DrawText(a, a.fInfo, T("페가수스(game_dirs.txt)·ES-DE 의 롬 폴더는 자동으로 찾습니다"), W / 2, 376, kGrey, 1180, 1);
    } else if (a.mode == Mode::Categories) {
        RenderCategories(a);
    } else {
        RenderGames(a);
    }
    if (a.optPick) RenderOptions(a);
    if (a.bgmPick) RenderPopup(a, T("BGM · 소리 설정"), gBgmMenu, a.bgmSel, 760);
    if (a.themePick) RenderThemePick(a);
    if (a.appPick) RenderAppPick(a);
    if (a.emuPick) RenderEmuPick(a);
    if (a.exitAsk) RenderExitAsk(a);
    if (a.updLink) RenderUpdateLink(a);
    if (a.ftpOpen) RenderFtp(a);
    if (a.messageModal) {
        Fill(a, 0, 0, W, H, {0, 0, 0, 120});
        NoticeBox(a, Menu(), 240, 280, 800, 140, a.messageColor);
        DrawText(a, a.fList, a.message, W / 2, 310, kWhite, 760, 1, 0, 0, kScrollMsg);
        DrawText(a, a.fInfo, PadHint(T("[B] 닫기")), W / 2, 370, kWhite, 0, 1);
    } else if (!a.message.empty() && SDL_GetTicks() < a.messageUntil) {
        // 여러 줄 알림 ("\n" 으로 나눔): 줄마다 가운데 정렬, 한 번에 보이게
        std::vector<std::string> lines;
        std::stringstream ss(a.message);
        for (std::string l; std::getline(ss, l, '\n');) lines.push_back(l);
        int n = std::max(1, (int)lines.size()), lh = 40;
        int boxH = 50 + n * lh, top = 345 - boxH / 2;
        NoticeBox(a, Menu(), 240, top, 800, boxH, a.messageColor);
        for (int i = 0; i < n; i++)
            DrawText(a, a.fList, lines[i], W / 2, top + 30 + i * lh - (n > 1 ? 5 : 0), kWhite, 760, 1, 0, 0, kScrollMsg);
    }
}

// 화면 전환 효과 (layout.txt [theme] transition): 직전 화면을 한 장으로 남겨 새 화면과 섞거나 밀어냄.
// out: 최종으로 그릴 곳 (nullptr = 화면). 전환이 없으면 그대로 Render
static SDL_Texture *gTrCur = nullptr, *gTrPrev = nullptr;
static Uint32 gTrAt = 0;
static int gTrDir = 1, gTrCol = -1, gTrView = 0;
static Mode gTrMode = Mode::Categories;
static bool gTrInit = false, gTrPrevOk = false;
static void RenderFrame(App &a, SDL_Texture *out) {
    const Layout &l = gTheme.layout;
    bool on = l.transition > 0 && l.transitionMs > 0 && AnimOn();
    if (!on) {
        gTrInit = false;
        SDL_SetRenderTarget(a.r, out);
        Render(a);
        return;
    }
    if (!gTrCur) {
        gTrCur = SDL_CreateTexture(a.r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ToPx(W), ToPx(H));
        gTrPrev = SDL_CreateTexture(a.r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ToPx(W), ToPx(H));
        if (!gTrCur || !gTrPrev) {  // 렌더 대상 그림을 못 만들면 전환 없이
            gTheme.layout.transition = 0;
            SDL_SetRenderTarget(a.r, out);
            Render(a);
            return;
        }
    }
    Uint32 now = SDL_GetTicks();
    if (gTrInit && (a.mode != gTrMode || (a.mode == Mode::Games && (a.col != gTrCol || a.view != gTrView)))) {
        std::swap(gTrCur, gTrPrev);  // 직전 프레임 = 지난 화면
        gTrPrevOk = true;
        gTrAt = now;
        int n = std::max(1, (int)a.cols.size());
        if (a.mode != gTrMode) gTrDir = a.mode == Mode::Games ? 1 : -1;
        else if (a.col != gTrCol) gTrDir = ((a.col - gTrCol + n) % n) == 1 ? 1 : -1;
        else gTrDir = ((a.view - gTrView + 3) % 3) == 1 ? 1 : -1;
    }
    gTrInit = true;
    gTrMode = a.mode;
    gTrCol = a.col;
    gTrView = a.view;
    SDL_SetRenderTarget(a.r, gTrCur);
    if (gScale != 1.0f) SDL_RenderSetLogicalSize(a.r, W, H);  // 이 그림 안에서도 1280x720 좌표로
    Render(a);
    SDL_SetRenderTarget(a.r, out);
    float t = (now - gTrAt) / (float)l.transitionMs;
    SDL_SetTextureBlendMode(gTrCur, SDL_BLENDMODE_NONE);
    if (gTrPrevOk && t < 1.0f) {
        float k = Ease(t);
        SDL_SetTextureBlendMode(gTrPrev, SDL_BLENDMODE_NONE);
        if (l.transition == 1) {
            SDL_RenderCopy(a.r, gTrPrev, nullptr, nullptr);
            SDL_SetTextureBlendMode(gTrCur, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(gTrCur, (Uint8)(255 * k));
            SDL_RenderCopy(a.r, gTrCur, nullptr, nullptr);
            SDL_SetTextureAlphaMod(gTrCur, 255);
        } else {
            int off = (int)(W * k);
            SDL_Rect pr = {-gTrDir * off, 0, W, H}, cr = {gTrDir * (W - off), 0, W, H};
            SDL_RenderCopy(a.r, gTrPrev, nullptr, &pr);
            SDL_RenderCopy(a.r, gTrCur, nullptr, &cr);
        }
    } else {
        SDL_RenderCopy(a.r, gTrCur, nullptr, nullptr);
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
    int n = (int)CurCol(a).games.size();
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
    // 캐러셀: 방향대로 한 칸, 다른 방향은 보이는 칸 수만큼
    if (const Elem *l = (games ? gTheme.layout.games : gTheme.layout.categories).List())
        if (l->style == 2) {
            bool hz = l->direction == 0;
            int page = std::max(1, l->visibleItems);
            if (b == (hz ? 12 : 13)) return sel - 1;
            if (b == (hz ? 14 : 15)) return sel + 1;
            if (b == (hz ? 13 : 12)) return sel - page;
            return sel + page;
        }
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
      << (CurCol(a).games.empty() ? std::string() : CurCol(a).games[a.sel].file) << "\n"
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

// 실행 기록: /JoyLauncher/launch_log.txt 에 최근 kLogMax 개만 (최신이 위)
static const size_t kLogMax = 20;

// what: 실행한 에뮬레이터·앱과 인텐트 인자, how: 그렇게 정한 근거
static void LogLaunch(const App &a, const Game &g, const std::string &what, const std::string &how, const std::string &result) {
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
    // 게임이 하나도 없어도 앱 목록에서 앱을 실행할 수 있다: 카테고리가 없을 수 있음
    e += T("  카테고리: ") + (a.cols.empty() ? std::string("-") : T(a.cols[std::max(0, std::min(a.col, (int)a.cols.size() - 1))].name)) + "\n";
    e += T("  롬 파일: ") + g.file + "\n";
    e += T("  목록의 에뮬레이터: ") + (g.metaCore.empty() ? std::string(T("(메타데이터에 없음)")) : g.metaCore) + "\n";
    e += T("  선택 방법: ") + (how.empty() ? std::string("?") : T(how)) + "\n";
    e += T("  실행: ") + (what.empty() ? std::string(T("(없음)")) : what) + "\n";
    e += T("  결과: ") + result + "\n";
    entries.insert(entries.begin(), e);
    if (entries.size() > kLogMax) entries.resize(kLogMax);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << Fmt(T("# Joy Launcher 실행 기록 (최근 %d개, 최신이 위)"), (int)kLogMax) << "\n\n";
    for (const auto &s : entries) o << s << "\n";
}

static void Notice(App &a, const std::string &text, SDL_Color color, Uint32 ms) {
    a.message = text;
    a.messageColor = color;
    a.messageUntil = SDL_GetTicks() + ms;
}
static const SDL_Color kMsgRed = {120, 40, 40, 240}, kMsgGreen = {40, 100, 60, 240}, kMsgBlue = {60, 70, 120, 240};

// 에뮬레이터를 고를 때 쓰는 정보: 열린 카테고리와 롬 폴더들
static LaunchCtx MakeLaunchCtx(App &a, const Game *g) {
    LaunchCtx c;
    c.game = g;
    if (!a.cols.empty()) {
        const Collection &col = a.cols[std::max(0, std::min(a.col, (int)a.cols.size() - 1))];
        c.category = col.name;
        c.shortName = col.shortName;
        c.folders = col.folders;
    }
    c.roots = gRoots;
    return c;
}

// 앱(앱 즐겨찾기·앱 목록의 항목) 실행. 런처는 뒤에 남아 있다가 앱을 끝내면 다시 보인다
static bool LaunchApp(App &a, const Game &g) {
    if (gDemo) {
        Notice(a, T("데모: 실행 대신 알림 — ") + g.title, kMsgGreen, 2500);
        return false;
    }
    SaveState(a, true);
    VideoStop();
    std::string err;
    bool ok = AppsLaunch(g, err);
    if (ok) gVideoHoldUntil = SDL_GetTicks() + 5000;
    if (ok) CountPlay(g.file);
    else Notice(a, T("앱을 실행하지 못했습니다: ") + err, kMsgRed, 4500);
    LogLaunch(a, g, AppsKey(g.file), T("앱"), ok ? std::string(T("실행함")) : T("실패: ") + err);
    return ok;
}

// 게임 실행: 에뮬레이터를 정해(메타데이터 → 롬 폴더의 기종 → 설치된 에뮬레이터) 인텐트로 연다
static bool LaunchGame(App &a) {
    if (CurCol(a).games.empty()) return false;
    const Game &g = CurCol(a).games[a.sel];
    if (g.launch == kLaunchApp) return LaunchApp(a, g);
    LaunchCtx ctx = MakeLaunchCtx(a, &g);
    LaunchPlan plan;
    std::string err;
    if (!EmuBuildLaunch(ctx, plan, err)) {
        Notice(a, err, kMsgRed, 6000);
        LogLaunch(a, g, "", plan.how, T("실행 안 함: ") + err);
        return false;
    }
    std::string what = plan.emulator + "  |  am start";
    for (const auto &s : plan.args) what += " " + (s.find(' ') == std::string::npos ? s : "'" + s + "'");
    if (gDemo) {
        Notice(a, T("데모: 실행 대신 알림 — ") + g.title + " (" + plan.emulator + ")", kMsgGreen, 2500);
        return false;
    }
    SaveState(a, true);
    VideoStop();
    bool ok = plat::StartActivity(plan.args, err);
    if (ok) {
        gVideoHoldUntil = SDL_GetTicks() + 5000;
        CountPlay(g.file);
        Notice(a, Fmt(T("%s 실행 중…"), plan.emulator.c_str()), kMsgBlue, 2500);
    } else {
        Notice(a, Fmt(T("실행하지 못했습니다 (%s)"), plan.emulator.c_str()) + std::string("\n") + err, kMsgRed, 7000);
    }
    LogLaunch(a, g, what, plan.how, ok ? std::string(T("실행함")) : T("실패: ") + err);
    return ok;
}

// ---------------------------------------------------------------- 롬 폴더 찾기
// 줄 끝 공백·/ 를 떼고, 내부 저장소의 다른 이름(/sdcard 등)을 실제 경로로
static std::string CleanPath(std::string p) {
    while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || p.back() == ' ' || p.back() == '\t')) p.pop_back();
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t lead = p.find_first_not_of(" \t");
    if (lead == std::string::npos) return "";
    p = p.substr(lead);
    for (const char *alias : {"/sdcard", "/storage/self/primary", "/mnt/sdcard"}) {
        size_t n = strlen(alias);
        if (p.compare(0, n, alias) == 0 && (p.size() == n || p[n] == '/')) {
            p = plat::PrimaryStorage() + p.substr(n);
            break;
        }
    }
    return p;
}

// 롬 폴더 목록.
//   /JoyLauncher/roots.txt 에 적은 폴더 (esde=<ES-DE 폴더>, auto=off 도 여기에)
//   자동: 페가수스의 game_dirs.txt, ES-DE 설정의 롬 폴더, 저장소(내부·SD 카드)마다 ROMs·Roms·roms
static std::vector<std::string> FindRoots(const std::string &first, bool autoDefault, std::string &log) {
    std::vector<std::string> roots;
    auto add = [&](const std::string &raw, const char *why) {
        std::string p = CleanPath(raw);
        if (p.empty() || p[0] == '#' || !IsDirPath(p) || std::find(roots.begin(), roots.end(), p) != roots.end()) return;
        roots.push_back(p);
        log += "[root] " + p + " (" + why + ")\n";
    };
    bool autoFind = autoDefault;
    if (!first.empty()) add(first, "--root");
    {
        std::ifstream rf(gHome + "/roots.txt");
        std::string line;
        bool firstLine = true;
        while (std::getline(rf, line)) {
            if (firstLine && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
            firstLine = false;
            std::string t = CleanPath(line);
            if (t.empty() || t[0] == '#') continue;
            if (t.rfind("esde=", 0) == 0) { SetEsdeHint(CleanPath(t.substr(5))); continue; }
            if (t.rfind("auto=", 0) == 0) { autoFind = LowerAscii(t.substr(5)) != "off"; continue; }
            add(t, "roots.txt");
        }
    }
    std::vector<plat::Volume> vols = plat::Volumes();
    std::vector<std::string> esde;
    for (const auto &v : vols) esde.push_back(v.path + "/ES-DE");
    SetEsdeCandidates(esde);
    if (!autoFind) return roots;
    for (const auto &v : vols) {
        // 페가수스: 게임 폴더 목록 (한 줄에 폴더 하나)
        std::ifstream gd(v.path + "/pegasus-frontend/game_dirs.txt");
        for (std::string line; std::getline(gd, line);) add(line, "pegasus game_dirs.txt");
        // ES-DE: 설정에 적힌 롬 폴더
        std::ifstream es(v.path + "/ES-DE/settings/es_settings.xml", std::ios::binary);
        std::stringstream ss;
        ss << es.rdbuf();
        std::string x = ss.str();
        size_t p = x.find("name=\"ROMDirectory\"");
        if (p != std::string::npos && (p = x.find("value=\"", p)) != std::string::npos) {
            p += 7;
            add(x.substr(p, x.find('"', p) - p), "ES-DE ROMDirectory");
        }
    }
    for (const auto &v : vols)
        for (const auto &d : ListSubdirs(v.path)) {
            std::string low = LowerAscii(d);
            if (low == "roms" || low == "rom") add(v.path + "/" + d, "storage");
            if (low == "emulation") add(v.path + "/" + d + "/roms", "storage");
        }
    return roots;
}

// 처음 실행: APK 에 든 기본 테마(assets 의 themes/<팩>.zip)를 /JoyLauncher/themes 에 풀어 둔다.
// 설정 파일이 이미 있으면(= 처음이 아님) 하지 않는다: 사용자가 테마를 지운 것일 수 있음
static void InstallBundledThemes(App &a) {
    if (!ThemeNames().empty() || std::ifstream(gHome + "/settings.txt").good()) return;
    mkdir((gHome + "/themes").c_str(), 0777);
    for (const auto &p : kPacks) {
        std::vector<char> zip;
        if (!plat::ReadAsset(std::string("themes/") + p.folder + ".zip", zip)) continue;
        SDL_SetRenderDrawColor(a.r, 16, 16, 22, 255);
        SDL_RenderClear(a.r);
        DrawText(a, a.fBig, T("기본 테마를 준비하는 중..."), W / 2, 300, kWhite, 0, 1);
        DrawText(a, a.fInfo, PackName(p), W / 2, 360, kGrey, 0, 1);
        SDL_RenderPresent(a.r);
        std::string tmp = plat::PrivateDir() + "/joy_theme_tmp.zip";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) continue;
        bool ok = fwrite(zip.data(), 1, zip.size(), f) == zip.size();
        if (fclose(f) != 0) ok = false;
        int files = 0;
        std::string err;
        if (ok && !ExtractThemeZip(tmp, std::string(p.folder) + "/", gHome + "/themes/" + p.folder, files, err))
            printf("bundled theme %s: %s\n", p.folder, err.c_str());
        remove(tmp.c_str());
    }
}

// 닌텐도 계열 컨트롤러인지 (도움말의 [+] [-] 를 그대로 둘지, START·SELECT 로 바꿀지)
static bool IsNintendoPad(SDL_GameController *gc) {
    if (!gc) return false;
    switch (SDL_GameControllerGetType(gc)) {
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return true;
    default:
        break;
    }
    const char *n = SDL_GameControllerName(gc);
    std::string name = LowerAscii(n ? n : "");
    for (const char *k : {"nintendo", "joy-con", "joycon", "switch"})
        if (name.find(k) != std::string::npos) return true;
    return false;
}

static bool UpdateScale(App &a);
static void Rescale(App &a);

// 저장소 권한이 없으면 안내 화면을 띄우고 허용될 때까지 기다린다. 종료를 골랐으면 false
static bool WaitStorageAccess(App &a) {
    Uint32 asked = 0;
    bool first = true;
    while (!plat::HasStorageAccess()) {
        bool go = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button <= SDL_CONTROLLER_BUTTON_Y) go = true;
            if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START) go = true;
            if (e.type == SDL_FINGERDOWN) go = true;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_AC_BACK || k == SDLK_ESCAPE) return false;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_SELECT) go = true;
            }
        }
        if (go && (first || SDL_GetTicks() - asked > 1500)) {
            first = false;
            asked = SDL_GetTicks();
            plat::RequestStorageAccess();
        }
        if (UpdateScale(a)) Rescale(a);
        SDL_SetRenderDrawColor(a.r, 16, 16, 22, 255);
        SDL_RenderClear(a.r);
        DrawText(a, a.fTitle, "Joy Launcher", W / 2, 150, kAccent, 0, 1);
        DrawText(a, a.fBig, T("저장소의 모든 파일에 접근할 권한이 필요합니다"), W / 2, 260, kWhite, 1180, 1);
        DrawText(a, a.fInfo, T("롬 폴더를 읽고, 저장소의 JoyLauncher 폴더에 설정·테마를 두기 위해서입니다"), W / 2, 330, kGrey, 1180, 1);
        DrawText(a, a.fInfo, T("아무 버튼이나 화면을 누르면 설정 화면이 열립니다 → Joy Launcher 를 허용한 뒤 돌아오세요"), W / 2, 372, kGrey, 1180, 1);
        DrawText(a, a.fInfo, T("뒤로 가기 = 종료"), W / 2, 450, kGrey, 0, 1);
        SDL_RenderPresent(a.r);
        SDL_Delay(30);
    }
    return true;
}

// 런처가 늘 쓰는 글꼴 (창·알림용)
static void OpenAppFonts(App &a) {
    for (TTF_Font **f : {&a.fTitle, &a.fBig, &a.fList, &a.fInfo, &a.fDesc})
        if (*f) { TTF_CloseFont(*f); *f = nullptr; }
    a.fTitle = OpenFontMem(gFontBold, 34);
    a.fBig = OpenFontMem(gFontBold, 30);
    a.fList = OpenFontMem(gFontReg, 27);
    a.fInfo = OpenFontMem(gFontReg, 22);
    a.fDesc = OpenFontMem(gFontReg, 20);
}

// 기기 화면 크기에서 배율을 정한다 (화면이 1920x1080 이면 1.5). 바뀌었으면 true
static bool UpdateScale(App &a) {
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(a.r, &ow, &oh) != 0 || ow <= 0 || oh <= 0) return false;
    float s = std::max(0.5f, std::min(4.0f, std::min(ow / (float)W, oh / (float)H)));
    if (fabsf(s - gScale) < 0.001f) return false;
    gScale = s;
    return true;
}

static void ResetGpu(App &a, bool all);
// 배율이 바뀌었을 때 (화면 크기 변경): 글꼴·글자·작은 그림을 새 해상도로 다시 만든다
static void Rescale(App &a) {
    a.text.Clear();
    a.thumbs.Clear();
    OpenAppFonts(a);
    gFonts.serial = 0xffffffffu;  // 다음에 그릴 때 테마 글꼴도 다시 연다 (FontsSync)
    ResetGpu(a, false);
}

// 그림 장치가 다시 만들어졌을 때: 올려 둔 그림을 모두 버린다 (다음에 그릴 때 다시 읽음)
static void ResetGpu(App &a, bool all) {
    if (all) {
        a.text.Clear();
        a.images.Clear();
        a.backs.Clear();
        a.thumbs.Clear();
        a.lastBack.clear();
        OverlayReset();
        VideoResetGpu();
    }
    if (gTrCur) SDL_DestroyTexture(gTrCur);
    if (gTrPrev) SDL_DestroyTexture(gTrPrev);
    gTrCur = gTrPrev = nullptr;
    gTrInit = gTrPrevOk = false;
}

#ifndef JOY_BUILDER
int main(int argc, char **argv) {
    std::string shot;
    int wantCol = -1, wantSel = -1, wantMode = -1, wantCat = -1, wantOpt = -1;
    std::string wantTheme, wantLang, rootArg;
    bool wantPick = false;
    bool wantAppPick = false;   // 시험: 앱 목록 창
    bool wantBgm = false;       // 시험: BGM · 소리 설정 창
    bool wantEmuPick = false;   // 시험: 에뮬레이터 선택 창
    bool wantFtp = false;
    bool wantUpdLink = false, wantUpdErr = false;
    bool dumpLaunch = false;    // 시험: 모든 게임의 실행 인자를 적고 끝
    bool homeArg = false;
#ifdef __ANDROID__
    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");        // 뒤로 가기 = B (취소)
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");       // 기울기 센서를 컨트롤러로 세지 않음
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");                 // 컨트롤러는 안드로이드 입력으로 (버튼 이름이 기기 설정을 따름)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");            // 1280x720 화면을 기기 해상도로 늘릴 때 부드럽게
#else
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--root" && i + 1 < argc) rootArg = argv[++i];
        else if (s == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (s == "--col" && i + 1 < argc) wantCol = atoi(argv[++i]);
        else if (s == "--cat" && i + 1 < argc) wantCat = atoi(argv[++i]);  // 카테고리 화면 커서
        else if (s == "--opt" && i + 1 < argc) wantOpt = atoi(argv[++i]);  // 옵션 창 열고 커서
        else if (s == "--lang" && i + 1 < argc) wantLang = argv[++i];  // ko / en (설정보다 우선)
        else if (s == "--theme-unzip" && i + 3 < argc) {  // 테마 zip 풀기 시험: --theme-unzip <zip> <zip 안 폴더/> <풀 곳>
            int files = 0;
            std::string err;
            bool ok = ExtractThemeZip(argv[i + 1], argv[i + 2], argv[i + 3], files, err);
            printf("unzip ok=%d files=%d err=%s\n", ok, files, err.c_str());
            return ok ? 0 : 1;
        } else if (s == "--theme-download") {  // --home 의 themes 에 테마 팩 받기 시험: --theme-download [폴더] (기본 JoyBasic)
            plat::Init();
            int files = 0;
            std::string err, pack = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "JoyBasic";
            bool ok = DownloadTheme(gHome + "/themes", pack, files, err, [](long long, long long) { return true; });
            printf("theme ok=%d files=%d err=%s\n", ok, files, err.c_str());
            UpdaterShutdown();
            return ok ? 0 : 1;
        } else if (s == "--update-check") {  // 깃헙 최신 릴리스 조회 결과 출력
            plat::Init();
            UpdateInfo u;
            std::string err;
            bool ok = FetchLatestRelease(u, err);
            printf("current=%s ok=%d latest=%s newer=%d size=%lld sha256=%s url=%s err=%s\n", CurrentVersion(), ok,
                   u.version.c_str(), ok ? CompareVersions(u.version, CurrentVersion()) > 0 : 0, u.size,
                   u.sha256.c_str(), u.url.c_str(), err.c_str());
            return ok ? 0 : 1;
        } else if (s == "--sel" && i + 1 < argc) wantSel = atoi(argv[++i]);
        else if (s == "--mode" && i + 1 < argc) wantMode = atoi(argv[++i]);
        else if (s == "--home" && i + 1 < argc) { gHome = argv[++i]; homeArg = true; }
        else if (s == "--theme" && i + 1 < argc) wantTheme = argv[++i];
        else if (s == "--themepick") wantPick = true;
        else if (s == "--demo") gDemo = true;
        else if (s == "--ftp") wantFtp = true;  // 시험: FTP 받기 화면을 열고 시작
        else if (s == "--apppick") wantAppPick = true;
        else if (s == "--bgm") wantBgm = true;
        else if (s == "--emupick") wantEmuPick = true;
        else if (s == "--updlink" && i + 1 < argc) {  // 시험: 업데이트 창 (버전 "-" = 최신, "!" = 확인 실패)
            std::string v = argv[++i];
            wantUpdLink = true;
            if (v == "!") wantUpdErr = true;
            else if (v != "-") { gUpdate.version = v; gUpdateFound = true; }
        }
        else if (s == "--dump-launch") dumpLaunch = true;
    }
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    OverlayWatchTouches();  // 렌더러를 만들기 전에 (터치의 원래 좌표를 먼저 봐야 함)
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();
    plat::Init();
#ifdef __ANDROID__
    SDL_Window *win = SDL_CreateWindow("Joy Launcher", 0, 0, W, H, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
#else
    // JOY_SCALE=1.5 (또는 JOY_WINDOW=2400x1080): 고해상도·다른 비율 화면 흉내
    int winW = W, winH = H;
    if (const char *sc = getenv("JOY_SCALE")) { winW = (int)lroundf(W * (float)atof(sc)); winH = (int)lroundf(H * (float)atof(sc)); }
    if (const char *ws = getenv("JOY_WINDOW")) sscanf(ws, "%dx%d", &winW, &winH);
    SDL_Window *win = SDL_CreateWindow(gDemo ? "Joy Launcher 데모 :  Enter/Space=A  Esc=B  T=X  Y=Y  Q/W=L/R  Z/X=ZL/ZR  F=-  방향키" : "Joy Launcher",
                                       gDemo ? SDL_WINDOWPOS_CENTERED : 0, gDemo ? SDL_WINDOWPOS_CENTERED : 0, winW, winH,
                                       shot.empty() ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    // 데모 창 제목(한글): 만든 뒤 다시 정해야 X11 의 _NET_WM_NAME(UTF-8) 이 붙어 제목 표시줄에서 깨지지 않음
    if (gDemo && win) SDL_SetWindowTitle(win, SDL_GetWindowTitle(win));
#endif
    App a;
    a.r = SDL_CreateRenderer(win, -1, shot.empty() ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
                                                   : SDL_RENDERER_SOFTWARE);
    if (!a.r) a.r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!a.r) { printf("renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(a.r, SDL_BLENDMODE_BLEND);
    // 화면은 늘 1280x720 좌표로 그린다 (테마의 좌표가 이 크기 기준). 기기 화면에는 비율을 지켜 늘려 맞춤
    SDL_RenderSetLogicalSize(a.r, W, H);
    a.text.r = a.r;
    a.images.r = a.r;
    a.backs.r = a.r;
    a.backs.cap = 4;
    a.thumbs.r = a.r;
    if (!plat::ReadAsset("font.ttf", gFontReg) || !plat::ReadAsset("font_bold.ttf", gFontBold)) { printf("font read failed\n"); return 1; }
    UpdateScale(a);
    OpenAppFonts(a);
    LayoutDefault(gTheme.layout);

    gSystemLang = SystemLang();
    ApplyLanguage();
    if (!wantLang.empty()) SetLang(wantLang == "ko" ? Lang::Ko : Lang::En);
    if (!WaitStorageAccess(a)) return 0;
#ifdef __ANDROID__
    gHome = plat::PrimaryStorage() + "/JoyLauncher";
    homeArg = true;
#endif
    (void)homeArg;
    mkdir(gHome.c_str(), 0777);
    gShotMode = !shot.empty();
    if (!gShotMode) InstallBundledThemes(a);
    LoadSettings();
    if (!wantLang.empty()) { gLanguage = wantLang; ApplyLanguage(); }
#ifndef __ANDROID__
    if (const char *m = getenv("JOY_MARQUEE_MS")) gMarqueeShift = (Uint32)atoi(m);
#endif
    MakeUserFolders();
    gAnimAllowed = !gShotMode;
    LoadUserImages();
    if (!gShotMode) {
        gBgmLive = true;
        ApplyBgm();
    }
    ApplyVideo();
    if (!gShotMode && !gDemo && gAutoUpdate == "on" && UpdaterEnabled()) UpdateCheckStart();
    remove((plat::PrivateDir() + "/JoyLauncher-update.apk").c_str());  // 1.2.0 까지 앱이 받아 두던 업데이트 apk
    if (!gShotMode) ImgLoaderStart();
    // 이전에 중단된 임시 파일 정리
    if (DIR *d = opendir((gHome + "/cache").c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0) remove((gHome + "/cache/" + n).c_str());
        }
        closedir(d);
    }
    if (!wantTheme.empty()) LoadTheme(wantTheme == "-" ? "" : wantTheme);
    std::string log;
#ifdef __ANDROID__
    std::vector<std::string> roots = FindRoots("", true, log);
#else
    // 데스크톱 시험판: --root (기본 "roms"). JOY_PRIMARY 를 정했으면 안드로이드처럼 저장소에서도 찾음
    std::vector<std::string> roots = FindRoots(rootArg.empty() && !getenv("JOY_PRIMARY") ? gRoot : rootArg, getenv("JOY_PRIMARY") != nullptr, log);
#endif
    {
        std::string esde = EsdeLocate(roots);
        EmuLoad(gHome, esde.empty() ? std::vector<std::string>() : std::vector<std::string>{esde});
    }
    LoadFavorites();
    LoadAppFav();
    LoadPlayCounts();
    gRoots = roots;
    {
        std::string scan;
        LoadGames(a, false, scan);
        log += scan;
    }
    printf("%s", log.c_str());
    for (auto &c : a.cols) printf("collection %s (%s): %d\n", c.name.c_str(), c.sortBy.c_str(), (int)c.games.size());
#ifndef __ANDROID__
    if (dumpLaunch) {  // 시험: 게임마다 고른 에뮬레이터와 인텐트 인자
        for (size_t ci = 0; ci < a.cols.size(); ci++) {
            a.col = (int)ci;
            for (const auto &g : a.cols[ci].games) {
                if (g.launch == kLaunchApp) continue;
                LaunchCtx ctx = MakeLaunchCtx(a, &g);
                LaunchPlan plan;
                std::string err;
                bool ok = EmuBuildLaunch(ctx, plan, err);
                printf("GAME %s | %s\n  launch: %s\n  ok=%d emu=%s how=%s err=%s\n  args:", a.cols[ci].name.c_str(), g.file.c_str(),
                       g.launch.substr(0, 100).c_str(), ok, plan.emulator.c_str(), plan.how.c_str(), err.c_str());
                for (const auto &s : plan.args) printf(" [%s]", s.c_str());
                printf("\n");
            }
        }
        return 0;
    }
#endif
    (void)dumpLaunch;
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
        // 함께 배포하는 팩(기본테마·레트로테마·벽골김씨테마·레트로게임갤러리테마)을 앞에, 나머지는 이름순
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
        a.themeList.push_back(kThemeDownload);  // 기본 테마 전부 한 번에
        a.themeTitles.push_back("");
        a.themeList.push_back(kThemeFtp);
        a.themeTitles.push_back("");
        a.themeSel = 0;
        for (size_t i = 1; i < a.themeList.size(); i++)
            if (a.themeList[i] == gThemeName) a.themeSel = (int)i;
        a.themePick = true;
    };
    auto openFtp = [&]() {
        std::string err;
        a.ftpOpen = true;
        a.optPick = false;
        a.themePick = false;
        if (!FtpServerStart(gHome, 5000, err)) {
            a.ftpOpen = false;
            a.message = T("FTP 를 열지 못했습니다: ") + err;
            a.messageColor = kMsgRed;
            a.messageModal = true;
        }
    };
    auto closeFtp = [&]() {
        FtpServerStatus st = FtpServerGet();
        FtpServerStop();
        a.ftpOpen = false;
        if (st.files || st.removed) {
            // 받은 테마·내 카테고리 다시 읽기
            std::string keep = gThemeName;
            LoadTheme(IsDirPath(gHome + "/themes/" + keep) ? keep : "");
            if (gThemeName != keep) SaveSettings();
            a.backs.Clear();
            a.lastBack.clear();
            a.images.Clear();
            a.thumbs.Clear();
            LoadUserImages();
            SaveState(a);
            std::string log2;
            LoadGames(a, false, log2);
            LoadState(a, true);
            BuildMenu();
            Clamp(a);
            LayoutNotice(a);
            Notice(a, Fmt(T("FTP: 받은 파일 %d개 · 지운 것 %d개 — 테마를 다시 읽었습니다"), st.files, st.removed), kMsgGreen, 3500);
        }
    };
    // 앱 목록 창 (X): 설치된 앱 실행·앱 즐겨찾기
    auto openAppPick = [&]() {
        a.appList = AppsScan();
        if (a.appList.empty()) {
            Notice(a, T("실행할 수 있는 앱을 찾지 못했습니다"), kMsgBlue, 3500);
            return;
        }
        a.optPick = false;
        a.appPick = true;
        a.appSel = std::min(a.appSel, (int)a.appList.size() - 1);
    };
    // 에뮬레이터 선택 창: 지금 카테고리(게임 화면이면 고른 게임의 롬 폴더)의 기종
    auto openEmuPick = [&]() {
        const Game *g = nullptr;
        LaunchCtx ctx;
        if (!a.cols.empty()) {
            if (a.mode == Mode::Games) {
                if (!CurCol(a).games.empty()) g = &CurCol(a).games[a.sel];
                ctx = MakeLaunchCtx(a, g);
            } else {
                int keep = a.col;
                a.col = a.catSel;
                if (!a.cols[a.col].games.empty() && !a.cols[a.col].virt) g = &a.cols[a.col].games[0];
                ctx = MakeLaunchCtx(a, g);
                a.col = keep;
            }
        }
        bool app = g && g->launch == kLaunchApp;
        if (!g || app) {
            Notice(a, T("게임이 있는 카테고리(또는 게임)를 고른 뒤 여세요"), kMsgBlue, 3500);
            return;
        }
        a.emuList = EmuChoices(ctx, a.emuSystem);
        if (a.emuList.size() <= 1) {
            Notice(a, T("이 카테고리의 기종을 알 수 없어 고를 수 없습니다\n/JoyLauncher/emulators.txt 에 직접 적을 수 있습니다"), kMsgBlue, 5000);
            return;
        }
        size_t sl = g->file.find_last_of('/'), sl2 = sl == std::string::npos || sl == 0 ? std::string::npos : g->file.find_last_of('/', sl - 1);
        a.emuKey = sl == std::string::npos ? ctx.category : g->file.substr(sl2 == std::string::npos ? 0 : sl2 + 1, sl - (sl2 == std::string::npos ? 0 : sl2 + 1));
        a.emuSel = 0;
        for (size_t i = 0; i < a.emuList.size(); i++)
            if (a.emuList[i].current) a.emuSel = (int)i;
        a.optPick = false;
        a.emuPick = true;
    };
    if (wantPick) openThemePick();
    if (wantFtp) openFtp();
    if (wantAppPick) {
        openAppPick();
        if (wantSel >= 0 && a.appPick) a.appSel = std::min(wantSel, (int)a.appList.size() - 1);
    }
    if (wantEmuPick) openEmuPick();
    if (wantUpdLink) {
        BuildMenu();
        a.updLink = true;
        if (wantUpdErr) a.updErr = "timeout";
    }
    if (wantBgm) {
        BuildMenu();
        a.bgmPick = true;
        a.bgmSel = gSound == "common" ? 0 : gSound == "theme" ? 1 : gSound == "video" ? 2 : 3;
    }
    OverlayInit(a.r, &gFontBold);

    if (!shot.empty()) {
#ifndef __ANDROID__
        // 실행을 먼저 시도한 뒤의 화면 (실행 실패 알림 등 확인용)
        if (getenv("JOY_SHOT_LAUNCH") && wantSel >= 0 && a.mode == Mode::Games) { LaunchGame(a); wantSel = -1; }
        // 방향키 이동 시험: JOY_SHOT_NAV="15,14,0" (12 ◀ 13 ▲ 14 ▶ 15 ▼, 0 열기, 1 뒤로) — 이동한 뒤의 화면을 찍음
        if (const char *nav = getenv("JOY_SHOT_NAV")) {
            std::stringstream ns(nav);
            for (std::string tok; std::getline(ns, tok, ',');) {
                int b = atoi(tok.c_str());
                if (b >= 12 && b <= 15) {
                    if (a.mode == Mode::Categories) a.catSel = MoveSel(a.catSel, (int)a.cols.size(), false, b);
                    else a.sel = MoveSel(a.sel, (int)CurCol(a).games.size(), true, b);
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
        // 시험용 표시 상태: JOY_PAD=xbox (START·SELECT 표시) / JOY_SWAP_AB=1 / JOY_OVERLAY=1 (화면 컨트롤러도 그림)
        if (const char *p = getenv("JOY_PAD")) gPadStartSelect = strcmp(p, "nintendo") != 0;
        if (getenv("JOY_SWAP_AB")) { gSwapAB = true; BuildMenu(); }
#endif
        a.selChanged = SDL_GetTicks() - 1000;
        Render(a);
#ifndef __ANDROID__
        // 미리보기 영상까지 찍기: JOY_SHOT_VIDEO=<첫 장면 뒤 더 기다릴 ms> (영상이 없으면 그대로)
        if (const char *sv = getenv("JOY_SHOT_VIDEO")) {
            VideoEndFrame(100000, true);
            bool got = VideoWaitFrame(8000);
            if (got) SDL_Delay((Uint32)atoi(sv));
            printf("VIDEO %s\n", got ? "frame" : "none");
            Render(a);
        }
        if (getenv("JOY_OVERLAY")) OverlayDraw(a.r, gPadStartSelect);
#endif
        // 화면 전체(실제 점)를 그대로 저장: 논리 크기를 풀고 읽는다 (화면 비율이 다르면 양옆 여백까지)
        int ow = W, oh = H;
        SDL_GetRendererOutputSize(a.r, &ow, &oh);
        SDL_RenderFlush(a.r);
        SDL_RenderSetLogicalSize(a.r, 0, 0);
        SDL_RenderSetViewport(a.r, nullptr);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(a.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        IMG_SavePNG(s, shot.c_str());
        if (wantSel >= 0 && a.mode == Mode::Games) LaunchGame(a);
        return 0;
    }

    // ---- 입력: 게임 컨트롤러(안드로이드 입력 장치)·키보드·화면 컨트롤러(터치)
    std::set<SDL_JoystickID> pads;  // 연결된 컨트롤러
    enum { InNone, InPad, InTouch } lastInput = InNone;  // 마지막으로 쓴 입력 (화면 컨트롤러 자동 표시)
    auto usePad = [&](SDL_GameController *gc) { gPadStartSelect = gc && !IsNintendoPad(gc); };
    auto openPad = [&](int index) {
        if (!SDL_IsGameController(index)) return;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
        SDL_GameController *gc = SDL_GameControllerFromInstanceID(id);
        if (!gc) gc = SDL_GameControllerOpen(index);
        if (!gc) return;
        pads.insert(id);
        usePad(gc);
        lastInput = InNone;
    };
    for (int i = 0; i < SDL_NumJoysticks(); i++) openPad(i);
    int padStickDir = -1;
    bool padZL = false, padZR = false;
    bool running = true;
    int held = -1;
    Uint32 nextRepeat = 0;
    // 화면 가운데 상자에 한 줄 표시 (오래 걸리는 작업 전)
    auto showBusy = [&](const std::string &text) {
        Render(a);
        MenuStyle m = Menu();
        MenuPanel(a, m, 240, 300, 800, 90, 0);
        DrawText(a, a.fList, text, W / 2, 330, m.text, 760, 1, 0, 0, kScrollMsg);
        SDL_RenderPresent(a.r);
    };
    auto modal = [&](const std::string &text, bool ok) {
        a.message = text;
        a.messageColor = ok ? kMsgGreen : kMsgRed;
        a.messageModal = true;
    };
    // 내려받는 동안의 진행 화면. 취소했으면 false
    Uint32 lastDraw = 0;
    auto drawProgress = [&](const std::string &title, double part) {
        bool cancel = PollCancel();
        if (SDL_GetTicks() - lastDraw >= 100 || part >= 1.0) {
            lastDraw = SDL_GetTicks();
            Render(a);
            MenuStyle m = Menu();
            MenuPanel(a, m, 240, 280, 800, 150, 0);
            DrawText(a, a.fList, title, W / 2, 300, m.text, 760, 1);
            ProgressBar(a, m, 280, 350, 720, 14, part);
            DrawText(a, a.fInfo, PadHint(T("[B] 취소")), W / 2, 380, m.dim, 760, 1);
            SDL_RenderPresent(a.r);
        }
        return !cancel;
    };
    // 옵션 창의 "업데이트": 확인 → 결과와 릴리스 페이지 주소를 보이는 창 (설치는 사용자가 브라우저로 받아 직접)
    auto runUpdate = [&](int) {
        a.updErr.clear();
        if (!gUpdateFound) {
            showBusy(T("업데이트 확인 중..."));
            UpdateCheckStop();
            UpdateInfo info;
            std::string err;
            if (!FetchLatestRelease(info, err)) a.updErr = err;
            else if (CompareVersions(info.version, CurrentVersion()) > 0) {
                gUpdate = info;
                gUpdateFound = true;
                BuildMenu();
            }
        }
        a.optPick = false;
        a.updLink = true;
    };
    // 테마 선택 창의 "기본 테마 n종 내려받기": 최신 릴리스에서 팩마다 받아 /JoyLauncher/themes/<팩 폴더> 에 풀기
    auto runThemeDownload = [&]() {
        int cur = 0;
        auto progress = [&](long long got, long long total) {
            double part = total > 0 ? (double)got / total : 0.0;
            return drawProgress(Fmt(T("%s 내려받는 중 (%d/%d)  %.1f / %.1f MB"), PackName(kPacks[cur]), cur + 1, kPackCount, got / 1048576.0,
                                    total / 1048576.0),
                                (cur + part) / kPackCount);
        };
        showBusy(T("최신 릴리스 확인 중..."));
        int total = 0, got = 0;
        std::string failed;
        for (cur = 0; cur < kPackCount; cur++) {
            int files = 0;
            std::string e;
            if (DownloadTheme(gHome + "/themes", kPacks[cur].folder, files, e, progress)) {
                total += files;
                got++;
            } else {
                failed += std::string(failed.empty() ? "" : ", ") + PackName(kPacks[cur]) + " (" + e + ")";
                if (e == T("취소했습니다")) break;
            }
        }
        // 새 그림으로 다시 읽기 (지금 그 테마를 쓰고 있었으면 바로 반영)
        a.backs.Clear();
        a.images.Clear();
        a.lastBack.clear();
        for (const auto &p : kPacks)
            if (gThemeName == p.folder) {
                LoadTheme(p.folder);
                a.top = CenterTop(a.sel, true);
                a.catTop = CenterTop(a.catSel, false);
                Clamp(a);
            }
        openThemePick();
        if (!failed.empty()) {
            modal(Fmt(T("기본 테마 %d종 받음 (전체 %d종) · 실패: "), got, kPackCount) + failed, false);
            return;
        }
        Notice(a, PadHint(Fmt(T("기본 테마 %d종을 받았습니다 (파일 %d개) · 테마를 골라 [A] 로 적용합니다"), kPackCount, total)), kMsgGreen, 4000);
    };
    // 옵션 창에서 값을 바꾸는 항목 (A 또는 ◀ ▶). dir: +1 / -1
    auto adjust = [&](MenuAct act, int dir) {
        if (act == MenuAct::Language) {
            static const char *kLangs[] = {"auto", "ko", "en"};
            int cur = gLanguage == "ko" ? 1 : gLanguage == "en" ? 2 : 0;
            gLanguage = kLangs[(cur + 3 + dir) % 3];
            ApplyLanguage();
        } else if (act == MenuAct::Effects) {
            gEffects = !gEffects;
        } else if (act == MenuAct::Video) {
            gVideoOn = !gVideoOn;
            ApplyVideo();
        } else if (act == MenuAct::SwapAB) {
            gSwapAB = !gSwapAB;
        } else if (act == MenuAct::Overlay) {
            static const char *kModes[] = {"auto", "on", "off"};
            int cur = gOverlayMode == "on" ? 1 : gOverlayMode == "off" ? 2 : 0;
            gOverlayMode = kModes[(cur + 3 + dir) % 3];
        } else if (act == MenuAct::SoundCommon || act == MenuAct::SoundTheme || act == MenuAct::SoundVideo || act == MenuAct::SoundOff) {
            // 소리 선택: 넷 중 하나만 (BGM 과 영상 소리가 함께 나지 않음)
            gSound = act == MenuAct::SoundCommon ? "common" : act == MenuAct::SoundTheme ? "theme" : act == MenuAct::SoundVideo ? "video" : "off";
            ApplyVideo();
            if ((gSound == "common" || gSound == "theme") && BgmCountIn(BgmDir(gSound)) == 0)  // 곡이 없으면 넣을 곳을 알려 줌
                Notice(a,
                       gSound == "common" ? std::string(T("/JoyLauncher/bgm 폴더에 mp3 또는 ogg 파일을 넣으세요"))
                       : gThemeName.empty() ? std::string(T("테마를 쓰지 않고 있습니다 (테마 BGM 은 테마 폴더의 bgm 에 넣습니다)"))
                                            : Fmt(T("/JoyLauncher/themes/%s/bgm 폴더에 mp3 또는 ogg 파일을 넣으세요"), gThemeName.c_str()),
                       kMsgBlue, 4500);
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
        a.bgmSel = std::min(a.bgmSel, (int)gBgmMenu.size() - 1);  // BGM 이 아니면 "다음 곡" 이 빠짐
    };
    // BGM · 소리 설정 창 (X 버튼, 옵션 창의 항목). 그 사이 넣은 곡이 있으면 반영하고, 커서는 지금 고른 소리에
    auto openBgm = [&]() {
        ApplyBgm();
        BuildMenu();
        a.bgmPick = true;
        a.bgmSel = gSound == "common" ? 0 : gSound == "theme" ? 1 : gSound == "video" ? 2 : 3;
    };
    auto runMenu = [&](int idx) {
        if (idx < 0 || idx >= (int)gMenu.size()) return;
        const MenuItem m = gMenu[idx];
        if (m.act == MenuAct::FtpRecv) {
            openFtp();
        } else if (m.act == MenuAct::Theme) {
            a.optPick = false;
            openThemePick();
        } else if (m.act == MenuAct::Refresh) {
            a.optPick = false;
            SaveState(a);
            // 롬 폴더·에뮬레이터 표도 다시 (SD 카드를 꽂았거나 설정 파일을 고쳤을 수 있음)
            std::string log2;
#ifdef __ANDROID__
            gRoots = FindRoots("", true, log2);
#endif
            {
                std::string esde = EsdeLocate(gRoots);
                EmuLoad(gHome, esde.empty() ? std::vector<std::string>() : std::vector<std::string>{esde});
            }
            LoadGames(a, true, log2);
            LoadState(a, true);
            Clamp(a);
            int n = 0;
            for (const auto &col : a.cols)
                if (!col.virt) n += (int)col.games.size();
            a.message = Fmt(T("게임 목록을 새로 읽었습니다 (%d개)"), n);
            a.messageColor = kMsgGreen;
            a.messageModal = true;
        } else if (m.act == MenuAct::Update) {
            runUpdate(idx);
        } else if (m.act == MenuAct::Language || m.act == MenuAct::Effects || m.act == MenuAct::SwapAB || m.act == MenuAct::Overlay ||
                   m.act == MenuAct::Video) {
            adjust(m.act, 1);
        } else if (m.act == MenuAct::AppFav) {
            openAppPick();
        } else if (m.act == MenuAct::EmuPick) {
            openEmuPick();
        } else if (m.act == MenuAct::BgmMenu) {
            openBgm();
        } else if (m.act == MenuAct::Exit) {
            SaveState(a);
            running = false;
        }
    };
    auto act = [&](int b) {
        if (a.ftpOpen) {  // FTP 화면: B 로 닫기만
            if (b == 1) closeFtp();
            return;
        }
        if (a.messageModal) {  // 알림: B(또는 A)로 닫고, 그 외 입력은 무시
            if (b == 1 || b == 0) {
                a.messageModal = false;
                a.message.clear();
            }
            return;
        }
        if (a.exitAsk) {
            if (b == 0) { SaveState(a); running = false; }
            if (b == 0 || b == 1) a.exitAsk = false;
            return;
        }
        if (a.updLink) {  // A: 브라우저로 릴리스 페이지, B: 닫기
            if (b == 0) {
                std::string url = ReleasePageUrl(gUpdateFound ? gUpdate.version : std::string());
                if (gDemo || SDL_OpenURL(url.c_str()) != 0) Notice(a, T("브라우저를 열지 못했습니다. 주소를 직접 입력하세요"), kMsgRed, 4500);
            }
            if (b == 0 || b == 1) a.updLink = false;
            return;
        }
        if (a.appPick) {
            int n = (int)a.appList.size();
            if (b == 13) a.appSel = (a.appSel + n - 1) % n;
            else if (b == 15) a.appSel = (a.appSel + 1) % n;
            else if (b == 12) a.appSel = std::max(0, a.appSel - 10);
            else if (b == 14) a.appSel = std::min(n - 1, a.appSel + 10);
            else if (b == 0) LaunchApp(a, a.appList[a.appSel]);
            else if (b == 11) {  // − (SELECT): 앱 즐겨찾기에 넣기 / 빼기
                std::string key = AppsKey(a.appList[a.appSel].file);
                auto it = std::find(gAppFav.begin(), gAppFav.end(), key);
                if (it != gAppFav.end()) gAppFav.erase(it);
                else gAppFav.push_back(key);
                SaveAppFav();
            } else if (b == 1 || b == 2) {  // 닫을 때 카테고리를 다시 만든다
                a.appPick = false;
                RebuildAppFavorites(a, true);
                BuildMenu();
                Clamp(a);
            }
            a.selChanged = SDL_GetTicks() - 1000;
            return;
        }
        if (a.emuPick) {
            int n = (int)a.emuList.size();
            if (b == 13) a.emuSel = (a.emuSel + n - 1) % n;
            else if (b == 15) a.emuSel = (a.emuSel + 1) % n;
            else if (b == 0 && n > 0) {
                const EmuChoice &ch = a.emuList[a.emuSel];
                EmuSetUser(a.emuKey, ch.label);
                a.emuPick = false;
                Notice(a, ch.label.empty() ? Fmt(T("%s: 자동으로 고릅니다"), a.emuKey.c_str()) : Fmt(T("%s: %s 로 실행합니다"), a.emuKey.c_str(), ch.label.c_str()),
                       kMsgGreen, 3000);
            } else if (b == 1) a.emuPick = false;
            return;
        }
        if (a.bgmPick) {
            int n = (int)gBgmMenu.size();
            if (b == 13) a.bgmSel = (a.bgmSel + n - 1) % n;
            else if (b == 15) a.bgmSel = (a.bgmSel + 1) % n;
            else if (b == 0) adjust(gBgmMenu[a.bgmSel].act, 1);
            else if ((b == 12 || b == 14) && Adjustable(gBgmMenu[a.bgmSel].act)) adjust(gBgmMenu[a.bgmSel].act, b == 14 ? 1 : -1);
            else if (b == 1 || b == 2) a.bgmPick = false;  // B, 또는 연 버튼(X)을 한 번 더
            return;
        }
        if (a.themePick && a.themeDelAsk >= 0) {  // 테마 지우기 확인
            if (b == 0 && a.themeDelAsk > 0 && a.themeDelAsk < (int)a.themeList.size()) {
                std::string folder = a.themeList[a.themeDelAsk], title = a.themeTitles[a.themeDelAsk];
                bool wasCurrent = folder == gThemeName;
                if (wasCurrent) LoadTheme("");  // 쓰던 테마: 먼저 놓고 지움
                bool ok = RemoveTree(gHome + "/themes/" + folder);
                if (wasCurrent) {
                    // 기본테마가 남아 있으면 그것으로, 없으면 테마 없이
                    auto names = ThemeNames();
                    if (std::find(names.begin(), names.end(), "JoyBasic") != names.end()) LoadTheme("JoyBasic");
                    SaveSettings();
                    a.backs.Clear();
                    a.lastBack.clear();
                    BuildMenu();
                    Clamp(a);
                }
                int keep = a.themeDelAsk;
                openThemePick();
                int lastTheme = 0;  // 내려받기 줄이 아닌 마지막 줄
                for (int i = 0; i < (int)a.themeList.size(); i++)
                    if (!IsThemeSpecial(a.themeList[i])) lastTheme = i;
                a.themeSel = std::min(keep, lastTheme);
                Notice(a, ok ? Fmt(T("테마를 지웠습니다: %s"), (title.empty() ? folder : title).c_str())
                             : Fmt(T("테마를 다 지우지 못했습니다: %s"), folder.c_str()),
                       ok ? kMsgGreen : kMsgRed, 2500);
            }
            if (b == 0 || b == 1) a.themeDelAsk = -1;
            return;
        }
        if (a.themePick) {
            int n = (int)a.themeList.size();
            if (b == 0 && a.themeList[a.themeSel] == kThemeFtp) {
                openFtp();
                return;
            }
            if ((b == 2 || b == 11) && a.themeSel > 0 && !IsThemeSpecial(a.themeList[a.themeSel])) {  // X / −: 테마 지우기 (묻고)
                a.themeDelAsk = a.themeSel;
                return;
            }
            if (b == 13) a.themeSel = (a.themeSel + n - 1) % n;
            else if (b == 15) a.themeSel = (a.themeSel + 1) % n;
            else if (b == 0 && IsThemeDownload(a.themeList[a.themeSel])) runThemeDownload();
            else if (b == 0) {
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
            return;
        }
        if (b == 2) {  // X: BGM · 소리 설정 (스위치판의 홈브루 메뉴 자리. 앱 목록은 옵션 창에서)
            openBgm();
            return;
        }
        if (a.cols.empty()) { if (b == 1 || b == 10) a.exitAsk = true; return; }
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
            case 1: case 10: a.exitAsk = true; return;  // 맨 위 화면의 B (또는 +·START): 종료할지 한 번 더 확인
            }
        } else {
            switch (b) {
            case 12: case 13: case 14: case 15:
                a.sel = MoveSel(a.sel, (int)CurCol(a).games.size(), true, b);
                break;
            case 6: a.col = (a.col + ncol - 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 7: a.col = (a.col + 1) % ncol; a.catSel = a.col; a.sel = a.top = 0; break;
            case 0: LaunchGame(a); return;  // 런처는 뒤에 남는다 (에뮬레이터를 끝내면 이 화면으로 돌아옴)
            case 1: a.mode = Mode::Categories; a.catSel = a.col; break;
            case 8: case 9:  // ZL / ZR: 보기 (전체 → 즐겨찾기 → 최근 실행)
                CurCol(a);
                a.view = (a.view + (b == 9 ? 1 : 2)) % 3;
                a.viewFor = a.col;
                a.sel = a.top = 0;
                break;
            case 11: {  // − (SELECT): 즐겨찾기 넣기/빼기
                if (CurCol(a).games.empty()) break;
                std::string file = CurCol(a).games[a.sel].file;
                if (a.cols[a.col].shortName == "hbfav") {  // 앱 즐겨찾기에서는 빼기만 (넣기는 앱 목록 창)
                    std::string key = AppsKey(file);
                    gAppFav.erase(std::remove(gAppFav.begin(), gAppFav.end(), key), gAppFav.end());
                    SaveAppFav();
                    RebuildAppFavorites(a, true);
                    BuildMenu();
                    Notice(a, T("앱 즐겨찾기에서 뺐습니다"), kMsgBlue, 1500);
                    break;
                }
                bool add = !gFavSet.count(file);
                if (add) gFavSet.insert(file);
                else gFavSet.erase(file);
                SaveFavorites();
                gViewVersion++;
                RebuildFavorites(a, true);
                Notice(a, add ? T("즐겨찾기에 넣었습니다") : T("즐겨찾기에서 뺐습니다"), kMsgBlue, 1500);
                break;
            }
            case 10: a.exitAsk = true; return;
            }
        }
        a.selChanged = SDL_GetTicks();
        Clamp(a);
    };
    // 버튼 눌림·뗌 (방향은 누르고 있으면 반복)
    auto press = [&](int b, bool down) {
        if (b < 0) return;
        if (down) {
            act(b);
            held = b;
            nextRepeat = SDL_GetTicks() + 400;
        } else if (held == b) {
            held = -1;
        }
    };
    // A·B 바꾸기: 컨트롤러·화면 컨트롤러의 A 와 B 가 선택(0)·취소(1) 중 무엇인지
    auto swapAB = [&](int b) { return gSwapAB && (b == 0 || b == 1) ? 1 - b : b; };
    bool overlayOn = false;
    bool inBackground = false;  // 다른 앱이 앞에 있음 (이 프레임을 끝으로 SDL 이 런처를 멈춰 둔다)
    bool updateNotified = gAutoUpdate != "on" || gShotMode || gDemo || !UpdaterEnabled();
    while (running) {
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
                    if (!a.messageModal) Notice(a, PadHint(Fmt(T("새 버전 v%s 이 있습니다  ·  [Y] 옵션 → 새 버전 받기"), info.version.c_str())), kMsgBlue, 5000);
                }
            }
        }
        // 기기 화면 크기: 창을 만든 직후에는 시스템 막대를 숨기기 전 값일 수 있어 프레임마다 확인한다
        if (UpdateScale(a)) {
            Rescale(a);
            OverlayReset();
        }
        // 화면 컨트롤러: 켬 = 항상, 자동 = 컨트롤러가 없거나 마지막 입력이 터치일 때
#ifdef __ANDROID__
        bool wantOverlay = gOverlayMode == "on" || (gOverlayMode == "auto" && (lastInput == InTouch || (lastInput == InNone && pads.empty())));
#else
        bool wantOverlay = gOverlayMode == "on" || (gOverlayMode == "auto" && lastInput == InTouch);  // 데스크톱: 키보드가 있으므로 터치했을 때만
#endif
        if (overlayOn && !wantOverlay) {
            std::vector<OverlayPress> rel;
            OverlayReleaseAll(rel);
            for (const auto &r : rel) press(swapAB(r.button), false);
        }
        overlayOn = wantOverlay;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            // ---- 앱 상태 (다른 앱으로 갔다가 돌아옴)
            if (e.type == SDL_APP_WILLENTERBACKGROUND) {
                inBackground = true;
                VideoStop();
                BgmPause();
                held = -1;
                continue;
            }
            if (e.type == SDL_APP_DIDENTERFOREGROUND) {
                inBackground = false;
                gVideoHoldUntil = 0;
                BgmResume();
                EmuForgetInstalled();    // 그 사이 에뮬레이터를 깔았거나 지웠을 수 있음
                gBattery.nextRead = 0;
                held = -1;
                if (!a.appPick) RebuildAppFavorites(a, true);
                SaveState(a);            // 복귀 표시 지움 (런처가 살아 있었음)
                Clamp(a);
                a.selChanged = SDL_GetTicks() - 1000;
                continue;
            }
            if (e.type == SDL_RENDER_DEVICE_RESET) { ResetGpu(a, true); continue; }
            if (e.type == SDL_RENDER_TARGETS_RESET) { ResetGpu(a, false); continue; }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { OverlayReset(); continue; }
            // ---- 화면 컨트롤러 (터치)
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) {
                std::vector<OverlayPress> ev;
                OverlayEvent(e, overlayOn, ev);
                for (const auto &p : ev) press(swapAB(p.button), p.down);
                if (e.type == SDL_FINGERDOWN) lastInput = InTouch;  // 감춰져 있었으면 다음 프레임부터 보임 (이번 터치는 버튼으로 치지 않음)
                continue;
            }
            // ---- 게임 컨트롤러 (Xbox·플스·스위치 프로·휴대기 내장 패드 등)
            if (e.type == SDL_CONTROLLERDEVICEADDED) { openPad(e.cdevice.which); continue; }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (SDL_GameController *gc = SDL_GameControllerFromInstanceID(e.cdevice.which)) SDL_GameControllerClose(gc);
                pads.erase(e.cdevice.which);
                usePad(pads.empty() ? nullptr : SDL_GameControllerFromInstanceID(*pads.begin()));
                lastInput = InNone;  // 컨트롤러가 하나도 없으면 화면 컨트롤러가 뜬다
                held = -1;
                continue;
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
                int b = -1;
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: b = swapAB(0); break;
                case SDL_CONTROLLER_BUTTON_B: b = swapAB(1); break;
                case SDL_CONTROLLER_BUTTON_X: b = 2; break;
                case SDL_CONTROLLER_BUTTON_Y: b = 3; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: b = 6; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: b = 7; break;
                case SDL_CONTROLLER_BUTTON_START: b = 10; break;
                case SDL_CONTROLLER_BUTTON_BACK: b = 11; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: b = 12; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: b = 13; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: b = 14; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: b = 15; break;
                }
                if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    lastInput = InPad;
                    usePad(SDL_GameControllerFromInstanceID(e.cbutton.which));  // 지금 쓰는 컨트롤러 기준으로 버튼 이름 표시
                }
                press(b, e.type == SDL_CONTROLLERBUTTONDOWN);
                continue;
            }
            if (e.type == SDL_CONTROLLERAXISMOTION) {
                int v = e.caxis.value;
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    bool &st = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? padZL : padZR;
                    bool on = v > 16000;
                    if (on && !st) { lastInput = InPad; act(e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 8 : 9); }
                    st = on;
                } else if (SDL_GameController *gc = SDL_GameControllerFromInstanceID(e.caxis.which)) {
                    int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
                    const int dead = 16000;
                    int dir = -1;
                    if (std::abs(y) >= std::abs(x) && std::abs(y) > dead) dir = y < 0 ? 13 : 15;
                    else if (std::abs(x) > dead) dir = x < 0 ? 12 : 14;
                    if (dir != padStickDir) {
                        padStickDir = dir;
                        if (dir >= 0) {
                            lastInput = InPad;
                            press(dir, true);
                        } else if (held >= 12 && held <= 15) {
                            held = -1;
                        }
                    }
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                int b = -1;
                switch (e.key.keysym.sym) {
                case SDLK_UP: b = 13; break;
                case SDLK_DOWN: b = 15; break;
                case SDLK_LEFT: b = 12; break;
                case SDLK_RIGHT: b = 14; break;
                case SDLK_q: b = 6; break;
                case SDLK_w: b = 7; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_SELECT: b = 0; break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: b = 1; break;  // 안드로이드의 뒤로 가기 = 취소
                case SDLK_t: b = 2; break;
                case SDLK_f: b = 11; break;
                case SDLK_z: b = 8; break;
                case SDLK_x: b = 9; break;
                case SDLK_y: case SDLK_MENU: b = 3; break;
                }
                if (b >= 0) {
                    if (e.key.keysym.sym != SDLK_AC_BACK) lastInput = InPad;
                    if (!e.key.repeat || (b >= 12 && b <= 15)) act(b);
                }
            }
        }
        if (held >= 12 && held <= 15 && SDL_GetTicks() >= nextRepeat) {
            act(held);
            nextRepeat = SDL_GetTicks() + 70;
        }
        BgmUpdate();
        RenderFrame(a, nullptr);
        // 미리보기 영상: 창(옵션·앱 목록·알림 등)이 화면을 가리면 멈추고, 선택이 잠시 멈춰 있으면 시작
        // 물러나는 프레임이나 게임을 막 실행한 뒤에 다시 시작하면 다른 앱 뒤에서 소리가 나므로 그때도 멈춘 채로 둔다
        bool videoHold = inBackground || (gVideoHoldUntil && (Sint32)(gVideoHoldUntil - SDL_GetTicks()) > 0);
        VideoEndFrame(SDL_GetTicks() - a.selChanged, !videoHold && !(a.optPick || a.bgmPick || a.themePick || a.appPick || a.emuPick ||
                                                                     a.exitAsk || a.updLink || a.ftpOpen || a.messageModal));
        if (overlayOn) OverlayDraw(a.r, gPadStartSelect);
        SDL_RenderPresent(a.r);
    }
    // 끝내기 전에 소리를 줄이고 오디오·스레드를 모두 닫는다
    BgmShutdown(400);
    FtpServerStop();
    ImgLoaderStop();
    VideoStop();
    UpdaterShutdown();
    OverlayShutdown();
    a.text.Clear();
    a.images.Clear();
    a.backs.Clear();
    a.thumbs.Clear();
    for (TTF_Font *f : {a.fTitle, a.fBig, a.fList, a.fInfo, a.fDesc})
        if (f) TTF_CloseFont(f);
    SDL_DestroyRenderer(a.r);
    SDL_DestroyWindow(win);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    fflush(nullptr);
    return 0;
}
#endif  // JOY_BUILDER
