#include "i18n.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {

Lang gLang = Lang::Ko;

// { 한국어, English }
const char *const kTable[][2] = {
    // 옵션 창·메뉴
    {"홈브루 메뉴 (sphaira)", "Homebrew menu (sphaira)"},
    {"홈브루 메뉴 (nx-hbmenu)", "Homebrew menu (nx-hbmenu)"},
    {"영상 장면 일괄 추출 (남은 게임 %d개)", "Extract video frames (%d games left)"},
    {"게임 목록 새로 고침 (롬·그림·영상을 넣거나 뺐을 때)", "Refresh game list (after adding or removing ROMs, images, videos)"},
    {"업데이트 설치: v%s (현재 v%s)", "Install update: v%s (current v%s)"},
    {"업데이트 확인 (현재 v%s)", "Check for updates (current v%s)"},
    {"테마 설정 (현재: %s)", "Theme (current: %s)"},
    {"기본", "Default"},
    {"언어 / Language: %s", "Language / 언어: %s"},
    {"자동 (%s)", "Auto (%s)"},
    {"BGM: 곡 없음  (/JoyLauncher/bgm 에 mp3·ogg 넣기)", "BGM: no songs  (put mp3/ogg in /JoyLauncher/bgm)"},
    {"BGM: 켜짐 (%d곡)", "BGM: On (%d songs)"},
    {"BGM: 꺼짐 (%d곡)", "BGM: Off (%d songs)"},
    {"BGM 설정 (켜짐 (On) · 볼륨 %d%% · %s)", "BGM (On · volume %d%% · %s)"},
    {"BGM 설정 (꺼짐 (Off))", "BGM (Off)"},
    {"BGM: 켜짐 (On) (%d곡)", "BGM: On (%d songs)"},
    {"BGM: 꺼짐 (Off) (%d곡)", "BGM: Off (%d songs)"},
    {"즐겨찾기", "Favorites"},
    {"즐겨찾기에 넣었습니다", "Added to favorites"},
    {"즐겨찾기에서 뺐습니다", "Removed from favorites"},
    {"BGM 설정", "BGM"},
    {"무작위", "shuffle"},
    {"차례대로", "in order"},
    {"볼륨: %d%%", "Volume: %d%%"},
    {"순서: 차례대로", "Order: In order"},
    {"순서: 무작위", "Order: Shuffle"},
    {"다음 곡", "Next song"},
    {"런처를 종료합니다", "Exit the launcher?"},
    {"[A] 종료   [B] 돌아가기", "[A] Exit   [B] Back"},
    {"/JoyLauncher/bgm 폴더에 mp3 또는 ogg 파일을 넣고 다시 실행하세요", "Put mp3 or ogg files in /JoyLauncher/bgm and start again"},
    {"원래 홈브루 메뉴로 되돌리기", "Restore the original homebrew menu"},
    {"기본 홈브루 메뉴를 Joy Launcher 로 업데이트 (현재: 이전 버전)", "Update the default homebrew menu to this Joy Launcher (current: older version)"},
    {"Joy Launcher 를 기본 홈브루 메뉴로 설정 (현재: %s)", "Set Joy Launcher as the default homebrew menu (current: %s)"},
    {"종료 (HOME)", "Exit (HOME)"},
    {"옵션", "Options"},
    {"[A] 선택   [B] 닫기", "[A] Select   [B] Close"},
    {"[A] 선택   ◀ ▶ 조절   [B] 닫기", "[A] Select   ◀ ▶ Adjust   [B] Close"},
    // 화면
    {"애플릿 모드", "Applet mode"},
    {"[A] 열기     [B] 종료     [X] 홈브루 메뉴     [Y] 옵션     ▲▼ 선택",
     "[A] Open     [B] Exit     [X] Homebrew menu     [Y] Options     ▲▼ Select"},
    {"[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ◀ ▶ 페이지  [-] 즐겨찾기  [X] 홈브루  [Y] 옵션",
     "[A] Play  [B] Categories  [L][R] Category  ◀ ▶ Page  [-] Favorite  [X] Homebrew  [Y] Options"},
    {"%s인", "%sP"},
    {"테마 선택", "Select theme"},
    {"기본 (배경 없음)", "Default (no background)"},
    {"[A] 적용   [B] 닫기   (테마 폴더: /JoyLauncher/themes)", "[A] Apply   [B] Close   (theme folder: /JoyLauncher/themes)"},
    {"영상 장면 추출 중  %d / %d", "Extracting video frames  %d / %d"},
    {"[B] 중지  (추출한 장면은 /JoyLauncher/cache 에 저장)", "[B] Stop  (frames are saved in /JoyLauncher/cache)"},
    {"영상 장면 %d개 추출", "Extracted %d video frames"},
    {", 실패 %d", ", %d failed"},
    {" (중지)", " (stopped)"},
    {"게임 목록을 읽는 중...", "Reading the game list..."},
    {"처음 한 번만 오래 걸립니다. 이후에는 저장된 목록을 바로 씁니다.",
     "This takes a while only the first time. After that the saved list is used right away."},
    {"[B] 종료", "[B] Exit"},
    {"[B] 닫기", "[B] Close"},
    {"[B] 취소", "[B] Cancel"},
    {"게임 목록이 없습니다", "No games found"},
    {"SD 카드의 /roms 폴더(또는 /JoyLauncher/roots.txt)를 확인하세요", "Check the /roms folder on the SD card (or /JoyLauncher/roots.txt)"},
    {"프로그램이 없습니다: ", "Program not found: "},
    {"실행 예약 실패", "Could not schedule the launch"},
    {"실행 설정(launch)이 없습니다", "This game has no launch setting"},
    {"업데이트 확인 중...", "Checking for updates..."},
    {"업데이트 확인 실패: ", "Update check failed: "},
    {"최신 버전입니다 (v%s)", "You have the latest version (v%s)"},
    {"v%s 내려받는 중  %.1f / %.1f MB", "Downloading v%s  %.1f / %.1f MB"},
    {"업데이트 실패: ", "Update failed: "},
    {"v%s 설치 완료. 다시 시작합니다...", "v%s installed. Restarting..."},
    {"설치는 끝났습니다. 런처를 다시 실행해 주세요", "Installed. Please start the launcher again"},
    {"새 버전 v%s: A 를 한 번 더 누르면 내려받아 설치합니다", "New version v%s: press A again to download and install"},
    {"게임 목록을 새로 읽었습니다 (%d개)", "Game list reloaded (%d games)"},
    {"확인: A 를 한 번 더 누르면 실행합니다", "Confirm: press A again to proceed"},
    {"홈브루 메뉴(sphaira / nx-hbmenu)를 찾을 수 없습니다", "Homebrew menu (sphaira / nx-hbmenu) not found"},
    {"새 버전 v%s 이 있습니다  ·  Y 옵션에서 설치", "New version v%s is available  ·  install it from Y Options"},
    // 실행 기록 (launch_log.txt)
    {"# Joy Launcher 실행 기록 (최근 %d개, 최신이 위)", "# Joy Launcher launch log (last %d, newest first)"},
    {"  카테고리: ", "  Category: "},
    {"  롬 파일: ", "  ROM file: "},
    {"  원래 코어: ", "  Original core: "},
    {"(메타데이터에 없음)", "(not in metadata)"},
    {"  선택 방법: ", "  Chosen by: "},
    {"  실행: ", "  Launch: "},
    {"(없음)", "(none)"},
    {"  결과: ", "  Result: "},
    {"실패: 실행 설정 없음 (", "Failed: no launch setting ("},
    {"실행 예약됨", "Launch scheduled"},
    {"실패: ", "Failed: "},
    // settings.txt 설명
    {"# 숨길 코어 (스위치 성능 범위 밖 기종). 비우면 모든 기종 표시", "# Cores to hide (systems beyond the Switch's performance). Empty = show all"},
    {"# 영상 장면: auto = 게임을 볼 때 자동 추출, manual = 메뉴의 일괄 추출만",
     "# Video frames: auto = extract when a game is viewed, manual = only from the batch menu item"},
    {"# 자동 업데이트 확인: on = 시작할 때 새 버전 확인 (설치는 옵션 창에서), off = 끔",
     "# Update check: on = check for a new version at start (install from Options), off = disabled"},
    {"# 언어: auto = 스위치 본체 언어, ko = 한국어, en = English", "# Language: auto = Switch system language, ko = Korean, en = English"},
    {"# BGM: /JoyLauncher/bgm 의 mp3·ogg. bgm = on/off, bgm_volume = 0~100, bgm_shuffle = on/off",
     "# BGM: mp3/ogg in /JoyLauncher/bgm. bgm = on/off, bgm_volume = 0-100, bgm_shuffle = on/off"},
    // 홈브루 메뉴 교체 (hbmenu.cpp)
    {"기타", "Other"},
    {"없음", "None"},
    {"Joy Launcher 복사 실패", "Could not copy Joy Launcher"},
    {"Joy Launcher 가 기본 메뉴입니다", "Joy Launcher is the default menu"},
    {"nx-hbmenu 백업 실패", "Could not back up nx-hbmenu"},
    {"sphaira 백업 실패", "Could not back up sphaira"},
    {"sphaira 설정 변경 실패", "Could not change the sphaira setting"},
    {"기존 메뉴 백업 실패", "Could not back up the current menu"},
    {"기본 메뉴로 설정 완료 (이전: %s)", "Set as the default menu (previous: %s)"},
    {"sphaira 로 되돌렸습니다", "Restored sphaira"},
    {"nx-hbmenu 로 되돌렸습니다", "Restored nx-hbmenu"},
    {"이전 메뉴로 되돌렸습니다", "Restored the previous menu"},
    {"/hbmenu.nro 를 바꾸지 못했습니다", "Could not replace /hbmenu.nro"},
    {"sphaira 가 없어 되돌릴 수 없습니다", "Cannot restore: sphaira is missing"},
    {"/switch/hbmenu.nro 가 없어 되돌릴 수 없습니다", "Cannot restore: /switch/hbmenu.nro is missing"},
    {"백업 파일이 없어 되돌릴 수 없습니다", "Cannot restore: no backup file"},
    {"되돌릴 메뉴 정보가 없습니다", "No menu to restore"},
    // 업데이트 (updater.cpp)
    {"curl 초기화 실패", "curl initialization failed"},
    {"네트워크 오류: ", "Network error: "},
    {"아직 배포된 버전이 없습니다", "No release has been published yet"},
    {"서버 응답 ", "Server response "},
    {"네트워크를 사용할 수 없습니다", "Network is not available"},
    {"릴리스 정보를 읽을 수 없습니다", "Could not read the release information"},
    {"내려받을 파일이 없습니다", "No file to download"},
    {"릴리스에 %s 가 없습니다", "The release has no %s"},
    {"임시 파일을 만들 수 없습니다", "Could not create a temporary file"},
    {"취소했습니다", "Cancelled"},
    {"내려받기 실패: ", "Download failed: "},
    {"내려받기 실패 (응답 %d)", "Download failed (response %d)"},
    {"파일 크기가 맞지 않습니다", "File size does not match"},
    {"검증 정보(SHA-256)가 없어 설치하지 않았습니다", "Not installed: no SHA-256 checksum"},
    {"검증 실패 (SHA-256 불일치)", "Verification failed (SHA-256 mismatch)"},
    {"파일 교체 실패", "Could not replace the file"},
    {"런처는 업데이트했지만 기본 홈브루 메뉴(/hbmenu.nro) 교체에 실패했습니다. "
     "옵션 창의 \"기본 홈브루 메뉴를 Joy Launcher 로 업데이트\" 로 다시 시도하세요",
     "The launcher was updated, but replacing the default homebrew menu (/hbmenu.nro) failed. "
     "Try again with \"Update the default homebrew menu\" in Options"},
    // 런처가 정한 기종 이름 (롬팩 메타데이터 없이 폴더로 추정한 카테고리)
    {"아케이드 (FBNeo)", "Arcade (FBNeo)"},
    {"아케이드 (MAME)", "Arcade (MAME)"},
    {"드림캐스트", "Dreamcast"},
    {"게임보이", "Game Boy"},
    {"패미컴", "Famicom"},
    {"슈퍼패미컴", "Super Famicom"},
    {"메가드라이브", "Mega Drive"},
    {"마스터시스템", "Master System"},
    {"게임기어", "Game Gear"},
    {"PC엔진", "PC Engine"},
    {"원더스완", "WonderSwan"},
    {"네오지오 포켓", "Neo Geo Pocket"},
    {"새턴", "Saturn"},
    {"아타리 링크스", "Atari Lynx"},
    {"버추얼보이", "Virtual Boy"},
};

