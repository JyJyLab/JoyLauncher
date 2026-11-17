#!/usr/bin/env bash
# 빌드 PC 에서 실행: 에뮬레이터에 APK 를 깔고 액티비티를 (다시) 띄운 뒤 잠깐 기다린다.
#   android/emu/emu_run.sh android/build/JoySmoke.apk com.jyjylab.joysmoke/.SmokeActivity
#   EMU_RUN_WAIT  띄운 뒤 기다릴 시간(초)                        기본 6
#   EMU_ABI       설치할 때 쓸 ABI 를 강제, 예: arm64-v8a. google_apis x86_64 이미지는 ARM 코드를
#                 번역해 돌리므로 arm64 라이브러리도 여기서 돌려 볼 수 있다
# 띄우기 직전에 로그를 비우므로 바로 뒤의 emu_log.sh 는 이번 실행 것만 보여 준다.
# 기다린 뒤 앱 프로세스가 없으면(죽었거나 끝났으면) 종료 코드 1.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

apk="${1:-}"
component="${2:-}"
[ -n "$apk" ] && [ -n "$component" ] || emu_die "usage: emu_run.sh <apk> <package/activity>"
[ -f "$apk" ] || emu_die "APK not found: $apk"
case "$component" in */*) ;; *) emu_die "second argument must look like package/activity, got '$component'" ;; esac
pkg="${component%%/*}"
emu_require_online

args=( -r )
[ -z "${EMU_ABI:-}" ] || args+=( --abi "$EMU_ABI" )
emu_adb install "${args[@]}" "$apk" \
    || emu_die "install failed (signed with a different key than the installed copy? then: adb -s $EMU_SERIAL uninstall $pkg)"
emu_adb shell am force-stop "$pkg"
emu_adb logcat -c || true
started="$(emu_adb shell am start -W -n "$component" 2>&1 | tr -d '\r' || true)"
echo "$started"
case "$started" in
    *Error*|*"does not exist"*) emu_die "am start failed for $component" ;;
esac
sleep "${EMU_RUN_WAIT:-6}"
pid="$(emu_adb shell pidof "$pkg" | tr -d '\r\n' || true)"
if [ -z "$pid" ]; then
    echo "WARNING: $pkg is not running after ${EMU_RUN_WAIT:-6} s - see emu_log.sh" >&2
    exit 1
fi
echo "$pkg is running (pid $pid, ABI $(emu_adb shell "dumpsys package $pkg | grep -m1 primaryCpuAbi" | tr -d '\r' | sed 's/.*primaryCpuAbi=//'))"
