#!/usr/bin/env bash
# 빌드 PC 에서 한 번 실행: sdkmanager 로 에뮬레이터와 시스템 이미지를 깔고(라이선스는 물음 없이 동의)
# AVD "joytest" 를 만든다: 가로 1920x1080, 램 4 GB.
#   android/emu/emu_setup.sh              # 여러 번 돌려도 됨. 이미 있는 AVD 의 데이터는 그대로
#   android/emu/emu_setup.sh --recreate   # AVD 를 지우고 다시 만듦 (데이터 초기화)
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

RECREATE=0
case "${1:-}" in
    --recreate) RECREATE=1 ;;
    "") ;;
    *) emu_die "unknown argument '$1' (only --recreate is supported)" ;;
esac

SDKMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/sdkmanager"
AVDMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/avdmanager"
[ -x "$SDKMANAGER" ] && [ -x "$AVDMANAGER" ] || emu_die "cmdline-tools not found under $ANDROID_SDK/cmdline-tools/latest"
mkdir -p "$EMU_STATE"

image_dir="$ANDROID_SDK/$(printf '%s' "$EMU_IMAGE" | tr ';' '/')"
if [ ! -x "$EMULATOR_BIN" ] || [ ! -f "$image_dir/system.img" ]; then
    echo "== accepting SDK licenses"
    "$SDKMANAGER" --licenses > "$EMU_STATE/sdkmanager.log" 2>&1 < <(yes) \
        || { tail -n 20 "$EMU_STATE/sdkmanager.log" >&2; emu_die "sdkmanager --licenses failed"; }
    echo "== installing emulator + $EMU_IMAGE (large download, log: $EMU_STATE/sdkmanager.log)"
    "$SDKMANAGER" --install "platform-tools" "emulator" "$EMU_IMAGE" >> "$EMU_STATE/sdkmanager.log" 2>&1 < <(yes) \
        || { tail -n 20 "$EMU_STATE/sdkmanager.log" >&2; emu_die "sdkmanager --install failed"; }
fi
[ -x "$EMULATOR_BIN" ] || emu_die "emulator still missing after sdkmanager: $EMULATOR_BIN"
[ -f "$image_dir/system.img" ] || emu_die "system image still missing after sdkmanager: $image_dir"
echo "== emulator $(sed -n 's/^Pkg\.Revision *= *//p' "$ANDROID_SDK/emulator/source.properties"), image $EMU_IMAGE r$(sed -n 's/^Pkg\.Revision *= *//p' "$image_dir/source.properties")"

if [ "$RECREATE" = 1 ] || [ ! -f "$AVD_DIR/config.ini" ]; then
    [ -z "$(emu_pids)" ] || emu_die "the $AVD_NAME emulator is running - stop it first (emu_stop.sh)"
    echo "== creating AVD $AVD_NAME"
    # "no" 는 avdmanager 의 "하드웨어 프로필을 직접 만들겠느냐" 는 물음에 대한 답이다.
    "$AVDMANAGER" create avd --force -n "$AVD_NAME" -k "$EMU_IMAGE" > "$EMU_STATE/avdmanager.log" 2>&1 < <(echo no) \
        || { tail -n 20 "$EMU_STATE/avdmanager.log" >&2; emu_die "avdmanager create avd failed"; }
fi
[ -f "$AVD_DIR/config.ini" ] || emu_die "AVD config not found: $AVD_DIR/config.ini"

# AVD 의 config.ini 에서 키 하나를 바꾼다 (없으면 더한다).
set_cfg() {
    awk -F= -v k="$1" '{ key = $1; gsub(/[ \t]+$/, "", key); if (key != k) print }' "$AVD_DIR/config.ini" > "$AVD_DIR/config.ini.tmp"
    printf '%s=%s\n' "$1" "$2" >> "$AVD_DIR/config.ini.tmp"
    mv "$AVD_DIR/config.ini.tmp" "$AVD_DIR/config.ini"
}
set_cfg hw.lcd.width 1920            # 가로 > 세로: 화면 자체가 가로인 기기 (휴대용 게임기처럼)
set_cfg hw.lcd.height 1080
set_cfg hw.lcd.density 320
set_cfg hw.initialOrientation landscape
set_cfg skin.name 1920x1080
set_cfg skin.path _no_skin
set_cfg showDeviceFrame no
set_cfg hw.ramSize 4096
set_cfg vm.heapSize 512
set_cfg hw.cpu.ncore 4
set_cfg disk.dataPartition.size 6G
set_cfg hw.keyboard yes              # 하드웨어 키보드
set_cfg hw.dPad yes
set_cfg hw.mainKeys no
set_cfg hw.gpu.enabled yes
set_cfg hw.gpu.mode swiftshader_indirect
set_cfg hw.audioInput no
set_cfg hw.camera.back none
set_cfg hw.camera.front none
set_cfg fastboot.forceColdBoot yes
set_cfg fastboot.forceFastBoot no

echo "== AVD $AVD_NAME ready: $AVD_DIR"
if emu_kvm_ok; then
    case " $(id -nG) " in
        *" kvm "*) how="member of group kvm" ;;
        *) how="per-user ACL on /dev/kvm (logind grants it to whoever is logged in at the PC's own screen; not group membership)" ;;
    esac
    echo "== KVM: usable by $(id -un) - $how"
else
    echo "== KVM: /dev/kvm is NOT accessible for $(id -un). Fix: sudo usermod -aG kvm $(id -un), then log in again." >&2
    echo "   Without it the emulator only runs with EMU_NO_ACCEL=1 (software emulation, very slow)." >&2
fi
