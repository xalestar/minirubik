#!/bin/sh
# Stage 1 measurements on the pinned Ripes build (macOS: /usr/bin/time -l).
#  1. Host bytes per guest byte: a loop of sw over N bytes, peak RSS of the
#     Ripes process for several N; the slope between sizes is the ratio.
#  2. Retired instructions per second for each processor model, from the
#     model's own --exectime, on the 1 MiB loop (786,437 instructions).
#   RIPES=/path/to/Ripes sh measurements/stage1.sh
set -e
RIPES=${RIPES:-ripes}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
loop() {
    cat > "$work/mem_$1.s" <<ASM
.text
    li t0, 0x10000000
    li t1, $1
    add t1, t0, t1
loop:
    sw t0, 0(t0)
    addi t0, t0, 4
    bne t0, t1, loop
    li a7, 10
    ecall
ASM
}
echo "guest_bytes max_rss_bytes"
for n in 4096 1048576 4194304 16777216; do
    loop $n
    /usr/bin/time -l "$RIPES" --mode cli -t asm --src "$work/mem_$n.s" \
        --proc RV32_ISS >/dev/null 2>"$work/time"
    echo "$n $(awk '/maximum resident/ { print $1 }' "$work/time")"
done
echo "processor iret model_ms iret_per_second"
for proc in RV32_ISS RV32_SS RV32_5S RV32_6S_DUAL; do
    out=$("$RIPES" --mode cli -t asm --src "$work/mem_1048576.s" --proc $proc \
        --iret --exectime 2>&1)
    iret=$(echo "$out" | sed -n '/retired/{n;p;}')
    ms=$(echo "$out" | sed -n '/time (ms)/{n;p;}')
    echo "$proc $iret $ms $(awk -v i=$iret -v m=$ms 'BEGIN { printf "%.0f", i / m * 1000 }')"
done
