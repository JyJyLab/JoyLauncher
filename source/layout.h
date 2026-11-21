// 테마 화면 구성 (layout.txt): 카테고리·게임 목록 화면을 요소(그림·글자·목록 등)의 모음으로 표현한다.
// 기본 구성(내장)을 먼저 읽고, 테마 폴더의 layout.txt 가 있으면 그 위에 덮어쓴다.
#pragma once
#include <map>
#include <string>
#include <vector>

struct LColor {
    std::string name;  // 색 이름 (text, dim, accent, selected, selection, bar, 또는 테마가 정한 이름). "" = 직접 값
    unsigned char r = 255, g = 255, b = 255, a = 255;
};

enum class ElemType { Rect, Image, Text, List, Preview, Media, Info, Battery, Pager };

struct Elem {
    std::string name;
    // 확장 (v1.1): 조건부 표시 — visible_if 를 채운 결과가 비었거나 "0" 이면 숨김, hidden_if 는 반대
    std::string visibleIf, hiddenIf;
    // 목록 칸·줄 안 요소 ([화면.<목록>.<이름>]): rect 는 칸 기준. state 0 항상 / 1 선택된 칸만 / 2 선택 안 된 칸만
    std::vector<Elem> children;
    int state = 0;
    bool hasSelColor = false;  // 칸 안 글자: 선택된 칸에서 selected_color 사용
    ElemType type = ElemType::Rect;
    bool visible = true;
    int z = 20;
    int x = 0, y = 0, w = 0, h = 0;
    bool hasRect = false;
    LColor color;

    // text
    std::string text;      // {자리표} 포함 가능
    bool bold = false;
    int size = 22;
    int align = 0;         // 0 왼쪽 / 1 가운데 / 2 오른쪽
    int valign = 0;        // 0 위 / 1 가운데 / 2 아래 (h 가 있을 때)
    bool wrap = false;     // 여러 줄 (w 폭으로 줄바꿈, h 높이로 자름)
    bool scroll = true;    // 넘치면 흐르게
    bool logo = false;     // 카테고리 로고가 있으면 글자 대신 로고
    int lx = 0, ly = 0, lw = 0, lh = 0;

    // image
    std::string file;      // 테마 폴더 기준 파일
    std::string source;    // background / logo / icon / boxart / screenshot
    std::string folder;    // source=icon 일 때 찾을 폴더 (기본 icons)
    std::string fallback;  // source 그림이 없을 때 쓸 파일
    int fit = 0;           // 0 contain / 1 cover / 2 stretch
    int alpha = 255;
    int dim = -1;          // 그림 위에 덮는 검은 막 0~255 (-1: 배경은 theme.txt 의 dim, 그 밖은 0)
    bool hasTint = false;  // 그림에 색 입히기 (흰 로고를 다른 색으로 등)
    LColor tint;

