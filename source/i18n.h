// 화면 문구 언어 (한국어 / 영어).
// 소스에 적힌 한국어 문구를 그대로 키로 써서 영어 문구를 찾는다. 표에 없으면 한국어 그대로.
#pragma once
#include <string>

enum class Lang { Ko, En };

void SetLang(Lang l);
Lang CurrentLang();
// 스위치 본체 언어 (한국어면 Ko, 그 외 En). 데스크톱은 LANG 환경변수
Lang SystemLang();

const char *T(const char *ko);
std::string T(const std::string &ko);
// printf 형식 + 번역: Fmt(T("남은 게임 %d개"), n)
std::string Fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
