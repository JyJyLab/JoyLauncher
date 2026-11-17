// 화면 컨트롤러: 컨트롤러가 연결되지 않았을 때 화면 위에 띄우는 터치 버튼.
// 버튼 번호는 런처 공통: 0 A, 1 B, 2 X, 3 Y, 6 L, 7 R, 8 ZL, 9 ZR, 10 +(START), 11 −(SELECT), 12 ◀ 13 ▲ 14 ▶ 15 ▼
#pragma once
#include <SDL.h>

#include <vector>

struct OverlayPress {
    int button;
    bool down;
};

// 터치의 원래 좌표를 적어 두기 시작한다. SDL_Init 뒤, 렌더러를 만들기 전에 부를 것
// (렌더러가 이벤트의 좌표를 논리 크기에 맞춰 바꾸기 전에 봐야 함)
void OverlayWatchTouches();
// bold: 버튼 글자를 그릴 굵은 글꼴 파일 내용 (런처 내장 글꼴). 끝날 때까지 살아 있어야 함
void OverlayInit(SDL_Renderer *r, const std::vector<char> *bold);
void OverlayShutdown();
// 화면 크기가 바뀌었거나 그림 장치가 다시 만들어졌을 때 (자리·그림을 다시 만듦)
void OverlayReset();
// 터치 이벤트 처리. active = 화면 컨트롤러가 보이는 중 (아니면 눌림을 만들지 않음). 터치 이벤트였으면 true
bool OverlayEvent(const SDL_Event &e, bool active, std::vector<OverlayPress> &out);
// 눌려 있던 버튼을 모두 뗀다 (화면 컨트롤러를 감출 때)
void OverlayReleaseAll(std::vector<OverlayPress> &out);
// 화면 좌표로 그린다 (런처의 1280x720 논리 크기를 잠시 풀었다가 되돌림). startSelect: +/− 대신 START/SELECT
void OverlayDraw(SDL_Renderer *r, bool startSelect);