    // list
    int style = 0;         // 0 text / 1 grid / 2 carousel
    // 캐러셀 (v1.1): 가운데가 선택, 양옆으로 작아지고 흐려짐
    int direction = 0;     // 0 가로 / 1 세로
    int visibleItems = 5;
    float centerScale = 1.3f, sideScale = 0.8f;
    int sideAlpha = 160;
    int spacing = 0;       // 칸 중심 사이 거리 (0 = 칸 크기 + gap)
    int itemW = 0, itemH = 0;  // 칸 크기 (0 = 자리에서 계산)
    int animMs = -1;       // 움직임 시간 (-1 = [theme] anim_ms)
    float rowH = 42;       // 줄 높이 (소수 가능: 줄무늬 그림에 맞출 때)
    int columns = 1, rows = 0;  // rows 0 = 높이에서 계산
    int gapX = 0, gapY = 0;
    LColor selColor;       // 선택된 항목 글자
    LColor selection;      // 선택 막대 (또는 selector 그림이 없을 때 칸 바탕)
    int textX = 12, textY = -1;  // 줄 안 글자 위치 (-1: 세로 가운데)
    bool count = false;    // 카테고리 목록: 게임 수 표시
    LColor countColor;
    int countSize = 22;
    int countX = 18, countY = -1;
    // 스크롤바 (v1.3.4): 목록 오른쪽(캐러셀은 아래·오른쪽)에 지금 위치 표시
    int scrollbar = 0;     // 0 auto(넘칠 때만) / 1 on(항상) / 2 off
    LColor sbColor;        // 손잡이 색 (바탕은 같은 색 25%)
    int sbW = 4;           // 굵기
    int sbX = 0, sbY = 0;  // 기본 자리에서 옮기기
    std::string card;      // grid: 칸 바탕 그림
    bool hasCardColor = false;  // grid: 칸 바탕색 (그림 없이 색만 깔 때)
    LColor cardColor;
    std::string selector;  // 선택 표시 그림 (칸 위에 덮음. text 목록에서는 막대 대신)
    int cardGrow = 0, selectorGrow = 0;
    float selScale = 1.0f; // grid: 선택된 칸 확대
    std::string image;     // grid 칸 그림: icon / boxart / logo / none ("" = 화면에 맞게)
    int pad[4] = {0, 0, 0, 0};  // grid 칸 안쪽 여백 (왼, 위, 오른, 아래)
    int label = 0;         // grid 이름표: 0 auto(그림 없을 때만) / 1 on / 2 off
    int labelH = 36;

    // info / media
    LColor titleColor;
    int titleSize = 27, descSize = 20;
    bool overlay = true;   // media: 화면 사진 위에 박스 사진 겹치기
};

struct Screen {
    std::vector<Elem> elems;  // z 순
    const Elem *Find(const std::string &name) const;
    const Elem *List() const;  // 화면의 목록 요소 (없으면 nullptr)
};

struct Layout {
    Screen categories, games;
    std::map<std::string, LColor> colors;  // 테마가 정한 색
    std::string font, fontBold;            // 테마 폴더 기준 글꼴 파일
    std::map<std::string, std::string> names;  // [names]: 카테고리·롬 폴더 이름(소문자) -> 화면에 보일 이름
    bool hasClear = false;
    LColor clear;                          // 바탕색
    std::map<std::string, std::string> strings, stringsEn;  // [strings] / [strings.en]: 화면 문구 바꾸기 (원래 한국어 문구 또는 hint.* 키 -> 바꿀 말)
    std::string minVersion;
    // 화면 효과 (v1.1, 0 = 없음): 선택·스크롤 움직임, 배경 바뀜, 화면 전환
    int menuRadius = -1, menuBorderW = -1;  // [theme] menu_radius·menu_border_width: 메뉴 창 모서리·테두리 (-1 = 기본, v1.3)
    bool buttonIcons = true;  // [theme] button_icons: 도움말의 [A]·[B]… 를 버튼 그림으로 (off = 글자 그대로)
    int animMs = 0, bgFadeMs = 0, transition = 0, transitionMs = 220;  // transition: 0 none / 1 fade / 2 slide                 // [theme] requires = 최소 Joy Launcher 버전
    bool custom = false;                   // 테마의 layout.txt 를 읽었는지
    std::vector<std::string> problems;     // 읽으면서 생긴 문제 ("줄 번호: 내용")
};

// 내장 기본 구성
void LayoutDefault(Layout &l);
// 기본 구성 위에 path 의 layout.txt 를 덮어쓴다. 파일이 없으면 false (기본 그대로)
bool LayoutLoad(Layout &l, const std::string &path);
// 기본 구성 위에 글(layout.txt 내용)을 덮어쓴다 (테마 빌더 미리보기용)
void LayoutLoadText(Layout &l, const std::string &text);
// 내장 기본 구성의 원문 (설명서·시험용)
const char *LayoutDefaultText();
