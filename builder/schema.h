// 테마 빌더: layout.txt 속성 목록 (속성 창이 이 표로 만들어진다)
#pragma once
#include <string>

#include "../source/layout.h"

enum PropKind { PK_BOOL, PK_INT, PK_FLOAT, PK_RECT, PK_INT2, PK_INT4, PK_COLOR, PK_CHOICE, PK_TEXT, PK_TEXTML, PK_FILE, PK_FOLDER };

// 요소 종류 비트
enum : unsigned {
    TR = 1 << (int)ElemType::Rect, TI = 1 << (int)ElemType::Image, TT = 1 << (int)ElemType::Text, TL = 1 << (int)ElemType::List,
    TP = 1 << (int)ElemType::Preview, TM = 1 << (int)ElemType::Media, TN = 1 << (int)ElemType::Info,
    TB = 1 << (int)ElemType::Battery, TG = 1 << (int)ElemType::Pager, TALL = 0x1FF,
    CHILD_ONLY = 1 << 12,  // 칸 안 요소에서만
};

struct PropDef {
    const char *key;
    PropKind kind;
    unsigned types;
    const char *label;
    const char *choices;  // PK_CHOICE: "a|b|c"
    const char *help;
    const char *group;
};

static const PropDef kProps[] = {
    {"rect", PK_RECT, TALL, "자리 x·y·폭·높이", nullptr, "1280x720 기준. 칸 안 요소는 칸 기준, 폭·높이 0 이하 = 칸 끝까지", "기본"},
    {"visible", PK_BOOL, TALL, "보이기", nullptr, nullptr, "기본"},
    {"z", PK_INT, TALL, "겹침 순서 (z)", nullptr, "작을수록 아래. 배경 0, 막대 10, 목록 30, 글자 50. 칸 안 요소는 음수면 칸 내용 아래", "기본"},
    {"color", PK_COLOR, TR | TT | TL | TB | TG | TN, "색", nullptr, nullptr, "기본"},
    {"state", PK_CHOICE, TALL | CHILD_ONLY, "보일 칸", "all|selected|normal", "all 모든 칸 / selected 선택된 칸만 / normal 선택 안 된 칸만", "조건"},
    {"visible_if", PK_TEXT, TALL, "보이는 조건", nullptr, "자리표 값이 있을 때만 보임 (예: description, fav, {playcount})", "조건"},
    {"hidden_if", PK_TEXT, TALL, "숨기는 조건", nullptr, "자리표 값이 있으면 숨김", "조건"},
    // 글자
    {"text", PK_TEXTML, TT, "글자", nullptr, "{자리표} 사용 가능", "글자"},
    {"size", PK_INT, TT | TL | TG | TN, "글자 크기", nullptr, nullptr, "글자"},
    {"font", PK_CHOICE, TT | TL | TG, "글꼴", "regular|bold", nullptr, "글자"},
    {"align", PK_CHOICE, TT | TI | TL, "가로 정렬", "left|center|right", "글자 폭(w)이 0 이면 x 가 기준점", "글자"},
    {"valign", PK_CHOICE, TT | TI, "세로 정렬", "top|center|bottom", "높이(h)가 있을 때", "글자"},
    {"wrap", PK_BOOL, TT, "여러 줄", nullptr, "폭에서 줄바꿈, 높이에서 자름", "글자"},
    {"scroll", PK_BOOL, TT, "넘치면 흐르기", nullptr, nullptr, "글자"},
    {"selected_color", PK_COLOR, TT | TR | TL, "선택된 칸 색", nullptr, "목록: 선택된 항목 글자. 칸 안 요소: 선택된 칸에서 쓸 색", "글자"},
    {"logo", PK_BOOL, TT, "로고로 대신", nullptr, "카테고리 로고 그림이 있으면 글자 대신 로고", "글자"},
    {"logo_rect", PK_INT4, TT, "로고 자리", nullptr, nullptr, "글자"},
    // 그림
    {"source", PK_CHOICE, TI, "그림 출처", "|background|logo|icon|boxart|screenshot",
     "비우면 file. background 카테고리 배경 / logo 로고 / icon 아이콘 폴더 / boxart 박스 / screenshot 화면 사진", "그림"},
    {"file", PK_FILE, TI, "그림 파일", nullptr, "테마 폴더 기준 경로", "그림"},
    {"folder", PK_FOLDER, TI | TL, "아이콘 폴더", nullptr, "source=icon (목록은 image=icon) 일 때 찾을 폴더. 기본 icons", "그림"},
    {"fallback", PK_FILE, TI, "없을 때 그림", nullptr, nullptr, "그림"},
    {"fit", PK_CHOICE, TI, "맞추기", "contain|cover|stretch", "contain 안에 맞춤 / cover 가득(잘림) / stretch 늘림", "그림"},
    {"alpha", PK_INT, TI, "불투명도 (0~255)", nullptr, nullptr, "그림"},
    {"dim", PK_INT, TI, "어둡게 (0~255)", nullptr, nullptr, "그림"},
    {"tint", PK_COLOR, TI, "색 입히기", nullptr, nullptr, "그림"},
    // 목록
    {"style", PK_CHOICE, TL, "목록 모양", "text|grid", "text 줄 목록 / grid 격자", "목록"},
    {"rows", PK_INT, TL | TP, "줄 수", nullptr, "0 = 높이에서 계산", "목록"},
    {"columns", PK_INT, TL | TP, "열 수 (격자)", nullptr, nullptr, "목록"},
    {"row_height", PK_FLOAT, TL, "줄 높이", nullptr, nullptr, "목록"},
    {"gap", PK_INT2, TL | TP, "칸 간격 가로·세로", nullptr, nullptr, "목록"},
    {"selection", PK_COLOR, TL, "선택 막대 색", nullptr, nullptr, "목록"},
    {"text_offset", PK_INT2, TL, "글자 위치 (x, y)", nullptr, "y = -1 세로 가운데", "목록"},
    {"label", PK_CHOICE, TL, "이름 글자", "auto|on|off", "off = 이름을 그리지 않음 (칸 안 요소로 직접 배치)", "목록"},
    {"label_height", PK_INT, TL, "이름 글자 높이", nullptr, nullptr, "목록"},
    {"image", PK_CHOICE, TL, "칸 그림 (격자)", "|icon|boxart|logo|none", nullptr, "목록"},
    {"padding", PK_INT4, TL, "칸 안쪽 여백", nullptr, nullptr, "목록"},
    {"card", PK_FILE, TL, "칸 바탕 그림", nullptr, nullptr, "목록"},
    {"card_color", PK_COLOR, TL, "칸 바탕색", nullptr, nullptr, "목록"},
    {"card_grow", PK_INT, TL, "칸 바탕 넓히기", nullptr, nullptr, "목록"},
    {"selector", PK_FILE, TL, "선택 테두리 그림", nullptr, nullptr, "목록"},
    {"selector_grow", PK_INT, TL, "선택 테두리 넓히기", nullptr, nullptr, "목록"},
    {"selected_scale", PK_FLOAT, TL, "선택 칸 확대", nullptr, "0.5~2.0", "목록"},
    {"count", PK_BOOL, TL, "게임 수 표시", nullptr, nullptr, "목록"},
    {"count_color", PK_COLOR, TL, "게임 수 색", nullptr, nullptr, "목록"},
    {"count_size", PK_INT, TL, "게임 수 크기", nullptr, nullptr, "목록"},
    {"count_offset", PK_INT2, TL, "게임 수 위치", nullptr, nullptr, "목록"},
    // 정보·사진
    {"overlay", PK_BOOL, TM, "박스 사진 겹치기", nullptr, nullptr, "사진"},
    {"title_color", PK_COLOR, TN, "제목 색", nullptr, nullptr, "정보"},
    {"title_size", PK_INT, TN, "제목 크기", nullptr, nullptr, "정보"},
    {"desc_size", PK_INT, TN, "설명 크기", nullptr, nullptr, "정보"},
};

