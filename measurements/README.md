# Measurements

Every figure in the write-up comes from a file here or from a command that
reproduces it. Unless stated otherwise:

* Ripes `v2.2.6-106-g5b8a616` (the `continuous` prerelease, macOS universal2,
  run natively on arm64), CLI mode, no `--isaexts` (plain RV32I: `mul` is
  rejected by the assembler).
* Retired instructions are Ripes `--iret` on `RV32_ISS`, renderer compiled
  out (`RENDER=0`), one input state per run.
* Code size is the `.text` section of `rubik-cli.s` assembled with
  `riscv64-elf-as -march=rv32i -mabi=ilp32` (binutils 2.47), renderer
  compiled out. `la` expands to `auipc` + `addi` there as in Ripes, and the
  `.text` layout matches Ripes'.
* Static data is `.data` + `.bss` of the same object built with the default
  three test cases (`make rubik-cli.s`). Since `74376ec` the source uses no
  `.align`, so GNU as and Ripes lay `.data` out identically; before that,
  Ripes (which reads `.align 2` as 2 bytes, not 4) placed the test-case
  table 2 bytes earlier than GNU as.
* Host: MacBook Pro, Apple M5 Pro, 24 GB.

## Stage 1: the target

`stage1.sh` writes a loop of `sw` over N bytes and records the peak RSS of
the Ripes process (`/usr/bin/time -l`), then runs the 1 MiB loop on each
processor model with `--exectime`. Output: `stage1.txt`.

| Guest bytes written | Ripes max RSS |
| ---: | ---: |
| 4,096 | 68,616,192 |
| 1,048,576 | 125,878,272 |
| 4,194,304 | 306,036,736 |
| 16,777,216 | 1,026,506,752 |

Slope 1 MiB to 4 MiB: 57.27 host bytes per guest byte; 4 MiB to 16 MiB:
57.26. The baseline's 18,405,414-byte peak would cost about
57.26 x 18,405,414 = 1.05 GB of host memory.

| Model | iret / s (`--exectime`) |
| :--- | ---: |
| `RV32_ISS` | 24,576,156 |
| `RV32_SS` | 2,080,521 |
| `RV32_5S` | 442,564 |
| `RV32_6S_DUAL` | 152,145 |

A first run of the same programs gave 25.4 M, 2.24 M, 466 K and 154 K: the
rates move by a few percent between runs, the ratios between models do not.
The `RV32_ISS` figure rests on a 32 ms `--exectime` reading, so it is good to
about ±3%.

## Worst case over the 2,644 distance-11 states

`../ripes-sweep.sh` (or `make ripes-sweep RIPES=...`) runs every state in
`distance11.txt` (written by `gen`) and prints `state iret exit-code`. An
exit code of 0 means the program validated its own answer: the path replays
to solved and has the expected length 11.

| File | Code | Mean | Worst | Worst state |
| :--- | :--- | ---: | ---: | :--- |
| `sweep-v1.txt` | `d21851e` first RV32I version | 3,926,124 | 12,113,768 | `54721631111111` |
| `sweep-r1.txt` | `experiments/r1-leaf-loop.patch` on `d21851e` (dropped) | 3,960,205 | 12,218,939 | `54721631111111` |
| `sweep-r2.txt` | `f7b122b` | 3,868,784 | 11,936,538 | `54721631111111` |
| `sweep-r3.txt` | `b38b0ee` | 3,489,809 | 10,762,227 | `54721631111111` |
| `sweep-v2.txt` | `80b9446` pattern database | 1,177,899 | 3,455,334 | `41752632313211` |
| `sweep-v2-nibble.txt` | `experiments/nibble-pattern.patch` on `80b9446` (dropped) | 1,382,642 | 4,056,678 | `41752632313211` |
| `sweep-r4.txt` | `f881296` orientation test first (final) | 1,150,121 | 3,397,155 | `41752632313211` |

All seven sweeps: 2,644 states, 0 failures. The final worst case,
3,397,155, is 6.8% of the 5 x 10^7 limit.

For the nibble experiment, `pattern_dist` was also repacked two entries per
byte, the even `oS` in the low nibble, 8 bytes per permutation (40,320 bytes
instead of 80,640), by `experiments/nibble.py`, which first checks the
packed accessor against the unpacked table at all 80,640 indices, even and
odd (the H4 method). Its docstring gives the commands that reproduce the
1,238,511 below.

Since `74376ec` the sweep script sorts its output; the committed sweeps
are in completion order, and `sort` makes them comparable. A fresh sweep
of the final code is identical to `sweep-r4.txt` after sorting.

## Reference vector `21345671111111` and code size

