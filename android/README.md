# Joy Launcher 안드로이드 빌드·에뮬레이터 시험

Gradle 없이 NDK CMake + javac/d8/aapt2/zipalign/apksigner 로 APK 를 만든다.
소스 원본은 CT140, 빌드와 에뮬레이터는 빌드 PC(`ssh pc`)에서 돌린다.

| 파일 | 어디서 | 하는 일 |
|---|---|---|
| `sync_pc.sh` | CT140 | 프로젝트를 `pc:~/Claude/joylauncher-android/` 로 복사 (`android/deps`, `android/build` 는 PC 에만 있고 지우지 않음) |
| `fetch_deps.sh` | PC | SDL2 계열 소스를 `android/deps/` 에 받음 (버전·sha256 고정) |
| `CMakeLists.txt` | PC | 네이티브 빌드 설정 |
| `build_apk.sh` | PC | 네이티브 빌드 → APK (`android/build/JoyLauncher-<VERSION>.apk`) |
| `smoketest/` | PC | 빌드 과정 확인용 작은 앱 (`build_smoke.sh` → `android/build/JoySmoke.apk`) |
| `emu/` | PC | 에뮬레이터 설치·켜기·끄기·설치 실행·로그·화면 사진·가상 패드 |

## 처음 한 번

```sh
# CT140
android/sync_pc.sh
# PC (ssh pc), ~/Claude/joylauncher-android 에서
android/fetch_deps.sh          # SDL 소스 약 34 MB
android/emu/emu_setup.sh       # 에뮬레이터 + 시스템 이미지 약 5 GB, AVD "joytest" 생성
```

## 복사와 빌드

```sh
android/sync_pc.sh                                   # CT140: 고친 뒤 매번
# PC 에서
android/build_apk.sh                                 # arm64-v8a 배포용
ABIS="arm64-v8a x86_64" android/build_apk.sh         # x86_64 는 에뮬레이터용
DEBUGGABLE=1 android/build_apk.sh                    # debuggable (run-as 가능)
NATIVE_ONLY=1 android/build_apk.sh                   # 컴파일·링크만 (매니페스트 등이 없어도 됨)
CMAKE_EXTRA_ARGS='-DJOY_EXCLUDE=a.cpp;b.cpp' android/build_apk.sh   # 빌드에서 뺄 소스 파일
# CT140 에서 한 줄로
android/sync_pc.sh && ssh pc 'cd ~/Claude/joylauncher-android && ABIS="arm64-v8a x86_64" android/build_apk.sh'
```

나머지 환경 변수(`APP_DIR` `MANIFEST` `RES_DIR` `ASSETS_DIR` `JAVA_DIRS` `OUT_NAME` `NOCOMPRESS_EXT` `KEYSTORE` …)는
`build_apk.sh` 머리말에 있다. 경로는 실행한 폴더 기준 상대 경로도 된다.
처음부터 다시 빌드하려면 `rm -rf android/build/native-*`.

## 스모크 테스트와 에뮬레이터

```sh
ABIS="arm64-v8a x86_64" android/smoketest/build_smoke.sh
android/emu/emu_start.sh                             # 창 없이 켜고 부팅을 기다림 (약 15~20초)
android/emu/emu_run.sh android/build/JoySmoke.apk com.jyjylab.joysmoke/.SmokeActivity
android/emu/emu_log.sh -s JOYSMOKE JOYSMOKE_EV       # 마지막 JOYSMOKE 줄이 SMOKE OK / SMOKE FAIL <이유>
android/emu/emu_shot.sh android/build/smoke.png
android/emu/emu_stop.sh
scp pc:Claude/joylauncher-android/android/build/smoke.png android/build/   # CT140 에서 가져오기
```

