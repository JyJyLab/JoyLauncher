#include "hbmenu.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

static const char *kHb = "/hbmenu.nro";
static const char *kNxBackup = "/switch/hbmenu.nro";          // sphaira 와 같은 백업 위치
static const char *kSphaira = "/switch/sphaira/sphaira.nro";
static const char *kOtherBackup = "/switch/hbmenu_backup.nro";  // 0.8.1 까지의 기타 메뉴 백업
static const char *kPrevDir = "/switch/hbmenu_prev";              // 기타 메뉴 백업 (<이름>.nro)
static const char *kSphairaCfg = "/config/sphaira/config.ini";
static const char *kPrev = "/JoyLauncher/hbmenu_prev.txt";
static const char *kPrevOld = "/config/templauncher/hbmenu_prev.txt";  // 이전 이름(Temp Launcher) 시절 기록
static const char *kNotice = "/JoyLauncher/hbmenu_notice.txt";          // 마지막으로 알린 /hbmenu.nro (같은 알림 반복 방지)

std::string SdPath(const std::string &p) {
#ifdef __SWITCH__
    return p;
#else
    const char *root = getenv("JOY_SDROOT");
    if (!root) root = getenv("TEMP_SDROOT");
    return std::string(root ? root : "sdroot") + p;
#endif
}

bool SdExists(const std::string &p) {
    struct stat st;
    return stat(SdPath(p).c_str(), &st) == 0;
}

// NRO 의 NACP 에서 off 위치의 글자 (len 바이트)
static std::string NacpString(const std::string &sdPath, size_t at, size_t len) {
    FILE *f = fopen(SdPath(sdPath).c_str(), "rb");
    if (!f) return "";
    std::string out;
    unsigned char hdr[0x20];
    if (fseek(f, 0x10, SEEK_SET) == 0 && fread(hdr, 1, 0x20, f) == 0x20) {
        uint32_t nroSize;
        memcpy(&nroSize, hdr + 0x8, 4);  // NRO 헤더 오프셋 0x18
        unsigned char aset[0x38];
        if (fseek(f, nroSize, SEEK_SET) == 0 && fread(aset, 1, sizeof(aset), f) == sizeof(aset) &&
            memcmp(aset, "ASET", 4) == 0) {
            uint64_t off, size;
            memcpy(&off, aset + 0x18, 8);
            memcpy(&size, aset + 0x20, 8);
            std::vector<char> buf(len + 1, 0);
            if (size >= at + len && fseek(f, nroSize + off + at, SEEK_SET) == 0 && fread(buf.data(), 1, len, f) == len)
                out = buf.data();
        }
    }
    fclose(f);
    return out;
}

// 메뉴를 만들 때마다 여러 nro 를 다시 열지 않도록 (크기·수정 시각이 같으면) 읽은 이름·버전을 기억
struct NacpInfo {
    long long size = -1;
    long long mtime = 0;
    std::string title, version;
};
static const NacpInfo &Nacp(const std::string &sdPath) {
    static std::map<std::string, NacpInfo> cache;
    struct stat st;
    long long size = -1, mtime = 0;
    if (stat(SdPath(sdPath).c_str(), &st) == 0) { size = (long long)st.st_size; mtime = (long long)st.st_mtime; }
    NacpInfo &n = cache[sdPath];
    if (n.size != size || n.mtime != mtime || size < 0) {
        n.size = size;
        n.mtime = mtime;
        n.title = size < 0 ? "" : NacpString(sdPath, 0, 0x200);
        n.version = size < 0 ? "" : NacpString(sdPath, 0x3060, 0x10);
    }
    return n;
}

std::string NroTitle(const std::string &sdPath) { return Nacp(sdPath).title; }
std::string NroVersion(const std::string &sdPath) { return Nacp(sdPath).version; }

