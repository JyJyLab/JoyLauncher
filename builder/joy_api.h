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
// 지금 테마 위에 layout.txt 글을 다시 적용 (그림은 그대로). square: layout_1x1.txt 글 (nullptr = 파일 없음)
// 화면비(JoySetAspect)에 맞춰 구성을 고른다: 1:1 이면 square → 없으면 내장 1:1 또는 16:9 그대로, 16:10·4:3 은 폭에 맞춤
void JoySetLayoutText(const std::string &text, const std::string *square = nullptr);
// 미리보기 화면비 (가로/세로, 기본 16:9). 바꾼 뒤 JoySetLayoutText 를 다시 부를 것
void JoySetAspect(float aspect);
// 지금 미리보기 캔버스 크기 (16:9 = 1280x720, 4:3 = 960x720, 1:1 = 720x720 …)
void JoyCanvas(int &w, int &h);
// 마지막으로 적용한 구성 파일 ("layout.txt" / "layout_1x1.txt" / "" = 내장)
const std::string &JoyLayoutFile();
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
// 보기 탭 (0 전체 / 1 즐겨찾기 / 2 최근 실행)
int JoyView();
// home/categories.txt 와 내 그림 폴더(home/categories) 다시 읽기 (게임 목록은 그대로)
void JoyReloadCategories();
// 나만의 카테고리를 적용하기 전의 원래 카테고리
std::vector<JoyCollectionInfo> JoyBaseCollections();