- arm64 라이브러리를 에뮬레이터에서 돌리기(이미지가 ARM 코드를 번역): `EMU_ABI=arm64-v8a android/emu/emu_run.sh …`
- 키·터치: `~/Android/Sdk/platform-tools/adb -s emulator-5554 shell input keyevent DPAD_DOWN` (ENTER, ESCAPE …),
  `… shell input tap 960 540`. SDL 에는 키보드 키·터치로 들어온다.
- 가상 컨트롤러: `android/emu/emu_pad.sh on [xbox|switch|ps5]`, `emu_pad.sh press A`, `emu_pad.sh off`.
  SDL 에 진짜 컨트롤러로 잡힌다 (이름: Xbox One S Controller / Nintendo Switch Pro Controller / PS5 Controller).
- 스크립트는 `-s emulator-5554` 로만 adb 를 쓴다. PC 에 꽂힌 실제 기기는 건드리지 않는다.
- 에뮬레이터 기록·pid·가상 패드 상태는 스크립트가 있는 프로젝트 사본의 `android/build/emu/` 에 생긴다.
  `emu_pad.sh` 의 on / press / off 는 같은 사본에서 돌릴 것 (켜기·끄기·설치·사진은 어느 사본에서 해도 된다).

## 앱 쪽에서 맞출 것

- **라이브러리 구성**: `libSDL2.so` + `libmain.so` (+ NDK 의 `libc++_shared.so`, 자동 포함).
  `getLibraries()` 는 `{"SDL2", "main"}` — SDLActivity 의 기본값이라 고치지 않아도 된다.
- **main**: `SDL.h` 를 포함한 파일에 `int main(int argc, char *argv[])`. SDL 이 `SDL_main` 으로 이름을 바꿔 내보낸다
  (`libmain.so` 에 `SDL_main` 이 없으면 `build_apk.sh` 가 멈춘다).
- **SDL 자바와 네이티브 버전**: 자바는 `android/deps/SDL2/android-project/app/src/main/java` (SDL 2.32.10)를 그대로
  컴파일한다. SDLActivity 는 시작할 때 자바에 적힌 버전과 `libSDL2.so` 의 버전이 다르면 오류 창을 띄운다.
  둘 다 같은 묶음에서 나오므로 `fetch_deps.sh` 의 버전만 바꾸면 함께 바뀐다.
- **매니페스트** (SDL 본보기 매니페스트 기준, `smoketest/AndroidManifest.xml` 참고):
  - 액티비티 `android:configChanges="layoutDirection|locale|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|keyboard|keyboardHidden|navigation"`
    — 빠지면 회전·키보드 연결 때 액티비티가 다시 만들어지고 SDL 이 버티지 못한다
  - `android:exported="true"` (targetSdk 31 이상에서 런처 액티비티에 필수), `android:launchMode="singleInstance"`
  - `<uses-feature android:glEsVersion="0x00020000" />`, 전체 화면 테마(예: `@android:style/Theme.NoTitleBar.Fullscreen`)
  - `android:extractNativeLibs` 는 `"false"`, `"true"`(기본) 둘 다 된다 (둘 다 에뮬레이터에서 확인)
  - `versionCode`/`versionName`, minSdk 24, targetSdk 34 는 `build_apk.sh` 가 넣는다 (매니페스트에 적혀 있어도 `VERSION`,
    `android/VERSION_CODE` 값으로 바꿈)
- **에셋**: `ASSETS_DIR`(기본 `romfs`)의 내용이 `assets/` 가 된다. `SDL_RWFromFile("font.ttf", "rb")` 처럼 상대 경로로 열면
  SDL 은 앱 내부 저장소(`files/`)를 먼저 보고 그다음 에셋을 본다. 이름이 `.` 으로 시작하는 파일·폴더는 aapt2 가 넣지 않는다.

## 알아둘 점

- **버전**: SDL2 2.32.10, SDL2_image 2.8.12, SDL2_ttf 2.24.0(+ 딸린 FreeType 2.13.2, HarfBuzz 없음), SDL2_mixer 2.8.2.
  NDK r27c, build-tools 37.0.0, `platforms/android-37.0/android.jar`, CMake 4.2.3, JDK 17,
  에뮬레이터 37.2.13 + `system-images;android-34;google_apis;x86_64` r14.
