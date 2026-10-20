// 페가수스 metadata.pegasus.txt 파서 (Joy Launcher 용 최소 구현)
#pragma once
#include <map>
#include <string>
#include <vector>

struct Game {
    std::string title;
    std::string file;        // 절대 경로 (SD 기준)
    std::string launch;      // 실행 템플릿 (컬렉션 기본값 또는 게임별 값)
    std::string developer;
    std::string genre;
    std::string release;
    std::string players;
    std::string description;
    std::string boxart;      // 박스 이미지 절대 경로 (없으면 빈 값)
    std::string screenshot;  // 게임 화면 사진(스크린샷·타이틀) 절대 경로 (없으면 빈 값)
    std::string video;       // 화면 사진이 없을 때 장면을 뽑을 미리보기 영상 (없으면 빈 값)
    std::string sortKey;
    std::string metaCore;    // 메타데이터에 적힌 원래 코어(앱) 이름 (실행 기록용)
    std::string how;         // 실행 코어를 정한 방법 (실행 기록용)
};

struct Collection {
    std::string name;
    std::string sortBy;      // sort-by 값 (없으면 이름)
    std::string shortName;   // shortname 값 (테마 배경·로고 이름에 사용, 없으면 빈 값)
    bool virt = false;       // 런처가 만든 카테고리 (즐겨찾기). 목록 캐시에 저장하지 않음
    std::vector<std::string> folders;  // 게임이 들어 있는 롬 폴더 이름들 (/roms/<이름>)
    std::vector<Game> games;
};

// rootDirs 각각의 아래 metadata.pegasus.txt 를 (2단계 깊이까지) 읽어 컬렉션 목록을 만든다.
// 같은 collection 이름은 하나로 합친다. 파일이 실제로 없는 게임은 제외한다.
// 이 코어로 실행되는 게임은 목록에서 뺀다 (코어 이름, 예: "flycast"). 기본: N64·NDS·새턴·드림캐스트/NAOMI 코어
// /JoyLauncher/cores.txt 읽기·정리 ([auto] 다시 쓰기, [user] 보존). LoadLibrary 전에 호출
void LoadCoreConfig(const std::string &path);
void SetHiddenCores(const std::vector<std::string> &cores);
std::vector<Collection> LoadLibrary(const std::vector<std::string> &rootDirs, std::string &log);

// 게임 목록 저장/불러오기. 서명(sig)이 같을 때만 저장된 목록을 쓴다.
std::string LibrarySignature(const std::vector<std::string> &rootDirs);
bool SaveLibraryCache(const std::string &path, const std::string &sig, const std::vector<Collection> &cols);
bool LoadLibraryCache(const std::string &path, const std::string &sig, std::vector<Collection> &cols);

// launch 템플릿을 실행 파일 경로와 인자 문자열로 나눈다. {file.path} 를 치환한다.
bool BuildLaunch(const Game &g, std::string &nroPath, std::string &argString);
