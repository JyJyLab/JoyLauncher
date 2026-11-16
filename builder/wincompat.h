// 테마 빌더 윈도우(mingw) 빌드: 런처 코드의 POSIX 함수 맞추기 (-include 로 모든 파일 앞에)
#pragma once
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#define mkdir(path, mode) _mkdir(path)
#endif
