#include "updater.h"

#include "hbmenu.h"
#include "i18n.h"

#include <SDL.h>
#include <curl/curl.h>
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

#ifdef __SWITCH__
#include <switch.h>
#endif

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.0.0-dev"
#endif

namespace {
const char *kLatestApi = "https://api.github.com/repos/JyJyLab/JoyLauncher/releases/latest";
const char *kAssetName = "joylauncher.nro";
const char *kUserAgent = "JoyLauncher-Updater";

bool gCurlReady = false;  // 백그라운드 확인이 끝난 뒤에만 설치하므로 동시 호출 없음
volatile bool gAbort = false;  // 종료 중: 진행 중인 통신을 바로 끊음

// 통신 중 주기적으로 불림. 종료 중이면 중단
int AbortCb(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return gAbort ? 1 : 0; }

// ---------------------------------------------------------------- 네트워크 초기화 (스위치)
struct NetGuard {
    bool ok = true;
    NetGuard() {
#ifdef __SWITCH__
        ok = R_SUCCEEDED(socketInitializeDefault());
#endif
        if (ok && !gCurlReady) { curl_global_init(CURL_GLOBAL_DEFAULT); gCurlReady = true; }
    }
    ~NetGuard() {
#ifdef __SWITCH__
        if (ok) socketExit();
#endif
    }
};

size_t WriteString(char *p, size_t s, size_t n, void *ud) {
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}

bool HttpGet(const std::string &url, std::string &out, std::string &err) {
    CURL *c = curl_easy_init();
    if (!c) { err = T("curl 초기화 실패"); return false; }
    struct curl_slist *h = nullptr;
    h = curl_slist_append(h, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AbortCb);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) { err = std::string(T("네트워크 오류: ")) + curl_easy_strerror(r); return false; }
    if (code == 404) { err = T("아직 배포된 버전이 없습니다"); return false; }
    if (code != 200) { err = T("서버 응답 ") + std::to_string(code); return false; }
    return true;
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

struct DlCtx {
    FILE *f;
    Sha256 sha;
    long long got = 0, total = 0;
    const std::function<bool(long long, long long)> *progress;
    bool cancelled = false;
};

size_t WriteFileCb(char *p, size_t s, size_t n, void *ud) {
    DlCtx *d = static_cast<DlCtx *>(ud);
    size_t len = s * n;
    if (fwrite(p, 1, len, d->f) != len) return 0;
    d->sha.Update(reinterpret_cast<uint8_t *>(p), len);
    d->got += len;
    if (d->progress && *d->progress && !(*d->progress)(d->got, d->total)) { d->cancelled = true; return 0; }
    return len;
}

bool CopyFile(const std::string &src, const std::string &dst) {
    FILE *in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE *out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    std::vector<char> b(1 << 20);
    size_t n;
    bool ok = true;
    while ((n = fread(b.data(), 1, b.size(), in)) > 0)
        if (fwrite(b.data(), 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

// ---------------------------------------------------------------- 릴리스 자산
struct Asset {
    std::string name, url, sha256;
    long long size = 0;
};

// 최신 릴리스의 자산 목록 (version: 태그의 v 를 뺀 값)
bool FetchAssets(std::vector<Asset> &out, std::string &version, std::string &err) {
    std::string j;
    if (!HttpGet(kLatestApi, j, err)) return false;
    size_t p = FindKey(j, "tag_name", 0, j.size());
    if (p != std::string::npos) version = ReadJsonString(j, p);
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
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
    DlCtx d;
    d.f = fopen(path.c_str(), "wb");
    if (!d.f) { err = T("임시 파일을 만들 수 없습니다"); return false; }
    d.total = a.size;
    d.progress = &progress;
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, a.url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteFileCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    bool closed = fclose(d.f) == 0;
    auto fail = [&](const std::string &m) { remove(path.c_str()); err = m; return false; };
    if (d.cancelled) return fail(T("취소했습니다"));
    if (r != CURLE_OK) return fail(std::string(T("내려받기 실패: ")) + curl_easy_strerror(r));
    if (code != 200 || !closed) return fail(Fmt(T("내려받기 실패 (응답 %d)"), (int)code));
    if (a.size > 0 && d.got != a.size) return fail(T("파일 크기가 맞지 않습니다"));
    if (a.sha256.empty()) return fail(T("검증 정보(SHA-256)가 없어 설치하지 않았습니다"));
    if (d.sha.Hex() != a.sha256) return fail(T("검증 실패 (SHA-256 불일치)"));
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
    NetGuard net;
    if (!net.ok) { err = T("네트워크를 사용할 수 없습니다"); return false; }
    std::string j;
    if (!HttpGet(kLatestApi, j, err)) return false;
    size_t p = FindKey(j, "tag_name", 0, j.size());
    if (p == std::string::npos) { err = T("릴리스 정보를 읽을 수 없습니다"); return false; }
    info.version = ReadJsonString(j, p);
    if (!info.version.empty() && (info.version[0] == 'v' || info.version[0] == 'V')) info.version.erase(0, 1);
    size_t bp = FindKey(j, "body", 0, j.size());
    if (bp != std::string::npos) info.notes = ReadJsonString(j, bp);
    size_t ap = FindKey(j, "assets", 0, j.size());
    if (ap == std::string::npos || j[ap] != '[') { err = T("내려받을 파일이 없습니다"); return false; }
    size_t aend = MatchBracket(j, ap);
    std::string shaUrl;
    for (size_t o = j.find('{', ap); o != std::string::npos && o < aend; o = j.find('{', o + 1)) {
        size_t oend = MatchBracket(j, o);
        if (oend == std::string::npos || oend > aend) break;
        size_t np = FindKey(j, "name", o, oend);
        std::string name = np == std::string::npos ? "" : ReadJsonString(j, np);
        size_t up = FindKey(j, "browser_download_url", o, oend);
        std::string url = up == std::string::npos ? "" : ReadJsonString(j, up);
        if (name == kAssetName) {
            info.url = url;
            size_t sp = FindKey(j, "size", o, oend);
            if (sp != std::string::npos) info.size = atoll(j.c_str() + sp);
            size_t dp = FindKey(j, "digest", o, oend);
            std::string dg = dp == std::string::npos ? "" : ReadJsonString(j, dp);
            if (dg.rfind("sha256:", 0) == 0) info.sha256 = dg.substr(7);
        } else if (name == std::string(kAssetName) + ".sha256") {
            shaUrl = url;
        }
        o = oend;
    }
    if (info.url.empty()) { err = Fmt(T("릴리스에 %s 가 없습니다"), kAssetName); return false; }
    if (info.sha256.empty() && !shaUrl.empty()) {
        std::string s, e2;
        if (HttpGet(shaUrl, s, e2)) info.sha256 = s.substr(0, std::min<size_t>(64, s.find_first_of(" \t\r\n")));
    }
    for (auto &ch : info.sha256) ch = (char)tolower((unsigned char)ch);
    return true;
}

bool DownloadAndInstall(const UpdateInfo &info, const std::string &selfNro, bool alsoHbmenu, std::string &err,
                        const std::function<bool(long long, long long)> &progress) {
    NetGuard net;
    if (!net.ok) { err = T("네트워크를 사용할 수 없습니다"); return false; }
    std::string self = SdPath(selfNro);
    std::string tmp = self + ".new";
    DlCtx d;
    d.f = fopen(tmp.c_str(), "wb");
    if (!d.f) { err = T("임시 파일을 만들 수 없습니다"); return false; }
    d.total = info.size;
    d.progress = &progress;
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, info.url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteFileCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    bool closed = fclose(d.f) == 0;
    auto fail = [&](const std::string &m) { remove(tmp.c_str()); err = m; return false; };
    if (d.cancelled) return fail(T("취소했습니다"));
    if (r != CURLE_OK) return fail(std::string(T("내려받기 실패: ")) + curl_easy_strerror(r));
    if (code != 200 || !closed) return fail(Fmt(T("내려받기 실패 (응답 %d)"), (int)code));
    if (info.size > 0 && d.got != info.size) return fail(T("파일 크기가 맞지 않습니다"));
    std::string hex = d.sha.Hex();
    if (info.sha256.empty()) return fail(T("검증 정보(SHA-256)가 없어 설치하지 않았습니다"));
    if (hex != info.sha256) return fail(T("검증 실패 (SHA-256 불일치)"));
    // 실행 중인 nro(자기 자신 또는 /hbmenu.nro)는 romfs 때문에 열려 있어 지우거나 바꿀 수 없다.
    // 글꼴 등은 이미 메모리에 올라와 있으므로 romfs 를 닫고 교체한다. 실패하면 다시 연다 (성공하면 곧바로 다시 시작)
    struct RomfsReleased {
        bool keepClosed = false;
        RomfsReleased() {
#ifdef __SWITCH__
            romfsExit();
#endif
        }
        ~RomfsReleased() {
#ifdef __SWITCH__
            if (!keepClosed) romfsInit();
#endif
        }
    } romfsGuard;
    // 교체: 현재 파일은 .old 로 남겨 두고 새 파일로
    std::string old = self + ".old";
    remove(old.c_str());
    if (rename(self.c_str(), old.c_str()) != 0) CopyFile(self, old);  // 백업 (이름 바꾸기가 안 되면 복사)
    remove(self.c_str());
    if (rename(tmp.c_str(), self.c_str()) != 0 && !CopyFile(tmp, self)) {
        CopyFile(old, self);
        return fail(T("파일 교체 실패"));
    }
    remove(tmp.c_str());
    if (alsoHbmenu) {
        std::string hb = SdPath("/hbmenu.nro"), hbTmp = hb + ".new";
        remove(hbTmp.c_str());
        bool hbOk = CopyFile(self, hbTmp);
        if (hbOk) {
            remove(hb.c_str());
            hbOk = rename(hbTmp.c_str(), hb.c_str()) == 0;
            // 이름 바꾸기가 안 되면 내용 복사로 덮어쓰기
            if (!hbOk) hbOk = CopyFile(hbTmp, hb);
        }
        remove(hbTmp.c_str());
        if (!hbOk) {
            err = T("런처는 업데이트했지만 기본 홈브루 메뉴(/hbmenu.nro) 교체에 실패했습니다. "
                    "옵션 창의 \"기본 홈브루 메뉴를 Joy Launcher 로 업데이트\" 로 다시 시도하세요");
            return false;
        }
    }
#ifdef __SWITCH__
    if (FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc")) fsFsCommit(fs);
#endif
    romfsGuard.keepClosed = true;
    return true;
}

bool DownloadTheme(const std::string &themesDir, int &files, std::string &err,
                   const std::function<bool(long long, long long)> &progress) {
    files = 0;
    UpdateCheckStop();  // 시작할 때의 확인 스레드와 통신이 겹치지 않게
    NetGuard net;
    if (!net.ok) { err = T("네트워크를 사용할 수 없습니다"); return false; }
    std::vector<Asset> assets;
    std::string version;
    if (!FetchAssets(assets, version, err)) return false;
    const Asset *pick = nullptr;
    std::string prefix;
    for (const auto &a : assets)
        if (a.name == "JoyBasic.zip") { pick = &a; prefix = "JoyBasic/"; }
    if (!pick)  // 테마 전용 파일이 없는 릴리스: 런처 zip 에서 테마 부분만
        for (const auto &a : assets)
            if (a.name.rfind("JoyLauncher_", 0) == 0 && a.name.size() > 4 && a.name.compare(a.name.size() - 4, 4, ".zip") == 0) {
                pick = &a;
                prefix = "JoyLauncher/themes/JoyBasic/";
            }
    if (!pick) { err = T("릴리스에 기본 테마가 없습니다"); return false; }
    mkdir(themesDir.c_str(), 0777);
    std::string tmp = themesDir + "/.JoyBasic.download";
    if (!DownloadVerified(*pick, tmp, err, progress)) return false;
    bool ok = ExtractPrefix(tmp, prefix, themesDir + "/JoyBasic", files, err);
    remove(tmp.c_str());
#ifdef __SWITCH__
    if (FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc")) fsFsCommit(fs);
#endif
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
    // 스레드를 정리하지 않고 끝내면 그 스레드 스택(힙에서 빌려 다른 주소로 매핑됨)이 풀리지 않아,
    // 같은 hbloader 에서 다음에 실행되는 프로그램(게임 코어·sphaira·ezremote 등)의 메모리가 깨진다.
    gAbort = true;
    UpdateCheckStop();
    gAbort = false;
    if (gCurlReady) {
        curl_global_cleanup();  // curl 이 연 ssl·nifm·csrng 서비스 닫기
        gCurlReady = false;
    }
}
