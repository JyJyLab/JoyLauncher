#!/usr/bin/env bash
# CT140 에서 실행: 프로젝트를 빌드 PC 로 한 방향 복사한다 (CT140 이 원본).
#   android/sync_pc.sh            # 복사
#   android/sync_pc.sh -n -i      # 바뀔 것만 미리 보기 (덧붙인 인자는 rsync 로 넘어감)
# 환경 변수: PC_HOST (ssh 별칭, 기본 pc), PC_DIR (PC 의 $HOME 기준 폴더, 기본 Claude/joylauncher-android)
#
# android/deps (받아 둔 SDL 소스)와 android/build (빌드 결과·에뮬 기록)는 PC 에만 있다.
# 둘 다 제외 목록에 있고, rsync --delete 는 제외한 경로를 받는 쪽에서 지우지 않는다
# (--delete-excluded 를 붙이면 지워지므로 붙이지 말 것). 끝에 / 를 붙이지 않아 다른 사본을 가리키는 링크여도 남는다.
set -euo pipefail

die() { echo "sync_pc.sh: ERROR: $*" >&2; exit 1; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PC_HOST="${PC_HOST:-pc}"
PC_DIR="${PC_DIR:-Claude/joylauncher-android}"

command -v rsync >/dev/null || die "rsync not found"
# 프로젝트 폴더가 아닌 곳(빈 폴더 등)을 --delete 로 보내면 PC 사본이 지워지므로 먼저 확인한다.
[ -f "$ROOT/VERSION" ] || die "$ROOT/VERSION missing - not the project root?"
[ -d "$ROOT/source" ] || die "$ROOT/source missing - not the project root?"
[ -f "$ROOT/android/CMakeLists.txt" ] || die "$ROOT/android/CMakeLists.txt missing"
case "$PC_DIR" in
    ""|/|.|..|*/..|../*|*/../*) die "refusing PC_DIR='$PC_DIR'" ;;
esac

ssh -o ConnectTimeout=10 "$PC_HOST" "mkdir -p -- '$PC_DIR'" \
    || die "cannot reach '$PC_HOST' (PC off or booted into Windows?)"

rsync -a --delete \
    --exclude='/android/deps' \
    --exclude='/android/build' \
    --exclude='/.git/' \
    --exclude='/build-desktop/' \
    --exclude='/joylauncher_desktop' \
    --exclude='*.o' \
    "$@" \
    "$ROOT/" "$PC_HOST:$PC_DIR/"

echo "synced $ROOT/ -> $PC_HOST:~/$PC_DIR/"
