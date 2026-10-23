// 게임 목록 읽기
//  1) 페가수스 metadata.pegasus.txt (안드로이드용 launch 는 스위치용으로 변환)
//  2) ES-DE / EmulationStation gamelist.xml
//  3) 목록 파일이 없는 롬 폴더 (폴더 이름·확장자로 기종 자동 인식)
// 같은 롬 파일은 먼저 읽은 쪽 하나만 사용한다.
#include "pegasus.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <unordered_map>
#include <sstream>
#include <sys/stat.h>

namespace {

// ---------------------------------------------------------------- 공통 유틸
// 폴더 목록 기억: 목록을 읽는 동안 같은 폴더에서 그림·영상 이름 후보를 수십만 번 확인하므로
// (응애팩 전체 약 30만 번) SD 카드에 매번 묻지 않고 폴더마다 한 번만 읽어 둔다.
struct DirEntry {
    std::string name;
    bool dir;
};
bool gDirCacheOn = false;
std::unordered_map<std::string, std::unordered_map<std::string, DirEntry>> gDirCache;

std::string EntryKey(const std::string &name) {
#ifdef __SWITCH__
    std::string k = name;  // SD 카드(FAT/exFAT)는 대소문자 구분 없음
    for (auto &ch : k) ch = (char)std::tolower((unsigned char)ch);
    return k;
#else
    return name;
#endif
}

const std::unordered_map<std::string, DirEntry> &DirIndex(const std::string &dir) {
    auto it = gDirCache.find(dir);
    if (it != gDirCache.end()) return it->second;
    std::unordered_map<std::string, DirEntry> m;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *ent = readdir(d)) {
            std::string n = ent->d_name;
            if (n == "." || n == "..") continue;
            bool isDir;
            if (ent->d_type == DT_DIR) isDir = true;
            else if (ent->d_type == DT_REG) isDir = false;
            else {
                struct stat st;
                isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            }
            m[EntryKey(n)] = {n, isDir};
        }
        closedir(d);
    }
    return gDirCache.emplace(dir, std::move(m)).first->second;
}

// 기억한 목록에서 찾기. 없으면 nullptr (기억 기능이 꺼져 있으면 사용하지 않음)
const DirEntry *CachedEntry(const std::string &p, bool &usable) {
    usable = false;
    if (!gDirCacheOn) return nullptr;
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= p.size()) return nullptr;
    usable = true;
    const auto &m = DirIndex(slash == 0 ? "/" : p.substr(0, slash));
    auto it = m.find(EntryKey(p.substr(slash + 1)));
    return it == m.end() ? nullptr : &it->second;
}

