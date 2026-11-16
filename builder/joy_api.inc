// main.cpp 끝에 포함됨 (JOY_BUILDER). 런처의 화면 코드를 테마 빌더에서 쓰기 위한 함수들
#include "../builder/joy_api.h"

static App gB;
static bool gBReady = false;

bool JoyInit(SDL_Renderer *r, const std::string &home, const std::string &roms, std::string &log) {
    gB.r = r;
    gB.text.r = r;
    gB.images.r = r;
    gB.backs.r = r;
    gB.backs.cap = 8;
    gB.thumbs.r = r;
    if (!ReadAll(kFont, gFontReg) || !ReadAll(kFontBold, gFontBold)) { log = "font read failed (romfs/font.ttf)"; return false; }
    gB.fTitle = OpenFontMem(gFontBold, 34);
    gB.fBig = OpenFontMem(gFontBold, 30);
    gB.fList = OpenFontMem(gFontReg, 27);
    gB.fInfo = OpenFontMem(gFontReg, 22);
    gB.fDesc = OpenFontMem(gFontReg, 20);
    gHome = home;
    gShotMode = true;  // 그림을 바로 읽음 (나눠 읽기 없음)
    gTrackBox = true;
    gBattery.percent = 76;
    gBattery.nextRead = 0xFFFFFFFFu;  // 빌더: 배터리 76% 고정
    SetLang(Lang::Ko);
    LoadTheme("");
    gBReady = true;
    JoySetRoms(roms, log);
    return true;
}

void JoySetRoms(const std::string &roms, std::string &log) {
    gRoot = roms;
    gRoots = {roms};
    LoadCoreConfig(gHome + "/cores.txt");
    LoadFavorites();
    LoadHbFav();
    LoadPlayCounts();
    gB.cols.clear();
    LoadGames(gB, true, log);
    gB.catSel = gB.catTop = gB.col = gB.sel = gB.top = 0;
    Clamp(gB);
}

void JoyLoadTheme(const std::string &folder) {
    gB.images.Clear();
    gB.backs.Clear();
    gB.lastBack.clear();
    LoadTheme(folder);
    Clamp(gB);
}

void JoySetLayoutText(const std::string &text) {
    std::string oldFont = gTheme.layout.font, oldBold = gTheme.layout.fontBold;
    SetStringOverrides(nullptr, nullptr);
    LayoutLoadText(gTheme.layout, text);
    if (!gTheme.layout.minVersion.empty() && CompareVersions(gTheme.layout.minVersion, CurrentVersion()) > 0)
        gTheme.layout.problems.insert(gTheme.layout.problems.begin(),
                                      Fmt(T("이 테마는 Joy Launcher v%s 이상이 필요합니다 (지금 v%s)"), gTheme.layout.minVersion.c_str(), CurrentVersion()));
    SetStringOverrides(&gTheme.layout.strings, &gTheme.layout.stringsEn);
    gTheme.folders.clear();
    if (oldFont != gTheme.layout.font || oldBold != gTheme.layout.fontBold) gThemeSerial++;
    Clamp(gB);
}

const Layout &JoyLayout() { return gTheme.layout; }

void JoySetLang(bool en) {
    SetLang(en ? Lang::En : Lang::Ko);
    gB.text.Clear();
}

void JoyRender(bool games) {
    if (!gBReady) return;
    gB.mode = games && !gB.cols.empty() ? Mode::Games : Mode::Categories;
    gB.selChanged = SDL_GetTicks() - 1000;
    gBounds.clear();
    Clamp(gB);
    Render(gB);
}

const std::map<std::string, SDL_Rect> &JoyBounds() { return gBounds; }

void JoyNav(int b) {
    App &a = gB;
    if (a.cols.empty()) return;
    if (b >= 12 && b <= 15) {
        if (a.mode == Mode::Categories) a.catSel = MoveSel(a.catSel, (int)a.cols.size(), false, b);
        else a.sel = MoveSel(a.sel, (int)a.cols[a.col].games.size(), true, b);
    } else if (b == 0 && a.mode == Mode::Categories) {
        if (a.col != a.catSel) { a.col = a.catSel; a.sel = a.top = 0; }
    } else if (b == 1) {
        a.catSel = a.col;
    } else if (b == 20 || b == 21) {
        int n = (int)a.cols.size();
        a.col = (a.col + (b == 21 ? 1 : n - 1)) % n;
        a.catSel = a.col;
        a.sel = a.top = 0;
    } else if (b == 30 && a.mode == Mode::Games && !a.cols[a.col].games.empty()) {
        const std::string &file = a.cols[a.col].games[a.sel].file;
        if (gFavSet.count(file)) gFavSet.erase(file);
        else gFavSet.insert(file);
    }
    Clamp(a);
}

int JoyCategory() { return gB.mode == Mode::Games ? gB.col : gB.catSel; }
int JoyGameIndex() { return gB.sel; }
void JoySetCategory(int idx) {
    if (gB.cols.empty()) return;
    idx = std::max(0, std::min(idx, (int)gB.cols.size() - 1));
    gB.catSel = idx;
    if (gB.col != idx) { gB.col = idx; gB.sel = gB.top = 0; }
    gB.catTop = CenterTop(gB.catSel, false);
    Clamp(gB);
}
void JoySetGameIndex(int idx) { gB.sel = idx; Clamp(gB); }

std::vector<JoyCollectionInfo> JoyCollections() {
    std::vector<JoyCollectionInfo> v;
    for (const auto &c : gB.cols) v.push_back({c.name, c.shortName, c.folders.empty() ? "" : c.folders[0], (int)c.games.size()});
    return v;
}

const char *JoyVersion() { return CurrentVersion(); }
