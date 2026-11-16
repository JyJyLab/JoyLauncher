// 화면 문구 언어 (한국어 / 영어).
// 소스에 적힌 한국어 문구를 그대로 키로 써서 영어 문구를 찾는다. 표에 없으면 한국어 그대로.
#pragma once
#include <string>

enum class Lang { Ko, En };

void SetLang(Lang l);
Lang CurrentLang();
// 스위치 본체 언어 (한국어면 Ko, 그 외 En). 데스크톱은 LANG 환경변수
Lang SystemLang();

// 테마의 [strings] 문구 바꾸기 (원래 한국어 문구 -> 바꿀 말). nullptr 이면 해제. map 은 해제 전까지 살아 있어야 함
#include <map>
void SetStringOverrides(const std::map<std::string, std::string> *ko, const std::map<std::string, std::string> *en);
// 테마 문구 중 이름 키 (예: "hint.games"). 없으면 nullptr
const char *StringOverride(const char *key);

const char *T(const char *ko);
std::string T(const std::string &ko);
// printf 형식 + 번역: Fmt(T("남은 게임 %d개"), n)
std::string Fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