struct Placeholder { const char *key, *desc; };
static const Placeholder kPlaceholders[] = {
    {"{category}", "카테고리 이름"}, {"{games}", "카테고리의 게임 수"}, {"{index}", "선택 위치"}, {"{count}", "목록 전체 수"},
    {"{category_index}", "카테고리 순번"}, {"{category_count}", "카테고리 수"}, {"{title}", "게임 제목"},
    {"{longtitle}", "긴 제목"}, {"{developer}", "제작사"}, {"{year}", "연도"}, {"{release}", "발매일"}, {"{players}", "인원"},
    {"{genre}", "장르"}, {"{meta}", "제작사 · 연도 · 인원"}, {"{description}", "설명"}, {"{file}", "롬 파일 이름"},
    {"{name}", "칸 항목 이름 (카테고리 또는 게임)"}, {"{short}", "약칭 (shortname·폴더)"}, {"{system}", "ES-DE 기종 이름"},
    {"{num}", "칸 번호 (1부터)"}, {"{num_desc}", "칸 번호 (거꾸로, 게시판 번호)"}, {"{fav}", "즐겨찾기면 ★"},
    {"{playcount}", "실행 횟수"}, {"{lastplayed}", "마지막 실행 날짜"}, {"{selected}", "선택된 칸이면 1"},
    {"{clock}", "시각"}, {"{date}", "날짜"}, {"{hint}", "조작 도움말"}, {"{app}", "Joy Launcher"}, {"{version}", "버전"},
    {"{developer,year,players,genre| · }", "값 있는 것만 · 로 잇기"}, {"{fav?★ 개념글:☆ 개념 보내기}", "조건: 값이 있으면 앞, 없으면 뒤"},
};

static const char *kTypeNames[] = {"rect", "image", "text", "list", "preview", "media", "info", "battery", "pager"};
static const char *kTypeKo[] = {"네모", "그림", "글자", "목록", "미리보기", "사진", "정보", "배터리", "쪽 표시"};