bool Exists(const std::string &p) {
    bool usable;
    const DirEntry *e = CachedEntry(p, usable);
    if (usable) return e != nullptr;
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool IsDir(const std::string &p) {
    bool usable;
    const DirEntry *e = CachedEntry(p, usable);
    if (usable) return e && e->dir;
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Join(const std::string &dir, const std::string &rel) {
    if (rel.empty() || rel[0] == '/') return rel;
    std::string r = rel;
    while (r.rfind("./", 0) == 0) r = r.substr(2);
    return dir + "/" + r;
}

std::string Base(const std::string &path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Stem(const std::string &path) {
    std::string base = Base(path);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string Ext(const std::string &path) {
    std::string base = Base(path);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? "" : Lower(base.substr(dot + 1));
}

// 중복 판정용 경로 키 (대소문자 무시, ./ 정리)
std::string PathKey(std::string p) {
    p = Lower(p);
    size_t q;
    while ((q = p.find("/./")) != std::string::npos) p.erase(q, 2);
    while ((q = p.find("//")) != std::string::npos) p.erase(q, 1);
    return p;
}

std::vector<std::string> ListDir(const std::string &dir, bool dirs) {
    std::vector<std::string> out;
    if (gDirCacheOn) {
        for (const auto &kv : DirIndex(dir))
            if (kv.second.name[0] != '.' && kv.second.dir == dirs) out.push_back(kv.second.name);
        std::sort(out.begin(), out.end());
        return out;
    }
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent *ent = readdir(d)) {
        std::string n = ent->d_name;
        if (n == "." || n == ".." || n[0] == '.') continue;
        if (IsDir(dir + "/" + n) == dirs) out.push_back(n);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

const char *kImgExts[] = {".png", ".jpg", ".jpeg", ".PNG", ".JPG"};

// 박스아트 찾기: media/<이름>/boxFront.*, 그리고 흔한 이미지 폴더들(<폴더>/<이름>.*)
std::string FindBoxart(const std::string &dir, const std::string &rel) {
    std::string stem = Stem(rel);
    // 페가수스 media/<이름>/ 방식 (대소문자 다른 이름도 확인)
    static const char *mediaNames[] = {"boxFront", "boxfront", "box2dfront", "box", "cartridge", "titlescreen",
                                       "screenshot", "logo", "wheel", "marquee"};
    for (const char *n : mediaNames)
        for (const char *e : kImgExts) {
            std::string m = dir + "/media/" + stem + "/" + n + e;
            if (Exists(m)) return m;
        }
    // 앞쪽일수록 우선. 동영상(video, snap/*.mp4 등)은 쓰지 않음
    static const char *folders[] = {"boxFront", "boxfront", "boxart", "boxarts", "covers", "box", "images", "3dboxes",
                                    "media/images", "media/covers", "media/boxart", "Named_Boxarts", "cartridge",
                                    "titlescreen", "screenshot", "screenshots", "snap", "snaps", "Named_Snaps",
                                    "Named_Titles", "wheel", "logo", "marquee", "marquees"};
    for (const char *f : folders)
        for (const char *e : kImgExts)
            if (Exists(dir + "/" + f + "/" + stem + e)) return dir + "/" + f + "/" + stem + e;
    return "";
}

// 게임 화면 사진(스크린샷·타이틀 화면) 찾기. 동영상은 쓰지 않음
std::string FindScreenshot(const std::string &dir, const std::string &rel) {
    std::string stem = Stem(rel);
    static const char *mediaNames[] = {"screenshot", "screenshots", "snap", "screenshotTitle", "titlescreen", "fanart"};
    for (const char *n : mediaNames)
        for (const char *e : kImgExts) {
            std::string m = dir + "/media/" + stem + "/" + n + e;
            if (Exists(m)) return m;
        }
    static const char *folders[] = {"screenshot", "screenshots", "snap", "snaps", "Named_Snaps", "media/screenshots",
                                    "media/snaps", "titlescreen", "titlescreens", "Named_Titles", "media/titlescreens"};
    for (const char *f : folders)
        for (const char *e : kImgExts)
            if (Exists(dir + "/" + f + "/" + stem + e)) return dir + "/" + f + "/" + stem + e;
    return "";
}

// 미리보기 영상 찾기 (화면 사진이 없을 때 한 장면을 뽑아 쓰기 위해)
std::string FindVideo(const std::string &dir, const std::string &rel) {
    std::string stem = Stem(rel);
    static const char *vexts[] = {".mp4", ".MP4", ".mkv", ".webm", ".avi", ".m4v"};
    for (const char *n : {"video", "videos"})
        for (const char *e : vexts)
            if (Exists(dir + "/media/" + stem + "/" + n + e)) return dir + "/media/" + stem + "/" + n + e;
    static const char *folders[] = {"snap", "snaps", "videos", "video", "media/videos", "Named_Videos", "movies"};
    for (const char *f : folders)
        for (const char *e : vexts)
            if (Exists(dir + "/" + f + "/" + stem + e)) return dir + "/" + f + "/" + stem + e;
    return "";
}

std::string FolderKey(const std::string &name);

// ---------------------------------------------------------------- ES-DE (롬 폴더 밖의 목록·그림)
// ES-DE 는 목록을 ES-DE/gamelists/<기종 폴더>/gamelist.xml 에, 그림·영상을
// ES-DE/downloaded_media/<기종 폴더>/<종류>/<롬 경로(확장자 뺌)>.png 에 둔다 (목록에는 그림 경로가 없음).
std::string gEsdeHint;   // roots.txt 의 esde=<경로>
std::string gEsdeDir;    // 찾은 ES-DE 폴더 ("" = 없음)
std::string gEsdeMedia;  // 그림·영상 폴더 (기본 <ES-DE>/downloaded_media)
std::map<std::string, std::string> gEsdeSysDirs;  // "<상위 폴더>|<기종 폴더 이름>" -> 실제 폴더 ("" = 없음)

std::string ParentDir(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos || s == 0 ? "" : p.substr(0, s);
}

// parent 안에서 이름이 같은(대소문자 무시) 폴더
std::string EsdeChild(const std::string &parent, const std::string &name) {
    std::string key = parent + "|" + Lower(name);
    auto it = gEsdeSysDirs.find(key);
    if (it != gEsdeSysDirs.end()) return it->second;
    std::string found;
    for (const auto &n : ListDir(parent, true))
        if (Lower(n) == Lower(name)) { found = parent + "/" + n; break; }
    gEsdeSysDirs[key] = found;
    return found;
}

// settings/es_settings.xml 의 MediaDirectory (안드로이드 절대 경로는 ES-DE 폴더 기준으로 바꿈)
std::string EsdeMediaSetting(const std::string &esde) {
    std::ifstream in(esde + "/settings/es_settings.xml", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string x = ss.str();
    size_t p = x.find("name=\"MediaDirectory\"");
    if (p == std::string::npos) return "";
    size_t v = x.find("value=\"", p);
    if (v == std::string::npos) return "";
    v += 7;
    std::string val = Trim(x.substr(v, x.find('"', v) - v));
    while (!val.empty() && val.back() == '/') val.pop_back();
    if (val.empty()) return "";
    size_t e = val.find("/ES-DE/");
    if (e != std::string::npos && IsDir(esde + val.substr(e + 6))) return esde + val.substr(e + 6);
    if (IsDir(val)) return val;
    if (IsDir(esde + "/" + Base(val))) return esde + "/" + Base(val);
    return "";
}

// ES-DE 폴더 찾기: roots.txt 의 esde=, 롬 폴더 옆(/roms → /ES-DE), 롬 폴더 안
void FindEsde(const std::vector<std::string> &rootDirs) {
    gEsdeDir.clear();
    gEsdeMedia.clear();
    gEsdeSysDirs.clear();
    std::vector<std::string> cand;
    if (!gEsdeHint.empty()) cand.push_back(gEsdeHint);
    for (const auto &r : rootDirs) cand.push_back(ParentDir(r) + "/ES-DE");
    for (const auto &r : rootDirs) cand.push_back(r + "/ES-DE");
    for (const auto &c : cand) {
        if (!IsDir(c + "/gamelists") && !IsDir(c + "/downloaded_media")) continue;
        gEsdeDir = c;
        gEsdeMedia = EsdeMediaSetting(c);
        if (gEsdeMedia.empty()) gEsdeMedia = c + "/downloaded_media";
        return;
    }
}

// <ES-DE>/gamelists/<폴더 이름>/gamelist.xml ("" = 없음)
std::string EsdeGamelist(const std::string &dir) {
    if (gEsdeDir.empty()) return "";
    std::string d = EsdeChild(gEsdeDir + "/gamelists", Base(dir));
    return !d.empty() && Exists(d + "/gamelist.xml") ? d + "/gamelist.xml" : "";
}

// kind 0 = 박스, 1 = 게임 화면, 2 = 영상. rel 은 롬 폴더 기준 경로 (하위 폴더 포함)
std::string EsdeMediaFile(const std::string &dir, const std::string &rel, int kind) {
    if (gEsdeMedia.empty()) return "";
    std::string sys = EsdeChild(gEsdeMedia, Base(dir));
    if (sys.empty()) return "";
    static const std::vector<std::vector<const char *>> types = {
        {"covers", "3dboxes", "physicalmedia", "miniatures", "marquees"}, {"screenshots", "titlescreens", "fanart"}, {"videos"}};
    static const char *vexts[] = {".mp4", ".MP4", ".mkv", ".webm", ".avi", ".m4v"};
    std::string r = rel;
    while (r.rfind("./", 0) == 0) r = r.substr(2);
    size_t dot = r.find_last_of('.');
    std::string noExt = dot == std::string::npos || dot < r.find_last_of('/') + 1 ? r : r.substr(0, dot);
    std::vector<std::string> names = {noExt};
    if (Stem(r) != noExt) names.push_back(Stem(r));
    for (const char *t : types[kind])
        for (const auto &n : names) {
            if (kind == 2) {
                for (const char *e : vexts)
                    if (Exists(sys + "/" + t + "/" + n + e)) return sys + "/" + t + "/" + n + e;
            } else {
                for (const char *e : kImgExts)
                    if (Exists(sys + "/" + t + "/" + n + e)) return sys + "/" + t + "/" + n + e;
            }
        }
    return "";
}

// 그림이 없는 게임만 ES-DE 그림으로 채움 (Pegasus·롬 폴더 쪽 그림이 있으면 그대로)
void AddEsdeMedia(const std::string &dir, const std::string &rel, Game &g) {
    if (gEsdeMedia.empty()) return;
    if (g.boxart.empty()) g.boxart = EsdeMediaFile(dir, rel, 0);
    if (g.screenshot.empty()) g.screenshot = EsdeMediaFile(dir, rel, 1);
    if (g.screenshot.empty() && g.video.empty()) g.video = EsdeMediaFile(dir, rel, 2);
}

// ES-DE 에뮬레이터 표시 이름("MAME 2003-Plus", "PCSX ReARMed", "PPSSPP (Standalone)")을 실행 설정으로.
// 안드로이드 코어 이름 형식으로 만들어 페가수스와 같은 변환(cores.txt 등)을 거친다. 모르면 ""
std::string EsdeEmulatorLaunch(const std::string &label) {
    static const std::vector<std::pair<const char *, const char *>> t = {
        {"ppsspp", "org.ppsspp.ppsspp"},
        {"mame2003plus", "mame2003_plus"}, {"mame2003", "mame2003"}, {"mame2010", "mame2010"}, {"mame2000", "mame2000"},
        {"finalburnneo", "fbneo"}, {"fbneo", "fbneo"},
        {"pcsxrearmed", "pcsx_rearmed"}, {"beetlepsxhw", "mednafen_psx_hw"}, {"beetlepsx", "mednafen_psx"}, {"swanstation", "swanstation"},
        {"genesisplusgxwide", "genesis_plus_gx_wide"}, {"genesisplusgx", "genesis_plus_gx"}, {"picodrive", "picodrive"},
        {"blastem", "blastem"}, {"gearsystem", "gearsystem"}, {"smsplus", "smsplus"},
        {"mgba", "mgba"}, {"gpsp", "gpsp"}, {"vbam", "vbam"}, {"vbanext", "vba_next"},
        {"snes9x2010", "snes9x2010"}, {"snes9x2005plus", "snes9x2005_plus"}, {"snes9x2005", "snes9x2005"},
        {"snes9x2002", "snes9x2002"}, {"snes9x", "snes9x"}, {"bsnes", "bsnes"}, {"mesens", "mesen-s"},
        {"mesen", "mesen"}, {"fceumm", "fceumm"}, {"nestopia", "nestopia"}, {"quicknes", "quicknes"},
        {"gambatte", "gambatte"}, {"sameboy", "sameboy"}, {"gearboy", "gearboy"},
        {"beetlepcefast", "mednafen_pce_fast"}, {"beetlesupergrafx", "mednafen_supergrafx"}, {"beetlepce", "mednafen_pce"},
        {"beetlewonderswan", "mednafen_wswan"}, {"beetleneopop", "mednafen_ngp"}, {"race", "race"},
        {"beetlelynx", "mednafen_lynx"}, {"handy", "handy"}, {"beetlevb", "mednafen_vb"},
        {"bluemsx", "bluemsx"}, {"fmsx", "fmsx"}, {"dosboxpure", "dosbox_pure"}, {"np2kai", "np2kai"},
        {"flycast", "flycast"}, {"mupen64plusnext", "mupen64plus_next"}, {"melonds", "melonds"}, {"desmume", "desmume"},
        {"beetlesaturn", "mednafen_saturn"}, {"yabasanshiro", "yabasanshiro"}, {"yabause", "yabause"}, {"kronos", "kronos"},
        {"mamecurrent", "mame"},
    };
    std::string key = FolderKey(label);
    if (key.empty()) return "";
    for (const auto &kv : t)
        if (key.find(kv.first) != std::string::npos)
            return std::string(kv.second).find('.') != std::string::npos ? kv.second
                                                                         : std::string("/cores/") + kv.second + "_libretro_android.so";
    return "";
}

bool CoreExists(const std::string &nro) {
#ifdef __SWITCH__
    return Exists(nro);
#else
    // 데스크톱 테스트: JOY_SDROOT 가 있으면 그 아래에서 확인, 없으면 모두 있다고 가정
    const char *root = getenv("JOY_SDROOT");
    if (!root) return true;
    return Exists(std::string(root) + nro);
#endif
}

std::string Quote(const std::string &nro) { return "\"" + nro + "\" \"{file.path}\""; }

// ---------------------------------------------------------------- 기종 표
struct System {
    const char *name;                 // 자동 인식 시 카테고리 이름
    std::vector<std::string> folders; // 폴더 이름 (소문자, 영숫자만). '*' 로 끝나면 접두어
    std::vector<std::string> exts;    // 이 기종 롬 확장자
    std::vector<std::string> targets; // 코어 이름 또는 nro 절대경로 (앞에서부터 있는 것 사용)
};

const std::vector<System> &Systems() {
    static const std::vector<System> s = {
        {"아케이드 (FBNeo)", {"fbneo*", "fba*", "arcade*", "cps1", "cps2", "cps3", "neogeo", "finalburn*"}, {"zip", "7z"}, {"fbneo"}},
        {"아케이드 (MAME)", {"mame*"}, {"zip", "7z"}, {"mame2003_plus", "mame2003"}},
        {"NAOMI", {"naomi*", "atomiswave*"}, {"zip", "7z", "lst", "dat"}, {"flycast"}},
        {"드림캐스트", {"dc", "dreamcast", "segadreamcast"}, {"cdi", "gdi", "chd", "cue", "m3u"}, {"flycast"}},
        {"PlayStation", {"psx", "ps1", "playstation", "playstation1", "ps"}, {"chd", "cue", "pbp", "m3u", "iso", "img", "ccd", "bin"}, {"pcsx_rearmed"}},
        {"PSP", {"psp", "pspminis"}, {"iso", "cso", "chd", "pbp", "elf"}, {"/switch/ppsspp/PPSSPP.nro", "ppsspp"}},
        {"GBA", {"gba", "gameboyadvance"}, {"gba", "zip", "7z"}, {"mgba", "vba_next", "gpsp"}},
        {"게임보이", {"gb", "gbc", "gameboy", "gameboycolor"}, {"gb", "gbc", "sgb", "zip", "7z"}, {"gambatte", "sameboy", "mgba"}},
        {"패미컴", {"nes", "fc", "famicom", "fds"}, {"nes", "fds", "unf", "unif", "zip", "7z"}, {"fceumm", "nestopia", "mesen"}},
        {"슈퍼패미컴", {"snes", "sfc", "superfamicom", "supernintendo"}, {"sfc", "smc", "swc", "fig", "bs", "zip", "7z"}, {"snes9x", "snes9x2010"}},
        {"메가드라이브", {"md", "megadrive", "genesis", "segacd", "megacd", "mdcd"}, {"md", "gen", "smd", "68k", "sgd", "zip", "7z"}, {"genesis_plus_gx", "picodrive"}},
        {"32X", {"32x", "sega32x"}, {"32x", "zip", "7z"}, {"picodrive"}},
        {"마스터시스템", {"sms", "mastersystem", "mark3", "segamastersystem"}, {"sms", "zip", "7z"}, {"genesis_plus_gx", "smsplus", "gearsystem"}},
        {"게임기어", {"gg", "gamegear"}, {"gg", "zip", "7z"}, {"genesis_plus_gx", "gearsystem"}},
        {"PC엔진", {"pce", "pcengine", "tg16", "turbografx*", "pcecd", "supergrafx", "sgx"}, {"pce", "sgx", "zip", "7z"}, {"mednafen_pce_fast", "mednafen_pce", "geargrafx"}},
        {"원더스완", {"ws", "wsc", "wonderswan*"}, {"ws", "wsc", "pc2", "zip", "7z"}, {"mednafen_wswan"}},
        {"네오지오 포켓", {"ngp", "ngpc", "neogeopocket*"}, {"ngp", "ngc", "zip", "7z"}, {"mednafen_ngp", "race"}},
        {"NDS", {"nds", "nintendods"}, {"nds"}, {"melonds", "desmume"}},
        {"N64", {"n64", "nintendo64"}, {"n64", "z64", "v64", "zip", "7z"}, {"mupen64plus_next"}},
        {"새턴", {"ss", "saturn", "segasaturn"}, {}, {"yabause"}},
        {"PC-98", {"pc98", "pc9801"}, {"hdi", "fdi", "d88", "hdm", "nhd"}, {"np2kai"}},
        {"DOS", {"dos", "msdos"}, {"dosz", "exe", "bat", "com"}, {"dosbox_pure"}},
        {"MSX", {"msx", "msx1", "msx2"}, {"mx1", "mx2", "rom", "dsk", "zip", "7z"}, {"bluemsx", "fmsx"}},
        {"아타리 링크스", {"lynx", "atarilynx"}, {"lnx", "zip", "7z"}, {"handy", "mednafen_lynx"}},
        {"버추얼보이", {"vb", "virtualboy"}, {"vb", "vboy", "zip", "7z"}, {"mednafen_vb"}},
        {"PC-FX", {"pcfx"}, {}, {"mednafen_pcfx"}},
    };
    return s;
}

// 스위치 성능/에뮬레이터 부재로 제외하는 폴더
bool ExcludedFolder(const std::string &key) {
    static const std::set<std::string> ex = {"ps2", "playstation2", "3ds", "n3ds", "gc", "gamecube", "ngc", "wii", "wiiu",
                                             "psvita", "vita", "switch", "ps3", "xbox", "xbox360", "apk", "app", "android",
                                             "win", "windows", "pc"};
    if (ex.count(key)) return true;
    // "Sony PlayStation 2", "Nintendo 3DS" 처럼 이름 안에 들어 있는 경우
    static const char *part[] = {"playstation2", "playstation3", "playstation4", "ps2", "ps3", "ps4", "3ds", "gamecube",
                                 "wiiu", "psvita", "nintendoswitch", "xbox"};
    for (const char *p : part)
        if (key.find(p) != std::string::npos) return true;
    return false;
}

std::string FolderKey(const std::string &name) {
    std::string k;
    for (char c : Lower(name))
        if (std::isalnum((unsigned char)c)) k += c;
    return k;
}

// 폴더 이름(또는 상위 폴더 이름)으로 기종 찾기. 못 찾으면 -1, 제외 폴더면 -2
int SystemByFolder(const std::string &dirName) {
    std::string key = FolderKey(dirName);
    if (key.empty()) return -1;
    if (ExcludedFolder(key)) return -2;
    // "FBNEO ACT" 처럼 단어가 붙은 경우 첫 단어도 확인
    std::string first = FolderKey(dirName.substr(0, dirName.find_first_of(" _-(")));
    if (ExcludedFolder(first)) return -2;
    const auto &sys = Systems();
    for (size_t i = 0; i < sys.size(); i++)
        for (const auto &f : sys[i].folders) {
            bool prefix = !f.empty() && f.back() == '*';
            std::string ff = prefix ? f.substr(0, f.size() - 1) : f;
            if (key == ff || first == ff || (prefix && (key.rfind(ff, 0) == 0 || first.rfind(ff, 0) == 0))) return (int)i;
        }
    return -1;
}

// 확장자만으로 기종 찾기 (확장자가 한 기종에만 쓰이는 경우)
int SystemByExt(const std::string &ext) {
    static const std::map<std::string, std::string> uniq = {
        {"gba", "GBA"}, {"gb", "게임보이"}, {"gbc", "게임보이"}, {"nes", "패미컴"}, {"fds", "패미컴"}, {"sfc", "슈퍼패미컴"},
        {"smc", "슈퍼패미컴"}, {"md", "메가드라이브"}, {"gen", "메가드라이브"}, {"smd", "메가드라이브"}, {"32x", "32X"},
        {"sms", "마스터시스템"}, {"gg", "게임기어"}, {"pce", "PC엔진"}, {"ws", "원더스완"}, {"wsc", "원더스완"},
        {"ngp", "네오지오 포켓"}, {"ngc", "네오지오 포켓"}, {"nds", "NDS"}, {"n64", "N64"}, {"z64", "N64"}, {"v64", "N64"},
        {"cso", "PSP"}, {"pbp", "PlayStation"}, {"cue", "PlayStation"}, {"chd", "PlayStation"}, {"lnx", "아타리 링크스"},
        {"vb", "버추얼보이"}, {"cdi", "드림캐스트"}, {"gdi", "드림캐스트"}, {"hdi", "PC-98"}, {"fdi", "PC-98"}, {"d88", "PC-98"},
        {"dosz", "DOS"}, {"zip", "아케이드 (FBNeo)"}, {"7z", "아케이드 (FBNeo)"},
    };
    auto it = uniq.find(ext);
    if (it == uniq.end()) return -1;
    const auto &sys = Systems();
    for (size_t i = 0; i < sys.size(); i++)
        if (it->second == sys[i].name) return (int)i;
    return -1;
}

// 메타데이터의 코어 이름 또는 안드로이드 앱 패키지명으로 기종 찾기.
// 못 찾으면 -1, 스위치 대상에서 제외한 기종의 에뮬레이터면 -2.
// (위에서부터 부분 일치. 더 구체적인 이름을 먼저)
int SystemByIdent(const std::string &ident) {
    static const std::vector<std::pair<const char *, const char *>> t = {
        // 제외 기종: PS2·PS3·3DS·GC/Wii/WiiU·스위치·비타·엑스박스·윈도우
        {"aethersx2", "-"}, {"nethersx2", "-"}, {"armsx2", "-"}, {"pcsx2", "-"}, {"lrps2", "-"}, {"virtualapplications.play", "-"},
        {"rpcs3", "-"}, {"aps3e", "-"}, {"citra", "-"}, {"lime3ds", "-"}, {"azahar", "-"}, {"mandarine", "-"},
        {"panda3ds", "-"}, {"dolphin", "-"}, {"cemu", "-"}, {"yuzu", "-"}, {"suyu", "-"}, {"sudachi", "-"},
        {"citron", "-"}, {"dev.eden", "-"}, {"strato", "-"}, {"skyline", "-"}, {"ryujinx", "-"}, {"vita3k", "-"},
        {"xenia", "-"}, {"winlator", "-"}, {"mobox", "-"}, {"exagear", "-"}, {"gamehub", "-"},
        // 성능상 기본 숨김 기종 (settings.txt 의 hide_cores 로 표시 가능)
        {"melonds", "NDS"}, {"desmume", "NDS"}, {"drastic", "NDS"}, {"nds4droid", "NDS"},
        {"mupen64", "N64"}, {"parallel_n64", "N64"}, {"m64plus", "N64"},
        {"yabause", "새턴"}, {"yabasanshiro", "새턴"}, {"yabasanshioro", "새턴"}, {"kronos", "새턴"},
        {"mednafen_saturn", "새턴"}, {"beetle_saturn", "새턴"},
        {"flycast", "드림캐스트"}, {"reicast", "드림캐스트"}, {"redream", "드림캐스트"},
        // 스위치 대상 기종
        {"duckstation", "PlayStation"}, {"swanstation", "PlayStation"}, {"pcsx", "PlayStation"},
        {"mednafen_psx", "PlayStation"}, {"beetle_psx", "PlayStation"}, {"epsxe", "PlayStation"}, {"fpse", "PlayStation"},
        {"ppsspp", "PSP"},
        {"mesen-s", "슈퍼패미컴"}, {"mesen_s", "슈퍼패미컴"}, {"snes9x", "슈퍼패미컴"}, {"bsnes", "슈퍼패미컴"},
        {"fceumm", "패미컴"}, {"nestopia", "패미컴"}, {"quicknes", "패미컴"}, {"mesen", "패미컴"},
        {"gambatte", "게임보이"}, {"sameboy", "게임보이"}, {"gearboy", "게임보이"}, {"tgbdual", "게임보이"},
        {"gpsp", "GBA"}, {"vba", "GBA"}, {"meteor", "GBA"}, {"myboy", "GBA"}, {"pizzaboy", "GBA"},
        {"blastem", "메가드라이브"}, {"md.emu", "메가드라이브"},
        {"mednafen_pce", "PC엔진"}, {"mednafen_supergrafx", "PC엔진"}, {"beetle_pce", "PC엔진"}, {"geargrafx", "PC엔진"},
        {"mednafen_wswan", "원더스완"}, {"beetle_wswan", "원더스완"},
        {"mednafen_ngp", "네오지오 포켓"}, {"beetle_ngp", "네오지오 포켓"},
        {"mame", "아케이드 (MAME)"}, {"fbneo", "아케이드 (FBNeo)"}, {"fbalpha", "아케이드 (FBNeo)"},
        {"dosbox", "DOS"}, {"np2", "PC-98"}, {"nekop2", "PC-98"}, {"bluemsx", "MSX"}, {"fmsx", "MSX"},
        {"handy", "아타리 링크스"}, {"mednafen_lynx", "아타리 링크스"}, {"mednafen_vb", "버추얼보이"},
        {"mednafen_pcfx", "PC-FX"},
    };
    std::string id = Lower(ident);
    if (id.empty()) return -1;
    for (const auto &kv : t) {
        if (id.find(kv.first) == std::string::npos) continue;
        if (kv.second[0] == '-') return -2;
        const auto &sys = Systems();
        for (size_t i = 0; i < sys.size(); i++)
            if (sys[i].name == std::string(kv.second)) return (int)i;
        return -1;
    }
    return -1;
}

std::string TargetFor(int sysIdx) {
    if (sysIdx < 0) return "";
    for (const auto &t : Systems()[sysIdx].targets) {
        std::string nro = t[0] == '/' ? t : "/retroarch/cores/" + t + "_libretro_libnx.nro";
        if (CoreExists(nro)) return Quote(nro);
    }
    return "";
}

// 롬 파일이 들어 있는 폴더 경로로 실행 설정 추정 (폴더 -> 상위 폴더 -> 확장자)
std::string AutoLaunch(const std::string &dir, const std::string &file) {
    std::string d = dir;
    for (int up = 0; up < 2 && !d.empty(); up++) {
        int s = SystemByFolder(Base(d));
        if (s == -2) return "";
        if (s >= 0) return TargetFor(s);
        size_t slash = d.find_last_of('/');
        d = slash == std::string::npos ? "" : d.substr(0, slash);
    }
    return TargetFor(SystemByExt(Ext(file)));
}

// 안드로이드용 launch(am start ...)를 스위치 실행 템플릿으로 바꾼다. 변환 불가면 빈 문자열.
// 숨길 코어 (스위치 성능 범위 밖). SetHiddenCores 로 바꿀 수 있음
std::vector<std::string> gHiddenCores = {"flycast", "mupen64plus_next", "melonds", "desmume", "yabause", "kronos",
                                         "yabasanshiro", "citra", "dolphin"};

// ---------------------------------------------------------------- MAME 계열 롬셋 확인
// 롬셋 이름(zip 이름)이 코어가 지원하는 목록에 있는지 보고 코어를 고른다 (romfs:/sets/<코어>.txt).
#ifdef __SWITCH__
static const char *kSetsDir = "romfs:/sets/";
#else
static const char *kSetsDir = "romfs/sets/";
#endif

const std::set<std::string> *CoreSets(const std::string &core) {
    static std::map<std::string, std::set<std::string>> cache;
    auto it = cache.find(core);
    if (it == cache.end()) {
        std::set<std::string> sets;
        std::ifstream in(std::string(kSetsDir) + core + ".txt");
        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (!line.empty()) sets.insert(line);
        }
        it = cache.emplace(core, std::move(sets)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

// launch 가 MAME 계열이면 롬셋을 지원하는 코어로 바꾼다. 지원하는 코어가 없으면 "" (숨김).
// prefer: 원래 지정된 코어 (없으면 가벼운 mame2003_plus 부터)
std::string PickMameCore(const std::string &launch, const std::string &file) {
    static const char *mames[] = {"mame2003_plus", "mame2010"};
    std::string prefer;
    for (const char *m : mames)
        if (launch.find(std::string("/") + m + "_libretro") != std::string::npos) prefer = m;
    bool latest = launch.find("mamearcade_libretro") != std::string::npos ||
                  launch.find("/mame_libretro") != std::string::npos;
    if (prefer.empty() && !latest) return launch;  // MAME 계열 아님
    std::string ext = Ext(file);
    if (ext != "zip" && ext != "7z") return launch;
    std::string set = Lower(Stem(file));
    std::vector<std::string> order;
    if (!prefer.empty()) order.push_back(prefer);
    for (const char *m : mames)
        if (prefer != m) order.push_back(m);
    bool anyList = false;
    for (const auto &c : order) {
        const std::set<std::string> *sets = CoreSets(c);
        std::string nro = "/retroarch/cores/" + c + "_libretro_libnx.nro";
        if (!sets) continue;
        anyList = true;
        if (sets->count(set) && CoreExists(nro)) return Quote(nro);
    }
    // 목록 파일이 없으면 판단하지 않고 원래대로
    if (!anyList) return launch;
    return "";
}

// ---------------------------------------------------------------- 코어 대체 설정 (/JoyLauncher/cores.txt)
// [auto] 런처 기본값 (실행할 때마다 다시 씀), [user] 사용자 지정 (런처가 건드리지 않음, auto 보다 우선)
//   형식: <안드로이드 코어 이름 또는 앱 패키지명>=<스위치 코어 이름>[,<다음 후보>...]
//         값이 / 로 시작하면 nro 경로, none 이면 그 코어의 게임을 숨김
typedef std::vector<std::pair<std::string, std::string>> CoreMap;  // 순서 유지

const CoreMap &BuiltinCoreMap() {
    static const CoreMap m = {
        {"snes9x2005_plus", "snes9x2010,snes9x"},
        {"snes9x2005", "snes9x2010,snes9x"},
        {"mednafen_pce", "mednafen_pce,mednafen_pce_fast"},
        {"mame2010", "mame2010,mame2003_plus"},
        {"mamearcade", "mame2003_plus,mame2010"},
        {"mame", "mame2003_plus,mame2010"},
        {"org.ppsspp.ppsspp", "/switch/ppsspp/PPSSPP.nro,ppsspp"},
    };
    return m;
}

CoreMap gUserCores;

std::vector<std::string> SplitList(const std::string &v) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : v + ",") {
        if (c == ',') {
            cur = Trim(cur);
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

// 후보 목록에서 스위치에 있는 첫 번째 실행 파일. none 이면 "-" (숨김)
std::string ResolveCores(const std::string &list) {
    for (const auto &c : SplitList(list)) {
        if (Lower(c) == "none") return "-";
        std::string nro = c[0] == '/' ? c : "/retroarch/cores/" + c + "_libretro_libnx.nro";
        if (CoreExists(nro)) return Quote(nro);
    }
    return "";
}

// key 에 대한 설정값 (user 우선). 없으면 ""
std::string CoreSetting(const std::string &key, bool &fromUser) {
    for (const auto &kv : gUserCores)
        if (Lower(kv.first) == Lower(key)) { fromUser = true; return kv.second; }
    for (const auto &kv : BuiltinCoreMap())
        if (kv.first == key) { fromUser = false; return kv.second; }
    return "";
}

std::vector<std::string> Tokenize(const std::string &s);

// 실행 설정에 적힌 코어 이름 (xxx_libretro_android.so -> xxx), 없으면 앱 패키지/nro 경로, 그것도 없으면 ""
std::string LaunchCoreName(const std::string &launch) {
    size_t p = launch.find("_libretro_android");
    if (p != std::string::npos) {
        size_t b = launch.rfind('/', p);
        return launch.substr(b + 1, p - b - 1);
    }
    for (const auto &t : Tokenize(launch)) {
        if (t.find(".nro") != std::string::npos) return t;
        size_t s = t.find('/');  // -n 패키지/액티비티
        if (t.find('.') != std::string::npos && s != std::string::npos && t[0] != '/') return t.substr(0, s);
    }
    return "";
}

// 실행 설정 변환. fromUser: [user] 설정을 썼는지. 반환 "" = 변환 불가, "-" = 숨김
std::string TranslateLaunch(const std::string &launch, bool &fromUser) {
    fromUser = false;
    if (launch.find(".nro") != std::string::npos) return launch;  // 이미 스위치용
    size_t p = launch.find("_libretro_android");
    if (p != std::string::npos) {
        size_t b = launch.rfind('/', p);
        std::string name = launch.substr(b + 1, p - b - 1);
        std::string set = CoreSetting(name, fromUser);
        return ResolveCores(set.empty() ? name : set);
    }
    // 단독 에뮬레이터 앱 (패키지명). user 설정 먼저, 그다음 기본값
    for (int pass = 0; pass < 2; pass++) {
        const CoreMap &m = pass == 0 ? gUserCores : BuiltinCoreMap();
        for (const auto &kv : m)
            if (kv.first.find('.') != std::string::npos && launch.find(kv.first) != std::string::npos) {
                fromUser = pass == 0;
                return ResolveCores(kv.second);
            }
    }
    return "";
}

// 바이오스 등 게임이 아닌 아케이드 롬
bool IsBiosName(const std::string &stem) {
    static const std::set<std::string> b = {"neogeo", "pgm", "skns", "qsound", "decocass", "isgsm", "nmk004", "ym2608",
                                            "bubsys", "coleco", "msx", "spectrum", "cchip", "naomi", "naomi2", "awbios",
                                            "hod2bios", "f355bios", "f355dlx", "airlbios", "naomigd", "stvbios",
                                            "konamigx", "cpzn1", "cpzn2", "coh1000c", "coh1000t", "coh1002m", "tps",
                                            "taitofx1", "hng64", "megaplay", "megatech", "segasp", "triforce", "chihiro"};
    return b.count(Lower(stem)) > 0;
}

// ---------------------------------------------------------------- 공용 추가 함수
struct Library {
    std::map<std::string, Collection> cols;
    std::set<std::string> seen;  // PathKey
    std::map<std::string, std::string> shortNames;  // collection 이름 -> shortname
    std::string log;

    bool Add(const std::string &colName, const std::string &sortBy, Game g) {
        std::string key = PathKey(g.file);
        if (seen.count(key)) return false;
        seen.insert(key);
        if (g.sortKey.empty()) g.sortKey = g.title;
        Collection &c = cols[colName];
        c.name = colName;
        // 이 게임이 들어 있는 롬 폴더 이름 (테마 배경을 폴더 이름으로 찾음)
        std::string folder = Base(g.file.substr(0, g.file.find_last_of('/')));
        if (std::find(c.folders.begin(), c.folders.end(), folder) == c.folders.end()) c.folders.push_back(folder);
        if (c.sortBy.empty()) c.sortBy = sortBy.empty() ? colName : sortBy;
        c.games.push_back(g);
        return true;
    }
};

// 메타데이터의 실행 설정(launch)으로 스위치에서 실행할 대상을 정한다 (cur.launch·metaCore·how).
// 실행할 수 없으면(숨김) false. ident: launch 가 없을 때 기종 판단에 쓸 에뮬레이터 이름 (ES-DE 표시 이름 등)
bool ResolveLaunch(const std::string &dir, const std::string &launch, Game &cur, const std::string &ident = "") {
    bool userCore = false;
    cur.launch = launch.empty() ? "" : TranslateLaunch(launch, userCore);
    if (cur.launch == "-") return false;  // cores.txt 에서 none
    cur.metaCore = launch.empty() ? ident : LaunchCoreName(launch);
    if (cur.launch.empty()) cur.how = "폴더 이름/확장자로 추정";
    else if (launch.find(".nro") != std::string::npos) cur.how = "메타데이터의 nro 그대로";
    else if (userCore) cur.how = "cores.txt [user]";
    else {
        bool dummy = false;
        cur.how = CoreSetting(cur.metaCore, dummy).empty() && launch.find("_libretro_android") != std::string::npos
                      ? "같은 이름 코어"
                      : "cores.txt [auto]";
    }
    if (cur.launch.empty()) {
        // 변환 불가: 원래 코어/에뮬레이터 이름으로 기종을 먼저 판단, 모르면 폴더/확장자로 추정
        int s = SystemByIdent(cur.metaCore);
        if (s == -2) return false;  // 제외 기종 에뮬레이터 (PS2·3DS·GC/Wii·스위치·비타 등)
        if (s >= 0) {
            cur.launch = TargetFor(s);
            cur.how = std::string("코어 이름으로 기종 판단 (") + Systems()[s].name + ")";
        } else {
            cur.launch = AutoLaunch(dir, cur.file);
        }
    }
    if (cur.launch.empty()) return false;
    // MAME 계열: 최신 MAME 용 목록이면 latest 표시를 남겨 롬셋으로 코어 선택
    // (cores.txt [user] 로 직접 정한 코어는 그대로)
    if (!userCore) {
        std::string before = cur.launch;
        cur.launch = PickMameCore(launch.find("mamearcade_libretro") != std::string::npos ? launch : cur.launch, cur.file);
        if (cur.launch != before) cur.how += " + MAME 롬셋 목록으로 선택";
    }
    if (cur.launch.empty()) return false;
    return true;
}

// ---------------------------------------------------------------- 1) 페가수스
struct Entry {
    std::string key;
    std::string value;
};

std::vector<Entry> ReadEntries(const std::string &path) {
    std::vector<Entry> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
                (unsigned char)line[2] == 0xBF)
                line = line.substr(3);
            first = false;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if ((line[0] == ' ' || line[0] == '\t') && !out.empty()) {
            std::string t = Trim(line);
            std::string &v = out.back().value;
            if (t == ".") v += "\n";
            else if (!v.empty() && v.back() != '\n') v += " " + t;
            else v += t;
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        out.push_back({Lower(Trim(line.substr(0, colon))), Trim(line.substr(colon + 1))});
    }
    return out;
}

void ParsePegasus(const std::string &dir, Library &lib) {
    // 제외 기종 폴더(ps2, 3ds, win 등)는 목록이 있어도 읽지 않음
    if (ExcludedFolder(FolderKey(Base(dir)))) {
        lib.log += "[skip] " + dir + "\n";
        return;
    }
    std::vector<Entry> entries = ReadEntries(dir + "/metadata.pegasus.txt");
    std::string colName, colLaunch, colSort;
    std::set<std::string> ignored;
    Game cur;
    // 이미지 asset 우선순위: boxFront > box > cartridge > titlescreen > screenshot > logo/wheel > marquee
    static const char *kAssetKeys[] = {"assets.boxfront", "assets.box_front", "assets.box", "assets.boxfull", "assets.box_full",
                                       "assets.cartridge", "assets.titlescreen", "assets.screenshot", "assets.screenshots",
                                       "assets.logo", "assets.wheel", "assets.marquee"};
    const int kAssetN = sizeof(kAssetKeys) / sizeof(kAssetKeys[0]);
    // 게임 화면 사진 우선순위: screenshot > titlescreen
    static const char *kShotKeys[] = {"assets.screenshot", "assets.screenshots", "assets.screenshot_title",
                                      "assets.titlescreen", "assets.title_screen"};
    const int kShotN = sizeof(kShotKeys) / sizeof(kShotKeys[0]);
    std::vector<std::string> shots(kShotN);
    std::string videoAsset;
    std::vector<std::string> assets(kAssetN);
    bool inGame = false;
    int added = 0, missing = 0, unsupported = 0;

    auto flush = [&]() {
        if (!inGame) return;
        inGame = false;
        if (cur.file.empty()) return;
        std::string rel = cur.file;
        // files: 목록이면 첫 파일
        if (rel.find(' ') != std::string::npos && !Exists(Join(dir, rel))) rel = rel.substr(0, rel.find(' '));
        cur.file = Join(dir, rel);
        if (ignored.count(Base(cur.file))) return;
        if (Ext(cur.file) == "custom" || Ext(cur.file) == "apk") { unsupported++; return; }  // 안드로이드 앱 실행용 항목
        if (!Exists(cur.file)) { missing++; return; }
        std::string launch = cur.launch.empty() ? colLaunch : cur.launch;
        if (!ResolveLaunch(dir, launch, cur)) { unsupported++; return; }
        // assets.* 중 실제로 있는 이미지 중 우선순위가 가장 높은 것, 없으면 폴더 탐색
        cur.boxart.clear();
        for (const std::string &a : assets)
            if (!a.empty() && Exists(Join(dir, a))) { cur.boxart = Join(dir, a); break; }
        if (cur.boxart.empty()) cur.boxart = FindBoxart(dir, rel);
        cur.screenshot.clear();
        for (const std::string &a : shots)
            if (!a.empty() && Exists(Join(dir, a))) { cur.screenshot = Join(dir, a); break; }
        if (cur.screenshot.empty()) cur.screenshot = FindScreenshot(dir, rel);
        cur.video.clear();
        if (cur.screenshot.empty()) {
            if (!videoAsset.empty() && Exists(Join(dir, videoAsset))) cur.video = Join(dir, videoAsset);
            else cur.video = FindVideo(dir, rel);
        }
        AddEsdeMedia(dir, rel, cur);
        std::string name = colName.empty() ? Base(dir) : colName;
        if (lib.Add(name, colSort, cur)) added++;
    };

    for (const Entry &e : entries) {
        if (e.key == "collection") {
            flush();
            colName = e.value;
            colLaunch.clear();
            colSort.clear();
            continue;
        }
        if (e.key == "game") {
            flush();
            cur = Game();
            std::fill(assets.begin(), assets.end(), std::string());
            std::fill(shots.begin(), shots.end(), std::string());
            videoAsset.clear();
            cur.title = e.value;
            inGame = true;
            continue;
        }
        if (!inGame) {
            if (e.key == "launch" || e.key == "command") colLaunch = e.value;
            else if (e.key == "sort-by" || e.key == "sort_by") colSort = e.value;
            else if ((e.key == "shortname" || e.key == "short_name") && !colName.empty() && !e.value.empty() &&
                     !lib.shortNames.count(colName))
                lib.shortNames[colName] = e.value;
            else if (e.key == "ignore-file" || e.key == "ignore-files") {
                ignored.insert(Base(e.value));
                lib.seen.insert(PathKey(Join(dir, e.value)));  // 플레이리스트·자동 인식에서도 숨김
            }
            continue;
        }
        if (e.key == "file" || e.key == "files") cur.file = e.value;
        else if (e.key == "launch" || e.key == "command") cur.launch = e.value;
        else if (e.key == "developer") cur.developer = e.value;
        else if (e.key == "genre") cur.genre = e.value;
        else if (e.key == "release") cur.release = e.value;
        else if (e.key == "players") cur.players = e.value;
        else if (e.key == "description" || e.key == "summary") cur.description = e.value;
        else if (e.key == "sort-title" || e.key == "sort_title") cur.sortKey = e.value;
        else if (e.key.compare(0, 7, "assets.") == 0) {
            for (int i = 0; i < kAssetN; i++)
                if (e.key == kAssetKeys[i] && assets[i].empty()) assets[i] = e.value;  // video 등은 무시
            for (int i = 0; i < kShotN; i++)
                if (e.key == kShotKeys[i] && shots[i].empty()) shots[i] = e.value;
            if ((e.key == "assets.video" || e.key == "assets.videos") && videoAsset.empty()) videoAsset = e.value;
        }
    }
    flush();
    char buf[512];
    snprintf(buf, sizeof(buf), "[pegasus] %s: %d (missing %d, unsupported %d)\n", dir.c_str(), added, missing, unsupported);
    lib.log += buf;
}

// ---------------------------------------------------------------- 2) gamelist.xml
std::string XmlUnescape(std::string s) {
    static const std::pair<const char *, const char *> ents[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}};
    for (const auto &e : ents) {
        size_t p;
        while ((p = s.find(e.first)) != std::string::npos) s.replace(p, strlen(e.first), e.second);
    }
    return Trim(s);
}

std::string XmlTag(const std::string &block, const char *tag) {
    std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t a = block.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t b = block.find(close, a);
    return b == std::string::npos ? "" : XmlUnescape(block.substr(a, b - a));
}

// xmlPath: 롬 폴더 밖의 목록 (ES-DE/gamelists/<폴더>/gamelist.xml). 비어 있으면 <dir>/gamelist.xml
void ParseGamelist(const std::string &dir, Library &lib, const std::string &xmlPath = "") {
    bool esde = !xmlPath.empty();
    if (ExcludedFolder(FolderKey(Base(dir)))) {
        lib.log += "[skip] " + dir + "\n";
        return;
    }
    std::ifstream in(esde ? xmlPath : dir + "/gamelist.xml", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string xml = ss.str();
    // 기종 전체 에뮬레이터 (ES-DE: 목록 맨 앞 <alternativeEmulator><label>)
    std::string sysEmu;
    size_t ae = xml.find("<alternativeEmulator>");
    if (ae != std::string::npos && ae < xml.find("<gameList")) {
        size_t aeEnd = xml.find("</alternativeEmulator>", ae);
        sysEmu = XmlTag(xml.substr(ae, aeEnd == std::string::npos ? std::string::npos : aeEnd - ae), "label");
    }
    // ES-DE 는 폴더 이름이 곧 기종 이름(gba, megadrive) → 카테고리는 런처의 기종 이름으로
    int sys = SystemByFolder(Base(dir));
    std::string col = esde && sys >= 0 ? Systems()[sys].name : Base(dir);
    int added = 0, missing = 0, unsupported = 0;
    size_t pos = 0;
    while ((pos = xml.find("<game", pos)) != std::string::npos) {
        if (xml.compare(pos, 6, "<game>") != 0 && xml.compare(pos, 6, "<game ") != 0) { pos += 5; continue; }
        size_t end = xml.find("</game>", pos);
        if (end == std::string::npos) break;
        std::string blk = xml.substr(pos, end - pos);
        pos = end + 7;
        if (XmlTag(blk, "hidden") == "true") continue;
        Game g;
        std::string rel = XmlTag(blk, "path");
        if (rel.empty()) continue;
        g.file = Join(dir, rel);
        if (!Exists(g.file) || IsDir(g.file)) { missing++; continue; }
        g.title = XmlTag(blk, "name");
        if (g.title.empty()) g.title = Stem(rel);
        g.description = XmlTag(blk, "desc");
        g.developer = XmlTag(blk, "developer");
        g.genre = XmlTag(blk, "genre");
        g.players = XmlTag(blk, "players");
        std::string rd = XmlTag(blk, "releasedate");
        if (rd.size() >= 4) g.release = rd.substr(0, 4);
        std::string emu = XmlTag(blk, "altemulator");
        if (emu.empty()) emu = sysEmu;
        if (!ResolveLaunch(dir, EsdeEmulatorLaunch(emu), g, Lower(emu))) { unsupported++; continue; }
        g.how = std::string(esde ? "ES-DE gamelist" : "gamelist.xml") + (emu.empty() ? "" : " (" + emu + ")") + ", " + g.how;
        std::string img = XmlTag(blk, "image");
        if (img.empty()) img = XmlTag(blk, "thumbnail");
        g.boxart = !img.empty() && Exists(Join(dir, img)) ? Join(dir, img) : FindBoxart(dir, rel);
        g.screenshot = FindScreenshot(dir, rel);
        if (g.screenshot.empty()) g.video = FindVideo(dir, rel);
        AddEsdeMedia(dir, rel, g);
        if (lib.Add(col, "", g)) added++;
    }
    char buf[640];
    snprintf(buf, sizeof(buf), "[%s] %s: %d (missing %d, unsupported %d)\n", esde ? "esde" : "gamelist", dir.c_str(), added,
             missing, unsupported);
    lib.log += buf;
}

// ---------------------------------------------------------------- 3) 목록 없는 롬 폴더
bool IsRomFile(const std::string &file, int sysIdx) {
    std::string e = Ext(file);
    if (e.empty()) return false;
    static const std::set<std::string> never = {"txt", "xml", "json", "png", "jpg", "jpeg", "gif", "bmp", "mp4", "mkv",
                                                "avi", "pdf", "nfo", "dat", "ini", "cfg", "srm", "sav", "state", "db",
                                                "lpl", "log", "htm", "html", "ips", "bps", "ups", "xdelta", "sha1",
                                                "md5", "sfv", "url", "lnk", "pegasus"};
    if (never.count(e)) return false;
    // .md 는 메가드라이브 폴더에서만 롬 (그 밖에서는 README.md 같은 문서)
    if (e == "md" && (sysIdx < 0 || std::string(Systems()[sysIdx].name) != "메가드라이브")) return false;
    if (sysIdx >= 0) {
        const auto &ex = Systems()[sysIdx].exts;
        if (ex.empty()) return e == "chd" || e == "cue" || e == "iso" || e == "m3u" || e == "ccd";
        return std::find(ex.begin(), ex.end(), e) != ex.end();
    }
    return SystemByExt(e) >= 0;
}

void ScanAuto(const std::string &dir, Library &lib) {
    int sys = SystemByFolder(Base(dir));
    if (sys == -2) return;
    std::vector<std::string> files = ListDir(dir, false);
    // m3u 가 가리키는 디스크 파일, cue 와 짝인 bin 은 숨김
    std::set<std::string> hide;
    for (const auto &f : files) {
        std::string e = Ext(f);
        if (e == "m3u") {
            std::ifstream in(dir + "/" + f);
            std::string line;
            while (std::getline(in, line)) {
                line = Trim(line);
                if (!line.empty() && line[0] != '#') hide.insert(Lower(Base(line)));
            }
        }
        if (e == "cue") {
            std::string st = Lower(Stem(f));
            for (const auto &g : files)
                if (Ext(g) == "bin" && Lower(g).rfind(st, 0) == 0) hide.insert(Lower(g));
        }
    }
    int added = 0;
    for (const auto &f : files) {
        if (hide.count(Lower(f)) || !IsRomFile(f, sys)) continue;
        if (Ext(f) == "zip" && IsBiosName(Stem(f))) continue;
        Game g;
        g.file = dir + "/" + f;
        g.title = Stem(f);
        g.launch = sys >= 0 ? TargetFor(sys) : AutoLaunch(dir, g.file);
        g.how = "메타데이터 없음, 폴더 이름/확장자로 추정";
        if (!g.launch.empty()) {
            std::string before = g.launch;
            g.launch = PickMameCore(g.launch, g.file);
            if (g.launch != before) g.how += " + MAME 롬셋 목록으로 선택";
        }
        if (g.launch.empty()) continue;
        g.boxart = FindBoxart(dir, f);
        g.screenshot = FindScreenshot(dir, f);
        if (g.screenshot.empty()) g.video = FindVideo(dir, f);
        AddEsdeMedia(dir, f, g);
        std::string col = Base(dir);
        if (lib.Add(col, "~" + col, g)) added++;  // 자동 인식 카테고리는 뒤쪽으로
    }
    if (added) {
        char buf[512];
        snprintf(buf, sizeof(buf), "[auto] %s: %d\n", dir.c_str(), added);
        lib.log += buf;
    }
}

// ---------------------------------------------------------------- 폴더 순회
// 그림·영상만 들어 있는 폴더 (롬 폴더로 보지 않음)
bool IsMediaFolder(const std::string &dir) {
    std::string key = FolderKey(Base(dir));
    return key == "media" || key == "boxart" || key == "snap" || key == "wheel" || key == "images" || key == "videos" ||
           key == "marquees" || key == "manuals" || key == "downloadedmedia" || key == "covers" || key == "screenshots" ||
           key == "box" || key == "boxfront" || key == "cartridge" || key == "logo" || key == "marquee" ||
           key == "screenshot" || key == "titlescreen" || key == "video" || key == "boxarts" || key == "3dboxes" ||
           key == "snaps" || key == "namedboxarts" || key == "esde" || key == "gamelists" || key == "downloadedmedia";
}

void Scan(const std::string &dir, int depth, Library &lib, bool parentHasMeta = false) {
    if (IsMediaFolder(dir)) return;
    bool meta = true;
    if (Exists(dir + "/metadata.pegasus.txt")) ParsePegasus(dir, lib);
    else if (Exists(dir + "/gamelist.xml")) ParseGamelist(dir, lib);
    else if (!parentHasMeta && !EsdeGamelist(dir).empty()) ParseGamelist(dir, lib, EsdeGamelist(dir));
    else {
        meta = false;
        // 메타데이터 폴더의 하위 폴더(NAOMI/ikaruga 같은 게임 데이터 폴더)는 자동 등록하지 않음
        if (!parentHasMeta) ScanAuto(dir, lib);
    }
    if (depth <= 0) return;
    for (const auto &s : ListDir(dir, true)) Scan(dir + "/" + s, depth - 1, lib, meta || parentHasMeta);
}


std::vector<std::string> Tokenize(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool quote = false, any = false;
    for (char c : s) {
        if (c == '"') { quote = !quote; any = true; continue; }
        if (!quote && (c == ' ' || c == '\t')) {
            if (any) out.push_back(cur);
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if (any) out.push_back(cur);
    return out;
}

}  // namespace

void SetHiddenCores(const std::vector<std::string> &cores) { gHiddenCores = cores; }

void LoadCoreConfig(const std::string &path) {
    // [user] 구역은 그대로 보존하고, [auto] 구역은 현재 기본값으로 다시 쓴다
    std::vector<std::string> userLines;
    gUserCores.clear();
    {
        std::ifstream in(path);
        std::string line;
        bool inUser = false, first = true;
        while (std::getline(in, line)) {
            if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);  // BOM
            first = false;
            while (!line.empty() && line.back() == '\r') line.pop_back();
            std::string t = Trim(line);
            if (!t.empty() && t[0] == '[') { inUser = Lower(t) == "[user]"; continue; }
            if (!inUser) continue;
            if (t.rfind("# 사용자 지정 (런처가 바꾸지 않음)", 0) == 0) continue;  // 구역 머리말은 매번 새로 씀
            userLines.push_back(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = Trim(t.substr(0, eq)), v = Trim(t.substr(eq + 1));
            if (!k.empty()) gUserCores.push_back({k, v});
        }
    }
    while (!userLines.empty() && Trim(userLines.back()).empty()) userLines.pop_back();
    if (userLines.empty())
        userLines = {"# 예) 슈퍼패미컴을 최신 snes9x 로 실행:  snes9x2005_plus=snes9x",
                     "# 예) 특정 코어의 게임 숨기기:          mednafen_wswan=none"};
    std::ofstream out(path);
    if (!out) return;
    out << "# Joy Launcher 코어 대체 설정\n"
           "# 안드로이드 목록의 코어(또는 에뮬레이터 앱 패키지명)를 스위치에서 어떤 코어로 실행할지 정합니다.\n"
           "# 형식: <안드로이드 코어 이름>=<스위치 코어 이름>[,<없을 때 다음 후보>...]\n"
           "#       스위치 코어 이름은 /retroarch/cores/<이름>_libretro_libnx.nro 의 <이름>\n"
           "#       / 로 시작하면 nro 경로,  none 이면 그 코어의 게임을 목록에서 숨김\n"
           "# 적혀 있지 않은 코어는 같은 이름의 스위치 코어를 찾고, 없으면 폴더 이름·확장자로 기종을 추정합니다.\n"
           "\n"
           "[auto]\n"
           "# 런처 기본값입니다. 런처를 실행할 때마다 다시 써지므로 여기는 고치지 마세요.\n";
    for (const auto &kv : BuiltinCoreMap()) out << kv.first << "=" << kv.second << "\n";
    out << "\n[user]\n"
           "# 사용자 지정 (런처가 바꾸지 않음). [auto] 보다 우선합니다.\n";
    for (const auto &l : userLines) out << l << "\n";
}

std::vector<Collection> LoadLibrary(const std::vector<std::string> &rootDirs, std::string &log) {
    Library lib;
    gDirCache.clear();
    gDirCacheOn = true;
    FindEsde(rootDirs);
    if (!gEsdeDir.empty()) lib.log += "[esde] " + gEsdeDir + " (media " + gEsdeMedia + ")\n";
    for (const auto &r : rootDirs) {
        if (!IsDir(r)) continue;
        // 루트 자체는 목록 파일이 있을 때만 (롬을 루트에 직접 두는 경우는 드묾)
        if (Exists(r + "/metadata.pegasus.txt")) ParsePegasus(r, lib);
        for (const auto &s : ListDir(r, true)) Scan(r + "/" + s, 1, lib);
    }
    gDirCacheOn = false;
    gDirCache.clear();
    log = lib.log;
    std::vector<Collection> out;
    for (auto &kv : lib.cols) {
        Collection c = kv.second;
        // 숨길 코어(성능 범위 밖)로 실행되는 게임 빼기
        if (!gHiddenCores.empty()) {
            int before = (int)c.games.size();
            c.games.erase(std::remove_if(c.games.begin(), c.games.end(),
                                         [](const Game &g) {
                                             for (const auto &h : gHiddenCores)
                                                 if (g.launch.find("/" + h + "_libretro") != std::string::npos ||
                                                     g.launch.find("/" + h + ".nro") != std::string::npos)
                                                     return true;
                                             return false;
                                         }),
                          c.games.end());
            if ((int)c.games.size() != before) {
                char buf[256];
                snprintf(buf, sizeof(buf), "[hide] %s: %d\n", c.name.c_str(), before - (int)c.games.size());
                log += buf;
            }
        }
        if (c.games.empty()) continue;
        auto sn = lib.shortNames.find(c.name);
        if (sn != lib.shortNames.end()) c.shortName = sn->second;
        std::sort(c.games.begin(), c.games.end(), [](const Game &a, const Game &b) { return a.sortKey < b.sortKey; });
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Collection &a, const Collection &b) { return a.sortBy < b.sortBy; });
    return out;
}

bool BuildLaunch(const Game &g, std::string &nroPath, std::string &argString) {
    std::vector<std::string> tok = Tokenize(g.launch);
    if (tok.empty()) return false;
    argString.clear();
    for (size_t i = 0; i < tok.size(); i++) {
        std::string t = tok[i];
        size_t p;
        while ((p = t.find("{file.path}")) != std::string::npos) t.replace(p, 11, g.file);
        if (i == 0) nroPath = t;
        if (!argString.empty()) argString += " ";
        argString += "\"" + t + "\"";
    }
    return Lower(nroPath).size() > 4 && Lower(nroPath).substr(Lower(nroPath).size() - 4) == ".nro";
}

// ---------------------------------------------------------------- 게임 목록 저장 (/JoyLauncher/library.cache)
// 시작할 때마다 전체 롬 폴더를 다시 읽지 않도록 읽은 결과를 저장해 두고 그대로 쓴다.
// 다음이 바뀌면 자동으로 다시 읽는다: 런처 버전, 롬 폴더 구성(2단계), metadata.pegasus.txt·gamelist.xml 의
// 수정 시각·크기, 스위치 코어 목록, cores.txt [user], 숨길 코어 설정. (롬·그림만 넣고 뺀 경우는 메뉴에서 새로 고침)
namespace {
const char *kCacheMagic = "JOYLIB 1";

void SigDir(const std::string &dir, int depth, std::string &sig) {
    if (IsMediaFolder(dir)) return;
    sig += "D" + dir + "\n";
    for (const char *f : {"/metadata.pegasus.txt", "/gamelist.xml"}) {
        struct stat st;
        if (stat((dir + f).c_str(), &st) == 0)
            sig += std::string(f) + " " + std::to_string((long long)st.st_mtime) + " " + std::to_string((long long)st.st_size) + "\n";
    }
    if (depth <= 0) return;
    for (const auto &s : ListDir(dir, true)) SigDir(dir + "/" + s, depth - 1, sig);
}

std::string Esc(const std::string &s) {
    std::string o;
    for (char ch : s) {
        if (ch == '\\') o += "\\\\";
        else if (ch == '\t') o += "\\t";
        else if (ch == '\n') o += "\\n";
        else if (ch == '\r') continue;
        else o += ch;
    }
    return o;
}

std::string Unesc(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::vector<std::string> SplitTab(const std::string &line) {
    std::vector<std::string> v;
    size_t a = 0;
    while (true) {
        size_t b = line.find('\t', a);
        v.push_back(Unesc(line.substr(a, b == std::string::npos ? std::string::npos : b - a)));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}
}  // namespace

void SetEsdeHint(const std::string &path) { gEsdeHint = path; }

std::string LibrarySignature(const std::vector<std::string> &rootDirs) {
    std::string sig = std::string(kCacheMagic) + " " + __DATE__ + " " + __TIME__ + "\n";
    for (const auto &h : gHiddenCores) sig += "H" + h + "\n";
    for (const auto &kv : gUserCores) sig += "U" + kv.first + "=" + kv.second + "\n";
    std::string coreDir = "/retroarch/cores";
#ifndef __SWITCH__
    if (const char *sd = getenv("JOY_SDROOT")) coreDir = std::string(sd) + coreDir;
#endif
    for (const auto &n : ListDir(coreDir, false)) sig += "C" + n + "\n";
    sig += CoreExists("/switch/ppsspp/PPSSPP.nro") ? "PPSSPP\n" : "";
    // ES-DE 폴더와 그 목록들
    FindEsde(rootDirs);
    if (!gEsdeDir.empty()) {
        sig += "E" + gEsdeDir + "\nM" + gEsdeMedia + "\n";
        for (const auto &d : ListDir(gEsdeDir + "/gamelists", true)) {
            struct stat st;
            std::string f = gEsdeDir + "/gamelists/" + d + "/gamelist.xml";
            if (stat(f.c_str(), &st) == 0)
                sig += "G" + d + " " + std::to_string((long long)st.st_mtime) + " " + std::to_string((long long)st.st_size) + "\n";
        }
    }
    for (const auto &r : rootDirs) {
        if (!IsDir(r)) continue;
        sig += "R" + r + "\n";
        SigDir(r, 2, sig);
    }
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : sig) h = (h ^ ch) * 1099511628211ULL;
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

bool SaveLibraryCache(const std::string &path, const std::string &sig, const std::vector<Collection> &cols) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << kCacheMagic << "\t" << sig << "\n";
        for (const auto &c : cols) {
            o << "C\t" << Esc(c.name) << "\t" << Esc(c.sortBy) << "\t" << Esc(c.shortName);
            for (const auto &f : c.folders) o << "\t" << Esc(f);
            o << "\n";
            for (const auto &g : c.games)
                o << "G\t" << Esc(g.title) << "\t" << Esc(g.file) << "\t" << Esc(g.launch) << "\t" << Esc(g.developer)
                  << "\t" << Esc(g.genre) << "\t" << Esc(g.release) << "\t" << Esc(g.players) << "\t"
                  << Esc(g.description) << "\t" << Esc(g.boxart) << "\t" << Esc(g.screenshot) << "\t" << Esc(g.video)
                  << "\t" << Esc(g.sortKey) << "\t" << Esc(g.metaCore) << "\t" << Esc(g.how) << "\n";
        }
        o << "END\n";
        if (!o) return false;
    }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool LoadLibraryCache(const std::string &path, const std::string &sig, std::vector<Collection> &cols) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    if (!std::getline(in, line) || line != std::string(kCacheMagic) + "\t" + sig) return false;
    std::vector<Collection> out;
    bool end = false;
    while (std::getline(in, line)) {
        if (line == "END") { end = true; break; }
        std::vector<std::string> v = SplitTab(line);
        if (v[0] == "C" && v.size() >= 4) {
            Collection c;
            c.name = v[1];
            c.sortBy = v[2];
            c.shortName = v[3];
            c.folders.assign(v.begin() + 4, v.end());
            out.push_back(c);
        } else if (v[0] == "G" && v.size() >= 15 && !out.empty()) {
            Game g;
            g.title = v[1]; g.file = v[2]; g.launch = v[3]; g.developer = v[4]; g.genre = v[5]; g.release = v[6];
            g.players = v[7]; g.description = v[8]; g.boxart = v[9]; g.screenshot = v[10]; g.video = v[11];
            g.sortKey = v[12]; g.metaCore = v[13]; g.how = v[14];
            out.back().games.push_back(g);
        } else {
            return false;
        }
    }
    if (!end || out.empty()) return false;  // 중간에 끊긴 파일
    cols = std::move(out);
    return true;
}
