#include "i18n.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "esde.h"
#include "platform.h"

namespace {

Lang gLang = Lang::Ko;

// { 한국어, English }
const char *const kTable[][2] = {
    // 옵션 창·메뉴
    {"%s 로 되돌렸습니다", "Restored %s"},
    {"게임 목록 새로 고침 (롬·그림·영상을 넣거나 뺐을 때)", "Refresh game list (after adding or removing ROMs, images, videos)"},
    {"업데이트 확인 (현재 v%s)", "Check for updates (current v%s)"},
    {"테마 설정 (현재: %s)", "Theme (current: %s)"},
    {"기본", "Default"},
    {"언어 / Language: %s", "Language / 언어: %s"},
    {"자동 (%s)", "Auto (%s)"},
    {"화면 효과 (움직임·전환): %s", "Screen effects (motion, transitions): %s"},
    {"# 화면 효과: on = 테마의 움직임·화면 전환 사용, off = 끔", "# Screen effects: on = use theme motion and transitions, off = none"},
    {"★ 즐겨찾기", "★ Favorites"},
    {"[A] 적용   [X] 삭제   [B] 닫기   (테마 폴더: /JoyLauncher/themes)", "[A] Apply   [X] Delete   [B] Close   (theme folder: /JoyLauncher/themes)"},
    {"'%s' 테마를 지울까요?", "Delete the theme '%s'?"},
    {"[A] 삭제   [B] 취소", "[A] Delete   [B] Cancel"},
    {"테마를 지웠습니다: %s", "Theme deleted: %s"},
    {"테마를 다 지우지 못했습니다: %s", "Could not delete the whole theme: %s"},
    {"켜짐 (On)", "On"},
    {"꺼짐 (Off)", "Off"},
    {"최근 실행", "Recently played"},
    {"즐겨찾기한 게임이 없습니다", "No favorite games here"},
    {"실행한 게임이 없습니다", "No played games here"},
    {"이 테마는 Joy Launcher v%s 이상이 필요합니다 (지금 v%s)", "This theme needs Joy Launcher v%s or later (now v%s)"},
    {"즐겨찾기", "Favorites"},
    {"[A] 또는 [-] 넣기 / 빼기   [B] 닫기   (%d개)", "[A] or [-] Add / remove   [B] Close   (%d)"},
    {"즐겨찾기에 넣었습니다", "Added to favorites"},
    {"즐겨찾기에서 뺐습니다", "Removed from favorites"},
    {"BGM · 소리 설정", "BGM / sound"},
    {"BGM · 소리 설정 (%s · 볼륨 %d%%)", "BGM / sound (%s · volume %d%%)"},
    {"BGM · 소리 설정 (%s)", "BGM / sound (%s)"},
    {"공통 BGM", "Common BGM"},
    {"테마 BGM", "Theme BGM"},
    {"영상 소리", "Video sound"},
    {"무음", "Silent"},
    {"공통 BGM (%d곡)  /JoyLauncher/bgm", "Common BGM (%d songs)  /JoyLauncher/bgm"},
    {"테마 BGM (%d곡)  themes/%s/bgm", "Theme BGM (%d songs)  themes/%s/bgm"},
    {"테마 BGM (테마 없음)", "Theme BGM (no theme in use)"},
    {"영상 소리 (미리보기 영상)", "Video sound (preview videos)"},
    {"BGM 볼륨: %d%%", "BGM volume: %d%%"},
    {"BGM 순서: 차례대로", "BGM order: In order"},
    {"BGM 순서: 무작위", "BGM order: Shuffle"},
    {"/JoyLauncher/bgm 폴더에 mp3 또는 ogg 파일을 넣으세요", "Put mp3 or ogg files in /JoyLauncher/bgm"},
    {"/JoyLauncher/themes/%s/bgm 폴더에 mp3 또는 ogg 파일을 넣으세요", "Put mp3 or ogg files in /JoyLauncher/themes/%s/bgm"},
    {"테마를 쓰지 않고 있습니다 (테마 BGM 은 테마 폴더의 bgm 에 넣습니다)", "No theme is in use (Theme BGM goes in the bgm folder of a theme)"},
    {"다음 곡", "Next song"},
    {"기본 테마 %d종 다시 받기 (최신으로)", "Download the %d stock themes again"},
    {"기본 테마 %d종 내려받기", "Download the %d stock themes"},
    {"%s 내려받는 중 (%d/%d)  %.1f / %.1f MB", "Downloading %s (%d/%d)  %.1f / %.1f MB"},
    {"기본 테마 %d종 받음 (전체 %d종) · 실패: ", "Got %d of the %d stock themes · failed: "},
    {"  (사용 중)", "  (in use)"},
    {"PC에서 테마 받기 (FTP)", "Receive themes from PC (FTP)"},
    {"FTP 를 열지 못했습니다: ", "Could not open FTP: "},
    {"FTP: 받은 파일 %d개 · 지운 것 %d개 — 테마를 다시 읽었습니다", "FTP: %d files received · %d removed — themes reloaded"},
    {"PC 를 기다리는 중…", "Waiting for the PC…"},
    {"PC 연결됨", "PC connected"},
    {"[B] 닫기 (받은 테마를 다시 읽음) · 이 화면이 열린 동안만 FTP 가 열립니다", "[B] Close (reload received themes) · FTP is open only while this screen is shown"},
    {"데모: 실행 대신 알림 — ", "Demo: no launch — "},
    {"받은 파일 %d개 · 지운 것 %d개", "%d files received · %d removed"},
    {"테마 빌더 → 파일 → 스위치로 보내기 에 위 주소·포트를 적으세요 (사용자·비밀번호 비움)", "In Theme Builder → File → Send to Switch, enter the address and port above (leave user and password empty)"},
    {"최신 릴리스 확인 중...", "Checking the latest release..."},
    {"내려받은 파일을 열 수 없습니다", "Could not open the downloaded file"},
    {"압축 파일이 손상되었습니다", "The archive is damaged"},
    {"압축 해제 실패", "Could not unpack the archive"},
    {"지원하지 않는 압축 방식입니다", "Unsupported compression method"},
    {"테마 파일을 쓸 수 없습니다", "Could not write theme file"},
    {"받은 파일에 테마가 없습니다", "The download contains no theme"},
    {"릴리스에 이 테마가 없습니다", "The release does not have this theme"},
    {"런처를 종료합니다", "Exit the launcher?"},
    {"[A] 종료   [B] 돌아가기", "[A] Exit   [B] Back"},
    {"옵션", "Options"},
    {"[A] 선택   [B] 닫기", "[A] Select   [B] Close"},
    {"[A] 선택   ◀ ▶ 조절   [B] 닫기", "[A] Select   ◀ ▶ Adjust   [B] Close"},
    // 화면
    {"테마 layout.txt 에 문제가 %d건 있습니다\n테마 폴더의 layout_errors.txt 를 확인하세요", "The theme's layout.txt has %d problem(s)\nSee layout_errors.txt in the theme folder"},
    {"%s인", "%sP"},
    {"테마 선택", "Select theme"},
    {"기본 (배경 없음)", "Default (no background)"},
    {"게임 목록을 읽는 중...", "Reading the game list..."},
    {"처음 한 번만 오래 걸립니다. 이후에는 저장된 목록을 바로 씁니다.", "This takes a while only the first time. After that the saved list is used right away."},
    {"[B] 닫기", "[B] Close"},
    {"[B] 취소", "[B] Cancel"},
    {"게임 목록이 없습니다", "No games found"},
    {"프로그램이 없습니다: ", "Program not found: "},
    {"업데이트 확인 중...", "Checking for updates..."},
    {"최신 버전입니다 (v%s)", "You have the latest version (v%s)"},
    {"게임 목록을 새로 읽었습니다 (%d개)", "Game list reloaded (%d games)"},
    // 실행 기록 (launch_log.txt)
    {"# Joy Launcher 실행 기록 (최근 %d개, 최신이 위)", "# Joy Launcher launch log (last %d, newest first)"},
    {"  카테고리: ", "  Category: "},
    {"  롬 파일: ", "  ROM file: "},
    {"(메타데이터에 없음)", "(not in metadata)"},
    {"  선택 방법: ", "  Chosen by: "},
    {"  실행: ", "  Launch: "},
    {"(없음)", "(none)"},
    {"  결과: ", "  Result: "},
    {"실패: ", "Failed: "},
    // settings.txt 설명
    {"# 미리보기 영상: on = 고른 게임에 영상이 있으면 화면 사진 자리에서 재생, off = 끔",
     "# Preview videos: on = play the selected game's video in place of the screenshot, off = disabled"},
    {"미리보기 영상 재생: %s", "Preview videos: %s"},
    {"# 자동 업데이트 확인: on = 시작할 때 새 버전 확인 (설치는 옵션 창에서), off = 끔", "# Update check: on = check for a new version at start (install from Options), off = disabled"},
    {"# 소리 (하나만 남): common = 공통 BGM (/JoyLauncher/bgm 의 mp3·ogg), theme = 테마 BGM (지금 테마 폴더의 bgm), video = 미리보기 영상의 소리, off = 무음",
     "# Sound (one at a time): common = Common BGM (mp3/ogg in /JoyLauncher/bgm), theme = Theme BGM (bgm folder of the current theme), video = sound of preview videos, off = silent"},
    {"# BGM: bgm_volume = 0~100, bgm_shuffle = on/off (무작위)", "# BGM: bgm_volume = 0-100, bgm_shuffle = on/off"},
    // 홈브루 메뉴 교체 (hbmenu.cpp)
    {"Joy Launcher 복사 실패", "Could not copy Joy Launcher"},
    // 업데이트 (updater.cpp)
    {"네트워크 오류: ", "Network error: "},
    {"아직 배포된 버전이 없습니다", "No release has been published yet"},
    {"서버 응답 ", "Server response "},
    {"릴리스 정보를 읽을 수 없습니다", "Could not read the release information"},
    {"내려받을 파일이 없습니다", "No file to download"},
    {"릴리스에 %s 가 없습니다", "The release has no %s"},
    {"취소했습니다", "Cancelled"},
    {"내려받기 실패: ", "Download failed: "},
    {"내려받기 실패 (응답 %d)", "Download failed (response %d)"},
    {"파일 크기가 맞지 않습니다", "File size does not match"},
    {"검증 정보(SHA-256)가 없어 설치하지 않았습니다", "Not installed: no SHA-256 checksum"},
    {"검증 실패 (SHA-256 불일치)", "Verification failed (SHA-256 mismatch)"},
    // 런처가 정한 기종 이름 (롬팩 메타데이터 없이 폴더로 추정한 카테고리)
    {"아케이드 (FBA2012)", "Arcade (FBA2012)"},
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
    // 안드로이드판
    {"[A] 열기     [B] 종료     [X] BGM     [Y] 옵션     ▲▼ 선택", "[A] Open     [B] Exit     [X] BGM     [Y] Options     ▲▼ Select"},
    {"[A] 열기     [B] 종료     [X] BGM     [Y] 옵션     ▲▼◀▶ 선택", "[A] Open     [B] Exit     [X] BGM     [Y] Options     ▲▼◀▶ Select"},
    {"[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ◀ ▶ 페이지  [-] 즐겨찾기  [X] BGM  [Y] 옵션", "[A] Play  [B] Categories  [L][R] Category  ◀ ▶ Page  [-] Favorite  [X] BGM  [Y] Options"},
    {"[A] 실행  [B] 카테고리  [L][R] 카테고리 이동  ▲▼◀▶ 선택  [-] 즐겨찾기  [X] BGM  [Y] 옵션", "[A] Play  [B] Categories  [L][R] Category  ▲▼◀▶ Select  [-] Favorite  [X] BGM  [Y] Options"},
    {"[B] 종료     [X] BGM     [Y] 옵션", "[B] Exit     [X] BGM     [Y] Options"},
    {"저장소의 ROMs (또는 Roms·roms) 폴더에 게임을 넣거나, /JoyLauncher/roots.txt 에 롬 폴더를 적으세요", "Put games in the ROMs (or Roms / roms) folder of the storage, or list your ROM folders in /JoyLauncher/roots.txt"},
    {"페가수스(game_dirs.txt)·ES-DE 의 롬 폴더는 자동으로 찾습니다", "ROM folders of Pegasus (game_dirs.txt) and ES-DE are found automatically"},
    {"앱 즐겨찾기", "App Favorites"},
    {"앱", "Apps"},
    {"앱 목록 · 앱 즐겨찾기 추가·빼기 (%d개)", "Apps · add / remove App Favorites (%d)"},
    {"[A] 실행   [-] 앱 즐겨찾기에 넣기 / 빼기   [B] 닫기   (즐겨찾기 %d개)", "[A] Open   [-] Add to / remove from App Favorites   [B] Close   (%d favorites)"},
    {"앱 즐겨찾기에서 뺐습니다", "Removed from App Favorites"},
    {"실행할 수 있는 앱을 찾지 못했습니다", "No apps found"},
    {"앱을 실행하지 못했습니다: ", "Could not open the app: "},
    {"에뮬레이터 선택", "Choose emulator"},
    {"에뮬레이터 선택 (지금 카테고리)", "Choose emulator (this category)"},
    {"대상: %s  ·  기종: %s", "For: %s  ·  system: %s"},
    {"자동 (롬 모음의 설정 → 설치된 에뮬레이터)", "Auto (ROM pack setting → installed emulator)"},
    {"  (설치 안 됨)", "  (not installed)"},
    {"[A] 선택   [B] 닫기   (기록: /JoyLauncher/emulators.txt)", "[A] Select   [B] Close   (saved in /JoyLauncher/emulators.txt)"},
    {"%s: 자동으로 고릅니다", "%s: chosen automatically"},
    {"%s: %s 로 실행합니다", "%s: will run with %s"},
    {"게임이 있는 카테고리(또는 게임)를 고른 뒤 여세요", "Open this on a category (or game) that has games"},
    {"이 카테고리의 기종을 알 수 없어 고를 수 없습니다\n/JoyLauncher/emulators.txt 에 직접 적을 수 있습니다", "The system of this category is unknown, so there is nothing to choose\nYou can write it in /JoyLauncher/emulators.txt"},
    {"A·B 버튼 바꾸기 (선택 ↔ 취소): %s", "Swap A and B (select ↔ cancel): %s"},
    {"화면 컨트롤러 (터치): %s", "On-screen controller (touch): %s"},
    {"자동 (컨트롤러가 없을 때)", "Auto (when no controller is connected)"},
    {"항상 표시", "Always"},
    {"끔", "Off"},
    {"종료", "Exit"},
    {"# 언어: auto = 기기 언어, ko = 한국어, en = English", "# Language: auto = device language, ko = Korean, en = English"},
    {"# A·B 버튼 바꾸기: off = A 선택·B 취소, on = B 선택·A 취소", "# Swap A and B: off = A selects / B cancels, on = B selects / A cancels"},
    {"# 화면 컨트롤러(터치): auto = 컨트롤러가 없을 때, on = 항상, off = 끔", "# On-screen controller (touch): auto = when no controller is connected, on = always, off = never"},
    {"/JoyLauncher/themes/%s 폴더가 삭제됩니다", "The folder /JoyLauncher/themes/%s will be deleted"},
    {"기본 테마를 준비하는 중...", "Preparing the stock themes..."},
    {"기본 테마 %d종을 받았습니다 (파일 %d개) · 테마를 골라 [A] 로 적용합니다", "Downloaded the %d stock themes (%d files) · pick one and press [A]"},
    {"새 버전 v%s 이 있습니다  ·  [Y] 옵션 → 새 버전 받기", "New version v%s is available  ·  [Y] Options → Get new version"},
    {"새 버전 v%s 받기 (현재 v%s)", "Get new version v%s (current v%s)"},
    {"새 버전 v%s 이 있습니다", "New version v%s is available"},
    {"업데이트 확인 실패", "Update check failed"},
    {"지금 버전: v%s", "Installed version: v%s"},
    {"내려받는 곳 (브라우저 주소)", "Download page (open in a browser)"},
    {"페이지의 JoyLauncher.apk 를 받아 직접 설치하세요", "Download JoyLauncher.apk from the page and install it yourself"},
    {"덮어 설치하면 /JoyLauncher 의 설정·테마는 그대로입니다", "Installing over the old version keeps settings and themes in /JoyLauncher"},
    {"처음이면 브라우저(또는 파일 앱)에 '출처를 알 수 없는 앱 설치' 허용이 필요할 수 있습니다", "The first time, your browser (or file app) may need permission to install unknown apps"},
    {"[A] 브라우저로 열기   [B] 닫기", "[A] Open in browser   [B] Close"},
    {"브라우저를 열지 못했습니다. 주소를 직접 입력하세요", "Could not open a browser. Type the address yourself"},
    {"저장소의 모든 파일에 접근할 권한이 필요합니다", "Access to all files on the storage is required"},
    {"롬 폴더를 읽고, 저장소의 JoyLauncher 폴더에 설정·테마를 두기 위해서입니다", "It is needed to read your ROM folders and to keep settings and themes in the JoyLauncher folder of the storage"},
    {"아무 버튼이나 화면을 누르면 설정 화면이 열립니다 → Joy Launcher 를 허용한 뒤 돌아오세요", "Press any button or the screen to open the settings → allow Joy Launcher, then come back"},
    {"뒤로 가기 = 종료", "Back = exit"},
    // 게임 실행
    {"%s 실행 중…", "Starting %s…"},
    {"실행하지 못했습니다 (%s)", "Could not start (%s)"},
    {"실행함", "Started"},
    {"실행 안 함: ", "Not started: "},
    {"  목록의 에뮬레이터: ", "  Emulator in metadata: "},
    {"%s 에뮬레이터가 설치되어 있지 않습니다 (지원: %s)", "No %s emulator is installed (supported: %s)"},
    {"에뮬레이터가 설치되어 있지 않습니다: %s", "The emulator is not installed: %s"},
    {"앱이 설치되어 있지 않습니다: %s", "The app is not installed: %s"},
    {"앱 정보 파일이 비었거나 읽을 수 없습니다", "The app info file is empty or unreadable"},
    {"이 게임을 실행할 에뮬레이터를 알 수 없습니다 (emulators.txt 에 롬 폴더 이름으로 정해 주세요)", "No emulator is known for this game (set one for its ROM folder in emulators.txt)"},
    {"실행 명령의 %s 가 잘못되었습니다", "The launch command has a bad %s"},
    {"emulators.txt 에서 실행하지 않도록 정한 폴더입니다", "emulators.txt says not to run games in this folder"},
    {"emulators.txt 의 실행 명령은 am start 로 시작해야 합니다", "A launch command in emulators.txt must start with am start"},
    {"emulators.txt 의 에뮬레이터 이름을 찾지 못했습니다: %s", "Emulator name in emulators.txt not found: %s"},
    {"기종을 알 수 없어 emulators.txt 의 에뮬레이터 이름을 쓸 수 없습니다 (am start … 명령으로 적어 주세요)", "The system is unknown, so an emulator name in emulators.txt cannot be used (write an am start … command instead)"},
};

// 실행 기록의 "선택 방법" 문구 조각
const char *const kHowParts[][2] = {
    {"메타데이터 없음, ", "no metadata, "},
    {"메타데이터 실행 명령 그대로", "launch command from metadata as-is"},
    {"메타데이터의 실행 명령", "launch command from metadata"},
    {"메타데이터의 앱이 없어 같은 에뮬레이터의 다른 앱으로", "metadata app missing, using another build of the same emulator"},
    {"메타데이터의 코어 이름으로 RetroArch 실행", "RetroArch with the core named in metadata"},
    {"메타데이터의 앱(", "metadata app ("},
    {")이 없어 기종의 에뮬레이터 목록에서 고름", ") missing, chosen from the system's emulator list"},
    {"기종의 에뮬레이터 목록에서 고름", "chosen from the system's emulator list"},
    {"폴더 이름으로 기종 판단", "system judged from the folder name"},
    {"카테고리 이름으로 기종 판단", "system judged from the category name"},
    {"확장자로 기종 판단", "system judged from the file extension"},
};

const std::unordered_map<std::string, const char *> &Map() {
    static std::unordered_map<std::string, const char *> m = [] {
        std::unordered_map<std::string, const char *> r;
        // 기종 이름 (ES-DE 목록으로 만든 카테고리): 한글 이름 → 영어 이름. 아래 표에 같은 말이 있으면 그쪽이 우선
        for (const auto &e : EsdeSystems()) r[e.ko] = e.en;
        for (const auto &p : kTable) r[p[0]] = p[1];
        return r;
    }();
    return m;
}

}  // namespace

void SetLang(Lang l) { gLang = l; }
Lang CurrentLang() { return gLang; }

Lang SystemLang() { return plat::SystemKorean() ? Lang::Ko : Lang::En; }

static const std::map<std::string, std::string> *gOvKo = nullptr, *gOvEn = nullptr;
void SetStringOverrides(const std::map<std::string, std::string> *ko, const std::map<std::string, std::string> *en) {
    gOvKo = ko && !ko->empty() ? ko : nullptr;
    gOvEn = en && !en->empty() ? en : nullptr;
}
// 영어 화면: [strings.en], 한국어 화면: [strings]
const char *StringOverride(const char *key) {
    const auto *m = gLang == Lang::En ? gOvEn : gOvKo;
    if (!key || !m) return nullptr;
    auto it = m->find(key);
    return it != m->end() ? it->second.c_str() : nullptr;
}

const char *T(const char *ko) {
    if (const char *o = StringOverride(ko)) return o;
    if (gLang == Lang::Ko || !ko) return ko;
    auto it = Map().find(ko);
    return it != Map().end() ? it->second : ko;
}

std::string T(const std::string &ko) {
    if (const char *o = StringOverride(ko.c_str())) return o;
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
