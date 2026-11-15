// 원인 파악용 동작 기록: /JoyLauncher/debug_log.txt (스위치 전용, 데스크톱은 아무 일도 안 함)
// 시작(START)·종료(EXIT)·크래시(CRASH) 기록은 debuglog.cpp 의 userAppInit / userAppExit / 예외 진입점이 직접 쓴다.
#pragma once

// envSetNextLoad 성공 직후: 다음에 hbloader 가 올릴 nro 와 인자
void DebugLogLaunch(const char *nro, const char *argv);
