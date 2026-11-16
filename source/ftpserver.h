// 런처 안의 FTP 받기 (옵션 "PC에서 테마 받기 (FTP)" 화면이 열린 동안만).
// 쓰기·지우기는 /JoyLauncher/themes, /JoyLauncher/categories.txt, /JoyLauncher/categories 아래만 허용.
#pragma once
#include <string>
#include <vector>

struct FtpServerStatus {
    bool running = false;
    std::string ip, error;
    int port = 0;
    bool client = false;         // PC 가 붙어 있는지
    int files = 0;               // 받은 파일 수
    int removed = 0;             // 지운 파일·폴더 수
    long long bytes = 0;
    std::vector<std::string> log;  // 최근 기록 (최신이 뒤)
};

// home: Joy Launcher 폴더 (스위치 "/JoyLauncher"). FTP 에서는 /JoyLauncher 로 보인다
bool FtpServerStart(const std::string &home, int port, std::string &err);
void FtpServerStop();
FtpServerStatus FtpServerGet();
