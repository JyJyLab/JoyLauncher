#include "hbmenu.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

static const char *kHb = "/hbmenu.nro";
static const char *kNxBackup = "/switch/hbmenu.nro";          // sphaira 와 같은 백업 위치
static const char *kSphaira = "/switch/sphaira/sphaira.nro";
static const char *kOtherBackup = "/switch/hbmenu_backup.nro";
static const char *kSphairaCfg = "/config/sphaira/config.ini";
static const char *kPrev = "/JoyLauncher/hbmenu_prev.txt";
static const char *kPrevOld = "/config/templauncher/hbmenu_prev.txt";  // 이전 이름(Temp Launcher) 시절 기록

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

std::string NroTitle(const std::string &sdPath) {
    FILE *f = fopen(SdPath(sdPath).c_str(), "rb");
    if (!f) return "";
    std::string title;
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
            char name[0x201] = {0};
            if (size >= 0x200 && fseek(f, nroSize + off, SEEK_SET) == 0 && fread(name, 1, 0x200, f) == 0x200)
                title = name;
        }
    }
    fclose(f);
    return title;
}

static HbKind KindOf(const std::string &title) {
    if (title.empty()) return HbKind::None;
    if (title == "Joy Launcher" || title == "Temp Launcher") return HbKind::Self;
    if (title == "nx-hbmenu") return HbKind::NxHbmenu;
    if (title == "sphaira") return HbKind::Sphaira;
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
    case HbKind::Other: return "기타";
    default: return "없음";
    }
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

// sphaira config.ini 의 [config] replace_hbmenu 값 읽기/쓰기 ("" = 키 없음)
static std::string ReadSphairaReplace() {
    std::ifstream in(SdPath(kSphairaCfg));
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("replace_hbmenu=", 0) == 0) return line.substr(15);
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

bool InstallAsHbmenu(const std::string &selfNro, std::string &msg) {
    HbKind kind = CurrentHbmenu();
    if (kind == HbKind::Self) {
        // 이전 버전(Temp Launcher)이 설치돼 있으면 새 버전으로 교체만
        if (NroTitle(kHb) != "Joy Launcher" && !CopyToHbmenu(selfNro)) { msg = "Joy Launcher 복사 실패"; return false; }
        msg = "Joy Launcher 가 기본 메뉴입니다";
        return true;
    }
    std::string prev;
    if (kind == HbKind::NxHbmenu) {
        if (!SdExists(kNxBackup) && !CopyFile(kHb, kNxBackup)) { msg = "nx-hbmenu 백업 실패"; return false; }
        prev = "nx-hbmenu";
    } else if (kind == HbKind::Sphaira) {
        MkdirP("/switch/sphaira");
        if (!SdExists(kSphaira) && !CopyFile(kHb, kSphaira)) { msg = "sphaira 백업 실패"; return false; }
        std::string old = ReadSphairaReplace();
        // sphaira 가 다시 /hbmenu.nro 를 덮어쓰지 않도록
        if (!WriteSphairaReplace("0")) { msg = "sphaira 설정 변경 실패"; return false; }
        prev = "sphaira:" + (old.empty() ? std::string("1") : old);
    } else if (kind == HbKind::Other) {
        if (!CopyFile(kHb, kOtherBackup)) { msg = "기존 메뉴 백업 실패"; return false; }
        prev = "other";
    } else {
        prev = "none";
    }
    if (!CopyToHbmenu(selfNro)) { msg = "Joy Launcher 복사 실패"; return false; }
    MkdirP("/JoyLauncher");
    std::ofstream(SdPath(kPrev)) << prev << "\n";
    msg = std::string("기본 메뉴로 설정 완료 (이전: ") + HbKindName(kind) + ")";
    return true;
}

bool RestoreHbmenu(std::string &msg) {
    std::string prev;
    {
        std::ifstream in(SdPath(kPrev));
        std::getline(in, prev);
    }
    if (prev.empty()) {
        std::ifstream in(SdPath(kPrevOld));
        std::getline(in, prev);
    }
    // 기록이 없으면 있는 메뉴로 추정 (sphaira 우선)
    if (prev.empty()) prev = SdExists(kSphaira) ? "sphaira:1" : (SdExists(kNxBackup) ? "nx-hbmenu" : "");
    bool ok = false;
    if (prev.rfind("sphaira", 0) == 0) {
        ok = SdExists(kSphaira) && CopyToHbmenu(kSphaira);
        if (ok) {
            size_t c = prev.find(':');
            WriteSphairaReplace(c == std::string::npos ? "1" : prev.substr(c + 1));
        }
        msg = ok ? "sphaira 로 되돌렸습니다" : (SdExists(kSphaira) ? "/hbmenu.nro 를 바꾸지 못했습니다" : "sphaira 가 없어 되돌릴 수 없습니다");
    } else if (prev == "nx-hbmenu") {
        ok = SdExists(kNxBackup) && CopyToHbmenu(kNxBackup);
        msg = ok ? "nx-hbmenu 로 되돌렸습니다" : (SdExists(kNxBackup) ? "/hbmenu.nro 를 바꾸지 못했습니다" : "/switch/hbmenu.nro 가 없어 되돌릴 수 없습니다");
    } else if (prev == "other") {
        ok = SdExists(kOtherBackup) && CopyToHbmenu(kOtherBackup);
        msg = ok ? "이전 메뉴로 되돌렸습니다" : "백업 파일이 없어 되돌릴 수 없습니다";
    } else {
        msg = "되돌릴 메뉴 정보가 없습니다";
    }
    if (ok) {
        remove(SdPath(kPrev).c_str());
        remove(SdPath(kPrevOld).c_str());
    }
    return ok;
}
