#!/usr/bin/env bash
# 빌드 PC 에서 실행: 에뮬레이터 안에 가상 게임 컨트롤러를 꽂는다.
# AVD 에는 게임패드 하드웨어 설정이 없다. 대신 안드로이드의 "uinput" 셸 도구로 진짜 입력 장치를
# 등록하므로 SDL 은 USB 패드를 꽂은 것처럼 SDL_CONTROLLERDEVICEADDED 와 버튼 사건을 받는다.
#   android/emu/emu_pad.sh on [xbox|switch|ps5]        꽂기 (기본 xbox). "off" 할 때까지 유지
#   android/emu/emu_pad.sh press <버튼> [누르는_ms]    눌렀다 떼기 (기본 80 ms)
#   android/emu/emu_pad.sh off                         뽑기
# 버튼: A B X Y L1 R1 L2 R2 SELECT START GUIDE L3 R3 UP DOWN LEFT RIGHT
# A/B/X/Y 는 Xbox 배치의 "자리" 다 (A = 아래, B = 오른쪽, X = 왼쪽, Y = 위). 실제 패드의 커널
# 드라이버가 그 자리 버튼에 쓰는 리눅스 키 코드로 보낸다. SELECT/START 는 가운데 왼쪽/오른쪽
# 버튼(스위치 패드의 - 와 +). "switch" 의 GUIDE 는 홈 버튼이고, 안드로이드가 HOME 키로 처리해
# 실제 컨트롤러처럼 앱에서 빠져나간다.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

FIFO="$EMU_STATE/pad.fifo"
PIDFILE="$EMU_STATE/pad.pid"
TYPEFILE="$EMU_STATE/pad.type"

