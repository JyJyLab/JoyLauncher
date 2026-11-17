#!/usr/bin/env bash
# 빌드 PC 에서, 복사해 온 프로젝트 안에서 실행: 네이티브 빌드(NDK CMake + Ninja) -> javac -> d8
# -> aapt2 -> zip -> zipalign -> apksigner. Gradle 은 쓰지 않는다.
#
#   android/build_apk.sh                               # arm64-v8a, 배포용
#   ABIS="arm64-v8a x86_64" android/build_apk.sh       # x86_64 는 에뮬레이터용
#   DEBUGGABLE=1 android/build_apk.sh                  # android:debuggable=true (run-as, jdwp)
#   NATIVE_ONLY=1 android/build_apk.sh                 # 컴파일·링크만, APK 는 만들지 않음
#
# 환경 변수 (경로는 스크립트를 실행한 폴더 기준 상대 경로도 됨):
#   ABIS          빈칸으로 나눈 ABI 목록                     기본: arm64-v8a
#   DEBUGGABLE    1 = aapt2 --debug-mode, javac -g, d8 --debug
#   NATIVE_ONLY   1 = 네이티브 빌드까지만. 매니페스트·리소스·에셋·자바가 없어도 된다
#   APP_DIR       ../source 대신 이 폴더의 *.cpp 를 빌드 (-DJOY_APP_DIR)
#   MANIFEST      AndroidManifest.xml                        기본: android/AndroidManifest.xml
#   RES_DIR       리소스 폴더                                기본: android/res
#   ASSETS_DIR    APK 의 assets/ 로 들어갈 폴더              기본: romfs
#   JAVA_DIRS     SDL 자바와 함께 컴파일할 자바 소스 폴더들(빈칸으로 나눔)
#                                                            기본: android/java
#   OUT_NAME      결과는 android/build/<OUT_NAME>.apk        기본: JoyLauncher-<VERSION>
#   VERSION_NAME / VERSION_CODE                              기본: ../VERSION, android/VERSION_CODE
#   NOCOMPRESS_EXT  압축하지 않고 넣을 에셋 확장자           기본: "ttf otf ttc"
#   CMAKE_EXTRA_ARGS  cmake 설정에 덧붙일 인자, 예: '-DJOY_EXCLUDE=a.cpp;b.cpp'
#   KEYSTORE      서명 키 저장소, 없으면 만든다              기본: ~/.joylauncher-android/debug.keystore
#   ANDROID_SDK, NDK, BUILD_TOOLS(폴더), PLATFORM_JAR(android.jar)
#                 기본: $HOME/Android/Sdk, 그 안의 ndk/android-ndk-r27c, 가장 새 build-tools·platform
set -euo pipefail

