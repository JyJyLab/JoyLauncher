// 테마 빌더 빌드용: 네트워크(업데이트)·영상·BGM 기능은 쓰지 않으므로 빈 함수로 대신한다
#include <cctype>
#include <string>

#include "../source/bgm.h"
#include "../source/updater.h"
#include "../source/videoframe.h"

const char *CurrentVersion() { return APP_VERSION_STR; }
int CompareVersions(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
        while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
        if (x != y) return x > y ? 1 : -1;
        bool ha = i < a.size() && a[i] == '-', hb = j < b.size() && b[j] == '-';
        if (ha != hb) return ha ? -1 : 1;
        while (i < a.size() && !isdigit((unsigned char)a[i])) i++;
        while (j < b.size() && !isdigit((unsigned char)b[j])) j++;
    }
    return 0;
}
bool FetchLatestRelease(UpdateInfo &, std::string &err) { err = "builder"; return false; }
bool DownloadAndInstall(const UpdateInfo &, const std::string &, bool, std::string &err, const std::function<bool(long long, long long)> &) { err = "builder"; return false; }
bool ExtractThemeZip(const std::string &, const std::string &, const std::string &, int &files, std::string &err) { files = 0; err = "builder"; return false; }
bool DownloadTheme(const std::string &, const std::string &, int &files, std::string &err, const std::function<bool(long long, long long)> &) { files = 0; err = "builder"; return false; }
void UpdateCheckStart() {}
int UpdateCheckState(UpdateInfo &, std::string &) { return 0; }
void UpdateCheckStop() {}
void UpdaterShutdown() {}

bool ExtractVideoFrame(const std::string &, const std::string &, int) { return false; }
void FrameWorkerStart() {}
void FrameWorkerStop() {}
void FrameWorkerRequest(const std::string &, const std::string &) {}
int FrameWorkerState(const std::string &) { return -1; }

void BgmScan(const std::string &) {}
int BgmCount() { return 0; }
int BgmCountIn(const std::string &) { return 0; }
void BgmConfigure(bool, int, bool) {}
void BgmUpdate() {}
void BgmSkip() {}
void BgmPause() {}
void BgmResume() {}
void BgmShutdown(int) {}
