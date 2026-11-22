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
    gAnimAllowed = true;
    LoadUserImages();
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

// 화면비 미리보기 (안드로이드판): 가로/세로. 16:9 = 스위치와 같은 1280x720
static float gBAspect = 1280.0f / 720.0f;
static std::string gBLayoutFile = "layout.txt";
void JoySetAspect(float aspect) { gBAspect = aspect; }
void JoyCanvas(int &w, int &h) { w = W; h = H; }
const std::string &JoyLayoutFile() { return gBLayoutFile; }

void JoySetLayoutText(const std::string &text, const std::string *square) {
    std::string oldFont = gTheme.layout.font, oldBold = gTheme.layout.fontBold;
    SetStringOverrides(nullptr, nullptr);
    LayoutLoadFitText(gTheme.layout, &text, square, gBAspect, gBLayoutFile);
    int cw = gTheme.layout.canvasW > 0 ? gTheme.layout.canvasW : 1280, ch = gTheme.layout.canvasH > 0 ? gTheme.layout.canvasH : 720;
    if (cw != W || ch != H) {  // 캔버스 크기가 바뀜: 화면 전환용 그림을 새 크기로
        W = cw;
        H = ch;
        if (gTrCur) SDL_DestroyTexture(gTrCur);
        if (gTrPrev) SDL_DestroyTexture(gTrPrev);
        gTrCur = gTrPrev = nullptr;
        gTrInit = gTrPrevOk = false;
        gBatteryLeft = W;
    }
    if (!gTheme.layout.minVersion.empty() && CompareVersions(gTheme.layout.minVersion, std::string(CurrentVersion()).substr(0, std::string(CurrentVersion()).find('-'))) > 0)
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
    RenderFrame(gB, SDL_GetRenderTarget(gB.r));
}

const std::map<std::string, SDL_Rect> &JoyBounds() { return gBounds; }

void JoyNav(int b) {
    App &a = gB;
    if (a.cols.empty()) return;
    if (b >= 12 && b <= 15) {
        if (a.mode == Mode::Categories) a.catSel = MoveSel(a.catSel, (int)a.cols.size(), false, b);
        else a.sel = MoveSel(a.sel, (int)CurCol(a).games.size(), true, b);
    } else if (b == 0 && a.mode == Mode::Categories) {
        if (a.col != a.catSel) { a.col = a.catSel; a.sel = a.top = 0; }
    } else if (b == 1) {
        a.catSel = a.col;
    } else if (b == 20 || b == 21) {
        int n = (int)a.cols.size();
        a.col = (a.col + (b == 21 ? 1 : n - 1)) % n;
        a.catSel = a.col;
        a.sel = a.top = 0;
    } else if (b == 22 || b == 23) {
        CurCol(a);
        a.view = (a.view + (b == 23 ? 1 : 2)) % 3;
        a.viewFor = a.col;
        a.sel = a.top = 0;
    } else if (b == 30 && a.mode == Mode::Games && !CurCol(a).games.empty()) {
        const std::string &file = CurCol(a).games[a.sel].file;
        if (gFavSet.count(file)) gFavSet.erase(file);
        else gFavSet.insert(file);
        gViewVersion++;
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

int JoyView() { return gB.view; }

void JoyReloadCategories() {
    LoadUserImages();
    LoadCustomCategories();
    gB.cols = gBaseCols;
    ApplyCustomCategories(gB.cols);
    RebuildHbFavorites(gB, false);
    RebuildFavorites(gB, false);
    gB.images.Clear();
    gB.thumbs.Clear();
    gB.backs.Clear();
    Clamp(gB);
}

std::vector<JoyCollectionInfo> JoyBaseCollections() {
    std::vector<JoyCollectionInfo> v;
    for (const auto &c : gBaseCols) v.push_back({c.name, c.shortName, c.folders.empty() ? "" : c.folders[0], (int)c.games.size()});
    return v;
}