die() { echo "build_apk.sh: ERROR: $*" >&2; exit 1; }
log() { echo "== $*"; }
abspath() { case "$1" in /*) printf '%s\n' "$1" ;; *) printf '%s\n' "$PWD/$1" ;; esac; }
newest() { { ls -1d "$@" 2>/dev/null || true; } | sort -V | tail -n 1; }
flag() { case "${1:-}" in ""|0|no|false|off) echo "" ;; *) echo 1 ;; esac; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$HERE/build"

MIN_SDK=24
TARGET_SDK=34

# --------------------------------------------------------------------------- 입력
DEBUGGABLE="$(flag "${DEBUGGABLE:-}")"
NATIVE_ONLY="$(flag "${NATIVE_ONLY:-}")"
ABIS="${ABIS:-arm64-v8a}"
MANIFEST="$(abspath "${MANIFEST:-$HERE/AndroidManifest.xml}")"
RES_DIR="$(abspath "${RES_DIR:-$HERE/res}")"
ASSETS_DIR="$(abspath "${ASSETS_DIR:-$ROOT/romfs}")"
JAVA_DIRS="${JAVA_DIRS:-$HERE/java}"
APP_DIR_ABS=""
[ -z "${APP_DIR:-}" ] || APP_DIR_ABS="$(abspath "$APP_DIR")"
NOCOMPRESS_EXT="${NOCOMPRESS_EXT-ttf otf ttc}"
KEYSTORE="${KEYSTORE:-$HOME/.joylauncher-android/debug.keystore}"
KS_PASS="joylauncher"       # 디버그 키 전용. 배포용 키의 암호는 스크립트에 적지 않는다
KS_ALIAS="joylauncher"
# 배포용: KEYSTORE=~/.joylauncher-android/release.keystore KS_PASS_FILE=~/.joylauncher-android/release.pass
#   (암호 파일은 한 줄. 사본은 NAS /mnt/claude/credentials/joylauncher-android/ — 저장소에는 넣지 않는다)
if [ -n "${KS_PASS_FILE:-}" ]; then
    [ -f "$KS_PASS_FILE" ] || die "KS_PASS_FILE not found: $KS_PASS_FILE"
    [ -f "$KEYSTORE" ] || die "release keystore not found: $KEYSTORE"
    KS_PASS="$(tr -d '\r\n' < "$KS_PASS_FILE")"
fi

[ -f "$ROOT/VERSION" ] || die "$ROOT/VERSION missing"
VERSION_NAME="${VERSION_NAME:-$(tr -d '[:space:]' < "$ROOT/VERSION")}"
[ -n "$VERSION_NAME" ] || die "empty version name ($ROOT/VERSION)"
if [ -z "$NATIVE_ONLY" ]; then
    if [ -z "${VERSION_CODE:-}" ]; then
        [ -f "$HERE/VERSION_CODE" ] || die "$HERE/VERSION_CODE missing (or set VERSION_CODE)"
        VERSION_CODE="$(tr -d '[:space:]' < "$HERE/VERSION_CODE")"
    fi
    case "$VERSION_CODE" in ""|*[!0-9]*) die "version code must be a positive integer, got '$VERSION_CODE'" ;; esac
    [ -f "$MANIFEST" ] || die "manifest not found: $MANIFEST"
    [ -d "$ASSETS_DIR" ] || die "assets directory not found: $ASSETS_DIR"
fi
OUT_NAME="${OUT_NAME:-JoyLauncher-$VERSION_NAME}"
OUT_NAME="${OUT_NAME%.apk}"
case "$OUT_NAME" in */*) die "OUT_NAME must be a file name, not a path: $OUT_NAME" ;; esac
OUT_APK="$BUILD/$OUT_NAME.apk"
WORK="$BUILD/apk-$OUT_NAME"

[ -z "$APP_DIR_ABS" ] || [ -d "$APP_DIR_ABS" ] || die "APP_DIR not found: $APP_DIR_ABS"

# --------------------------------------------------------------------------- 도구
ANDROID_SDK="${ANDROID_SDK:-$HOME/Android/Sdk}"
NDK="${NDK:-$ANDROID_SDK/ndk/android-ndk-r27c}"
[ -d "$ANDROID_SDK" ] || die "Android SDK not found: $ANDROID_SDK (set ANDROID_SDK)"
TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
[ -f "$TOOLCHAIN_FILE" ] || die "NDK not found: $NDK (set NDK)"
BUILD_TOOLS="${BUILD_TOOLS:-$(newest "$ANDROID_SDK"/build-tools/*/)}"
BUILD_TOOLS="${BUILD_TOOLS%/}"
[ -n "$BUILD_TOOLS" ] && [ -x "$BUILD_TOOLS/aapt2" ] || die "no build-tools with aapt2 under $ANDROID_SDK/build-tools (set BUILD_TOOLS)"
PLATFORM_JAR="${PLATFORM_JAR:-$(newest "$ANDROID_SDK"/platforms/android-*/android.jar)}"
[ -n "$PLATFORM_JAR" ] && [ -f "$PLATFORM_JAR" ] || die "no platforms/android-*/android.jar under $ANDROID_SDK (set PLATFORM_JAR)"
AAPT2="$BUILD_TOOLS/aapt2"
D8="$BUILD_TOOLS/d8"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"
for t in "$D8" "$ZIPALIGN" "$APKSIGNER"; do [ -x "$t" ] || die "missing tool: $t"; done
for t in cmake ninja javac jar keytool zip unzip sha256sum; do
    command -v "$t" >/dev/null || die "'$t' not found in PATH"
done
NDK_BIN="$(newest "$NDK"/toolchains/llvm/prebuilt/*/bin)"
[ -x "$NDK_BIN/llvm-strip" ] || die "llvm-strip not found under $NDK/toolchains/llvm/prebuilt"
STRIP="$NDK_BIN/llvm-strip"
READELF="$NDK_BIN/llvm-readelf"
NM="$NDK_BIN/llvm-nm"
NDK_SYSROOT_LIB="$(dirname "$NDK_BIN")/sysroot/usr/lib"

SDL_JAVA="$HERE/deps/SDL2/android-project/app/src/main/java"
[ -d "$SDL_JAVA" ] || die "SDL2 sources missing ($SDL_JAVA) - run android/fetch_deps.sh first"

