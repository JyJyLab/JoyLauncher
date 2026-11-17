# Joy Launcher 안드로이드 네이티브 빌드 (NDK CMake 툴체인 + Ninja, Gradle 없음).
#
# 결과물은 <빌드 폴더>/lib 에 생긴다 (SDL 의 android-project 와 같은 구성):
#   libSDL2.so   SDL2 공유 라이브러리. JNI_OnLoad 와 자바 연결 함수가 여기 들어 있다
#   libmain.so   앱 + SDL2_image, SDL2_ttf(+ 딸린 FreeType), SDL2_mixer 를 정적으로 묶은 것.
#                SDL_main(= 앱의 main(), SDL_main.h 가 이름을 바꿈)을 내보낸다
# 둘 다 C++ 코드가 있어(SDL2 의 hidapi) STL 은 공유판(ANDROID_STL=c++_shared)을 쓰고,
# build_apk.sh 가 NDK 의 libc++_shared.so 를 함께 넣는다. SDL 의 SDLActivity 가 그대로 읽는다:
# getLibraries() 는 {"SDL2", "main"} 을 돌려주면 되고 이것이 SDLActivity 의 기본값이다.
#
# 보통은 build_apk.sh 가 부른다. 손으로 돌릴 때:
#   cmake -S android -B android/build/native-arm64-v8a -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_shared \
#     -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build android/build/native-arm64-v8a --target main
#
# 앱 쪽 설정 (캐시 변수, -D 로 넘김):
#   JOY_APP_DIR        ../source 대신 이 폴더의 *.cpp 를 빌드 (스모크 테스트용)
#   JOY_EXCLUDE        그 목록에서 뺄 파일 이름(폴더 없이)
#   JOY_SOURCES        소스 목록을 직접 지정. 비어 있지 않으면 폴더 검색과 JOY_EXCLUDE 를 쓰지 않는다
#   JOY_IMAGE_FORMATS  SDL2_image 에 넣을 그림 형식 (기본 PNG;JPG)
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(joylauncher_android LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "Android only: configure with the NDK toolchain file (see build_apk.sh).")
endif()

