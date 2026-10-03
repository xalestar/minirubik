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
  compiled out. `la` expands to `auipc` + `addi` there as in Ripes.
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

All six sweeps: 2,644 states, 0 failures.

For the nibble experiment, `pattern_dist` in `tables.s` was also repacked two
entries per byte, the even `oS` in the low nibble, 8 bytes per permutation
(40,320 bytes instead of 80,640). Every one of the 80,640 packed lookups
was checked against the unpacked table (gate H4) before the run.

## Reference vector `21345671111111` and code size

| Version | iret | `.text` | Static data (`.data` + `.bss`) |
| :--- | ---: | ---: | ---: |
| gcc -O2, v1 C (`ripes_ref.c` at `d21851e`) | 12,554,456 | 1,308 | 40,548 `.rodata` |
| v1 `d21851e` | 4,430,785 | 1,116 | 46,350 + 192 |
| r1 (dropped) | 4,469,353 | 1,200 | |
| r2 `f7b122b` | 4,365,991 | 1,140 | |
| r3 `b38b0ee` | 3,936,562 | 1,192 | 46,350 + 192 |
| gcc -O2, v2 C (`ripes_ref.c` at `637b365`) | 3,095,385 | 1,500 | 119,604 `.rodata` |
| v2 `80b9446` | 1,054,871 | 1,448 | 121,332 + 392 |
| v2 nibble (dropped) | 1,238,511 | 1,512 | 80,964 + 392 |

gcc reference: `riscv64-elf-gcc` 16.2.0 (Homebrew; the same compiler as
`riscv64-unknown-elf-gcc`, different target triple name),
`-O2 -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -static`, run on
Ripes as an ELF. On the worst v2 state `41752632313211` it retires
10,137,571 instructions against 3,455,334 for `rubik.s`.

## T7: the test cases on the pipelined models

`make ripes-check RIPES=...` at `80b9446`, three test cases (solved, the
3-move scramble `24173562322133`, the reference vector) in one run:

| Model | Exit | Cycles | iret |
| :--- | :---: | ---: | ---: |
| `RV32_ISS` | 0 | 1,056,419 | 1,056,419 |
| `RV32_5S` | 0 | 1,277,660 | 1,056,418 |
| `RV32_6S_DUAL` | 0 | 1,253,853 | 1,056,418 |

`RV32_ISS` reports one instruction more than the pipelined models. A
4-instruction test (`addi`, `li a0, 0`, `li a7, 93`, `ecall`, followed by two
more instructions that never run) gives 5 on `RV32_ISS` and 4 on `RV32_5S`
and `RV32_6S_DUAL`; with the `ecall` as the last instruction of `.text`, all
three report 4. So the ISS counts one extra when the halting `ecall` is not
the last instruction, which is the case in `rubik.s`. Every `RV32_ISS`
figure here therefore includes that one extra instruction; it is left in,
since the assignment defines the measure as `--iret` on `RV32_ISS`.
