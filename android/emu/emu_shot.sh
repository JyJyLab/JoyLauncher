#!/usr/bin/env bash
# 빌드 PC 에서 실행: 에뮬레이터 화면을 PNG 로 저장한다.
#   android/emu/emu_shot.sh android/build/shot.png
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

out="${1:-}"
[ -n "$out" ] || emu_die "usage: emu_shot.sh <out.png>"
emu_require_online
mkdir -p "$(dirname "$out")"
emu_adb exec-out screencap -p > "$out.part" || { rm -f "$out.part"; emu_die "screencap failed"; }
# PNG 는 0x89 'P' 'N' 'G' 로 시작한다.
if [ "$(head -c 4 "$out.part" | od -An -tx1 | tr -d ' \n')" != "89504e47" ]; then
    rm -f "$out.part"
    emu_die "screencap did not return a PNG"
fi
mv "$out.part" "$out"
# 너비와 높이는 IHDR 덩어리의 16번째 바이트부터 있는 32비트(빅 엔디언) 정수 둘이다.
dims="$(od -An -tu1 -j16 -N8 "$out" | awk '{ print $1*16777216 + $2*65536 + $3*256 + $4 "x" $5*16777216 + $6*65536 + $7*256 + $8 }')"
echo "saved $out ($dims, $(stat -c %s "$out") bytes)"