static std::string LowerStr(std::string s) {
    for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

// NACP 이름으로 종류 판별 (대소문자 무시, 포함 검사)
static HbKind KindOf(const std::string &title) {
    if (title.empty()) return HbKind::None;
    std::string t = LowerStr(title);
    if (t.find("joy launcher") != std::string::npos || t.find("temp launcher") != std::string::npos) return HbKind::Self;
    if (t.find("sphaira") != std::string::npos) return HbKind::Sphaira;
    if (t.find("hbmenu") != std::string::npos || t.find("homebrew menu") != std::string::npos) return HbKind::NxHbmenu;
    return HbKind::Other;
}

HbKind CurrentHbmenu() {
    if (!SdExists(kHb)) return HbKind::None;
    return KindOf(NroTitle(kHb));
}

const char *HbKindName(HbKind k) {
    switch (k) {
    case HbKind::Self: return "Joy Launcher";
    case HbKind::NxHbmenu: return "nx-hbmenu";
    case HbKind::Sphaira: return "sphaira";
    case HbKind::Other: return T("기타");
    default: return T("없음");
    }
}

// 화면 표시용 이름: 기타는 실제 이름도 함께
static std::string KindLabel(HbKind k, const std::string &sdPath) {
    if (k == HbKind::Other) return Fmt(T("기타 (%s)"), NroTitle(sdPath).c_str());
    return HbKindName(k);
}

static bool CopyFile(const std::string &src, const std::string &dst) {
    FILE *in = fopen(SdPath(src).c_str(), "rb");
    if (!in) return false;
    FILE *out = fopen(SdPath(dst).c_str(), "wb");
    if (!out) { fclose(in); return false; }
    std::vector<char> buf(1 << 20);
    bool ok = true;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0)
        if (fwrite(buf.data(), 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
#ifdef __SWITCH__
    if (FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc")) fsFsCommit(fs);
#endif
    return ok;
}

// /hbmenu.nro 에 복사. 런처가 /hbmenu.nro 로 실행 중이면 romfs 가 그 파일을 열어 두고 있어 덮어쓸 수 없으므로
// 잠시 romfs 를 닫고 복사한다 (글꼴 등은 이미 메모리에 있음)
static bool CopyToHbmenu(const std::string &src) {
#ifdef __SWITCH__
    romfsExit();
#endif
    bool ok = CopyFile(src, kHb);
#ifdef __SWITCH__
    romfsInit();
#endif
    return ok;
}

static void MkdirP(const std::string &sdDir) {
    std::string cur;
    std::stringstream ss(sdDir);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += "/" + part;
        mkdir(SdPath(cur).c_str(), 0777);
    }
}

static long long FileSize(const std::string &sdPath) {
    struct stat st;
    return stat(SdPath(sdPath).c_str(), &st) == 0 ? (long long)st.st_size : -1;
}

// "3.6.1" 과 "3.5.2" 비교 (숫자 단위)
static int CompareVer(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
        while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y ? -1 : 1;
        while (i < a.size() && !isdigit((unsigned char)a[i])) i++;
        while (j < b.size() && !isdigit((unsigned char)b[j])) j++;
    }
    return 0;
}

// 백업 파일 이름: 영문·숫자·-_ 만 (스위치 SD 는 ASCII 이름이 안전)
static std::string SafeName(const std::string &title) {
    std::string o;
    for (unsigned char ch : title) o += (isalnum(ch) || ch == '-' || ch == '_') ? (char)ch : '_';
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o.empty() ? "menu" : o;
}

// sphaira config.ini 의 [config] replace_hbmenu 값 읽기/쓰기 ("" = 키 없음)
static std::string ReadSphairaReplace() {
    std::ifstream in(SdPath(kSphairaCfg));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("replace_hbmenu=", 0) == 0) return line.substr(15);
    }
    return "";
}

