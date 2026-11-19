// 화면 공통 꾸밈 — 스위치판·안드로이드판이 같은 파일을 쓴다 (bgm.cpp 처럼).
//  ① 메뉴 창(옵션·BGM·테마 선택 등)의 색·둥근 모서리: 테마 색에 맞춰 정함
//  ② 화면 도움말의 "[X] <이름>": 판마다 X 버튼 기능이 달라(스위치 홈브루 / 안드로이드 BGM) 이름을 맞춤
#pragma once
#include <SDL.h>
#include <string>
#include <vector>
#include "layout.h"

struct MenuStyle {
    SDL_Color panel, border, title, text, dim, sel, selText, on, track;
    int radius = 14;    // 창 모서리 반지름
    int selRadius = 8;  // 선택 막대 모서리 반지름
    int borderW = 0;    // 테두리 굵기 (0 = 없음)
    bool themed = false;
};

// 메뉴 창 꾸밈. themed=false (테마 없음) 는 예전 색 그대로 + 둥근 모서리만.
// 테마는 [theme] color.menu / menu_border / menu_title / menu_text / menu_dim / menu_select / menu_select_text 를 쓰고,
// 없는 색은 테마 색(background_color·bar·text·dim)에서 계산한다 (비슷한 색의 테두리, 판과 대비되는 글자, 보색 제목).
void MenuStyleFrom(const Layout &l, bool themed, MenuStyle &out);

// 둥근 네모의 귀퉁이 그림: 지름 2R 의 원 (RGBA32, 4x4 표본으로 부드럽게). bw = 테두리 굵기(점), 안쪽은 fill
void RoundCornerPixels(int R, int bw, SDL_Color fill, SDL_Color border, std::vector<Uint8> &rgba);

// 글 안의 "[X]" 바로 뒤 이름(다음 [버튼] 또는 공백 두 칸 전까지)을 label 로 바꾼다.
// replaceAny=true: 어떤 이름이든 바꿈 (안드로이드: X = BGM · 소리 설정)
// replaceAny=false: BGM·소리 계열 이름일 때만 바꿈 (스위치: 안드로이드용으로 만든 테마의 "[X] BGM" → 홈브루, 테마 고유 이름은 그대로)
std::string FixXLabel(const std::string &s, const std::string &label, bool replaceAny);
