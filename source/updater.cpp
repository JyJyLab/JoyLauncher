#include "updater.h"

#include "i18n.h"
#include "platform.h"

#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.0.0-dev"
#endif
// 런처(apk) 업데이트를 받을 깃헙 저장소 "소유자/이름". 비어 있으면 업데이트 기능을 끈다 (옵션 창에도 보이지 않음).
// 스위치판과 같은 저장소(JyJyLab/JoyLauncher)에 릴리스만 따로 둔다: 스위치판 태그 v1.2.0, 안드로이드판 태그 android-v1.2.0
#ifndef JOY_UPDATE_REPO
#define JOY_UPDATE_REPO "JyJyLab/JoyLauncher"
#endif

namespace {
const char *kUpdateRepo = JOY_UPDATE_REPO;
// 기본 테마 팩은 스위치판 저장소의 릴리스(<팩>.zip)에서 받는다: 테마 문법이 같아 그대로 쓸 수 있음
const char *kThemeRepo = "JyJyLab/JoyLauncher";
const char *kAssetName = "JoyLauncher.apk";
const char *kAndroidTag = "android-v";  // 안드로이드판 릴리스의 태그 앞부분
const char *kSwitchTag = "v";           // 스위치판 릴리스 (기본 테마 팩 zip 이 여기에 있음)

// 최근 릴리스 목록 (새것부터). 한 저장소에 두 플랫폼의 릴리스가 섞여 있어 "latest" 하나로는 고를 수 없다
std::string ReleasesApi(const char *repo) { return std::string("https://api.github.com/repos/") + repo + "/releases?per_page=30"; }

bool HttpGet(const std::string &url, std::string &out, std::string &err) {
    std::string e;
    if (plat::HttpGet(url, out, e)) return true;
    if (e == "HTTP 404") err = T("아직 배포된 버전이 없습니다");
    else if (e.rfind("HTTP ", 0) == 0) err = T("서버 응답 ") + e.substr(5);
    else err = std::string(T("네트워크 오류: ")) + e;
    return false;
}

// ---------------------------------------------------------------- 아주 작은 JSON 읽기 (필요한 문자열/숫자만)
// pos 부터 "key": 다음 값의 시작 위치. 없으면 npos (end 를 넘으면 npos)
size_t FindKey(const std::string &j, const std::string &key, size_t pos, size_t end) {
    std::string k = "\"" + key + "\"";
    while (true) {
        pos = j.find(k, pos);
        if (pos == std::string::npos || pos >= end) return std::string::npos;
        size_t p = pos + k.size();
        while (p < j.size() && (j[p] == ' ' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t')) p++;
        if (p < j.size() && j[p] == ':') {
            p++;
            while (p < j.size() && (j[p] == ' ' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t')) p++;
            return p;
        }
        pos = p;
    }
}

std::string ReadJsonString(const std::string &j, size_t p) {
    std::string out;
    if (p >= j.size() || j[p] != '"') return out;
    for (p++; p < j.size() && j[p] != '"'; p++) {
        if (j[p] != '\\') { out += j[p]; continue; }
        if (++p >= j.size()) break;
        switch (j[p]) {
        case 'n': out += '\n'; break;
        case 'r': break;
        case 't': out += '\t'; break;
        case 'u': {  // \uXXXX → UTF-8 (BMP 만)
            if (p + 4 >= j.size()) break;
            unsigned cp = (unsigned)strtoul(j.substr(p + 1, 4).c_str(), nullptr, 16);
            p += 4;
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            break;
        }
        default: out += j[p];
        }
    }
    return out;
}

// [start, end) 범위의 JSON 객체/배열 끝 찾기 (문자열 안의 괄호 무시)
size_t MatchBracket(const std::string &j, size_t open) {
    char o = j[open], c = o == '[' ? ']' : '}';
    int depth = 0;
    bool str = false;
    for (size_t p = open; p < j.size(); p++) {
        char ch = j[p];
        if (str) {
            if (ch == '\\') p++;
            else if (ch == '"') str = false;
            continue;
        }
        if (ch == '"') str = true;
        else if (ch == o) depth++;
        else if (ch == c && --depth == 0) return p;
    }
    return std::string::npos;
}

// ---------------------------------------------------------------- SHA-256
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t len = 0;
    uint64_t total = 0;
    static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Block(const uint8_t *p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (p[i * 4] << 24) | (p[i * 4 + 1] << 16) | (p[i * 4 + 2] << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void Update(const uint8_t *p, size_t n) {
        total += n;
        while (n) {
            size_t take = std::min(n, 64 - len);
            memcpy(buf + len, p, take);
            len += take; p += take; n -= take;
            if (len == 64) { Block(buf); len = 0; }
        }
    }
    std::string Hex() {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t z = 0;
        while (len != 56) Update(&z, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
        Update(lb, 8);
        char out[65];
        for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
        return out;
    }
};

// 파일의 SHA-256 (소문자 16진수). 못 읽으면 ""
std::string FileSha256(const std::string &path, long long &size) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    Sha256 sha;
    std::vector<uint8_t> b(1 << 16);
    size_t n;
    size = 0;
    while ((n = fread(b.data(), 1, b.size(), f)) > 0) {
        sha.Update(b.data(), n);
        size += (long long)n;
    }
    fclose(f);
    return sha.Hex();
}

// ---------------------------------------------------------------- 릴리스 자산
struct Asset {
    std::string name, url, sha256;
    long long size = 0;
};

// 태그가 tagPrefix + 숫자로 시작하는 가장 새 릴리스(초안·시험판 제외)의 자산 목록 (version: 태그에서 tagPrefix 를 뺀 값)
bool FetchAssets(const char *repo, const char *tagPrefix, std::vector<Asset> &out, std::string &version, std::string &err) {
    std::string all;
    if (!HttpGet(ReleasesApi(repo), all, err)) return false;
    size_t lp = all.find('[');
    size_t lend = lp == std::string::npos ? std::string::npos : MatchBracket(all, lp);
    if (lend == std::string::npos) { err = T("아직 배포된 버전이 없습니다"); return false; }
    std::string j;  // 고른 릴리스 하나
    std::string pre = tagPrefix;
    for (size_t o = all.find('{', lp); o != std::string::npos && o < lend;) {
        size_t oend = MatchBracket(all, o);
        if (oend == std::string::npos) break;
        size_t tp = FindKey(all, "tag_name", o, oend);
        std::string tag = tp == std::string::npos ? "" : ReadJsonString(all, tp);
        size_t dp = FindKey(all, "draft", o, oend), pp = FindKey(all, "prerelease", o, oend);
        bool skip = (dp != std::string::npos && all.compare(dp, 4, "true") == 0) || (pp != std::string::npos && all.compare(pp, 4, "true") == 0);
        if (!skip && tag.size() > pre.size() && tag.compare(0, pre.size(), pre) == 0 && isdigit((unsigned char)tag[pre.size()])) {
            j = all.substr(o, oend - o + 1);
            version = tag.substr(pre.size());
            break;
        }
        o = all.find('{', oend + 1);
    }
    if (j.empty()) { err = T("아직 배포된 버전이 없습니다"); return false; }
    size_t ap = FindKey(j, "assets", 0, j.size());
    if (ap == std::string::npos || j[ap] != '[') { err = T("내려받을 파일이 없습니다"); return false; }
    size_t aend = MatchBracket(j, ap);
    for (size_t o = j.find('{', ap); o != std::string::npos && o < aend; o = j.find('{', o + 1)) {
        size_t oend = MatchBracket(j, o);
        if (oend == std::string::npos || oend > aend) break;
        Asset a;
        size_t np = FindKey(j, "name", o, oend);
        if (np != std::string::npos) a.name = ReadJsonString(j, np);
        size_t up = FindKey(j, "browser_download_url", o, oend);
        if (up != std::string::npos) a.url = ReadJsonString(j, up);
        size_t sp = FindKey(j, "size", o, oend);
        if (sp != std::string::npos) a.size = atoll(j.c_str() + sp);
        size_t dp = FindKey(j, "digest", o, oend);
        std::string dg = dp == std::string::npos ? "" : ReadJsonString(j, dp);
        if (dg.rfind("sha256:", 0) == 0) a.sha256 = dg.substr(7);
        for (auto &ch : a.sha256) ch = (char)tolower((unsigned char)ch);
        out.push_back(a);
        o = oend;
    }
    return true;
}

// url → path 로 받고 크기·SHA-256 확인. 실패하면 파일을 지운다
bool DownloadVerified(const Asset &a, const std::string &path, std::string &err,
                      const std::function<bool(long long, long long)> &progress) {
    auto fail = [&](const std::string &m) { remove(path.c_str()); err = m; return false; };
    std::string e;
    // 전체 크기는 릴리스 정보의 값으로 (서버가 길이를 안 알려 줄 때도 진행률이 보이게)
    auto prog = [&](long long got, long long total) { return !progress || progress(got, a.size > 0 ? a.size : total); };
    if (!plat::HttpDownload(a.url, path, e, prog)) {
        if (e == "cancel") return fail(T("취소했습니다"));
        if (e.rfind("HTTP ", 0) == 0) return fail(Fmt(T("내려받기 실패 (응답 %d)"), atoi(e.c_str() + 5)));
        return fail(std::string(T("내려받기 실패: ")) + e);
    }
    long long size = 0;
    std::string hex = FileSha256(path, size);
    if (a.size > 0 && size != a.size) return fail(T("파일 크기가 맞지 않습니다"));
    if (a.sha256.empty()) return fail(T("검증 정보(SHA-256)가 없어 설치하지 않았습니다"));
    if (hex != a.sha256) return fail(T("검증 실패 (SHA-256 불일치)"));
    return true;
}

// ---------------------------------------------------------------- zip 풀기 (저장·deflate 만, zlib)
uint16_t Le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void MakeDirs(const std::string &path) {  // path 의 상위 폴더들
    for (size_t p = path.find('/', 1); p != std::string::npos; p = path.find('/', p + 1)) mkdir(path.substr(0, p).c_str(), 0777);
}

// zip 안에서 prefix 로 시작하는 파일을 dest 아래에 (prefix 를 뗀 경로로) 푼다
bool ExtractPrefix(const std::string &zip, const std::string &prefix, const std::string &dest, int &files, std::string &err) {
    FILE *f = fopen(zip.c_str(), "rb");
    if (!f) { err = T("내려받은 파일을 열 수 없습니다"); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> z(n > 0 ? n : 0);
    bool readOk = n > 0 && fread(z.data(), 1, n, f) == (size_t)n;
    fclose(f);
    if (!readOk || n < 22) { err = T("압축 파일이 손상되었습니다"); return false; }
    long eocd = -1;
    for (long i = n - 22; i >= 0 && i >= n - 22 - 65535; i--)
        if (Le32(&z[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { err = T("압축 파일이 손상되었습니다"); return false; }
    uint16_t count = Le16(&z[eocd + 10]);
    uint32_t cd = Le32(&z[eocd + 16]);
    files = 0;
    for (uint16_t k = 0; k < count; k++) {
        if (cd + 46 > (uint32_t)n || Le32(&z[cd]) != 0x02014b50) { err = T("압축 파일이 손상되었습니다"); return false; }
        uint16_t method = Le16(&z[cd + 10]);
        uint32_t crc = Le32(&z[cd + 16]), csize = Le32(&z[cd + 20]), usize = Le32(&z[cd + 24]);
        uint16_t nl = Le16(&z[cd + 28]), el = Le16(&z[cd + 30]), cl = Le16(&z[cd + 32]);
        uint32_t lho = Le32(&z[cd + 42]);
        std::string name(reinterpret_cast<char *>(&z[cd + 46]), nl);
        cd += 46 + nl + el + cl;
        if (name.rfind(prefix, 0) != 0 || name.back() == '/') continue;
        std::string rel = name.substr(prefix.size());
        if (rel.empty() || rel.find("..") != std::string::npos) continue;
        if (lho + 30 > (uint32_t)n || Le32(&z[lho]) != 0x04034b50) { err = T("압축 파일이 손상되었습니다"); return false; }
        uint32_t data = lho + 30 + Le16(&z[lho + 26]) + Le16(&z[lho + 28]);
        if ((uint64_t)data + csize > (uint64_t)n) { err = T("압축 파일이 손상되었습니다"); return false; }
        std::vector<uint8_t> out(usize);
        if (method == 0) {
            if (csize != usize) { err = T("압축 파일이 손상되었습니다"); return false; }
            memcpy(out.data(), &z[data], usize);
        } else if (method == 8) {
            z_stream s = {};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { err = T("압축 해제 실패"); return false; }
            s.next_in = &z[data];
            s.avail_in = csize;
            s.next_out = out.data();
            s.avail_out = usize;
            int r = inflate(&s, Z_FINISH);
            inflateEnd(&s);
            if (r != Z_STREAM_END || s.total_out != usize) { err = T("압축 해제 실패"); return false; }
        } else {
            err = T("지원하지 않는 압축 방식입니다");
            return false;
        }
        if (crc32(0, out.data(), usize) != crc) { err = T("압축 파일이 손상되었습니다"); return false; }
        std::string path = dest + "/" + rel, tmp = path + ".tmp";
        MakeDirs(path);
        FILE *o = fopen(tmp.c_str(), "wb");
        bool ok = o && fwrite(out.data(), 1, usize, o) == usize;
        if (o && fclose(o) != 0) ok = false;
        remove(path.c_str());
        if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
            remove(tmp.c_str());
            err = T("테마 파일을 쓸 수 없습니다") + std::string(": ") + rel;
            return false;
        }
        files++;
    }
    if (files == 0) { err = T("받은 파일에 테마가 없습니다"); return false; }
    return true;
}

// ---------------------------------------------------------------- 백그라운드 확인
SDL_Thread *gThread = nullptr;
SDL_mutex *gLock = nullptr;
int gState = 0;
UpdateInfo gInfo;
std::string gErr;

int CheckWorker(void *) {
    UpdateInfo info;
    std::string err;
    int st;
    if (!FetchLatestRelease(info, err)) st = -1;
    else st = CompareVersions(info.version, CurrentVersion()) > 0 ? 1 : 2;
    SDL_LockMutex(gLock);
    gInfo = info;
    gErr = err;
    gState = st;
    SDL_UnlockMutex(gLock);
    return 0;
}
}  // namespace

const char *CurrentVersion() { return APP_VERSION_STR; }

bool UpdaterEnabled() { return kUpdateRepo[0] != '\0'; }

int CompareVersions(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
        while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
        if (x != y) return x > y ? 1 : -1;
        // 숫자 아닌 부분(. 또는 -dev 등)은 건너뜀. 하이픈 접미사가 있으면 정식판보다 낮게
        bool ha = i < a.size() && a[i] == '-', hb = j < b.size() && b[j] == '-';
        if (ha != hb) return ha ? -1 : 1;
        while (i < a.size() && !isdigit((unsigned char)a[i])) i++;
        while (j < b.size() && !isdigit((unsigned char)b[j])) j++;
    }
    return 0;
}

bool FetchLatestRelease(UpdateInfo &info, std::string &err) {
    if (!UpdaterEnabled()) { err = T("아직 배포된 버전이 없습니다"); return false; }
    std::vector<Asset> assets;
    if (!FetchAssets(kUpdateRepo, kAndroidTag, assets, info.version, err)) return false;
    if (info.version.empty()) { err = T("릴리스 정보를 읽을 수 없습니다"); return false; }
    std::string shaUrl;
    for (const auto &a : assets) {
        if (a.name == kAssetName) {
            info.url = a.url;
            info.size = a.size;
            info.sha256 = a.sha256;
        } else if (a.name == std::string(kAssetName) + ".sha256") {
            shaUrl = a.url;
        }
    }
    if (info.url.empty()) { err = Fmt(T("릴리스에 %s 가 없습니다"), kAssetName); return false; }
    (void)shaUrl;  // 앱이 받지 않으므로 검증값은 읽지 않음
    info.page = ReleasePageUrl(info.version);
    return true;
}

std::string ReleasePageUrl(const std::string &version) {
    std::string base = std::string("https://github.com/") + kUpdateRepo + "/releases";
    return version.empty() ? base : base + "/tag/" + kAndroidTag + version;
}

bool ExtractThemeZip(const std::string &zip, const std::string &prefix, const std::string &dest, int &files, std::string &err) {
    return ExtractPrefix(zip, prefix, dest, files, err);
}

bool DownloadTheme(const std::string &themesDir, const std::string &pack, int &files, std::string &err,
                   const std::function<bool(long long, long long)> &progress) {
    files = 0;
    UpdateCheckStop();  // 시작할 때의 확인 스레드와 통신이 겹치지 않게
    std::vector<Asset> assets;
    std::string version;
    if (!FetchAssets(kThemeRepo, kSwitchTag, assets, version, err)) return false;
    const Asset *pick = nullptr;
    std::string prefix;
    for (const auto &a : assets)
        if (a.name == pack + ".zip") { pick = &a; prefix = pack + "/"; }
    if (!pick)  // 테마 전용 파일이 없는 릴리스: 런처 zip 에서 테마 부분만
        for (const auto &a : assets)
            if (a.name.rfind("JoyLauncher_", 0) == 0 && a.name.size() > 4 && a.name.compare(a.name.size() - 4, 4, ".zip") == 0) {
                pick = &a;
                prefix = "JoyLauncher/themes/" + pack + "/";
            }
    if (!pick) { err = T("릴리스에 이 테마가 없습니다"); return false; }
    mkdir(themesDir.c_str(), 0777);
    std::string tmp = themesDir + "/." + pack + ".download";
    if (!DownloadVerified(*pick, tmp, err, progress)) return false;
    bool ok = ExtractPrefix(tmp, prefix, themesDir + "/" + pack, files, err);
    remove(tmp.c_str());
    return ok;
}

void UpdateCheckStart() {
    if (gThread) return;
    if (!gLock) gLock = SDL_CreateMutex();
    gState = 0;
    gThread = SDL_CreateThreadWithStackSize(CheckWorker, "update", 256 * 1024, nullptr);
}

int UpdateCheckState(UpdateInfo &info, std::string &err) {
    if (!gLock) return 0;
    SDL_LockMutex(gLock);
    int st = gState;
    info = gInfo;
    err = gErr;
    SDL_UnlockMutex(gLock);
    return st;
}

void UpdateCheckStop() {
    if (!gThread) return;
    SDL_WaitThread(gThread, nullptr);
    gThread = nullptr;
}

void UpdaterShutdown() {
    // 끝내는 중: 진행 중인 통신을 바로 끊고 확인 스레드를 기다린다
    plat::HttpAbort(true);
    UpdateCheckStop();
    plat::HttpAbort(false);
}