get_filename_component(JOY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(JOY_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH
    "Directory holding the SDL2, SDL2_image, SDL2_ttf and SDL2_mixer source trees (fetch_deps.sh)")
foreach(dep SDL2 SDL2_image SDL2_ttf SDL2_mixer)
    if(NOT EXISTS "${JOY_DEPS_DIR}/${dep}/CMakeLists.txt")
        message(FATAL_ERROR "${JOY_DEPS_DIR}/${dep} is missing - run android/fetch_deps.sh first.")
    endif()
endforeach()

# ---------------------------------------------------------------------------------------------
# 전체 공통 옵션
# ---------------------------------------------------------------------------------------------
# NDK r27 툴체인 파일은 Release 를 -O3 로 잡는다. 여기서는 딸린 라이브러리까지 모두 -O2 로 맞춘다.
# (-g 는 툴체인이 늘 붙인다. APK 에 넣는 사본은 build_apk.sh 가 기호를 떼고, 떼지 않은 원본은
# 빌드 폴더에 남아 ndk-stack·addr2line 에 쓸 수 있다.)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# APK 에 들어갈 공유 라이브러리는 모두 <빌드 폴더>/lib 에 모은다.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# 메모리 페이지가 16 KB 인 기기: LOAD 구역이 16 KB 단위로 정렬되어 있어야 한다. NDK r27 은
# -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON 으로 설정했을 때만 그렇게 한다 (build_apk.sh 가 넘긴다.
# 이 옵션은 PAGE_SIZE 매크로도 감추므로 4 KB 를 가정한 코드는 컴파일에서 걸린다).
# 아래 링커 옵션은 그 툴체인 옵션 없이 손으로 설정했을 때도 정렬을 지키려고 따로 적어 둔 것이다.
add_link_options("-Wl,-z,max-page-size=16384")

# ---------------------------------------------------------------------------------------------
# 딸린 라이브러리: SDL2 는 공유, 나머지 셋은 정적으로 libmain.so 안에
# ---------------------------------------------------------------------------------------------
# SDL2_ttf 에 들어 있는 FreeType 은 아직 cmake_minimum_required(VERSION 3.0) 이라 CMake 4 이상이
# 거부한다. 남의 프로젝트를 add_subdirectory() 하기 전에 이 변수로 정책 하한을 올리는 것은
# CMake 문서에 적힌 쓰임새다.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

macro(joy_opt name value)
    set(${name} ${value} CACHE BOOL "" FORCE)
endmacro()

joy_opt(BUILD_SHARED_LIBS OFF)

# --- SDL2 ---
joy_opt(SDL_SHARED ON)
joy_opt(SDL_STATIC OFF)
joy_opt(SDL_TEST OFF)
joy_opt(SDL2_DISABLE_SDL2MAIN ON)   # 안드로이드는 SDL2main 이 필요 없다: SDLActivity 가 SDL_main 을 바로 부른다
joy_opt(SDL2_DISABLE_INSTALL ON)
joy_opt(SDL2_DISABLE_UNINSTALL ON)
joy_opt(SDL_CCACHE OFF)             # ccache 가 깔려 있든 아니든 같은 명령으로 빌드
add_subdirectory("${JOY_DEPS_DIR}/SDL2" deps/SDL2 EXCLUDE_FROM_ALL)
# BUILD_SHARED_LIBS 가 꺼져 있으면 SDL2_image·ttf·mixer 의 CMake 는 SDL2::SDL2-static 이라는
# 타깃에 링크한다. 그 이름을 공유 SDL2 로 돌려 두어 SDL 이 libSDL2.so 한 곳에만 있게 한다
# (그러지 않으면 초기화되지 않은 두 번째 SDL 이 libmain.so 안에 들어간다).
add_library(SDL2::SDL2-static ALIAS SDL2)

# --- SDL2_image: stb_image 만 사용 (PNG·JPG 읽기), IMG_SavePNG 는 들어 있는 miniz 로 ---
set(JOY_IMAGE_FORMATS "PNG;JPG" CACHE STRING
    "SDL2_image decoders to build. Dependency-free extras: BMP GIF LBM PCX PNM QOI SVG TGA XCF XPM XV")
joy_opt(SDL2IMAGE_INSTALL OFF)
joy_opt(SDL2IMAGE_SAMPLES OFF)
joy_opt(SDL2IMAGE_TESTS OFF)
joy_opt(SDL2IMAGE_VENDORED OFF)
joy_opt(SDL2IMAGE_DEPS_SHARED OFF)
joy_opt(SDL2IMAGE_BACKEND_STB ON)
foreach(fmt AVIF BMP GIF JPG JXL LBM PCX PNG PNM QOI SVG TGA TIF WEBP XCF XPM XV)
    if("${fmt}" IN_LIST JOY_IMAGE_FORMATS)
        joy_opt(SDL2IMAGE_${fmt} ON)
    else()
        joy_opt(SDL2IMAGE_${fmt} OFF)
    endif()
endforeach()
foreach(fmt AVIF JXL TIF WEBP)
    if("${fmt}" IN_LIST JOY_IMAGE_FORMATS)
        message(FATAL_ERROR "JOY_IMAGE_FORMATS: ${fmt} needs an external library that this build does not provide.")
    endif()
endforeach()
joy_opt(SDL2IMAGE_PNG_SAVE ON)
joy_opt(SDL2IMAGE_JPG_SAVE OFF)
add_subdirectory("${JOY_DEPS_DIR}/SDL2_image" deps/SDL2_image EXCLUDE_FROM_ALL)

# --- SDL2_ttf: 릴리스 묶음에 들어 있는 FreeType(external/freetype) 사용, HarfBuzz 없음 ---
joy_opt(SDL2TTF_INSTALL OFF)
joy_opt(SDL2TTF_SAMPLES OFF)
joy_opt(SDL2TTF_VENDORED ON)
joy_opt(SDL2TTF_HARFBUZZ OFF)
add_subdirectory("${JOY_DEPS_DIR}/SDL2_ttf" deps/SDL2_ttf EXCLUDE_FROM_ALL)

# --- SDL2_mixer: MP3(들어 있는 minimp3) + OGG Vorbis(들어 있는 stb_vorbis) + WAV, 바깥 라이브러리 없음 ---
joy_opt(SDL2MIXER_INSTALL OFF)
joy_opt(SDL2MIXER_SAMPLES OFF)
joy_opt(SDL2MIXER_VENDORED OFF)
joy_opt(SDL2MIXER_DEPS_SHARED OFF)
joy_opt(SDL2MIXER_CMD OFF)
joy_opt(SDL2MIXER_WAVE ON)
joy_opt(SDL2MIXER_MP3 ON)
joy_opt(SDL2MIXER_MP3_MINIMP3 ON)
joy_opt(SDL2MIXER_MP3_MPG123 OFF)
set(SDL2MIXER_VORBIS "STB" CACHE STRING "" FORCE)
joy_opt(SDL2MIXER_FLAC OFF)         # 필요해지면 들어 있는 dr_flac 을 쓸 수 있다 (FLAC ON + FLAC_DRFLAC ON)
joy_opt(SDL2MIXER_OPUS OFF)
joy_opt(SDL2MIXER_MOD OFF)
joy_opt(SDL2MIXER_MIDI OFF)
joy_opt(SDL2MIXER_WAVPACK OFF)
joy_opt(SDL2MIXER_GME OFF)
add_subdirectory("${JOY_DEPS_DIR}/SDL2_mixer" deps/SDL2_mixer EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------------
# 앱: libmain.so
# ---------------------------------------------------------------------------------------------
set(JOY_APP_DIR "" CACHE PATH "Directory whose *.cpp are built instead of ../source (smoke test)")
set(JOY_EXCLUDE "" CACHE STRING
    "File names (without directory) left out of the app source glob")
set(JOY_SOURCES "" CACHE STRING
    "Explicit app source list (absolute, or relative to the android/ directory); empty = glob")

if(JOY_APP_DIR)
    get_filename_component(joy_app_dir "${JOY_APP_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    set(joy_app_dir "${JOY_ROOT}/source")
endif()
if(NOT IS_DIRECTORY "${joy_app_dir}")
    message(FATAL_ERROR "App source directory not found: ${joy_app_dir}")
endif()

if(JOY_SOURCES)
    set(joy_sources ${JOY_SOURCES})
else()
    # CONFIGURE_DEPENDS: .cpp 를 더하거나 빼면 다음 빌드 때 저절로 반영된다 (손으로 다시 설정할 필요 없음).
    file(GLOB joy_sources CONFIGURE_DEPENDS "${joy_app_dir}/*.cpp")
    foreach(src IN LISTS joy_sources)
        get_filename_component(src_name "${src}" NAME)
        if("${src_name}" IN_LIST JOY_EXCLUDE)
            list(REMOVE_ITEM joy_sources "${src}")
        endif()
    endforeach()
endif()
if(NOT joy_sources)
    message(FATAL_ERROR "No app sources found in ${joy_app_dir} (JOY_EXCLUDE=${JOY_EXCLUDE})")
endif()

set(joy_version_file "${JOY_ROOT}/VERSION")
if(NOT EXISTS "${joy_version_file}")
    message(FATAL_ERROR "${joy_version_file} is missing")
endif()
file(STRINGS "${joy_version_file}" JOY_VERSION LIMIT_COUNT 1)
string(STRIP "${JOY_VERSION}" JOY_VERSION)
# VERSION 이 바뀌면 CMake 를 다시 돌려 APP_VERSION_STR 이 따라가게 한다.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${joy_version_file}")

add_library(main SHARED ${joy_sources})
set_target_properties(main PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON               # gnu++17 (데스크톱 빌드와 같음)
)
target_compile_definitions(main PRIVATE "APP_VERSION_STR=\"${JOY_VERSION}\"")
target_compile_options(main PRIVATE -Wall)
target_include_directories(main PRIVATE "${joy_app_dir}")
target_link_libraries(main PRIVATE
    SDL2_mixer::SDL2_mixer-static
    SDL2_ttf::SDL2_ttf-static
    SDL2_image::SDL2_image-static
    SDL2::SDL2
    android
    log
    z
)

list(LENGTH joy_sources joy_source_count)
message(STATUS "Joy: version ${JOY_VERSION}, ABI ${ANDROID_ABI}, ${ANDROID_PLATFORM}, STL ${ANDROID_STL}")
message(STATUS "Joy: app dir ${joy_app_dir} (${joy_source_count} sources)")
foreach(src IN LISTS joy_sources)
    message(STATUS "Joy:   ${src}")
endforeach()
