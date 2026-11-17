// 기종 ↔ 에뮬레이터 매칭과 게임 실행 인텐트 만들기 (안드로이드)
//
// 두 방식을 섞어 쓴다.
//   1차  페가수스: metadata.pegasus.txt 의 launch (am start …) 를 그대로 실행
//   2차  ES-DE   : 롬 폴더 이름 → 기종 → 그 기종의 에뮬레이터 목록에서 설치된 것
// 기종·에뮬레이터 표는 ES-DE 안드로이드판의 es_systems.xml / es_find_rules.xml (APK 에 동봉) 이고,
// 같은 문법의 파일을 /JoyLauncher/custom_systems/ 에 두면 기종·에뮬레이터를 더하거나 바꿀 수 있다.
// /JoyLauncher/emulators.txt [user] 로 카테고리·폴더·기종마다 쓸 에뮬레이터를 직접 정한다.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "pegasus.h"

struct EmuCommand {
    std::string label;     // "DuckStation (Standalone)"
    std::string command;   // ES-DE 명령 원문
    std::string emulator;  // %EMULATOR_X% 의 X ("" = 앱 실행 등)
    std::string core;      // RetroArch 코어 이름 ("" = 단독 에뮬레이터)
};

struct EmuSystem {
    std::string name, fullname, theme;
    std::vector<std::string> exts;  // 소문자, 점 없음
    std::vector<EmuCommand> commands;
};

// Game::launch 값 (목록 캐시에 저장됨)
//   "@sys:<기종>"             폴더 이름 등으로 정한 기종 (실행할 때 에뮬레이터를 고름)
//   "@sys:<기종>|<에뮬 이름>"  ES-DE gamelist 의 altemulator
//   그 밖                     페가수스 launch 원문 (am start …)
static const char *const kLaunchSys = "@sys:";
static const char *const kLaunchApp = "@app";  // 앱 즐겨찾기 항목 (Game::file = "app://패키지/액티비티")

// 표 읽기. esdeDirs: ES-DE 폴더 후보 (그 안의 custom_systems 도 읽음). 여러 번 불러도 됨
void EmuLoad(const std::string &home, const std::vector<std::string> &esdeDirs);
// 목록 캐시 서명에 넣을 글: 사용자 표·[user] 가 바뀌면 달라진다
std::string EmuSignature();
const std::vector<EmuSystem> &EmuSystems();
const EmuSystem *EmuFind(const std::string &name);
// 롬 폴더·카테고리 이름 → 기종 (모르면 nullptr). "FBNEO ACT", "Play Station", "슈퍼패미컴", "gba.custom" 같은 이름도 처리
const EmuSystem *EmuSystemForFolder(const std::string &name);
// 확장자가 한 기종에만 쓰일 때 그 기종
const EmuSystem *EmuSystemForExt(const std::string &ext);
// 코어 이름·앱 패키지 이름이 들어 있는 글 → 기종
const EmuSystem *EmuSystemForIdent(const std::string &ident);
// emulators.txt [user] 에 이 이름(카테고리·폴더·기종)의 줄이 있는지
bool EmuUserHas(const std::string &key);
// 페가수스 launch 에 적힌 코어 이름 (xxx_libretro_android.so → xxx), 없으면 패키지 이름
std::string EmuLaunchIdent(const std::string &launch);
// 읽은 게임 목록에서 쓰이는 RetroArch 코어를 기억 (설치돼 있을 가능성이 높은 코어부터 고르기 위함)
void EmuLearnCores(const std::vector<Collection> &cols);

struct LaunchCtx {
    const Game *game = nullptr;
    std::string category, shortName;    // 열려 있는 카테고리
    std::vector<std::string> folders;   // 그 카테고리의 롬 폴더 이름
    std::vector<std::string> roots;     // 롬 폴더들의 위쪽 폴더 (기종 폴더 = 이 바로 아래)
};

struct LaunchPlan {
    std::vector<std::string> args;  // am start 인자
    std::string emulator;           // 화면·기록에 보일 이름
    std::string package;
    std::string how;                // 고른 방법
};

// 게임을 실행할 인텐트 만들기. 못 만들면 false + err (화면에 보일 말)
bool EmuBuildLaunch(const LaunchCtx &c, LaunchPlan &plan, std::string &err);

// 옵션의 "에뮬레이터 선택": 이 카테고리(기종)에서 고를 수 있는 것
struct EmuChoice {
    std::string label;      // "" = 자동
    bool installed = false;
    bool current = false;
};
std::vector<EmuChoice> EmuChoices(const LaunchCtx &c, std::string &systemName);
// emulators.txt [user] 에 <key>=<value> 저장 (value "" = 그 줄 지움)
void EmuSetUser(const std::string &key, const std::string &value);
// 설치된 앱 기억 지우기 (다른 앱에 다녀온 뒤: 그 사이 에뮬레이터를 깔았을 수 있음)
void EmuForgetInstalled();

// ---- 주소 만들기 (시험할 수 있게 공개)
// 파일 제공자 주소: content://<앱>.files/root/<경로>
std::string EmuContentUri(const std::string &path);
// SAF 문서 주소: content://com.android.externalstorage.documents/tree/<treeDir>/document/<path>
std::string EmuDocumentUri(const std::string &path, const std::string &treeDir);
// 페가수스 방식 토큰 나누기 (작은·큰 따옴표)
std::vector<std::string> EmuTokenize(const std::string &s);
