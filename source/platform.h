// 플랫폼 층: 안드로이드(JNI, platform_android.cpp) / 데스크톱 시험판(platform_desktop.cpp)
// 런처 본체는 이 함수들만 쓰고 안드로이드 API 를 직접 부르지 않는다.
#pragma once
#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace plat {

struct App {
    std::string label, package, activity;
};

// 저장소 한 곳: 내부 저장소(primary) 또는 SD 카드·USB. id 는 SAF 문서 주소에 쓰는 이름 ("primary" 또는 "1234-ABCD")
struct Volume {
    std::string path, id;
    bool primary = false;
};

// 시작할 때 한 번 (메인 스레드). 안드로이드: 자바 쪽 함수 찾아 두기
void Init();

// ---- 저장소
// 설정·테마 폴더가 들어갈 공용 저장소 루트 (안드로이드 /storage/emulated/0)
std::string PrimaryStorage();
std::vector<Volume> Volumes();
// 모든 파일 접근 권한이 있는지 / 권한 화면 열기 (안드로이드 11 이상: 설정 화면, 그 아래: 권한 창)
bool HasStorageAccess();
void RequestStorageAccess();
// 앱 전용 폴더 (임시 파일용, 권한 없이 쓸 수 있음)
std::string PrivateDir();
// ES-DE 의 %INTERNALDATA% (/data/user/0)
std::string InternalDataRoot();

// ---- APK 에 든 파일 (romfs 폴더 = assets). 없으면 false
bool ReadAsset(const std::string &name, std::vector<char> &out);

// ---- 앱
bool PackageInstalled(const std::string &package, const std::string &activity = "");
// 실행할 수 있는 앱 전부 (런처 자신 제외), 이름순
std::vector<App> LaunchableApps();
// 앱 아이콘을 png 로 저장 (한 변 size 점). 성공하면 true
bool SaveAppIcon(const std::string &package, const std::string &activity, const std::string &outPng, int size);
// am start 의 인자 그대로 (start 다음부터) 로 액티비티 실행. 실패하면 false + err
bool StartActivity(const std::vector<std::string> &amArgs, std::string &err);
// 앱 실행 (activity 가 비면 그 앱의 기본 실행 화면)
bool StartApp(const std::string &package, const std::string &activity, std::string &err);
// 안드로이드 기본 파일 관리자의 폴더 선택 화면 (ACTION_OPEN_DOCUMENT_TREE). 열 수 없으면 false (앱 안 찾아보기로 대신)
bool OpenFolderPicker();
// 폴더 선택 결과: 다 골랐으면 true + out (실제 경로 / "" = 취소 / "!이유" = 실제 경로가 없는 위치)
bool FolderPickerResult(std::string &out);

// ---- 기기 상태
// 전지 잔량 0~100 (-1 = 모름)
int Battery(bool &charging);
// 기기 언어가 한국어인지
bool SystemKorean();

// ---- 그림: SDL2_image 가 못 읽는 형식(webp·avif·heif 등)을 기기의 해석기로. 긴 변이 maxSide 를 넘으면 줄여서.
// 실패하면 nullptr. 다른 스레드(그림 읽기 스레드)에서도 부른다
SDL_Surface *DecodeImage(const std::string &path, int maxSide);

// ---- 미리보기 영상 재생 (video.cpp 가 씀). 바로 돌아오고, 풀린 장면과 상태는 다른 스레드에서
// VideoSinkState / VideoSinkYuv / VideoSinkRgbx (video.h) 로 넘어온다. 영상은 끝나면 처음부터 되풀이한다.
// sound = false 면 소리 없이. token 은 그 요청의 번호 (장면을 넘길 때 그대로 돌려줌)
void VideoOpen(const std::string &path, bool sound, int token);
void VideoClose();

// ---- 네트워크 (업데이트 확인·테마 내려받기)
bool HttpGet(const std::string &url, std::string &out, std::string &err);
// progress(받은 바이트, 전체 바이트) 가 false 를 돌려주면 중지 (err = "cancel")
bool HttpDownload(const std::string &url, const std::string &path, std::string &err,
                  const std::function<bool(long long, long long)> &progress);
// 진행 중인 통신을 끊음 (종료할 때)
void HttpAbort(bool on);

// 런처 끝내기 (안드로이드: 액티비티 닫기)
void Finish();

// 데스크톱 시험판 전용: 마지막으로 실행하려던 인텐트 인자 (안드로이드에서는 빈 값)
const std::vector<std::string> &LastStart();

}  // namespace plat
