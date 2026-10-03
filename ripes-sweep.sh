#!/bin/sh
# Runs the CLI build of rubik.s on every state in distance11.txt on Ripes
# (RV32_ISS by default) and prints "state instructions-retired exit-code",
# one line per state in sorted order (ERROR if Ripes printed no count),
# then a summary on stderr. The pass condition of the
# assignment is the worst case over these states, so all 2,644 are run.
#   RIPES=/path/to/Ripes ./ripes-sweep.sh [jobs] > sweep.txt
set -e
RIPES=${RIPES:-ripes}
PROC=${PROC:-RV32_ISS}
JOBS=${1:-8}
SAMPLE=21345671111111
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -s distance11.txt rubik-cli.s CASES=$SAMPLE:11 >/dev/null
cp rubik-cli.s "$work/template.s"
make -s rubik-cli.s >/dev/null # restore the default test cases
export RIPES PROC work SAMPLE
xargs -P "$JOBS" -n 1 sh -c '
    sed "s/$SAMPLE/$1/" "$work/template.s" > "$work/$1.s"
    out=$("$RIPES" --mode cli -t asm --src "$work/$1.s" --proc "$PROC" --iret 2>&1)
    rm -f "$work/$1.s"
    iret=$(echo "$out" | sed -n "/instructions retired/{n;p;}")
    code=$(echo "$out" | sed -n "s/.*exited with code: //p")
    echo "$1 ${iret:-ERROR} ${code:-ERROR}"
' sh < distance11.txt | sort | tee "$work/out" 
awk '{ if ($3 != "0") bad++; sum += $2; if ($2 > max) { max = $2; at = $1 } }
     END { printf "%d states, %d failed, mean %.0f, worst %d at %s\n",
           NR, bad, sum / NR, max, at }' "$work/out" >&2
