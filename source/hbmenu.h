// 기본 홈브루 메뉴(/hbmenu.nro) 교체/복원 관리
// 종류: nx-hbmenu (Atmosphere 동봉), sphaira (스스로 /hbmenu.nro 를 덮어씀), 그 밖의 nro (기타)
#pragma once
#include <string>
#include <vector>

enum class HbKind { None, Self, NxHbmenu, Sphaira, Other };

// SD 경로 -> 실제 파일 경로 (데스크톱 테스트는 JOY_SDROOT 환경변수를 앞에 붙임)
std::string SdPath(const std::string &p);
bool SdExists(const std::string &p);
// NRO 의 NACP 첫 번째 이름 / 버전 (없으면 빈 값)
std::string NroTitle(const std::string &sdPath);
std::string NroVersion(const std::string &sdPath);
HbKind CurrentHbmenu();
const char *HbKindName(HbKind k);
// selfNro: 런처 nro 의 SD 경로 (예: /switch/joylauncher/joylauncher.nro)
bool InstallAsHbmenu(const std::string &selfNro, std::string &msg);
bool RestoreHbmenu(std::string &msg);
// Joy Launcher 를 기본 메뉴로 설정해 둔 기록이 있는지
bool HbInstalledRecord();
// 시작할 때 확인: 기록상 Joy 가 기본 메뉴인데 /hbmenu.nro 가 바뀌었거나(Atmosphere 업데이트 등)
// sphaira 의 자동 덮어쓰기가 다시 켜졌으면 처리하고 알림 문구를 돌려준다 ("" = 이상 없음)
std::string HbCheckOnStart();

// 실행할 수 있는 홈브루 메뉴들 (이전 기본 메뉴를 먼저). label 은 화면 표시용
struct HbMenuEntry {
    std::string label;
    std::string nro;
};
std::vector<HbMenuEntry> HomebrewMenus();