pad_alive() { [ -p "$FIFO" ] && [ -f "$PIDFILE" ] && [ -f "$TYPEFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }
send() { printf '%s\n' "$1" > "$FIFO"; }
pad_off() {
    if [ -f "$PIDFILE" ]; then kill "$(cat "$PIDFILE")" 2>/dev/null || true; fi
    rm -f "$PIDFILE" "$FIFO" "$TYPEFILE"
    # adb 를 끝내면 기기 쪽 도구도 보통 함께 끝나지만, 남지 않게 한 번 더 끝낸다.
    if emu_online; then emu_adb shell pkill -f commands.uinput.Uinput > /dev/null 2>&1 || true; fi
}

case "${1:-}" in
    on)
        emu_require_online
        if pad_alive; then
            echo "a virtual pad ($(cat "$TYPEFILE")) is already connected - 'emu_pad.sh off' first to change its type"
            exit 0
        fi
        # 장치 능력은 실제 커널 드라이버(xpad, hid-nintendo, hid-playstation)가 알리는 것과 같게 한다:
        # keys = 리눅스 BTN_* 코드, 축은 스틱 0,1 + 3,4, 십자키(hat) 16,17, 아날로그 트리거 2,5.
        type="${2:-xbox}"
        case "$type" in
            xbox)   name="Xbox Wireless Controller"; vid=1118; pid=746       # 045e:02ea
                    keys="304, 305, 307, 308, 310, 311, 314, 315, 316, 317, 318"; trig_max=1023 ;;
            switch) name="Nintendo Switch Pro Controller"; vid=1406; pid=8201 # 057e:2009
                    keys="304, 305, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318"; trig_max=0 ;;
            ps5)    name="Sony DualSense"; vid=1356; pid=3302                 # 054c:0ce6
                    keys="304, 305, 307, 308, 310, 311, 312, 313, 314, 315, 316, 317, 318"; trig_max=255 ;;
            *) emu_die "unknown controller type '$type' (xbox, switch, ps5)" ;;
        esac
        stick='"info": {"value": 0, "minimum": -32768, "maximum": 32767, "fuzz": 16, "flat": 128, "resolution": 0}'
        hat='"info": {"value": 0, "minimum": -1, "maximum": 1, "fuzz": 0, "flat": 0, "resolution": 0}'
        abs="0, 1, 3, 4, 16, 17"
        abs_info="{\"code\": 0, $stick}, {\"code\": 1, $stick}, {\"code\": 3, $stick}, {\"code\": 4, $stick}, {\"code\": 16, $hat}, {\"code\": 17, $hat}"
        if [ "$trig_max" -gt 0 ]; then
            trig="\"info\": {\"value\": 0, \"minimum\": 0, \"maximum\": $trig_max, \"fuzz\": 0, \"flat\": 0, \"resolution\": 0}"
            abs="$abs, 2, 5"
            abs_info="$abs_info, {\"code\": 2, $trig}, {\"code\": 5, $trig}"
        fi

        pad_off
        mkdir -p "$EMU_STATE"
        mkfifo "$FIFO"
        # adb 의 표준 입력은 읽기·쓰기로 연 FIFO 다: 명령 사이에 입력이 끝났다고 보지 않으므로
        # uinput 은 이 adb 가 살아 있는 동안 장치를 등록해 둔다.
        nohup setsid bash -c 'exec "$1" -s "$2" shell uinput - <> "$3"' _ "$ADB" "$EMU_SERIAL" "$FIFO" \
            > "$EMU_STATE/pad.log" 2>&1 &
        echo $! > "$PIDFILE"
        echo "$type" > "$TYPEFILE"
        # configuration 의 type 100 / 101 / 103 = UI_SET_EVBIT / UI_SET_KEYBIT / UI_SET_ABSBIT, EV_KEY = 1, EV_ABS = 3
        send "{\"id\": 1, \"command\": \"register\", \"name\": \"$name\", \"vid\": $vid, \"pid\": $pid, \"bus\": \"usb\",
 \"configuration\": [{\"type\": 100, \"data\": [1, 3]}, {\"type\": 101, \"data\": [$keys]}, {\"type\": 103, \"data\": [$abs]}],
 \"abs_info\": [$abs_info]}"
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            case "$(emu_adb shell dumpsys input 2>/dev/null || true)" in
                *": $name"*)
                    # SDL 은 새 입력 장치를 3초에 한 번만 찾는다. 찾기 전에 누른 버튼은 안드로이드의
                    # 대체 키(A -> Return, B -> Escape)로 앱에 가거나 아예 가지 않는다.
                    sleep 3.5
                    echo "virtual pad connected: $name ($type)"
                    exit 0 ;;
            esac
            sleep 0.5
        done
        cat "$EMU_STATE/pad.log" >&2 || true
        pad_off
        emu_die "the device did not show up in 'dumpsys input'"
        ;;
    press)
        pad_alive || emu_die "no virtual pad connected - run: emu_pad.sh on"
        type="$(cat "$TYPEFILE")"
        hold="${3:-80}"
        case "$hold" in ""|*[!0-9]*) emu_die "hold time must be milliseconds, got '$hold'" ;; esac
        # down/up 은 "종류,코드,값" 묶음이고, 여러 묶음이 한 번의 보고가 된다.
        west=308; north=307                               # hid-nintendo / hid-playstation: BTN_WEST, BTN_NORTH
        [ "$type" != xbox ] || { west=307; north=308; }   # xpad 는 왼쪽 버튼을 BTN_X(0x133)로 보낸다
        key() { down="1,$1,1"; up="1,$1,0"; }
        axis() { down="3,$1,$2"; up="3,$1,0"; }
        btn="$(printf '%s' "${2:-}" | tr '[:lower:]' '[:upper:]')"
        case "$btn" in
            A) key 304 ;;       B) key 305 ;;       X) key "$west" ;;   Y) key "$north" ;;
            L1) key 310 ;;      R1) key 311 ;;      SELECT) key 314 ;;  START) key 315 ;;
            GUIDE) key 316 ;;   L3) key 317 ;;      R3) key 318 ;;
            UP) axis 17 -1 ;;   DOWN) axis 17 1 ;;  LEFT) axis 16 -1 ;; RIGHT) axis 16 1 ;;
            L2|R2)
                n=0; [ "$btn" = L2 ] || n=1
                case "$type" in
                    xbox)   axis $((2 + 3 * n)) 1023 ;;                                  # ABS_Z / ABS_RZ 축만
                    switch) key $((312 + n)) ;;                                          # BTN_TL2 / BTN_TR2 버튼만
                    ps5)    down="3,$((2 + 3 * n)),255, 1,$((312 + n)),1"; up="3,$((2 + 3 * n)),0, 1,$((312 + n)),0" ;;
                esac ;;
            *) emu_die "unknown button '${2:-}' (A B X Y L1 R1 L2 R2 SELECT START GUIDE L3 R3 UP DOWN LEFT RIGHT)" ;;
        esac
        # 보고는 늘 EV_SYN / SYN_REPORT (0,0,0) 로 끝난다.
        send "{\"id\": 1, \"command\": \"inject\", \"events\": [$down, 0,0,0]}"
        send "{\"id\": 1, \"command\": \"delay\", \"duration\": $hold}"
        send "{\"id\": 1, \"command\": \"inject\", \"events\": [$up, 0,0,0]}"
        # 뗀 것이 앱에 닿은 뒤에 돌아간다: 바로 이어서 화면을 찍어도 결과가 흔들리지 않는다.
        ms=$((hold + 250))
        sleep "$((ms / 1000)).$(printf '%03d' $((ms % 1000)))"
        ;;
    off)
        pad_off
        echo "virtual pad disconnected"
        ;;
    *)
        emu_die "usage: emu_pad.sh on [xbox|switch|ps5] | press <button> [hold_ms] | off"
        ;;
esac
