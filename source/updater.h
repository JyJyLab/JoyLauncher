// 깃헙 릴리스(JyJyLab/JoyLauncher)에서 새 버전을 확인하고 런처를 업데이트한다.
#pragma once
#include <functional>
#include <string>

struct UpdateInfo {
    std::string version;   // "0.7.1" (태그의 v 제외)
    std::string url;       // joylauncher.nro 내려받기 주소
    std::string sha256;    // 소문자 16진수 (없으면 빈 값)
    long long size = 0;
    std::string notes;     // 릴리스 설명 (첫 몇 줄)
};

const char *CurrentVersion();
// a > b 이면 양수 ("0.7.10" > "0.7.9")
int CompareVersions(const std::string &a, const std::string &b);

// 최신 릴리스 조회 (네트워크, 수 초 걸릴 수 있음). 실패하면 false, err 에 이유
bool FetchLatestRelease(UpdateInfo &info, std::string &err);

// 받아서 검증 후 selfNro 를 교체. alsoHbmenu 이면 /hbmenu.nro 도 교체.
// progress(받은 바이트, 전체 바이트) 가 false 를 돌려주면 중지
bool DownloadAndInstall(const UpdateInfo &info, const std::string &selfNro, bool alsoHbmenu, std::string &err,
                        const std::function<bool(long long, long long)> &progress);

// 백그라운드 확인: 시작 시 한 번. 상태 0 진행 중/안 함, 1 새 버전 있음, 2 최신, -1 실패
void UpdateCheckStart();
int UpdateCheckState(UpdateInfo &info, std::string &err);
void UpdateCheckStop();
