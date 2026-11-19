// 데스크톱 시험판의 플랫폼 층: 안드로이드 기능을 흉내 낸다 (화면 스냅샷·실행 인자 확인용).
//   JOY_PRIMARY    공용 저장소 루트 (기본: 현재 폴더)
//   JOY_VOLUMES    추가 저장소 "경로=이름,경로=이름" (SD 카드 흉내, 이름 = SAF 의 저장소 이름)
//   JOY_PACKAGES   설치된 앱 목록 파일 (한 줄에 "패키지" 또는 "패키지/액티비티"). 없으면 무엇이든 설치된 것으로 봄
//   JOY_APPS       실행할 수 있는 앱 목록 파일 ("이름<TAB>패키지<TAB>액티비티")
//   JOY_BATTERY    76 또는 76,c (충전 중)
//   JOY_NOSTORAGE  1 이면 저장소 권한이 없는 것으로
//   JOY_ROMFS      APK assets 대신 읽을 폴더 (기본 romfs)
#ifndef __ANDROID__
#include "platform.h"
#include "video.h"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace {
std::vector<std::string> gLastStart;
std::thread gVideoThread;
std::atomic<bool> gVideoQuit{false};
volatile bool gAbort = false;
bool gStorageAsked = false;

std::string Env(const char *k, const char *def = "") {
    const char *v = getenv(k);
    return v && *v ? v : def;
}

const std::set<std::string> *Packages() {
    static bool loaded = false, have = false;
    static std::set<std::string> set;
    if (!loaded) {
        loaded = true;
        std::string f = Env("JOY_PACKAGES");
        if (!f.empty()) {
            have = true;
            std::ifstream in(f);
            for (std::string l; std::getline(in, l);) {
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                if (!l.empty() && l[0] != '#') set.insert(l);
            }
        }
    }
    return have ? &set : nullptr;
}

size_t WriteString(char *p, size_t s, size_t n, void *ud) {
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}
int AbortCb(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return gAbort ? 1 : 0; }

struct Dl {
    FILE *f;
    long long got = 0;
    const std::function<bool(long long, long long)> *progress;
    bool cancelled = false;
};
size_t WriteFileCb(char *p, size_t s, size_t n, void *ud) {
    Dl *d = static_cast<Dl *>(ud);
    size_t len = s * n;
    if (fwrite(p, 1, len, d->f) != len) return 0;
    d->got += (long long)len;
    return len;
}
int DlProgress(void *ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    Dl *d = static_cast<Dl *>(ud);
    if (gAbort) return 1;
    if (d->progress && *d->progress && !(*d->progress)((long long)now, (long long)total)) {
        d->cancelled = true;
        return 1;
    }
    return 0;
}
}  // namespace

namespace plat {

void Init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

std::string PrimaryStorage() {
    std::string p = Env("JOY_PRIMARY");
    if (!p.empty()) return p;
    char buf[4096];
    return getcwd(buf, sizeof(buf)) ? buf : ".";
}

std::vector<Volume> Volumes() {
    std::vector<Volume> out;
    Volume pv;
    pv.path = PrimaryStorage();
    pv.id = "primary";
    pv.primary = true;
    out.push_back(pv);
    std::stringstream ss(Env("JOY_VOLUMES"));
    for (std::string part; std::getline(ss, part, ',');) {
        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        Volume v;
        v.path = part.substr(0, eq);
        v.id = part.substr(eq + 1);
        out.push_back(v);
    }
    return out;
}

bool HasStorageAccess() { return Env("JOY_NOSTORAGE").empty() || gStorageAsked; }
void RequestStorageAccess() {
    printf("REQUEST storage access\n");
    gStorageAsked = true;
}
std::string PrivateDir() { return Env("JOY_PRIVATE", "/tmp"); }
std::string InternalDataRoot() { return "/data/user/0"; }

bool ReadAsset(const std::string &name, std::vector<char> &out) {
    std::string path = Env("JOY_ROMFS", "romfs") + "/" + name;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, fp) : 0;
    fclose(fp);
    return n > 0 && got == (size_t)n;
}

bool PackageInstalled(const std::string &package, const std::string &activity) {
    const std::set<std::string> *p = Packages();
    if (!p) return true;
    if (p->count(package)) return true;  // 패키지만 적은 줄: 그 앱의 어떤 화면이든 있는 것으로
    if (activity.empty()) {
        for (const auto &e : *p)
            if (e.compare(0, package.size() + 1, package + "/") == 0) return true;
        return false;
    }
    return p->count(package + "/" + activity) > 0;
}