- **넣은 기능**: SDL2_image 는 stb_image 로 PNG·JPG 읽기 + `IMG_SavePNG`(miniz). SDL2_mixer 는 MP3(minimp3)·OGG(stb_vorbis)·WAV.
  다른 그림 형식은 `-DJOY_IMAGE_FORMATS="PNG;JPG;BMP;GIF"` 처럼 더한다 (바깥 라이브러리가 필요한 WEBP·AVIF·TIF·JXL 은 안 됨).
- **소스 제외 목록**: `JOY_EXCLUDE` 기본값은 빈 값(`source/*.cpp` 전부 빌드). 뺄 파일이 있으면
  `CMAKE_EXTRA_ARGS='-DJOY_EXCLUDE=a.cpp;b.cpp'` 로 넘긴다.
  `CMAKE_EXTRA_ARGS` 가 바뀌면 그 ABI 의 CMake 캐시를 지우고 다시 설정한다 (약 8초, 다시 컴파일하지는 않음).
- **CMake 4**: FreeType 의 `cmake_minimum_required(VERSION 3.0)` 때문에 `CMAKE_POLICY_VERSION_MINIMUM=3.5` 를 CMakeLists 에서 준다.
  NDK r27c 툴체인 파일이 내는 "CMake < 3.10" 경고는 `-Wno-deprecated` 로 끈다. 설정 출력은
  `android/build/native-<abi>.configure.log` 에 남는다.
- **최적화**: NDK r27 툴체인의 Release 기본은 `-O3` 다. CMakeLists 에서 `-O2 -DNDEBUG` 로 바꾼다 (딸린 라이브러리 포함).
- **16 KB 페이지**: `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` + `-Wl,-z,max-page-size=16384`, zipalign `-P 16`.
  `build_apk.sh` 가 `.so` 마다 LOAD 정렬을 검사하고 16 KB 가 아니면 멈춘다. NDK 의 `libc++_shared.so` 도 16 KB 정렬이다.
  실제 16 KB 커널에서 돌려 보지는 않았다 (정렬 값과 `zipalign -c -P 16` 만 확인).
- **글꼴은 압축하지 않고 넣는다** (`NOCOMPRESS_EXT="ttf otf ttc"`): 압축된 에셋에서 SDL_ttf 가 글꼴을 바로 읽으면
  느리다. 에뮬레이터 측정으로 글꼴 열기 + 글자 8개 그리기가 압축 시 약 100 ms, 비압축 시 1~10 ms.
  글꼴을 통째로 메모리에 읽어 쓰면 압축해도 되고(`NOCOMPRESS_EXT=""`), 글꼴 하나에 APK 가 약 1.4 MB 줄어든다.
- **화면 크기**: 창을 만든 직후의 출력 크기는 시스템 막대를 숨기기 전 값일 수 있다 (에뮬레이터에서 1920x984 → 곧 1920x1080).
  `SDL_RenderSetLogicalSize` 를 쓰면 SDL 이 따라가지만, 출력 크기를 따로 기억해 두는 코드는 크기 변경 사건을 받아야 한다.
- **`SDL_RenderReadPixels(r, NULL, …)`** 은 논리 크기(1280x720)가 아니라 실제 출력 픽셀(예: 1920x1080)만큼 읽는다.
  1280x720 표면에 받으면 넘친다. 실제 출력 크기로 표면을 만들 것 (`smoketest/main.cpp` 의 `saveFramePng`).
