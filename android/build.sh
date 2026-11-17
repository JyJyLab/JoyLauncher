#!/usr/bin/env bash
# 런처 APK 만들기 (빌드 PC 에서, 복사해 온 프로젝트 안에서 실행).
# build_apk.sh 를 부르기 전에 APK 의 assets 를 모은다:
#   romfs/ (글꼴, 기종·에뮬레이터 표) + themes/<팩> 을 zip 으로 묶은 것 (처음 실행 때 /JoyLauncher/themes 에 풀림)
#
#   android/build.sh                          # arm64-v8a
#   ABIS="arm64-v8a x86_64" android/build.sh  # x86_64 = 에뮬레이터 시험용
#   DEBUGGABLE=1 android/build.sh
#   UPDATE_REPO=소유자/저장소 android/build.sh   # 런처 업데이트를 받을 깃헙 저장소 (없으면 업데이트 항목이 숨겨짐)
# 그 밖의 환경 변수는 build_apk.sh 머리말 참고.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
ASSETS="$HERE/build/assets"

rm -rf "$ASSETS"
mkdir -p "$ASSETS/themes"
cp -r "$ROOT/romfs/." "$ASSETS/"
# 테마 팩: zip 안의 경로는 "<팩>/…" (스위치판 릴리스의 <팩>.zip 과 같은 구성)
for t in JoyBasic RetroPack ByeokgolKim DcGall; do
    [ -d "$ROOT/themes/$t" ] || { echo "build.sh: themes/$t missing" >&2; exit 1; }
    (cd "$ROOT/themes" && zip -qr -X "$ASSETS/themes/$t.zip" "$t")
done

EXTRA=""
if [ -n "${UPDATE_REPO:-}" ]; then
    EXTRA="-DCMAKE_CXX_FLAGS=-DJOY_UPDATE_REPO=\\\"$UPDATE_REPO\\\""
fi
# zip·글꼴은 다시 압축하지 않는다 (이미 압축돼 있거나, 통째로 읽는 파일)
ASSETS_DIR="$ASSETS" NOCOMPRESS_EXT="${NOCOMPRESS_EXT-ttf otf ttc zip}" CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS:-} $EXTRA" \
    exec "$HERE/build_apk.sh" "$@"
