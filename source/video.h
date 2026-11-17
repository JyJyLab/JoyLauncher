// 미리보기 영상: 고른 게임에 영상이 있으면 화면 사진 자리에서 재생한다.
// 영상을 푸는 일은 플랫폼 층이 하고(안드로이드 MediaPlayer, 데스크톱 시험판 ffmpeg),
// 여기서는 넘어온 장면을 SDL 그림으로 올려 테마의 다른 그림과 똑같이 그릴 수 있게 한다.
#pragma once
#include <SDL.h>

#include <string>

// 설정: on = 영상 재생 (video=on/off), sound = 영상 소리 (소리 선택이 "영상 소리" 일 때만)
void VideoConfigure(bool on, bool sound);
// 이 영상을 보여 줄 수 있는지 (재생이 켜져 있고, 재생하지 못한 영상이 아님) — 그러면 사진 대신 영상 자리
bool VideoUsable(const std::string &path);
// 그리는 쪽: 이번 프레임에 이 영상을 보여 줄 자리가 있다.
// 재생 중이면 그 그림을 돌려주고(아니면 nullptr), shownMs 에 첫 장면이 나온 뒤 지난 시간을 적는다
SDL_Texture *VideoWant(SDL_Renderer *r, const std::string &path, Uint32 *shownMs);
// 프레임 끝: 보여 줄 자리가 없어졌으면 멈추고, 선택이 멈춘 지 settledMs 가 충분하면 재생을 시작한다.
// active = false 면 멈춤 (옵션 창 등이 화면을 가렸을 때)
void VideoEndFrame(Uint32 settledMs, bool active);
void VideoStop();      // 게임 실행·다른 앱으로 갈 때·끝낼 때
void VideoResetGpu();  // 그림 장치가 다시 만들어졌을 때: 올려 둔 그림을 버림
// 시험용(데스크톱 스냅샷): 첫 장면이 올 때까지 기다림. 왔으면 true
bool VideoWaitFrame(Uint32 ms);

// ---- 플랫폼 층이 영상을 푸는 스레드에서 부른다 (token = plat::VideoOpen 에 넘긴 값. 다른 값이면 버림)
void VideoSinkState(int token, int state, bool hasAudio);  // state: 2 = 재생 시작, -1 = 실패, 0 = 플랫폼 쪽에서 멈춤
// YUV 4:2:0. 밝기 판 y(줄 간격 yStride), 색 판 u·v(줄 간격 uvStride, 점 간격 uvPixelStride: 1 = 따로, 2 = 번갈아)
void VideoSinkYuv(int token, int w, int h, const unsigned char *y, int yStride, const unsigned char *u, const unsigned char *v,
                  int uvStride, int uvPixelStride);
// 한 점이 R G B X 4바이트인 그림 (YUV 로 읽을 수 없는 기기)
void VideoSinkRgbx(int token, int w, int h, const unsigned char *p, int stride);