- **컨트롤러**: SDL 은 새 입력 장치를 3초에 한 번 찾는다. 그 전에 누른 A/B 는 키보드 Return/Escape 로 들어온다.
  닌텐도 패드는 SDL 이 버튼에 적힌 글자대로 알려 준다 (아래쪽 버튼 = `b`). Xbox·PS 는 자리대로 (아래쪽 = `a`).
  이 AVD 의 내장 키보드 `qwerty2` 는 방향키(DPAD) 장치라서 SDL 에 늘 컨트롤러 하나로 잡힌다
  (`hw.dPad=no` 로 바꿔도 그대로였다). 그래서 이 에뮬레이터에서는 "컨트롤러 없음" 상태가 나오지 않는다.
- **에뮬레이터 끄기**: `emu_stop.sh` 는 `adb emu kill` 로 바로 끈다. 끄기 약 10초 전에 깐 앱이 다음 부팅 때 없었던 적이
  있다 (데이터 파티션 자체는 유지됨). `emu_run.sh` 는 매번 다시 설치하므로 실행에는 지장이 없다. 기기에 넣어 둔 파일·권한을
  남기려면 끄기 전에 충분히 기다리거나 `adb -s emulator-5554 shell reboot -p` 로 정상 종료할 것 (이 방법은 확인하지 않음).
- **KVM**: `/dev/kvm` 권한은 그룹이 아니라 logind 가 PC 화면에 로그인한 사용자에게 주는 ACL 로 얻는다
  (이 PC 는 자동 로그인이라 부팅하면 생긴다). 없으면 `emu_start.sh` 가 고치는 방법을 알리고 멈춘다.
- **서명 키**: 시험판은 `~/.joylauncher-android/debug.keystore` (PC, 없으면 자동 생성, 암호는 스크립트에 있는 디버그용).
  **배포판(릴리스)** 은 배포용 키로: `KEYSTORE=~/.joylauncher-android/release.keystore KS_PASS_FILE=~/.joylauncher-android/release.pass android/build.sh`
  (키·암호 파일은 저장소에 넣지 않는다. 사본은 NAS 의 자격 폴더). 같은 키로 서명해야 깔린 앱 위에 덮어 깔 수 있으므로
  배포용 키를 잃으면 사용자가 앱을 지우고 다시 깔아야 한다. 디버그 키로 깔린 시험판 위에는 배포판을 덮어 깔 수 없다(지우고 설치, `/JoyLauncher` 폴더는 남음).
- **릴리스**: 스위치판과 같은 저장소(JyJyLab/JoyLauncher)에 태그 `android-v<버전>` 으로, 자산 `JoyLauncher.apk`.
  런처의 업데이트 확인은 태그가 `android-v` 로 시작하는 가장 새 릴리스를 찾아 그 릴리스 페이지 주소를 보여 준다 (스위치판 `v<버전>` 릴리스와 섞여 있음).
  앱이 apk 를 받아 설치하지 않는다 (1.3.0 부터, REQUEST_INSTALL_PACKAGES 권한 삭제) — 사용자가 브라우저로 받아 직접 설치. 그래서 릴리스 자산 이름 `JoyLauncher.apk` 는 그대로 유지.
  이 릴리스는 "Latest" 로 표시하지 않는다 (스위치판 런처가 latest 를 보기 때문). 기본 테마 zip 은 스위치판 릴리스의 것을 함께 쓴다.
- **기호 정보**: APK 에는 기호를 뗀 사본이 들어간다. 떼지 않은 원본은 `android/build/native-<abi>/lib/` 에 있다
  (`ndk-stack -sym android/build/native-arm64-v8a/lib`).
- **확인하지 않은 것**: 실제 기기 설치, `armeabi-v7a`·`x86` ABI, KVM 없는 실행(`EMU_NO_ACCEL=1`).
- **저장소에 넣지 않을 것**: `android/deps/`, `android/build/`.

측정값 (빌드 PC 16코어, 스모크 테스트, 두 ABI): 빈 폴더에서 전체 빌드 31초, 바뀐 것 없이 다시 0.8초,
`.cpp` 하나 고친 뒤 2초. APK 는 arm64 만 5.7 MB, x86_64 만 5.9 MB, 둘 다 9.4 MB (글꼴 2 MB 포함).
