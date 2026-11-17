// 앱 즐겨찾기 (스위치판 "홈브루 즐겨찾기" 자리): 설치된 안드로이드 앱을 골라 카테고리로 보여 주고 실행한다.
#pragma once
#include <string>
#include <vector>

#include "pegasus.h"

// 실행할 수 있는 앱 전부를 목록 항목으로 (Game::file = "app://패키지/액티비티", launch = kLaunchApp).
// 앱 목록 창에서 쓰므로 아이콘은 만들지 않는다
std::vector<Game> AppsScan();
// "앱 즐겨찾기" 카테고리: favs = "패키지/액티비티" 들 (/JoyLauncher/app_favorites.txt).
// 지워진 앱은 목록에서만 빠진다 (기록은 그대로). 아이콘은 cacheDir 에 png 로
Collection AppsFavorites(const std::vector<std::string> &favs, const std::string &cacheDir);
// "app://패키지/액티비티" → "패키지/액티비티" (앱 항목이 아니면 "")
std::string AppsKey(const std::string &file);
// 앱 항목 실행
bool AppsLaunch(const Game &g, std::string &err);