// 실행 기록의 "선택 방법" 문구 조각
const char *const kHowParts[][2] = {
    {"메타데이터 없음, ", "no metadata, "},
    {"폴더 이름/확장자로 추정", "guessed from folder name/extension"},
    {"메타데이터의 nro 그대로", "nro from metadata as-is"},
    {"같은 이름 코어", "core with the same name"},
    {"코어 이름으로 기종 판단", "system judged from the core name"},
    {"MAME 롬셋 목록으로 선택", "chosen by the MAME romset list"},
};

const std::unordered_map<std::string, const char *> &Map() {
    static std::unordered_map<std::string, const char *> m = [] {
        std::unordered_map<std::string, const char *> r;
        for (const auto &p : kTable) r[p[0]] = p[1];
        return r;
    }();
    return m;
}

}  // namespace

void SetLang(Lang l) { gLang = l; }
Lang CurrentLang() { return gLang; }

Lang SystemLang() {
#ifdef __SWITCH__
    Lang out = Lang::Ko;
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        SetLanguage sl;
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &sl)))
            out = sl == SetLanguage_KO ? Lang::Ko : Lang::En;
        setExit();  // 서비스는 바로 닫는다 (다음 nro 로 새지 않게)
    }
    return out;
#else
    const char *l = getenv("JOY_LANG");
    if (!l) l = getenv("LANG");
    return l && strncmp(l, "ko", 2) == 0 ? Lang::Ko : Lang::En;
#endif
}

const char *T(const char *ko) {
    if (gLang == Lang::Ko || !ko) return ko;
    auto it = Map().find(ko);
    return it != Map().end() ? it->second : ko;
}

std::string T(const std::string &ko) {
    if (gLang == Lang::Ko) return ko;
    auto it = Map().find(ko);
    if (it != Map().end()) return it->second;
    // 실행 기록의 선택 방법처럼 여러 조각을 이어 붙인 문구
    std::string s = ko;
    for (const auto &p : kHowParts) {
        size_t pos = s.find(p[0]);
        if (pos != std::string::npos) s.replace(pos, strlen(p[0]), p[1]);
    }
    return s;
}

std::string Fmt(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}