abi_triple() {
    case "$1" in
        arm64-v8a)   echo aarch64-linux-android ;;
        armeabi-v7a) echo arm-linux-androideabi ;;
        x86_64)      echo x86_64-linux-android ;;
        x86)         echo i686-linux-android ;;
        *) die "unknown ABI '$1'" ;;
    esac
}

# PT_LOAD 구역의 정렬 값이 모두 16 KB 의 배수여야 한다 (메모리 페이지가 16 KB 인 기기).
check_16k() {
    local so="$1" align n=0
    while read -r align; do
        n=$((n + 1))
        [ $((align % 16384)) -eq 0 ] || die "$so: LOAD segment alignment $align is not 16 KB compatible"
    done < <("$READELF" -lW "$so" | awk '$1 == "LOAD" { print $NF }')
    [ "$n" -gt 0 ] || die "$so: no LOAD segments found (not an ELF shared object?)"
}

T0=$SECONDS
mkdir -p "$BUILD" "$WORK"
log "version $VERSION_NAME${VERSION_CODE:+ (code $VERSION_CODE)}, ABIs: $ABIS${DEBUGGABLE:+, debuggable}${NATIVE_ONLY:+, native only}"
log "build-tools $(basename "$BUILD_TOOLS"), $(basename "$(dirname "$PLATFORM_JAR")")/android.jar, NDK $(basename "$NDK")"