std::vector<App> LaunchableApps() {
    std::vector<App> out;
    std::ifstream in(Env("JOY_APPS"));
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && l.back() == '\r') l.pop_back();
        size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
        if (t1 == std::string::npos) continue;
        App a;
        a.label = l.substr(0, t1);
        a.package = l.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
        if (t2 != std::string::npos) a.activity = l.substr(t2 + 1);
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const App &a, const App &b) { return a.label < b.label; });
    return out;
}

bool SaveAppIcon(const std::string &, const std::string &, const std::string &, int) { return false; }

bool StartActivity(const std::vector<std::string> &amArgs, std::string &err) {
    gLastStart = amArgs;
    printf("START");
    for (const auto &a : amArgs) printf(" [%s]", a.c_str());
    printf("\n");
    if (!Env("JOY_START_FAIL").empty()) {
        err = Env("JOY_START_FAIL");
        return false;
    }
    return true;
}

bool StartApp(const std::string &package, const std::string &activity, std::string &err) {
    std::vector<std::string> a = activity.empty() ? std::vector<std::string>{"-p", package}
                                                  : std::vector<std::string>{"-n", package + "/" + activity};
    return StartActivity(a, err);
}

int Battery(bool &charging) {
    std::string b = Env("JOY_BATTERY");
    if (b.empty()) return -1;
    charging = b.find('c') != std::string::npos;
    return atoi(b.c_str());
}

bool SystemKorean() {
    std::string l = Env("JOY_LANG");
    if (l.empty()) l = Env("LANG");
    return l.compare(0, 2, "ko") == 0;
}

SDL_Surface *DecodeImage(const std::string &, int) { return nullptr; }  // 데스크톱은 SDL2_image 가 webp 도 읽음

// 미리보기 영상: ffprobe 로 크기를 알아내고, ffmpeg 가 제 속도로(-re) 풀어 주는 장면(yuv420p)을 읽는다.
// 소리는 내지 않는다 (그리기 확인용)
void VideoOpen(const std::string &path, bool, int token) {
    VideoClose();
    gVideoQuit = false;
    gVideoThread = std::thread([path, token] {
        std::string q = "'";
        for (char c : path) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
        q += "'";
        int w = 0, h = 0;
        if (FILE *p = popen(("ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 " + q + " 2>/dev/null").c_str(), "r")) {
            if (fscanf(p, "%d,%d", &w, &h) != 2) w = h = 0;
            pclose(p);
        }
        FILE *f = w > 0 && h > 0 ? popen(("exec ffmpeg -v error -re -stream_loop -1 -i " + q + " -an -f rawvideo -pix_fmt yuv420p - 2>/dev/null").c_str(), "r")
                                 : nullptr;
        if (!f) {
            VideoSinkState(token, -1, false);
            return;
        }
        VideoSinkState(token, 2, false);
        size_t cw = (w + 1) / 2, ch = (h + 1) / 2, ySize = (size_t)w * h;
        std::vector<unsigned char> buf(ySize + cw * ch * 2);
        while (!gVideoQuit && fread(buf.data(), 1, buf.size(), f) == buf.size())
            VideoSinkYuv(token, w, h, buf.data(), w, buf.data() + ySize, buf.data() + ySize + cw * ch, (int)cw, 1);
        pclose(f);
    });
}

void VideoClose() {
    gVideoQuit = true;
    if (gVideoThread.joinable()) gVideoThread.join();
}

bool HttpGet(const std::string &url, std::string &out, std::string &err) {
    CURL *c = curl_easy_init();
    if (!c) { err = "curl"; return false; }
    struct curl_slist *h = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "JoyLauncher-Updater");
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
    if (r != CURLE_OK) { err = curl_easy_strerror(r); return false; }
    if (code != 200) { err = "HTTP " + std::to_string(code); return false; }
    return true;
}

bool HttpDownload(const std::string &url, const std::string &path, std::string &err,
                  const std::function<bool(long long, long long)> &progress) {
    Dl d;
    d.f = fopen(path.c_str(), "wb");
    if (!d.f) { err = "open"; return false; }
    d.progress = &progress;
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "JoyLauncher-Updater");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, DlProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteFileCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    bool closed = fclose(d.f) == 0;
    if (d.cancelled) { remove(path.c_str()); err = "cancel"; return false; }
    if (r != CURLE_OK) { remove(path.c_str()); err = curl_easy_strerror(r); return false; }
    if (code != 200 || !closed) { remove(path.c_str()); err = "HTTP " + std::to_string(code); return false; }
    return true;
}

void HttpAbort(bool on) { gAbort = on; }

void Finish() {}

const std::vector<std::string> &LastStart() { return gLastStart; }

}  // namespace plat
#endif  // !__ANDROID__
