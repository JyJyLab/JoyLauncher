// 게임 목록 읽기
//  1) 페가수스 metadata.pegasus.txt (launch 의 am start … 를 그대로 실행)
//  2) ES-DE / EmulationStation gamelist.xml
//  3) 목록 파일이 없는 롬 폴더 (폴더 이름·확장자로 기종 자동 인식)
// 같은 롬 파일은 먼저 읽은 쪽 하나만 사용한다.
// 어떤 에뮬레이터로 실행할지는 여기서 정하지 않고 실행할 때 고른다 (emulators.cpp):
// 목록에는 페가수스 launch 원문 또는 "@sys:<기종>" 만 적어 둔다.
#include "pegasus.h"

#include "emulators.h"
#include "esde.h"

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
#ifdef __ANDROID__
    std::string k = name;  // 안드로이드 공용 저장소·SD 카드는 대소문자 구분 없음
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
#ifdef _WIN32  // 테마 빌더 윈도우 빌드: d_type 없음
            struct stat st;
            isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#else
            if (ent->d_type == DT_DIR) isDir = true;
            else if (ent->d_type == DT_REG) isDir = false;
            else {
                struct stat st;
                isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            }
#endif
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

// 목록에 글자 그대로 적힌 \n (줄바꿈)·\t 를 실제 글자로 (페가수스 테마들이 그렇게 보여 줌)
std::string Unescape(const std::string &s) {
    if (s.find('\\') == std::string::npos) return s;
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == 't')) {
            o += s[i + 1] == 'n' ? '\n' : ' ';
            i++;
        } else {
            o += s[i];
        }
    }
    return o;
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

// webp: 페가수스 JoiPlay 목록 등 (안드로이드는 기기 해석기로 읽음)
const char *kImgExts[] = {".png", ".jpg", ".jpeg", ".webp", ".PNG", ".JPG", ".WEBP"};

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

// 미리보기 영상 찾기 (게임 화면에서 재생)
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
std::vector<std::string> gEsdeMore;  // 그 밖에 찾아볼 자리
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
    for (const auto &c : gEsdeMore) cand.push_back(c);  // 저장소 루트의 /ES-DE (ES-DE 앱의 기본 자리)
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
    if (g.video.empty()) g.video = EsdeMediaFile(dir, rel, 2);
}