| Version | iret | `.text` | Static data (`.data` + `.bss`) |
| :--- | ---: | ---: | ---: |
| gcc -O2, v1 C (`ripes_ref.c` at `d21851e`) | 12,554,456 | 1,308 | 40,548 `.rodata` |
| v1 `d21851e` | 4,430,785 | 1,116 | 46,398 + 192 |
| r1 (dropped) | 4,469,353 | 1,200 | |
| r2 `f7b122b` | 4,365,991 | 1,140 | |
| r3 `b38b0ee` | 3,936,562 | 1,192 | 46,398 + 192 |
| gcc -O2, v2 C (`ripes_ref.c` at `637b365`) | 3,095,385 | 1,500 | 119,604 `.rodata` |
| v2 `80b9446` | 1,054,871 | 1,448 | 121,332 + 392 |
| v2 nibble (dropped) | 1,238,511 | 1,512 | 81,012 + 392 |
| gcc -O2, C at `f881296` | 3,088,700 | 1,508 | 119,604 `.rodata` |
| r4 `f881296` | 1,048,184 | 1,448 | 121,332 + 392 |
| gcc -O2, final C (`4867974`, parse validates first) | 3,088,745 | 1,540 | 119,604 `.rodata` |
| final `rubik.s` (unchanged since `f881296`) | 1,048,184 | 1,448 | 121,332 + 392 |
| final GUI build (`rubik-gui.s`, renderer in) | | 2,172 | 121,470 + 520 |

The GUI build is not measured with `--iret` (the CLI cannot assemble it),
but its static data, 121,990 bytes, is also within the 131,072 limit.

gcc reference: `riscv64-elf-gcc` 16.2.0 (Homebrew; the same compiler as
`riscv64-unknown-elf-gcc`, different target triple name),
`-O2 -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -static`, run on
Ripes as an ELF. On the worst state `41752632313211` the final C retires
10,079,439 instructions against 3,397,155 for `rubik.s` (C at `f881296`:
10,079,394; v2 C: 10,137,571 against 3,455,334). These gcc figures are for
that one state, the worst state of the assembly; the gcc build was not
swept over all 2,644 states (the reviewer's sweep of the `f881296` build
found the same worst state).

## T7: the test cases on the pipelined models

`make ripes-check RIPES=...`, three test cases (solved, the 3-move
scramble `24173562322133`, the reference vector) in one run:

| Model | Exit | Cycles | iret | Cycles at `80b9446` |
| :--- | :---: | ---: | ---: | ---: |
| `RV32_ISS` | 0 | 1,049,702 | 1,049,702 | 1,056,419 |
| `RV32_5S` | 0 | 1,287,143 | 1,049,701 | 1,277,660 |
| `RV32_6S_DUAL` | 0 | 1,260,325 | 1,049,701 | 1,253,853 |

r4 retires 0.6% fewer instructions than `80b9446` but takes 0.7% more
cycles on `RV32_5S`.

`RV32_ISS` reports one instruction more than the pipelined models. A
4-instruction test (`addi`, `li a0, 0`, `li a7, 93`, `ecall`, followed by two
more instructions that never run) gives 5 on `RV32_ISS` and 4 on `RV32_5S`
and `RV32_6S_DUAL`; with the `ecall` as the last instruction of `.text`, all
three report 4. So the ISS counts one extra when the halting `ecall` is not
the last instruction, which is the case in `rubik.s`. Every `RV32_ISS`
figure here therefore includes that one extra instruction; it is left in,
since the assignment defines the measure as `--iret` on `RV32_ISS`.

## Smaller experiments

* `experiments/heuristics.c`, output `experiments/heuristics.txt`: IDA*
  node counts over all distance-11 states for each candidate abstraction,
  the data behind the choice of heuristic. Run from the repository root:
  `cc -O2 -w measurements/experiments/heuristics.c -o heuristics && ./heuristics`.
* Pruning order, mean per distance-11 state with the v2 heuristic, printed
  by `make gates` (and by a `gates` built with `-DPATTERN_FIRST` for the
  other order): pattern test first prunes 37,978 of 51,185 children
  (74.2%) and needs the second lookup 13,207 times; orientation test first
  prunes 23,619 (46.1%) and needs the second lookup 27,566 times. With 6
  and 3 RV32I instructions per test, orientation first is cheaper, which r4
  confirmed.
* Bound step, also printed by `make gates`: an IDA* that raised the bound
  to the smallest pruned f instead of by 1 would skip one bound value in
  4,010 of the 3,674,160 states, and in none of the 2,644 at distance 11.
* The r2 prediction used v1's pass rate: 34,439 expanded of 206,618
  generated children per distance-11 state (`experiments/heuristics.txt`),
  so about 83% of children are pruned.
* `experiments/render_model.py`: the 3-D sticker model behind the renderer.
  It checks that the layer rotations reproduce `solver.c`'s `source` and
  `twist`, that `render.s`'s two tables equal the model's, and that the
  renderer's formula agrees with moving the stickers directly on 2,000
  random move sequences. `experiments/led-harness.sh` runs the GUI build in
  the CLI with the LED symbols mapped to memory and has the model compare
  all 875 LEDs after the scramble and after solving.
* `experiments/branchless-mod3.patch`: the twist sum in `parse` reduced
  with `addi`/`srai`/`andi`/`add` instead of a conditional branch. On top
  of `f881296`:

  | Case | Branch iret | Branchless iret | Branch 5S cycles | Branchless 5S cycles |
  | :--- | ---: | ---: | ---: | ---: |
  | `12345671111111` | 551 | 572 | 773 | 780 |
  | `21345671111111` | 1,048,184 | 1,048,205 | 1,285,087 | 1,285,094 |

  The reduction runs 7 times per query, only in `parse`: the search loop
  never computes a twist, since orientation arithmetic lives in the
  tables. The branch version wins on both measures and stays.