static bool WriteSphairaReplace(const std::string &value) {
    std::vector<std::string> lines;
    {
        std::ifstream in(SdPath(kSphairaCfg));
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    bool done = false, inConfig = false;
    for (size_t i = 0; i < lines.size() && !done; i++) {
        if (!lines[i].empty() && lines[i][0] == '[') inConfig = lines[i] == "[config]";
        if (lines[i].rfind("replace_hbmenu=", 0) == 0) { lines[i] = "replace_hbmenu=" + value; done = true; }
        else if (inConfig && (i + 1 == lines.size() || (!lines[i + 1].empty() && lines[i + 1][0] == '['))) {
            lines.insert(lines.begin() + i + 1, "replace_hbmenu=" + value);
            done = true;
        }
    }
    if (!done) { lines.push_back("[config]"); lines.push_back("replace_hbmenu=" + value); }
    MkdirP("/config/sphaira");
    std::ofstream out(SdPath(kSphairaCfg));
    for (auto &l : lines) out << l << "\n";
    return (bool)out;
}

static bool SphairaOn(const std::string &v) { return v == "1" || LowerStr(v) == "true"; }

// 기본 메뉴 교체 기록 (/JoyLauncher/hbmenu_prev.txt)
//  첫 줄: nx-hbmenu[:백업 경로] | sphaira | sphaira:<값>(0.8.1 까지) | other:<백업 경로>:<이름> | other(0.8.1 까지) | none
//  다음 줄(선택): sphaira_replace=<설정 전 replace_hbmenu 값, default = 키 없었음>
struct PrevRecord {
    std::string kind;     // nx-hbmenu, sphaira, other, none ("" = 기록 없음)
    std::string path;     // 이전 메뉴 백업 경로
    std::string name;     // 기타 메뉴 이름
    std::string sphaira;  // sphaira_replace 값
};

static PrevRecord ReadPrev() {
    PrevRecord r;
    std::vector<std::string> lines;
    for (const char *f : {kPrev, kPrevOld}) {
        std::ifstream in(SdPath(f));
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
        if (!lines.empty()) break;
    }
    if (lines.empty()) return r;
    const std::string &l = lines[0];
    if (l.rfind("nx-hbmenu", 0) == 0) {
        r.kind = "nx-hbmenu";
        r.path = l.size() > 10 ? l.substr(10) : kNxBackup;
    } else if (l.rfind("sphaira", 0) == 0) {
        r.kind = "sphaira";
        r.path = kSphaira;
        if (l.size() > 8) r.sphaira = l.substr(8);
    } else if (l.rfind("other", 0) == 0) {
        r.kind = "other";
        r.path = kOtherBackup;
        if (l.size() > 6) {
            std::string rest = l.substr(6);
            size_t c = rest.find(':');
            r.path = rest.substr(0, c);
            if (c != std::string::npos) r.name = rest.substr(c + 1);
        }
    } else {
        r.kind = "none";
    }
    for (size_t i = 1; i < lines.size(); i++)
        if (lines[i].rfind("sphaira_replace=", 0) == 0) r.sphaira = lines[i].substr(16);
    return r;
}

static void WritePrev(const PrevRecord &r) {
    MkdirP("/JoyLauncher");
    std::ofstream o(SdPath(kPrev));
    if (r.kind == "nx-hbmenu") o << (r.path == kNxBackup ? std::string("nx-hbmenu") : "nx-hbmenu:" + r.path) << "\n";
    else if (r.kind == "other") o << "other:" << r.path << ":" << r.name << "\n";
    else o << r.kind << "\n";
    if (!r.sphaira.empty()) o << "sphaira_replace=" << r.sphaira << "\n";
    remove(SdPath(kPrevOld).c_str());
}

bool HbInstalledRecord() { return !ReadPrev().kind.empty(); }

// /hbmenu.nro 의 nx-hbmenu 를 백업. /switch/hbmenu.nro 에 다른 프로그램이 있으면 덮어쓰지 않고 hbmenu_prev 로.
// 이미 같은(또는 더 새) 버전이 있으면 그대로. 백업 경로를 돌려준다 ("" = 실패)
static std::string BackupNx() {
    std::string dst = kNxBackup;
    if (SdExists(kNxBackup) && KindOf(NroTitle(kNxBackup)) != HbKind::NxHbmenu) {
        MkdirP(kPrevDir);
        dst = std::string(kPrevDir) + "/nx-hbmenu.nro";
    }
    if (SdExists(dst) && KindOf(NroTitle(dst)) == HbKind::NxHbmenu &&
        CompareVer(NroVersion(dst), NroVersion(kHb)) >= 0)
        return dst;
    return CopyFile(kHb, dst) ? dst : "";
}

// sphaira 가 다시 /hbmenu.nro 를 덮어쓰지 않도록 끄고, 원래 값을 기록에 남긴다 (처음 한 번만)
static bool DisableSphairaReplace(PrevRecord &r) {
    if (!SdExists(kSphaira) && !SdExists(kSphairaCfg)) return false;
    std::string v = ReadSphairaReplace();
    if (!SphairaOn(v)) {
        if (r.sphaira.empty()) r.sphaira = v.empty() ? "default" : v;
        return false;
    }
    WriteSphairaReplace("0");
    if (r.sphaira.empty() || r.sphaira == "default" || r.sphaira == "0") r.sphaira = v;
    return true;
}

bool InstallAsHbmenu(const std::string &selfNro, std::string &msg) {
    HbKind kind = CurrentHbmenu();
    PrevRecord r = ReadPrev();
    if (kind == HbKind::Self) {
        // 이전 버전(Temp Launcher)이 설치돼 있으면 새 버전으로 교체만
        if (NroTitle(kHb) != "Joy Launcher" && !CopyToHbmenu(selfNro)) { msg = T("Joy Launcher 복사 실패"); return false; }
        if (r.kind.empty()) r.kind = "none";
        DisableSphairaReplace(r);
        WritePrev(r);
        msg = T("Joy Launcher 가 기본 메뉴입니다");
        return true;
    }
    std::string label = KindLabel(kind, kHb);
    PrevRecord n;
    n.sphaira = r.sphaira;  // 처음 설정할 때의 sphaira 값은 계속 보존
    if (kind == HbKind::NxHbmenu) {
        n.kind = "nx-hbmenu";
        n.path = BackupNx();
        if (n.path.empty()) { msg = T("nx-hbmenu 백업 실패"); return false; }
    } else if (kind == HbKind::Sphaira) {
        MkdirP("/switch/sphaira");
        if (!SdExists(kSphaira) && !CopyFile(kHb, kSphaira)) { msg = T("sphaira 백업 실패"); return false; }
        n.kind = "sphaira";
        n.path = kSphaira;
    } else if (kind == HbKind::Other) {
        n.kind = "other";
        n.name = NroTitle(kHb);
        MkdirP(kPrevDir);
        n.path = std::string(kPrevDir) + "/" + SafeName(n.name) + ".nro";
        if (!CopyFile(kHb, n.path)) { msg = T("기존 메뉴 백업 실패"); return false; }
    } else {
        n.kind = "none";
    }
    DisableSphairaReplace(n);
    if (!CopyToHbmenu(selfNro)) { msg = T("Joy Launcher 복사 실패"); return false; }
    WritePrev(n);
    remove(SdPath(kNotice).c_str());
    msg = Fmt(T("기본 메뉴로 설정 완료 (이전: %s)"), label.c_str());
    return true;
}

// 기록이 없을 때: 남아 있는 백업에서 되돌릴 메뉴 찾기 (sphaira > nx-hbmenu > 기타 백업)
static PrevRecord GuessPrev() {
    PrevRecord r;
    if (SdExists(kSphaira)) { r.kind = "sphaira"; r.path = kSphaira; return r; }
    if (SdExists(kNxBackup) && KindOf(NroTitle(kNxBackup)) == HbKind::NxHbmenu) { r.kind = "nx-hbmenu"; r.path = kNxBackup; return r; }
    std::string nx = std::string(kPrevDir) + "/nx-hbmenu.nro";
    if (SdExists(nx)) { r.kind = "nx-hbmenu"; r.path = nx; return r; }
    if (DIR *d = opendir(SdPath(kPrevDir).c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && LowerStr(n.substr(n.size() - 4)) == ".nro") {
                r.kind = "other";
                r.path = std::string(kPrevDir) + "/" + n;
                r.name = NroTitle(r.path);
                break;
            }
        }
        closedir(d);
        if (!r.kind.empty()) return r;
    }
    if (SdExists(kOtherBackup)) { r.kind = "other"; r.path = kOtherBackup; r.name = NroTitle(kOtherBackup); }
    return r;
}

