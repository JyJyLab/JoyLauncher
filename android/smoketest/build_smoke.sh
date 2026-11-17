#!/usr/bin/env bash
# 빌드 PC 에서 실행: 빌드 과정 스모크 테스트 APK 를 만든다 -> android/build/JoySmoke.apk
#   ABIS="arm64-v8a x86_64" android/smoketest/build_smoke.sh
#   DEBUGGABLE=1 ABIS=x86_64 android/smoketest/build_smoke.sh
# 에셋(이 폴더의 시험 파일 + romfs/font.ttf)을 한곳에 모은 뒤, 스모크 테스트의 소스·매니페스트·
# 리소스·자바를 지정해 build_apk.sh 를 부를 뿐이다. ABIS·DEBUGGABLE 등은 그대로 넘어간다
# (매니페스트를 바꿔 시험할 때는 MANIFEST 와 OUT_NAME 을 따로 줄 수 있다).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_DIR="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$ANDROID_DIR/.." && pwd)"
STAGE="$ANDROID_DIR/build/smoke-assets"

[ -f "$ROOT/romfs/font.ttf" ] || { echo "build_smoke.sh: ERROR: $ROOT/romfs/font.ttf missing" >&2; exit 1; }
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -p "$HERE"/assets/* "$STAGE"/
cp -p "$ROOT/romfs/font.ttf" "$STAGE/font.ttf"

APP_DIR="$HERE" \
MANIFEST="${MANIFEST:-$HERE/AndroidManifest.xml}" \
RES_DIR="$HERE/res" \
ASSETS_DIR="$STAGE" \
JAVA_DIRS="$HERE/java" \
OUT_NAME="${OUT_NAME:-JoySmoke}" \
VERSION_CODE="${VERSION_CODE:-1}" \
    exec "$ANDROID_DIR/build_apk.sh"
