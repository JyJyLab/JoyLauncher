// 배경 음악: /JoyLauncher/bgm 의 mp3·ogg 를 차례대로 또는 무작위로 재생 (사용자가 직접 넣은 곡만)
// 오디오는 켜져 있고 곡이 있을 때만 연다. 다른 프로그램을 실행하거나 끝내기 전에는 반드시 BgmShutdown.
#pragma once
#include <string>

void BgmScan(const std::string &dir);  // 곡 목록 읽기
int BgmCount();
// 설정 반영: on 이면 오디오를 열고 재생, off 면 오디오를 닫는다. volume 0~100
void BgmConfigure(bool on, int volume, bool shuffle);
void BgmUpdate();  // 매 프레임: 곡이 끝나면 다음 곡, 페이드 인 진행
void BgmSkip();    // 다음 곡
void BgmPause();   // HOME·슬립으로 화면을 떠날 때
void BgmResume();  // 돌아왔을 때 (약 1초 페이드 인)
// 다른 프로그램 실행·종료 직전: fadeMs 동안 줄인 뒤 오디오를 완전히 닫는다
void BgmShutdown(int fadeMs);
