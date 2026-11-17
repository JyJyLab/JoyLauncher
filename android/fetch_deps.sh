#!/usr/bin/env bash
# 빌드 PC 에서 실행: 버전을 고정한 SDL2 계열 릴리스 소스를 android/deps/ 에 받아
# 정해진 이름(SDL2, SDL2_image, SDL2_ttf, SDL2_mixer)으로 푼다.
# 여러 번 돌려도 된다: 표시 파일이 고정한 버전·체크섬과 같으면 건드리지 않는다.
#   android/fetch_deps.sh           # 없거나 버전이 다른 것만 받음
#   android/fetch_deps.sh --force   # 모두 다시 풀기 (deps/dl 에 받아 둔 묶음은 다시 씀)
#
# 버전을 올릴 때는 아래 버전과 SHA256 을 함께 바꾼다. build_apk.sh 가 컴파일하는 SDL 자바 소스도
# 같은 SDL2 묶음에서 나오므로 자바와 네이티브 버전은 늘 같다.
set -euo pipefail

die() { echo "fetch_deps.sh: ERROR: $*" >&2; exit 1; }

# 이름 | github 저장소 | 버전 | <이름>-<버전>.tar.gz 의 sha256
# SDL2·SDL2_image·SDL2_mixer 는 GitHub 가 릴리스 파일에 붙여 둔 digest 와 같은 값이다.
# SDL2_ttf 2.24.0 은 digest 가 없던 때 올라온 파일이라 Homebrew formula 의 값과 맞춰 보았다.
DEPS="
SDL2|SDL|2.32.10|5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL2_image|SDL_image|2.8.12|393f5efb50536ec13ca4f4affb69cc9966d3c3f969e6c5e701faddf9f9785381
SDL2_ttf|SDL_ttf|2.24.0|0b2bf1e7b6568adbdbc9bb924643f79d9dedafe061fa1ed687d1d9ac4e453bfd
SDL2_mixer|SDL_mixer|2.8.2|938dff531d00ace2296557a6599abe6f34599e2f34f0a4a08a397e2ccac8b8f7
"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="${DEPS_DIR:-$HERE/deps}"
DL_DIR="$DEPS_DIR/dl"
FORCE=0
case "${1:-}" in
    --force) FORCE=1 ;;
    "") ;;
    *) die "unknown argument '$1' (only --force is supported)" ;;
esac

for tool in curl tar sha256sum; do
    command -v "$tool" >/dev/null || die "'$tool' not found"
done
mkdir -p "$DL_DIR"

sha_of() { sha256sum "$1" | cut -d' ' -f1; }

fetch_one() {
    local name="$1" repo="$2" ver="$3" sha="$4"
    local tarball="$name-$ver.tar.gz"
    local url="https://github.com/libsdl-org/$repo/releases/download/release-$ver/$tarball"
    local dest="$DEPS_DIR/$name"
    local stamp="$dest/.joy_dep_stamp"
    local want="$ver $sha"

    if [ "$FORCE" = 0 ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
        echo "ok      $name $ver"
        return 0
    fi

    if [ -f "$DL_DIR/$tarball" ] && [ "$(sha_of "$DL_DIR/$tarball")" != "$sha" ]; then
        echo "stale   $DL_DIR/$tarball (checksum mismatch), downloading again"
        rm -f "$DL_DIR/$tarball"
    fi
    if [ ! -f "$DL_DIR/$tarball" ]; then
        echo "fetch   $url"
        rm -f "$DL_DIR/$tarball.part"
        curl -fL --retry 3 --connect-timeout 20 -sS -o "$DL_DIR/$tarball.part" "$url" \
            || die "download failed: $url"
        local got
        got="$(sha_of "$DL_DIR/$tarball.part")"
        if [ "$got" != "$sha" ]; then
            rm -f "$DL_DIR/$tarball.part"
            die "sha256 mismatch for $tarball: expected $sha, got $got"
        fi
        mv "$DL_DIR/$tarball.part" "$DL_DIR/$tarball"
    fi

    # 임시 폴더에 다 푼 다음 자리를 바꾼다: 도중에 끊겨도 반쯤 풀린 폴더가 남지 않는다.
    echo "extract $name $ver"
    local tmp="$DEPS_DIR/.extract.$name"
    rm -rf "$tmp"
    mkdir -p "$tmp"
    tar -xzf "$DL_DIR/$tarball" -C "$tmp" --strip-components=1 --no-same-owner \
        || die "extracting $tarball failed"
    [ -f "$tmp/CMakeLists.txt" ] || die "$tarball has no top-level CMakeLists.txt after extraction"
    rm -rf "$dest"
    mv "$tmp" "$dest"
    printf '%s\n' "$want" > "$stamp"
}

while IFS='|' read -r name repo ver sha; do
    [ -n "$name" ] || continue
    fetch_one "$name" "$repo" "$ver" "$sha"
done <<EOF
$DEPS
EOF

# 빌드가 이 폴더들 안에 있다고 믿는 것들
[ -f "$DEPS_DIR/SDL2/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java" ] \
    || die "SDL2 tree has no android-project Java sources"
[ -f "$DEPS_DIR/SDL2_ttf/external/freetype/CMakeLists.txt" ] \
    || die "SDL2_ttf tree has no vendored FreeType (external/freetype); the release tarball should ship it"
[ -f "$DEPS_DIR/SDL2_image/src/stb_image.h" ] \
    || die "SDL2_image tree has no built-in stb_image backend"
[ -f "$DEPS_DIR/SDL2_mixer/src/codecs/stb_vorbis/stb_vorbis.h" ] \
    || die "SDL2_mixer tree has no built-in stb_vorbis"
[ -f "$DEPS_DIR/SDL2_mixer/src/codecs/minimp3/minimp3.h" ] \
    || die "SDL2_mixer tree has no built-in minimp3"

echo "deps ready in $DEPS_DIR"
