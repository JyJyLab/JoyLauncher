// 기본 홈브루 메뉴(/hbmenu.nro) 교체/복원 관리
// 두 종류를 고려: 원래 nx-hbmenu (Atmosphere 동봉), sphaira (스스로 /hbmenu.nro 를 덮어씀)
#pragma once
#include <string>

enum class HbKind { None, Self, NxHbmenu, Sphaira, Other };

// SD 경로 -> 실제 파일 경로 (데스크톱 테스트는 JOY_SDROOT 환경변수를 앞에 붙임)
std::string SdPath(const std::string &p);
bool SdExists(const std::string &p);
// NRO 의 NACP 첫 번째 이름 (없으면 빈 값)
std::string NroTitle(const std::string &sdPath);
HbKind CurrentHbmenu();
const char *HbKindName(HbKind k);
// selfNro: 런처 nro 의 SD 경로 (예: /switch/joylauncher/joylauncher.nro)
bool InstallAsHbmenu(const std::string &selfNro, std::string &msg);
bool RestoreHbmenu(std::string &msg);
