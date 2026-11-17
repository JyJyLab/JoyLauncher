#!/usr/bin/env bash
# 빌드 PC 에서 실행: "joytest" 에뮬레이터를 끄고 프로세스가 사라졌는지까지 확인한다.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

pids() {
    { emu_pids; if [ -f "$EMU_STATE/emulator.pid" ]; then p="$(cat "$EMU_STATE/emulator.pid")"; if kill -0 "$p" 2>/dev/null; then echo "$p"; fi; fi; } | sort -u
}
wait_gone() {  # 기다릴 시간(초)
    local end=$((SECONDS + $1))
    while [ -n "$(pids)" ] && [ "$SECONDS" -lt "$end" ]; do sleep 1; done
    [ -z "$(pids)" ]
}
# 가상 패드(emu_pad.sh)는 에뮬레이터 없이는 의미가 없다: 그쪽 adb 를 끝내고 파일을 지운다.
drop_pad() {
    if [ -f "$EMU_STATE/pad.pid" ]; then kill "$(cat "$EMU_STATE/pad.pid")" 2>/dev/null || true; fi
    rm -f "$EMU_STATE/pad.pid" "$EMU_STATE/pad.fifo" "$EMU_STATE/pad.type"
}

if [ -z "$(pids)" ] && ! emu_online; then
    drop_pad
    rm -f "$EMU_STATE/emulator.pid"
    echo "$EMU_SERIAL is not running"
    exit 0
fi
if emu_online; then emu_adb emu kill > /dev/null 2>&1 || true; fi
if ! wait_gone 30; then
    echo "emulator did not exit on 'emu kill', sending SIGTERM" >&2
    # shellcheck disable=SC2046
    kill $(pids) 2>/dev/null || true
    if ! wait_gone 15; then
        echo "still alive, sending SIGKILL" >&2
        # shellcheck disable=SC2046
        kill -9 $(pids) 2>/dev/null || true
        wait_gone 5 || emu_die "could not stop emulator process(es): $(pids | tr '\n' ' ')"
    fi
fi
drop_pad
rm -f "$EMU_STATE/emulator.pid"
echo "$EMU_SERIAL stopped"
