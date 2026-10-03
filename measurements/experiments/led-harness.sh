#!/bin/sh
# Runs the GUI build (RENDER=1) in the Ripes CLI and checks what it drew.
# The CLI has no LED peripheral, so the three LED symbols are defined as
# plain memory, RENDER_DELAY is set to 1, and the LED memory is printed
# after the scrambled cube is drawn and again at exit. render_model.py
# compares both images with the 3-D model, LED by LED.
#   RIPES=/path/to/Ripes sh measurements/experiments/led-harness.sh
set -e
RIPES=${RIPES:-ripes}
STATE=24173562322133
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -s rubik-gui.s CASES=$STATE:3 >/dev/null
dump() {
    cat <<ASM
    li   s10, LED_MATRIX_0_BASE
    li   s11, 875
led_dump_$1:
    lw   a0, 0(s10)
    li   a7, 34
    ecall
    li   a0, 44
    li   a7, 11
    ecall
    addi s10, s10, 4
    addi s11, s11, -1
    bnez s11, led_dump_$1
    li   a0, 124
    li   a7, 11
    ecall
ASM
}
dump 1 > "$work/d1"
dump 2 > "$work/d2"
{
    echo '.equ LED_MATRIX_0_BASE, 0xF0000000'
    echo '.equ LED_MATRIX_0_WIDTH, 35'
    echo '.equ LED_MATRIX_0_HEIGHT, 25'
    sed -e 's/^\.equ RENDER_DELAY, .*/.equ RENDER_DELAY, 1/' \
        -e "/jal  ra, render_init/r $work/d1" \
        -e "/^finish:/r $work/d2" rubik-gui.s
} > "$work/harness.s"
make -s rubik-gui.s >/dev/null # restore the default test cases
"$RIPES" --mode cli -t asm --src "$work/harness.s" --proc RV32_ISS > "$work/out"
grep -q "exited with code: 0" "$work/out"
python3 measurements/experiments/render_model.py dump "$work/out" $STATE