# --------------------------------------------------------------------------- 1. 네이티브
rm -rf "$WORK/lib"
for abi in $ABIS; do
    triple="$(abi_triple "$abi")"
    bdir="$BUILD/native-$abi"      # ABI 마다 빌드 폴더를 남겨 두어 다음 빌드는 바뀐 파일만 다시 한다
    t=$SECONDS
    cfg=(
        -G Ninja
        -Wno-deprecated     # NDK r27c 툴체인 파일이 CMake 4 에서 내는 "CMake < 3.10" 경고 수백 줄을 끈다
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
        "-DANDROID_ABI=$abi"
        "-DANDROID_PLATFORM=android-$MIN_SDK"
        -DANDROID_STL=c++_shared
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
        -DCMAKE_BUILD_TYPE=Release
        "-DJOY_APP_DIR=$APP_DIR_ABS"
    )
    # shellcheck disable=SC2206  # 사용자가 준 덧붙임 인자는 일부러 낱말 단위로 나눈다
    [ -z "${CMAKE_EXTRA_ARGS:-}" ] || cfg+=( $CMAKE_EXTRA_ARGS )
    cfg_text="$(printf '%s\n' "${cfg[@]}")"
    # 설정 인자가 바뀌었을 때만 cmake 설정을 돌린다. 그 밖에는 바로 ninja 로 가고, CMakeLists.txt·
    # VERSION·소스 파일 목록이 바뀌면 ninja 가 스스로 CMake 를 다시 돌린다.
    if [ ! -f "$bdir/build.ninja" ] || [ ! -f "$bdir/.joy_configure_args" ] \
        || [ "$(cat "$bdir/.joy_configure_args")" != "$cfg_text" ]; then
        log "[$abi] cmake configure"
        # CMAKE_EXTRA_ARGS 가 지난번과 다르면 캐시를 지우고 처음부터 설정한다: 그러지 않으면 뺀 -D 값이
        # CMakeCache.txt 에 남아 계속 쓰인다. 오브젝트 파일은 그대로라 다시 컴파일하지는 않는다.
        if [ ! -f "$bdir/.joy_extra_args" ] || [ "$(cat "$bdir/.joy_extra_args")" != "${CMAKE_EXTRA_ARGS:-}" ]; then
            rm -f "$bdir/CMakeCache.txt"
        fi
        rm -f "$bdir/.joy_configure_args"
        cmake -S "$HERE" -B "$bdir" "${cfg[@]}" > "$bdir.configure.log" 2>&1 \
            || { tail -n 40 "$bdir.configure.log" >&2; die "[$abi] cmake configure failed (full log: $bdir.configure.log)"; }
        grep '^-- Joy:' "$bdir.configure.log" || true
        printf '%s\n' "$cfg_text" > "$bdir/.joy_configure_args"
        printf '%s\n' "${CMAKE_EXTRA_ARGS:-}" > "$bdir/.joy_extra_args"
    fi
    log "[$abi] build"
    cmake --build "$bdir" --target main || die "[$abi] native build failed"

    # SDLActivity 가 시작할 때 찾는 것: libSDL2.so 의 JNI_OnLoad 와 hidapi JNI 함수
    # (System.loadLibrary("SDL2")), libmain.so 의 SDL_main (nativeRunMain 이 dlopen + dlsym).
    for need in "libSDL2.so JNI_OnLoad" "libSDL2.so Java_org_libsdl_app_HIDDeviceManager_HIDDeviceRegisterCallback" \
                "libmain.so SDL_main"; do
        so="${need%% *}"; sym="${need##* }"
        [ -f "$bdir/lib/$so" ] || die "[$abi] $bdir/lib/$so was not produced"
        grep -qw "$sym" <<<"$("$NM" -D --defined-only "$bdir/lib/$so")" \
            || die "[$abi] $so does not export $sym (for SDL_main: main() must be 'int main(int, char**)' in a file that includes SDL.h)"
    done

    # APK 에는 기호를 뗀 사본을 넣는다. libc++_shared.so 는 누가 필요로 할 때만 NDK 에서 가져온다.
    mkdir -p "$WORK/lib/$abi"
    need_cxx_shared=0
    for so in "$bdir"/lib/*.so; do
        "$STRIP" --strip-unneeded -o "$WORK/lib/$abi/$(basename "$so")" "$so"
        case "$("$READELF" -dW "$so")" in
            *'Shared library: [libc++_shared.so]'*) need_cxx_shared=1 ;;
        esac
    done
    if [ "$need_cxx_shared" = 1 ]; then
        [ -f "$NDK_SYSROOT_LIB/$triple/libc++_shared.so" ] || die "[$abi] libc++_shared.so not found in the NDK sysroot"
        "$STRIP" --strip-unneeded -o "$WORK/lib/$abi/libc++_shared.so" "$NDK_SYSROOT_LIB/$triple/libc++_shared.so"
    fi
    for so in "$WORK/lib/$abi"/*.so; do
        check_16k "$so"
        echo "   lib/$abi/$(basename "$so")  $(stat -c %s "$so") bytes, LOAD align 16 KB ok"
    done
    echo "   [$abi] native step took $((SECONDS - t)) s"
done

if [ -n "$NATIVE_ONLY" ]; then
    log "native only: unstripped libraries are in $BUILD/native-<abi>/lib ($((SECONDS - T0)) s)"
    exit 0
fi

# --------------------------------------------------------------------------- 2. 리소스 + 에셋
t=$SECONDS
rm -rf "$WORK/gen" "$WORK/res.zip" "$WORK/base.apk" "$WORK/unaligned.apk" "$WORK/aligned.apk"
# --replace-version: 매니페스트에 versionCode/versionName 이 적혀 있어도 VERSION 파일 쪽 값을 쓴다.
# --java: R.java 를 만들어 자바 소스가 R.* 를 쓸 수 있게 한다.
link=(
    link -o "$WORK/base.apk" -I "$PLATFORM_JAR" --manifest "$MANIFEST"
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK"
    --version-name "$VERSION_NAME" --version-code "$VERSION_CODE" --replace-version
    --java "$WORK/gen" -A "$ASSETS_DIR"
)
[ -z "$DEBUGGABLE" ] || link+=( --debug-mode )
# 압축된 에셋은 뒤로 찾아갈(seek) 때마다 처음부터 다시 풀기 때문에 SDL_ttf 가 글꼴을 에셋에서
# 바로 읽으면 매우 느려진다. 그래서 글꼴은 압축하지 않고 넣는다 (png·jpg·mp3·ogg 는 aapt2 가 원래 안 함).
for ext in $NOCOMPRESS_EXT; do link+=( -0 ".${ext#.}" ); done
if [ -d "$RES_DIR" ] && [ -n "$(find "$RES_DIR" -type f -print -quit)" ]; then
    "$AAPT2" compile --dir "$RES_DIR" -o "$WORK/res.zip" || die "aapt2 compile failed ($RES_DIR)"
    link+=( "$WORK/res.zip" )
else
    echo "   note: no resources in $RES_DIR - the manifest must not reference any"
fi
"$AAPT2" "${link[@]}" || die "aapt2 link failed"

# --------------------------------------------------------------------------- 3. 자바 -> dex
java_roots=( "$SDL_JAVA" )
for d in $JAVA_DIRS; do
    d="$(abspath "$d")"
    [ -d "$d" ] || die "Java source directory not found: $d (JAVA_DIRS)"
    java_roots+=( "$d" )
done
[ ! -d "$WORK/gen" ] || java_roots+=( "$WORK/gen" )
find "${java_roots[@]}" -type f -name '*.java' | sort > "$WORK/java_sources.txt"
[ -s "$WORK/java_sources.txt" ] || die "no Java sources found"
# 자바 소스 내용이 그대로면 javac·d8 을 건너뛴다 (C++ 만 고친 빌드를 빠르게).
java_key="$( { echo "jar=$PLATFORM_JAR d8=$D8 min=$MIN_SDK debuggable=$DEBUGGABLE";
               xargs -d '\n' sha256sum < "$WORK/java_sources.txt"; } | sha256sum | cut -d' ' -f1 )"
if [ -f "$WORK/dex/classes.dex" ] && [ "$(cat "$WORK/java.key" 2>/dev/null)" = "$java_key" ]; then
    echo "   java unchanged - reusing classes.dex"
else
    rm -rf "$WORK/classes" "$WORK/dex" "$WORK/classes.jar" "$WORK/java.key"
    mkdir -p "$WORK/classes" "$WORK/dex"
    javac --release 8 -nowarn -encoding UTF-8 ${DEBUGGABLE:+-g} -cp "$PLATFORM_JAR" \
        -d "$WORK/classes" @"$WORK/java_sources.txt" || die "javac failed"
    jar cf "$WORK/classes.jar" -C "$WORK/classes" .
    d8_mode=--release
    [ -z "$DEBUGGABLE" ] || d8_mode=--debug
    "$D8" "$d8_mode" --min-api "$MIN_SDK" --lib "$PLATFORM_JAR" --output "$WORK/dex" "$WORK/classes.jar" \
        || die "d8 failed"
    echo "$java_key" > "$WORK/java.key"
fi

# --------------------------------------------------------------------------- 4. 묶기, 정렬, 서명
cp "$WORK/base.apk" "$WORK/unaligned.apk"
(cd "$WORK/dex" && zip -q -X -D "$WORK/unaligned.apk" classes*.dex)
# 네이티브 라이브러리는 압축하지 않고(-0) 페이지 단위로 정렬해 넣는다. 그래야 매니페스트가
# android:extractNativeLibs="false" 여도(APK 안에서 바로 읽음), 기본값 "true" 여도 동작한다.
(cd "$WORK" && zip -q -X -D -0 -r "$WORK/unaligned.apk" lib)
case "$("$ZIPALIGN" 2>&1 || true)" in
    *'-P <pagesize_kb>'*) align=( -P 16 ) ;;
    *)  echo "   note: this zipalign has no -P option; aligning .so to 4 KB only"
        align=( -p ) ;;
esac
"$ZIPALIGN" -f "${align[@]}" 4 "$WORK/unaligned.apk" "$WORK/aligned.apk" || die "zipalign failed"
"$ZIPALIGN" -c "${align[@]}" 4 "$WORK/aligned.apk" >/dev/null || die "zipalign verification failed"

if [ ! -f "$KEYSTORE" ]; then
    log "generating debug keystore $KEYSTORE"
    (umask 077; mkdir -p "$(dirname "$KEYSTORE")")
    (umask 077; keytool -genkeypair -keystore "$KEYSTORE" -storetype PKCS12 -storepass "$KS_PASS" -keypass "$KS_PASS" \
        -alias "$KS_ALIAS" -keyalg RSA -keysize 2048 -validity 36500 -dname "CN=Joy Launcher Debug") \
        || die "keytool failed"
fi
rm -f "$OUT_APK" "$OUT_APK.idsig"
"$APKSIGNER" sign --ks "$KEYSTORE" --ks-key-alias "$KS_ALIAS" --ks-pass "pass:$KS_PASS" --key-pass "pass:$KS_PASS" \
    --v4-signing-enabled false --out "$OUT_APK" "$WORK/aligned.apk" || die "apksigner sign failed"
"$APKSIGNER" verify "$OUT_APK" || die "apksigner verify failed"
echo "   packaging step took $((SECONDS - t)) s"

# --------------------------------------------------------------------------- 요약
log "APK: $OUT_APK"
echo "   size: $(stat -c %s "$OUT_APK") bytes ($(du -h "$OUT_APK" | cut -f1))"
"$AAPT2" dump badging "$OUT_APK" \
    | { grep -E "^(package:|sdkVersion:|minSdkVersion:|targetSdkVersion:|application-label:|application-debuggable|launchable-activity:|native-code:|uses-permission:)" || true; } \
    | sed 's/^/   /'
unzip -lv "$OUT_APK" | awk '$NF ~ /^lib\// || $NF ~ /\.dex$/ { printf "   %-34s %10s bytes  %s\n", $NF, $1, $2 }'
echo "   total $((SECONDS - T0)) s"
echo "$OUT_APK"