std::string FolderKey(const std::string &name) {
    std::string k;
    for (char c : Lower(name))
        if (std::isalnum((unsigned char)c)) k += c;
    return k;
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

// 롬 폴더(또는 그 위 폴더)의 이름으로 기종 찾기. 모르면 nullptr
const EmuSystem *SystemForDir(const std::string &dir) {
    std::string d = dir;
    for (int up = 0; up < 2 && !d.empty(); up++, d = ParentDir(d))
        if (const EmuSystem *s = EmuSystemForFolder(Base(d))) return s;
    return nullptr;
}

// 화면에 보일 기종 이름: 런처의 한글 이름 표에 있으면 그것, 없으면 ES-DE 의 긴 이름
std::string SystemTitle(const EmuSystem &s) {
    for (const auto &e : EsdeSystems())
        if (s.name == e.id || (!s.theme.empty() && s.theme == e.id)) return e.ko;
    return s.fullname.empty() ? s.name : s.fullname;
}

// 게임의 실행 방법을 적어 둔다 (cur.launch·metaCore·how). 실제 에뮬레이터는 실행할 때 고른다.
//   launch: 목록(페가수스)에 적힌 실행 명령 — 있으면 그대로 (1차)
//   없으면 롬 폴더 이름 → 카테고리 이름 → 확장자로 기종을 정함 (2차, ES-DE 방식). alt: ES-DE 목록이 정한 에뮬레이터 이름
// 실행할 길이 없으면 false (목록에서 뺌)
bool ResolveLaunch(const std::string &dir, const std::string &launch, Game &cur, const std::string &colName, const std::string &alt = "") {
    if (!launch.empty()) {
        cur.launch = launch;
        cur.metaCore = EmuLaunchIdent(launch);
        cur.how = "메타데이터의 실행 명령";
        return true;
    }
    const EmuSystem *s = SystemForDir(dir);
    std::string how = "폴더 이름으로 기종 판단";
    if (!s && !colName.empty()) {
        s = EmuSystemForFolder(colName);
        how = "카테고리 이름으로 기종 판단";
    }
    if (!s) {
        s = EmuSystemForExt(Ext(cur.file));
        how = "확장자로 기종 판단";
    }
    if (s) {
        cur.launch = std::string(kLaunchSys) + s->name + (alt.empty() ? "" : "|" + alt);
        cur.metaCore = alt;
        cur.how = how + " (" + s->name + ")";
        return true;
    }
    // 기종은 모르지만 emulators.txt 에 이 폴더·카테고리의 줄이 있으면 실행할 때 그 설정을 쓴다
    if (EmuUserHas(Base(dir)) || EmuUserHas(colName)) {
        cur.launch = kLaunchSys;
        cur.how = "emulators.txt";
        return true;
    }
    return false;
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

// 목록 파일이 있는 폴더에서 목록 밖 롬도 등록 (ES-DE·Pegasus 와 같은 동작, 스위치 BACKLOG #51)
struct AutoOpt {
    std::string col;                       // 넣을 카테고리 (비면 폴더 이름, 자동 인식 카테고리)
    const std::set<std::string> *exts = nullptr;    // 이 확장자만 (Pegasus extensions:)
    const std::set<std::string> *noExts = nullptr;  // 이 확장자 제외 (Pegasus ignore-extensions:)
    const std::string *launch = nullptr;   // 메타데이터의 실행 설정 (Pegasus launch:)
    std::string sort;                      // 카테고리 정렬 이름 (col 을 쓸 때)
};
void ScanAuto(const std::string &dir, Library &lib, const AutoOpt &opt = AutoOpt());

void ParsePegasus(const std::string &dir, Library &lib) {
    std::vector<Entry> entries = ReadEntries(dir + "/metadata.pegasus.txt");
    std::string colName, colLaunch, colSort;
    std::set<std::string> ignored;
    // 컬렉션마다 extensions: 가 있으면 목록 밖 파일도 같은 컬렉션에 넣는다 (Pegasus 와 같게)
    struct ColScan { std::string name, launch, sort; std::set<std::string> exts, noExts; };
    std::vector<ColScan> colScans;
    auto extList = [](const std::string &v, std::set<std::string> &out) {
        std::string cur;
        for (char ch : v + ",") {
            if (ch == ',' || ch == ' ' || ch == '\t') {
                if (!cur.empty()) { if (cur[0] == '.') cur.erase(0, 1); if (!cur.empty()) out.insert(Lower(cur)); }
                cur.clear();
            } else cur += ch;
        }
    };
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
        if (!Exists(cur.file)) { missing++; return; }
        std::string launch = cur.launch.empty() ? colLaunch : cur.launch;
        if (!ResolveLaunch(dir, launch, cur, colName)) { unsupported++; return; }
        // assets.* 중 실제로 있는 이미지 중 우선순위가 가장 높은 것, 없으면 폴더 탐색
        cur.boxart.clear();
        for (const std::string &a : assets)
            if (!a.empty() && Exists(Join(dir, a))) { cur.boxart = Join(dir, a); break; }
        if (cur.boxart.empty()) cur.boxart = FindBoxart(dir, rel);
        cur.screenshot.clear();
        for (const std::string &a : shots)
            if (!a.empty() && Exists(Join(dir, a))) { cur.screenshot = Join(dir, a); break; }
        if (cur.screenshot.empty()) cur.screenshot = FindScreenshot(dir, rel);
        if (!videoAsset.empty() && Exists(Join(dir, videoAsset))) cur.video = Join(dir, videoAsset);
        else cur.video = FindVideo(dir, rel);
        AddEsdeMedia(dir, rel, cur);
        std::string name = colName.empty() ? Base(dir) : colName;
        if (lib.Add(name, colSort, cur)) added++;
    };

    for (const Entry &e : entries) {
        if (e.key == "collection") {
            flush();
            colScans.push_back(ColScan());
            colScans.back().name = e.value;
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
            else if ((e.key == "extension" || e.key == "extensions") && !colScans.empty()) extList(e.value, colScans.back().exts);
            else if ((e.key == "ignore-extension" || e.key == "ignore-extensions") && !colScans.empty())
                extList(e.value, colScans.back().noExts);
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
        else if (e.key == "description" || e.key == "summary") cur.description = Unescape(e.value);
        else if (e.key == "sort-title" || e.key == "sort_title") cur.sortKey = e.value;
        else if (e.key.compare(0, 7, "assets.") == 0) {
            for (int i = 0; i < kAssetN; i++)
                if (e.key == kAssetKeys[i] && assets[i].empty()) assets[i] = e.value;
            for (int i = 0; i < kShotN; i++)
                if (e.key == kShotKeys[i] && shots[i].empty()) shots[i] = e.value;
            if ((e.key == "assets.video" || e.key == "assets.videos") && videoAsset.empty()) videoAsset = e.value;
        }
    }
    flush();
    char buf[512];
    snprintf(buf, sizeof(buf), "[pegasus] %s: %d (missing %d, unsupported %d)\n", dir.c_str(), added, missing, unsupported);
    lib.log += buf;
    {
        size_t ci = (size_t)-1;
        bool inG = false;
        for (const Entry &e : entries) {
            if (e.key == "collection") { ci++; inG = false; continue; }
            if (e.key == "game") { inG = true; continue; }
            if (inG || ci >= colScans.size()) continue;
            if (e.key == "launch" || e.key == "command") colScans[ci].launch = e.value;
            else if (e.key == "sort-by" || e.key == "sort_by") colScans[ci].sort = e.value;
        }
    }
    for (const ColScan &c : colScans) {
        if (c.exts.empty()) continue;  // files: 만 있는 컬렉션은 목록 전용 (Pegasus 와 같음)
        AutoOpt o;
        o.col = c.name.empty() ? Base(dir) : c.name;
        o.exts = &c.exts;
        o.noExts = &c.noExts;
        o.launch = &c.launch;
        o.sort = c.sort;
        ScanAuto(dir, lib, o);
    }
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
    const EmuSystem *sys = EmuSystemForFolder(Base(dir));
    std::string col = esde && sys ? SystemTitle(*sys) : Base(dir);
    int added = 0, missing = 0, unsupported = 0;
    size_t pos = 0;
    while ((pos = xml.find("<game", pos)) != std::string::npos) {
        if (xml.compare(pos, 6, "<game>") != 0 && xml.compare(pos, 6, "<game ") != 0) { pos += 5; continue; }
        size_t end = xml.find("</game>", pos);
        if (end == std::string::npos) break;
        std::string blk = xml.substr(pos, end - pos);
        pos = end + 7;
        if (XmlTag(blk, "hidden") == "true") {
            std::string hp = XmlTag(blk, "path");
            if (!hp.empty()) lib.seen.insert(PathKey(Join(dir, hp)));  // 목록 밖 자동 등록에서도 숨김
            continue;
        }
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
        if (!ResolveLaunch(dir, "", g, "", emu)) { unsupported++; continue; }
        g.how = std::string(esde ? "ES-DE gamelist" : "gamelist.xml") + (emu.empty() ? "" : " (" + emu + ")") + ", " + g.how;
        std::string img = XmlTag(blk, "image");
        if (img.empty()) img = XmlTag(blk, "thumbnail");
        g.boxart = !img.empty() && Exists(Join(dir, img)) ? Join(dir, img) : FindBoxart(dir, rel);
        g.screenshot = FindScreenshot(dir, rel);
        std::string vid = XmlTag(blk, "video");
        g.video = !vid.empty() && Exists(Join(dir, vid)) ? Join(dir, vid) : FindVideo(dir, rel);
        AddEsdeMedia(dir, rel, g);
        if (lib.Add(col, "", g)) added++;
    }
    char buf[640];
    snprintf(buf, sizeof(buf), "[%s] %s: %d (missing %d, unsupported %d)\n", esde ? "esde" : "gamelist", dir.c_str(), added,
             missing, unsupported);
    lib.log += buf;
    // 목록에 없는 롬도 같은 카테고리로 (ES-DE 는 폴더를 먼저 다 찾고 gamelist 는 정보만 덧씌움)
    AutoOpt o;
    o.col = col;
    ScanAuto(dir, lib, o);
}

// ---------------------------------------------------------------- 3) 목록 없는 롬 폴더
// sys: 폴더의 기종 (nullptr = 모름: 확장자가 한 기종에만 쓰이는 파일만). any: 기종을 몰라도 모든 파일을 게임으로 (emulators.txt 로 정한 폴더)
bool IsRomFile(const std::string &file, const EmuSystem *sys, bool any) {
    std::string e = Ext(file);
    if (e.empty()) return false;
    static const std::set<std::string> never = {"txt", "xml", "json", "png", "jpg", "jpeg", "gif", "bmp", "mp4", "mkv",
                                                "avi", "pdf", "nfo", "dat", "ini", "cfg", "srm", "sav", "state", "db",
                                                "lpl", "log", "htm", "html", "ips", "bps", "ups", "xdelta", "sha1",
                                                "md5", "sfv", "url", "lnk", "pegasus", "tmp", "part", "bak"};
    if (never.count(e)) return false;
    if (sys) return std::find(sys->exts.begin(), sys->exts.end(), e) != sys->exts.end();
    if (any) return true;
    // .md 는 메가드라이브 폴더에서만 롬 (그 밖에서는 README.md 같은 문서)
    if (e == "md") return false;
    // 압축 파일은 기종을 모르는 폴더에서는 롬으로 보지 않음 (세이브·게임 데이터 묶음일 수 있음 — JoiPlay 폴더의 save.zip 등)
    if (e == "zip" || e == "7z" || e == "rar") return false;
    return EmuSystemForExt(e) != nullptr;
}

void ScanAuto(const std::string &dir, Library &lib, const AutoOpt &opt) {
    const EmuSystem *sys = SystemForDir(dir);
    bool user = !sys && EmuUserHas(Base(dir));
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
    // 폴더 하나가 게임 하나인 기종 (PS3 의 "게임.ps3" 폴더 등): 이름 끝이 그 기종 확장자인 폴더
    if (sys)
        for (const auto &d : ListDir(dir, true)) {
            std::string e = Ext(d);
            if (!e.empty() && e != "zip" && e != "7z" && std::find(sys->exts.begin(), sys->exts.end(), e) != sys->exts.end()) files.push_back(d);
        }
    int added = 0;
    for (const auto &f : files) {
        if (hide.count(Lower(f))) continue;
        if (opt.exts) {
            if (!opt.exts->count(Lower(Ext(f))) || (opt.noExts && opt.noExts->count(Lower(Ext(f))))) continue;
        } else if (!IsRomFile(f, sys, user)) continue;
        if (lib.seen.count(PathKey(dir + "/" + f))) continue;  // 목록에 이미 있음·ignore-files·hidden
        if (Ext(f) == "zip" && IsBiosName(Stem(f))) continue;
        Game g;
        g.file = dir + "/" + f;
        g.title = Stem(f);
        if (!ResolveLaunch(dir, opt.launch ? *opt.launch : "", g, "")) continue;
        g.how = (opt.col.empty() ? "메타데이터 없음, " : "목록에 없는 파일, ") + g.how;
        g.boxart = FindBoxart(dir, f);
        g.screenshot = FindScreenshot(dir, f);
        g.video = FindVideo(dir, f);
        AddEsdeMedia(dir, f, g);
        std::string col = opt.col.empty() ? Base(dir) : opt.col;
        std::string sortBy = opt.col.empty() ? "~" + col : opt.sort;  // 자동 인식 카테고리는 뒤쪽으로
        if (lib.Add(col, sortBy, g)) added++;
    }
    if (added) {
        char buf[512];
        snprintf(buf, sizeof(buf), "[auto%s] %s: %d\n", opt.col.empty() ? "" : "+list", dir.c_str(), added);
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

}  // namespace

std::vector<Collection> LoadLibrary(const std::vector<std::string> &rootDirs, std::string &log) {
    Library lib;
    gDirCache.clear();
    gDirCacheOn = true;
    FindEsde(rootDirs);
    if (!gEsdeDir.empty()) lib.log += "[esde] " + gEsdeDir + " (media " + gEsdeMedia + ")\n";
    for (const auto &r : rootDirs) {
        if (!IsDir(r)) continue;
        // 루트 자체: 목록 파일이 있으면 그것, 없으면 자동 인식 (roots.txt 에 롬 폴더 자체를 적은 경우: /…/GBA)
        bool rootMeta = Exists(r + "/metadata.pegasus.txt");
        if (rootMeta) ParsePegasus(r, lib);
        else if (!IsMediaFolder(r)) ScanAuto(r, lib);
        // 루트에 목록이 있으면 (페가수스 game_dirs.txt 의 게임 폴더 등) 그 하위 폴더는 게임 데이터 — 자동 등록하지 않는다
        for (const auto &s : ListDir(r, true)) Scan(r + "/" + s, 1, lib, rootMeta);
    }
    gDirCacheOn = false;
    gDirCache.clear();
    log = lib.log;
    std::vector<Collection> out;
    for (auto &kv : lib.cols) {
        Collection c = kv.second;
        if (c.games.empty()) continue;
        auto sn = lib.shortNames.find(c.name);
        if (sn != lib.shortNames.end()) c.shortName = sn->second;
        std::sort(c.games.begin(), c.games.end(), [](const Game &a, const Game &b) { return a.sortKey < b.sortKey; });
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Collection &a, const Collection &b) { return a.sortBy < b.sortBy; });
    return out;
}

// ---------------------------------------------------------------- 게임 목록 저장 (/JoyLauncher/library.cache)
// 시작할 때마다 전체 롬 폴더를 다시 읽지 않도록 읽은 결과를 저장해 두고 그대로 쓴다.
// 다음이 바뀌면 자동으로 다시 읽는다: 런처 버전, 롬 폴더 구성(2단계), metadata.pegasus.txt·gamelist.xml 의
// 수정 시각·크기, 사용자 기종 표(custom_systems), emulators.txt 에 적은 이름. (롬·그림만 넣고 뺀 경우는 메뉴에서 새로 고침)
namespace {
const char *kCacheMagic = "JOYLIB A5";  // A3: 목록 루트 하위 폴더·압축 파일 규칙 (A20), A4: webp 그림·설명의 \\n (A24), A5: 루트 자동 인식·롬 파일 서명 (A28)

// 목록을 다시 읽어야 하는지 가리는 서명. LoadLibrary 의 Scan 과 같은 규칙으로 훑는다:
// 목록 파일이 있는 폴더는 그 파일의 시각·크기, 자동 인식하는 폴더(목록 파일도 없고 위 폴더에도 없음)는 롬 파일 이름.
// 메타데이터 폴더 아래(게임 데이터 폴더, JoiPlay 게임 등)는 파일을 훑지 않는다 (느려짐 — RP6 에서 4초 → 21초 였음)
void SigDir(const std::string &dir, int depth, std::string &sig, bool parentHasMeta) {
    if (IsMediaFolder(dir)) return;
    sig += "D" + dir + "\n";
    bool meta = false;
    for (const char *f : {"/metadata.pegasus.txt", "/gamelist.xml"}) {
        struct stat st;  // 직접 stat (폴더 기억으로 확인하면 맨 아래 폴더까지 통째로 읽게 되어 느림: JoiPlay 게임 폴더 등)
        if (stat((dir + f).c_str(), &st) == 0) {
            meta = true;
            sig += std::string(f) + " " + std::to_string((long long)st.st_mtime) + " " + std::to_string((long long)st.st_size) + "\n";
        }
    }
    if (!meta && !parentHasMeta) {  // 자동 인식 폴더: 롬만 넣고 빼도 다음 시작 때 다시 읽게
        const EmuSystem *sys = SystemForDir(dir);
        bool user = !sys && EmuUserHas(Base(dir));
        for (const auto &f : ListDir(dir, false))
            if (IsRomFile(f, sys, user)) sig += "F" + f + "\n";
    } else if (meta && !parentHasMeta) {  // 목록 폴더도 목록 밖 롬을 넣으므로 항목 수로 확인
        if (DIR *d = opendir(dir.c_str())) {
            long n = 0;
            while (readdir(d)) n++;
            closedir(d);
            sig += "n" + std::to_string(n) + "\n";
        }
    }
    if (depth <= 0) return;
    for (const auto &s : ListDir(dir, true)) SigDir(dir + "/" + s, depth - 1, sig, meta || parentHasMeta);
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
void SetEsdeCandidates(const std::vector<std::string> &dirs) { gEsdeMore = dirs; }

std::string EsdeLocate(const std::vector<std::string> &rootDirs) {
    FindEsde(rootDirs);
    return gEsdeDir;
}

std::string LibrarySignature(const std::vector<std::string> &rootDirs) {
    std::string sig = std::string(kCacheMagic) + " " + __DATE__ + " " + __TIME__ + "\n";
    // 폴더마다 한 번만 읽고(d_type) 기억해 둔다: 항목마다 stat 을 부르지 않음
    gDirCache.clear();
    gDirCacheOn = true;
    sig += EmuSignature();
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
        SigDir(r, 2, sig, false);
    }
    gDirCacheOn = false;
    gDirCache.clear();
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
