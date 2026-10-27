#include "installed.h"

#include "hbmenu.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>


namespace {

uint64_t Fnv(const std::string &s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : s) h = (h ^ ch) * 1099511628211ull;
    return h;
}

std::string Lower(std::string s) {
    for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

// 아이콘 jpg 를 캐시 파일로 (같은 이름이 이미 있으면 그대로). 실패하면 빈 값
std::string WriteIcon(const std::string &cacheDir, const std::string &name, const std::vector<unsigned char> &jpg) {
    if (jpg.size() < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) return "";
    std::string sd = cacheDir + "/" + name;
    struct stat st;
    if (stat(SdPath(sd).c_str(), &st) == 0 && (size_t)st.st_size == jpg.size()) return sd;
    mkdir(SdPath(cacheDir).c_str(), 0777);
    FILE *f = fopen(SdPath(sd).c_str(), "wb");
    if (!f) return "";
    bool ok = fwrite(jpg.data(), 1, jpg.size(), f) == jpg.size();
    if (fclose(f) != 0) ok = false;
    return ok ? sd : "";
}

// NRO 의 NACP 이름 하나 (0x300 바이트 칸 16개: 이름 0x200 + 제작자 0x100). 한국어 화면이면 한국어 칸(12)부터
void NacpNames(const std::vector<unsigned char> &nacp, std::string &title, std::string &author) {
    const int order[] = {CurrentLang() == Lang::Ko ? 12 : 0, 0, 1};
    for (int idx : order) {
        size_t at = (size_t)idx * 0x300;
        if (nacp.size() < at + 0x300 || nacp[at] == 0) continue;
        title.assign((const char *)&nacp[at], strnlen((const char *)&nacp[at], 0x200));
        author.assign((const char *)&nacp[at + 0x200], strnlen((const char *)&nacp[at + 0x200], 0x100));
        return;
    }
}

// NRO 끝의 ASET(아이콘·NACP) 읽기
bool ReadNroAssets(const std::string &sdPath, std::vector<unsigned char> &icon, std::vector<unsigned char> &nacp) {
    FILE *f = fopen(SdPath(sdPath).c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    unsigned char hdr[0x20];
    if (fseek(f, 0x10, SEEK_SET) == 0 && fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && memcmp(hdr, "NRO0", 4) == 0) {
        uint32_t nroSize;
        memcpy(&nroSize, hdr + 0x8, 4);
        unsigned char aset[0x38];
        if (fseek(f, nroSize, SEEK_SET) == 0 && fread(aset, 1, sizeof(aset), f) == sizeof(aset) &&
            memcmp(aset, "ASET", 4) == 0) {
            uint64_t ioff, isize, noff, nsize;
            memcpy(&ioff, aset + 0x08, 8);
            memcpy(&isize, aset + 0x10, 8);
            memcpy(&noff, aset + 0x18, 8);
            memcpy(&nsize, aset + 0x20, 8);
            if (nsize >= 0x3000 && nsize <= 0x10000) {
                nacp.resize(nsize);
                ok = fseek(f, nroSize + noff, SEEK_SET) == 0 && fread(nacp.data(), 1, nsize, f) == nsize;
            }
            if (ok && isize > 0 && isize <= 0x40000) {
                icon.resize(isize);
                if (fseek(f, nroSize + ioff, SEEK_SET) != 0 || fread(icon.data(), 1, isize, f) != isize) icon.clear();
            }
        }
    }
    fclose(f);
    return ok;
}

void ScanDir(const std::string &dir, int depth, std::vector<std::string> &out) {
    DIR *d = opendir(SdPath(dir).c_str());
    if (!d) return;
    while (struct dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;  // ., .., .overlays, .packages, .emu (DrasticDS 내부 실행기)
        std::string path = dir + "/" + name;
        struct stat st;
        if (stat(SdPath(path).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0 && name != "hbmenu_prev") ScanDir(path, depth - 1, out);  // hbmenu_prev: 이전 기본 메뉴 백업
        } else if (Lower(name).size() > 4 && Lower(name).compare(Lower(name).size() - 4, 4, ".nro") == 0) {
            out.push_back(path);
        }
    }
    closedir(d);
}

bool TitleLess(const Game &a, const Game &b) { return a.sortKey < b.sortKey; }

}  // namespace

// nro 하나를 목록 항목으로: 이름·제작자·버전은 NACP, 아이콘은 cacheDir 에 jpg 로. nro 가 아니면 false
static bool NroGame(const std::string &path, const std::string &cacheDir, Game &g) {
    std::vector<unsigned char> icon, nacp;
    if (!ReadNroAssets(path, icon, nacp)) return false;
    g = Game();
    NacpNames(nacp, g.title, g.developer);
    if (g.title.empty()) {
        g.title = path.substr(path.rfind('/') + 1);
        g.title.resize(g.title.size() - 4);
    }
    if (nacp.size() >= 0x3070)
        g.release.assign((const char *)&nacp[0x3060], strnlen((const char *)&nacp[0x3060], 0x10));
    g.file = path;
    g.launch = kLaunchNro;
    g.description = path;
    struct stat st;
    long long stamp = stat(SdPath(path).c_str(), &st) == 0 ? (long long)st.st_size * 31 + (long long)st.st_mtime : 0;
    char name[64];
    snprintf(name, sizeof(name), "nro_%016llx.jpg", (unsigned long long)Fnv(path + "|" + std::to_string(stamp)));
    g.boxart = WriteIcon(cacheDir, name, icon);
    g.sortKey = Lower(g.title);
    return true;
}

Collection ScanHomebrew(const std::string &cacheDir, const std::string &selfNro) {
    Collection c;
    c.name = "홈브루";
    c.sortBy = "zzz1";
    c.shortName = "homebrew";
    c.virt = true;
    std::vector<std::string> nros;
    ScanDir("/switch", 2, nros);
    for (const auto &path : nros) {
        if (Lower(path) == Lower(selfNro)) continue;
        Game g;
        if (NroGame(path, cacheDir, g)) c.games.push_back(g);
    }
    std::sort(c.games.begin(), c.games.end(), TitleLess);
    return c;
}

Collection BuildHomebrewFavorites(const std::vector<std::string> &paths, const std::string &cacheDir) {
    Collection c;
    c.name = "홈브루 즐겨찾기";
    c.sortBy = "001";
    c.shortName = "hbfav";
    c.virt = true;
    for (const auto &path : paths) {
        Game g;
        if (NroGame(path, cacheDir, g)) c.games.push_back(g);  // 지워진 nro 는 목록에서만 빠짐 (기록은 그대로)
    }
    std::sort(c.games.begin(), c.games.end(), TitleLess);
    return c;
}