bool RestoreHbmenu(std::string &msg) {
    PrevRecord r = ReadPrev();
    bool guessed = r.kind.empty() || r.kind == "none";
    if (guessed) {
        std::string keep = r.sphaira;
        r = GuessPrev();
        r.sphaira = keep;
    }
    if (r.kind.empty()) { msg = T("되돌릴 메뉴 정보가 없습니다"); return false; }
    std::string name = r.kind == "other" ? Fmt(T("기타 (%s)"), (r.name.empty() ? NroTitle(r.path) : r.name).c_str()) : r.kind;
    if (!SdExists(r.path)) { msg = Fmt(T("%s 백업(%s)이 없어 되돌릴 수 없습니다"), name.c_str(), r.path.c_str()); return false; }
    if (!CopyToHbmenu(r.path)) { msg = T("/hbmenu.nro 를 바꾸지 못했습니다"); return false; }
    // sphaira 로 되돌릴 때만 자동 덮어쓰기 설정을 처음 값으로 (다른 메뉴로 되돌릴 때 켜면 sphaira 가 그 메뉴를 덮어씀)
    if (r.kind == "sphaira" && !r.sphaira.empty() && r.sphaira != "default")
        WriteSphairaReplace(r.sphaira);
    remove(SdPath(kPrev).c_str());
    remove(SdPath(kPrevOld).c_str());
    remove(SdPath(kNotice).c_str());
    msg = Fmt(T("%s 로 되돌렸습니다"), name.c_str());
    return true;
}

