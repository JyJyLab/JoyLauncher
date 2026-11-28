// 기종 이름 표: 롬 폴더·카테고리 이름(응애팩·페가수스·ES-DE·한글) -> ES-DE 기종 이름 (테마 그림 파일 이름)
#pragma once
#include <string>
#include <vector>

// 소문자 이름 -> ES-DE 기종 이름 (모르면 "")
const char *EsdeSystemName(const std::string &lowerName);
// ES-DE 기종 이름과 화면용 이름 (테마 빌더의 기종 목록)
struct EsdeSystem { const char *id, *ko, *en; };
const std::vector<EsdeSystem> &EsdeSystems();
// 그 이름의 그림이 테마에 없을 때 대신 찾아볼 기종 이름 (megadrivejp → megadrive, fbneo → arcade …). 없으면 ""
const char *EsdeArtFallback(const std::string &id);
