// 깃헙 릴리스에서 런처의 새 버전(apk)을 확인·설치하고, 기본 테마 팩을 내려받는다.
#pragma once
#include <functional>
#include <string>

struct UpdateInfo {
    std::string version;   // "0.7.1" (태그의 v 제외)
    std::string url;       // JoyLauncher.apk 내려받기 주소
    std::string sha256;    // 소문자 16진수 (없으면 빈 값)
    long long size = 0;
    std::string notes;     // 릴리스 설명 (첫 몇 줄)
};

const char *CurrentVersion();
// 런처 업데이트를 쓸 수 있는지 (업데이트 저장소가 정해진 빌드인지). 아니면 옵션 창에 업데이트 항목이 없다
bool UpdaterEnabled();
// a > b 이면 양수 ("0.7.10" > "0.7.9")
int CompareVersions(const std::string &a, const std::string &b);

// 최신 릴리스 조회 (네트워크, 수 초 걸릴 수 있음). 실패하면 false, err 에 이유
bool FetchLatestRelease(UpdateInfo &info, std::string &err);

// apk 를 받아 검증(SHA-256)한 뒤 설치 화면을 연다.
// progress(받은 바이트, 전체 바이트) 가 false 를 돌려주면 중지
bool DownloadAndInstall(const UpdateInfo &info, std::string &err, const std::function<bool(long long, long long)> &progress);

// zip 안의 prefix 폴더 아래 파일을 dest 에 푼다 (테마 내려받기의 풀기 단계, 데스크톱 시험용으로 공개)
bool ExtractThemeZip(const std::string &zip, const std::string &prefix, const std::string &dest, int &files, std::string &err);

// 테마 팩(pack = 폴더 이름: JoyBasic, RetroPack, ByeokgolKim, DcGall)을 스위치판 저장소의 최신 릴리스에서 받아
// themesDir/<pack> 에 설치한다. 릴리스의 <pack>.zip 을 쓰고, 없으면 런처 zip 안의 JoyLauncher/themes/<pack> 만 푼다.
// 기존 파일은 덮어쓰고, 사용자가 따로 넣은 파일은 그대로 둔다. files: 설치한 파일 수
bool DownloadTheme(const std::string &themesDir, const std::string &pack, int &files, std::string &err,
                   const std::function<bool(long long, long long)> &progress);

// 백그라운드 확인: 시작 시 한 번. 상태 0 진행 중/안 함, 1 새 버전 있음, 2 최신, -1 실패
void UpdateCheckStart();
int UpdateCheckState(UpdateInfo &info, std::string &err);
void UpdateCheckStop();
// 런처를 끝낼 때: 진행 중인 통신을 끊고 확인 스레드 정리
void UpdaterShutdown();
