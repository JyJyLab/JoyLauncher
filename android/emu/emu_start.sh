#!/usr/bin/env bash
# 빌드 PC 에서 실행: "joytest" 에뮬레이터를 창 없이 띄우고 안드로이드가 다 켜질 때까지
# (sys.boot_completed=1) 기다린다. 띄운 채로 끝나며, 끌 때는 emu_stop.sh.
#   EMU_BOOT_TIMEOUT  부팅을 기다릴 시간(초)                    기본 300
#   EMU_NO_ACCEL      1 = KVM 없이 실행 (매우 느림. /dev/kvm 을 쓸 수 없을 때만)
#   EMU_EXTRA_ARGS    에뮬레이터에 덧붙일 인자, 예: "-wipe-data"
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

[ -x "$EMULATOR_BIN" ] || emu_die "emulator not installed - run emu_setup.sh"
[ -f "$AVD_DIR/config.ini" ] || emu_die "AVD $AVD_NAME missing - run emu_setup.sh"

if emu_online && emu_booted; then
    echo "$EMU_SERIAL is already running"
    exit 0
fi
[ -z "$(emu_pids)" ] || emu_die "an emulator process for $AVD_NAME exists but is not usable through adb - run emu_stop.sh first"

accel=()
if ! emu_kvm_ok; then
    [ "${EMU_NO_ACCEL:-0}" = 1 ] || emu_die "/dev/kvm is not accessible for $(id -un). Fix: sudo usermod -aG kvm $(id -un) and log in again; or set EMU_NO_ACCEL=1 for slow software emulation."
    echo "WARNING: no KVM - software emulation, expect a boot of many minutes" >&2
    accel=( -no-accel )
fi

mkdir -p "$EMU_STATE"
rm -f "$EMU_STATE/pad.pid" "$EMU_STATE/pad.fifo" "$EMU_STATE/pad.type"    # 꺼진 에뮬레이터에서 남은 emu_pad.sh 흔적
"$ADB" start-server > /dev/null 2>&1 || true
t0=$SECONDS
# setsid + nohup + 터미널 없음: 띄운 ssh 연결이 끊겨도 에뮬레이터는 남는다.
# -no-snapshot: 늘 처음부터 부팅해 매번 같은 상태에서 시작한다. -no-metrics: 사용 통계를 보내지 않는다.
# shellcheck disable=SC2086  # EMU_EXTRA_ARGS 는 일부러 낱말 단위로 나눈다
nohup setsid "$EMULATOR_BIN" -avd "$AVD_NAME" -port "$EMU_PORT" \
    -no-window -no-audio -no-boot-anim -gpu swiftshader_indirect \
    -no-snapshot -no-metrics "${accel[@]}" ${EMU_EXTRA_ARGS:-} \
    > "$EMU_STATE/emulator.log" 2>&1 < /dev/null &
pid=$!
echo "$pid" > "$EMU_STATE/emulator.pid"
echo "emulator started (pid $pid, log $EMU_STATE/emulator.log), waiting for boot"

timeout="${EMU_BOOT_TIMEOUT:-300}"
until emu_online && emu_booted; do
    if ! kill -0 "$pid" 2>/dev/null && [ -z "$(emu_pids)" ]; then
        tail -n 30 "$EMU_STATE/emulator.log" >&2
        emu_die "the emulator exited during boot (log above)"
    fi
    if [ $((SECONDS - t0)) -ge "$timeout" ]; then
        tail -n 30 "$EMU_STATE/emulator.log" >&2
        emu_die "not booted after $timeout s (still running; emu_stop.sh stops it)"
    fi
    sleep 2
done
boot_s=$((SECONDS - t0))

# 시험하기 좋은 상태로: 화면 전환 효과 끔, 화면 꺼짐 없음, 잠금 화면 해제. 그리고 전체 화면 앱을
# 처음 띄울 때 나오는 "Viewing full screen - Got it" 안내 창이 첫 화면 사진을 가리지 않게 한다.
emu_adb shell 'settings put global window_animation_scale 0; settings put global transition_animation_scale 0;
    settings put global animator_duration_scale 0; settings put system screen_off_timeout 1800000;
    settings put secure immersive_mode_confirmations confirmed;
    svc power stayon true; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard' > /dev/null 2>&1 || true

echo "booted in $boot_s s: $EMU_SERIAL, Android $(emu_adb shell getprop ro.build.version.release | tr -d '\r') (API $(emu_adb shell getprop ro.build.version.sdk | tr -d '\r')), ABIs $(emu_adb shell getprop ro.product.cpu.abilist | tr -d '\r'), $(emu_adb shell wm size | tr -d '\r'), KVM $(emu_kvm_ok && [ "${#accel[@]}" = 0 ] && echo on || echo off)"
