#!/usr/bin/env bash
# 빌드 PC 에서 실행: 에뮬레이터의 로그를 내보낸다 (adb logcat -d). 인자는 그대로 넘어간다.
#   android/emu/emu_log.sh -s JOYSMOKE JOYSMOKE_EV     # 스모크 테스트의 줄만
#   android/emu/emu_log.sh -s SDL SDL/APP AndroidRuntime libc DEBUG
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

emu_require_online
emu_adb logcat -d "$@"
