#include "esde.h"

#include <map>

namespace {
// ES-DE 기종 이름, 한글 이름, 영어 이름 (테마 빌더가 보여 주는 순서)
const std::vector<EsdeSystem> kSystems = {
    {"default", "기본(전체)", "Default"}, {"favorites", "즐겨찾기", "Favorites"}, {"hbfav", "홈브루 즐겨찾기", "Homebrew favorites"},
    {"arcade", "아케이드", "Arcade"}, {"arcade_action", "액션-아케이드", "Arcade (action)"},
    {"arcade_casual", "캐주얼-아케이드", "Arcade (casual)"}, {"arcade_fighting", "대전-아케이드", "Arcade (fighting)"},
    {"arcade_flying", "비행-아케이드", "Arcade (flying)"}, {"arcade_shooting", "슈팅-아케이드", "Arcade (shooting)"},
    {"neogeo", "네오지오", "Neo Geo"}, {"neogeocd", "네오지오 CD", "Neo Geo CD"}, {"cps1", "CPS1", "CPS1"}, {"cps2", "CPS2", "CPS2"},
    {"cps3", "CPS3", "CPS3"}, {"naomi", "나오미", "NAOMI"}, {"model2", "모델2", "Model 2"}, {"model3", "모델3", "Model 3"},
    {"nes", "패미컴", "Famicom / NES"}, {"fds", "디스크 시스템", "Famicom Disk System"}, {"snes", "슈퍼패미컴", "Super Famicom / SNES"},
    {"satellaview", "새틀라뷰", "Satellaview"}, {"n64", "닌텐도 64", "Nintendo 64"}, {"gc", "게임큐브", "GameCube"},
    {"gb", "게임보이", "Game Boy"}, {"gbc", "게임보이 컬러", "Game Boy Color"}, {"gba", "게임보이 어드밴스", "Game Boy Advance"},
    {"nds", "닌텐도 DS", "Nintendo DS"}, {"n3ds", "닌텐도 3DS", "Nintendo 3DS"}, {"virtualboy", "버추얼보이", "Virtual Boy"}, {"gameandwatch", "게임&워치", "Game & Watch"},
    {"pokemini", "포켓몬 미니", "Pokémon Mini"}, {"mastersystem", "마스터 시스템", "Master System"},
    {"megadrive", "메가드라이브", "Mega Drive"}, {"sega32x", "메가드라이브 32X", "32X"}, {"segacd", "메가 CD", "Mega-CD"},
    {"gamegear", "게임기어", "Game Gear"}, {"sg-1000", "SG-1000", "SG-1000"}, {"saturn", "새턴", "Saturn"}, {"dreamcast", "드림캐스트", "Dreamcast"},
    {"pcengine", "PC 엔진", "PC Engine"}, {"pcenginecd", "PC 엔진 CD", "PC Engine CD"}, {"supergrafx", "슈퍼그래픽스", "SuperGrafx"},
    {"pcfx", "PC-FX", "PC-FX"}, {"psx", "플레이스테이션", "PlayStation"}, {"ps2", "플레이스테이션 2", "PlayStation 2"},
    {"psp", "PSP", "PSP"}, {"wswan", "원더스완", "WonderSwan"}, {"wswanc", "원더스완 컬러", "WonderSwan Color"},
    {"ngp", "네오지오 포켓", "Neo Geo Pocket"}, {"ngpc", "네오지오 포켓 컬러", "Neo Geo Pocket Color"}, {"msx1", "MSX", "MSX"},
    {"msx2", "MSX2", "MSX2"}, {"pc98", "PC-98", "PC-98"}, {"x68000", "X68000", "X68000"}, {"dos", "DOS", "DOS"}, {"pc", "PC", "PC"},
    {"atari2600", "아타리 2600", "Atari 2600"}, {"atari7800", "아타리 7800", "Atari 7800"}, {"atarilynx", "아타리 링크스", "Atari Lynx"},
    {"atarijaguar", "재규어", "Atari Jaguar"}, {"3do", "3DO", "3DO"}, {"cdimono1", "CD-i", "CD-i"}, {"colecovision", "콜레코비전", "ColecoVision"},
    {"intellivision", "인텔리비전", "Intellivision"}, {"vectrex", "벡트렉스", "Vectrex"}, {"amiga", "아미가", "Amiga"},
    {"c64", "코모도어 64", "Commodore 64"}, {"zxspectrum", "ZX 스펙트럼", "ZX Spectrum"}, {"amstradcpc", "암스트래드 CPC", "Amstrad CPC"},
    {"pico8", "PICO-8", "PICO-8"}, {"openbor", "OpenBOR", "OpenBOR"}, {"ports", "포트", "Ports"}, {"switch", "스위치", "Switch"},
};

// 소문자 별칭 -> ES-DE 이름
const std::map<std::string, std::string> kAlias = {
    // 응애팩·페가수스 폴더 / 기본테마 그림 이름
    {"fc", "nes"}, {"famicom", "nes"}, {"fc hack", "nes"}, {"sfc", "snes"}, {"superfamicom", "snes"}, {"supernintendo", "snes"},
    {"sfc hack", "snes"}, {"sfc-msu1", "snes"}, {"md", "megadrive"}, {"genesis", "megadrive"}, {"md hack", "megadrive"},
    {"md-32x", "sega32x"}, {"32x", "sega32x"}, {"sega-32x", "sega32x"}, {"md-cd", "segacd"}, {"sega-cd", "segacd"}, {"megacd", "segacd"},
    {"sms", "mastersystem"}, {"sega-ms", "mastersystem"}, {"mark3", "mastersystem"}, {"gg", "gamegear"}, {"pce", "pcengine"},
    {"tg16", "pcengine"}, {"turbografx16", "pcengine"}, {"pce-cd", "pcenginecd"}, {"pcecd", "pcenginecd"}, {"tg-cd", "pcenginecd"},
    {"pc-fx", "pcfx"}, {"ps1", "psx"}, {"ps", "psx"}, {"playstation", "psx"}, {"play station", "psx"}, {"ws", "wswan"},
    {"wonderswan", "wswan"}, {"wsc", "wswanc"}, {"wonderswancolor", "wswanc"}, {"neogeopocket", "ngp"}, {"neogeopocketcolor", "ngpc"},
    {"msx", "msx1"}, {"pc-98", "pc98"}, {"msdos", "dos"}, {"poke mini", "pokemini"}, {"gameboy", "gb"}, {"gameboycolor", "gbc"},
    {"gameboyadvance", "gba"}, {"ss", "saturn"}, {"dc", "dreamcast"}, {"ngc", "gc"}, {"gamecube", "gc"}, {"vb", "virtualboy"},
    {"game watch", "gameandwatch"}, {"3ds", "n3ds"}, {"nintendo 3ds", "n3ds"}, {"닌텐도 3ds", "n3ds"},
    {"playstation 2", "ps2"}, {"sony playstation 2", "ps2"}, {"플레이스테이션 2", "ps2"}, {"playstation2", "ps2"}, {"gw", "gameandwatch"}, {"lynx", "atarilynx"}, {"atari", "atari2600"}, {"cd-i", "cdimono1"},
    {"cdi", "cdimono1"}, {"pico-8", "pico8"}, {"neogeo-cd", "neogeocd"}, {"fba", "arcade"}, {"fbneo", "arcade"}, {"mame", "arcade"},
    {"mame2003plus", "arcade"}, {"mame2010", "arcade"}, {"fbalpha2012", "arcade"}, {"all", "default"}, {"main", "default"},
    // 응애팩 카테고리 / 런처가 붙이는 이름 (한글)
    {"액션-아케이드", "arcade_action"}, {"캐주얼-아케이드", "arcade_casual"}, {"대전-아케이드", "arcade_fighting"},
    {"비행-아케이드", "arcade_flying"}, {"슈팅-아케이드", "arcade_shooting"}, {"아케이드", "arcade"}, {"아케이드 (fbneo)", "arcade"},
    {"아케이드 (mame)", "arcade"}, {"아케이드 (fba2012)", "arcade"}, {"전체게임", "default"}, {"모든게임", "default"},
    {"즐겨찾기", "favorites"}, {"홈브루 즐겨찾기", "hbfav"}, {"패미컴", "nes"}, {"패미컴 디스크 시스템", "fds"}, {"디스크 시스템", "fds"},
    {"슈퍼패미컴", "snes"}, {"게임보이", "gb"}, {"게임보이 컬러", "gbc"}, {"게임보이 어드밴스", "gba"}, {"닌텐도 ds", "nds"},
    {"메가드라이브", "megadrive"}, {"메가드라이브 32x", "sega32x"}, {"메가 cd", "segacd"}, {"마스터시스템", "mastersystem"},
    {"마스터 시스템", "mastersystem"}, {"게임기어", "gamegear"}, {"pc엔진", "pcengine"}, {"pc 엔진", "pcengine"},
    {"pc 엔진 cd", "pcenginecd"}, {"원더스완", "wswan"}, {"원더스완 컬러", "wswanc"}, {"네오지오", "neogeo"},
    {"네오지오 포켓", "ngp"}, {"네오지오 포켓 컬러", "ngpc"}, {"플레이스테이션", "psx"}, {"새턴", "saturn"}, {"드림캐스트", "dreamcast"},
    {"버추얼보이", "virtualboy"}, {"포켓몬 미니", "pokemini"}, {"아타리 링크스", "atarilynx"},
};
}  // namespace

const char *EsdeSystemName(const std::string &lowerName) {
    auto it = kAlias.find(lowerName);
    if (it != kAlias.end()) return it->second.c_str();
    for (const auto &s : kSystems)
        if (lowerName == s.id) return s.id;
    return "";
}

const std::vector<EsdeSystem> &EsdeSystems() { return kSystems; }
