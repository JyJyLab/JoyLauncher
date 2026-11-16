// 테마 빌더 ↔ 런처 화면 코드 연결 (source/main.cpp 를 JOY_BUILDER 로 빌드하면 이 함수들이 생긴다)
#pragma once
#include <SDL.h>

#include <map>
#include <string>
#include <vector>

#include "../source/layout.h"

struct JoyCollectionInfo { std::string name, shortName, folder; int games = 0; };

// renderer: 미리보기를 그릴 렌더러. home: 설정·테마 폴더(themes/ 가 들어 있는 곳), roms: 시험용 롬 폴더
bool JoyInit(SDL_Renderer *r, const std::string &home, const std::string &roms, std::string &log);
// 롬 폴더 바꾸기 (게임 목록 다시 읽기)
void JoySetRoms(const std::string &roms, std::string &log);
// home/themes/<folder> 를 테마로 (그림 다시 읽기). "" = 테마 없음
void JoyLoadTheme(const std::string &folder);
// 지금 테마 위에 layout.txt 글을 다시 적용 (그림은 그대로)
void JoySetLayoutText(const std::string &text);
const Layout &JoyLayout();
void JoySetLang(bool en);
// 지금 렌더 대상(1280x720)에 화면 그리기. games: 게임 목록 화면
void JoyRender(bool games);
// 마지막으로 그린 화면의 요소 자리 ("categories.list", "games.list.title" …)
const std::map<std::string, SDL_Rect> &JoyBounds();
// 미리보기 조작: 12 ◀ 13 ▲ 14 ▶ 15 ▼, 0 열기(A), 1 뒤로(B), 20 L, 21 R, 30 즐겨찾기 토글(-)
void JoyNav(int button);
int JoyCategory();
int JoyGameIndex();
void JoySetCategory(int idx);
void JoySetGameIndex(int idx);
std::vector<JoyCollectionInfo> JoyCollections();
const char *JoyVersion();
