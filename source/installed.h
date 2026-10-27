// SD 의 홈브루(nro): 옵션의 홈브루 즐겨찾기 창 목록과 "홈브루 즐겨찾기" 카테고리
#pragma once
#include <cstdint>
#include <string>

#include "pegasus.h"

// Game::launch 값: 롬 대신 이 항목 자체를 실행
static const char *const kLaunchNro = "@nro";      // Game::file = nro 의 SD 경로

// /switch 아래 nro (두 단계 깊이, 점으로 시작하는 폴더 제외). 이름·제작자·버전은 NACP 에서,
// 아이콘은 cacheDir 에 jpg 로 풀어 둔다. selfNro(런처 자신)와 /hbmenu.nro 는 뺀다 (홈브루 즐겨찾기 창의 목록)
Collection ScanHomebrew(const std::string &cacheDir, const std::string &selfNro);

// "홈브루 즐겨찾기": 사용자가 옵션에서 고른 nro 들 (/JoyLauncher/homebrew_favorites.txt). 없는 파일은 빼고, 이름순
Collection BuildHomebrewFavorites(const std::vector<std::string> &paths, const std::string &cacheDir);