std::string HbCheckOnStart() {
    PrevRecord r = ReadPrev();
    if (r.kind.empty()) return "";
    HbKind kind = CurrentHbmenu();
    if (kind == HbKind::Self) {
        if (DisableSphairaReplace(r)) {
            WritePrev(r);
            return T("sphaira 의 «hbmenu 교체» 설정이 다시 켜져 있어 껐습니다 (Joy Launcher 를 기본 메뉴로 유지)");
        }
        return "";
    }
    // 같은 파일로는 한 번만 알림
    std::string sig = NroTitle(kHb) + " " + NroVersion(kHb) + " " + std::to_string(FileSize(kHb));
    {
        std::ifstream in(SdPath(kNotice));
        std::string last;
        std::getline(in, last);
        if (last == sig) return "";
    }
    MkdirP("/JoyLauncher");
    std::ofstream(SdPath(kNotice)) << sig << "\n";
    if (kind == HbKind::NxHbmenu) {
        std::string bak = BackupNx();
        return Fmt(T("기본 홈브루 메뉴가 nx-hbmenu %s 로 바뀌었습니다 (Atmosphere 업데이트 등). 백업: %s · 옵션(Y)에서 다시 설정할 수 있습니다"),
                   NroVersion(kHb).c_str(), bak.empty() ? T("실패") : bak.c_str());
    }
    return Fmt(T("기본 홈브루 메뉴가 %s 로 바뀌었습니다 · 옵션(Y)에서 다시 설정할 수 있습니다"), KindLabel(kind, kHb).c_str());
}

std::vector<HbMenuEntry> HomebrewMenus() {
    std::vector<HbMenuEntry> out;
    std::vector<std::string> seen;  // 같은 프로그램(이름·버전·크기)이 여러 곳에 있으면 한 번만
    auto add = [&](const std::string &path) {
        if (path.empty() || !SdExists(path)) return;
        std::string title = NroTitle(path);
        if (title.empty() || KindOf(title) == HbKind::Self) return;
        std::string ver = NroVersion(path);
        std::string key = LowerStr(title) + "|" + ver + "|" + std::to_string(FileSize(path));
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
        seen.push_back(key);
        out.push_back({Fmt(T("홈브루 메뉴 (%s)"), (title + (ver.empty() ? "" : " " + ver)).c_str()), path});
    };
    // X 버튼은 첫 항목을 연다: 이전 기본 메뉴 → nx-hbmenu(백업·현재 /hbmenu.nro) → sphaira → 그 밖의 백업
    add(ReadPrev().path);
    if (SdExists(kNxBackup) && KindOf(NroTitle(kNxBackup)) == HbKind::NxHbmenu) add(kNxBackup);
    add(std::string(kPrevDir) + "/nx-hbmenu.nro");
    if (CurrentHbmenu() == HbKind::NxHbmenu) add(kHb);
    add(kSphaira);
    if (SdExists(kNxBackup) && KindOf(NroTitle(kNxBackup)) != HbKind::Self) add(kNxBackup);
    if (DIR *d = opendir(SdPath(kPrevDir).c_str())) {
        std::vector<std::string> names;
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && LowerStr(n.substr(n.size() - 4)) == ".nro") names.push_back(n);
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto &n : names) add(std::string(kPrevDir) + "/" + n);
    }
    add(kOtherBackup);
    // Joy 가 기본 메뉴가 아니면 지금의 /hbmenu.nro 도
    if (CurrentHbmenu() != HbKind::Self) add(kHb);
    return out;
}
