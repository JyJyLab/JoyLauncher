# emu_*.sh 가 함께 쓰는 설정 (실행하지 않고 source 로 읽는다). 모두 빌드 PC 에서 돌린다.
# adb 명령은 모두 한 기기 emulator-$EMU_PORT 로만 보낸다: PC 에 연결된 실제 기기는 건드리지 않는다.
#   ANDROID_SDK  기본 $HOME/Android/Sdk
#   AVD_NAME     기본 joytest
#   EMU_PORT     기본 5554 (콘솔 포트. adb 기기 이름은 emulator-<포트>)
#   EMU_IMAGE    기본 system-images;android-34;google_apis;x86_64

emu_die() { echo "$(basename "$0"): ERROR: $*" >&2; exit 1; }

ANDROID_SDK="${ANDROID_SDK:-$HOME/Android/Sdk}"
AVD_NAME="${AVD_NAME:-joytest}"
EMU_PORT="${EMU_PORT:-5554}"
EMU_SERIAL="emulator-$EMU_PORT"
EMU_IMAGE="${EMU_IMAGE:-system-images;android-34;google_apis;x86_64}"
ADB="$ANDROID_SDK/platform-tools/adb"
EMULATOR_BIN="$ANDROID_SDK/emulator/emulator"
AVD_HOME="${ANDROID_AVD_HOME:-$HOME/.android/avd}"
AVD_DIR="$AVD_HOME/$AVD_NAME.avd"
EMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 기록과 pid 파일은 android/build/emu 에 둔다 (android/build 는 복사·커밋 대상이 아니다).
EMU_STATE="$(cd "$EMU_DIR/.." && pwd)/build/emu"

[ -x "$ADB" ] || emu_die "adb not found: $ADB (set ANDROID_SDK)"

emu_adb() { "$ADB" -s "$EMU_SERIAL" "$@"; }

# adb 가 에뮬레이터를 "device" 상태로 보고 있으면 참
emu_online() { [ "$("$ADB" -s "$EMU_SERIAL" get-state 2>/dev/null || true)" = "device" ]; }

emu_require_online() { emu_online || emu_die "$EMU_SERIAL is not running - start it with emu_start.sh"; }

emu_booted() { [ "$(emu_adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r\n' || true)" = "1" ]; }

# 이 사용자가 띄운, 이 AVD 의 에뮬레이터 프로세스 번호들 (없으면 빈 값)
emu_pids() { pgrep -u "$(id -u)" -f "qemu-system-[^ ]* .*-avd $AVD_NAME( |\$)" || true; }

# 이 사용자가 /dev/kvm 을 읽고 쓸 수 있으면 KVM 을 쓸 수 있다
emu_kvm_ok() { [ -r /dev/kvm ] && [ -w /dev/kvm ]; }
