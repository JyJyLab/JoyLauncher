// 동영상(미리보기 영상)에서 한 장면을 뽑아 jpg 로 저장한다.
// 게임 화면 사진이 없는 롬 모음(응애팩 등)에서 화면 사진 대신 쓰기 위한 것.
#pragma once
#include <string>

// video 의 앞부분(약 3초, 짧으면 30% 지점) 한 장면을 outJpg 로 저장. 성공하면 true
bool ExtractVideoFrame(const std::string &video, const std::string &outJpg, int maxW = 640);

// 백그라운드 추출: 마지막으로 요청한 하나만 처리 (빠르게 넘길 때 쌓이지 않게)
void FrameWorkerStart();
void FrameWorkerStop();
void FrameWorkerRequest(const std::string &video, const std::string &outJpg);
// 0: 아직, 1: 준비됨(파일 있음), -1: 실패
int FrameWorkerState(const std::string &outJpg);
