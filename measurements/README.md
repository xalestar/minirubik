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
| `sweep-r4.txt` | `f881296` orientation test first | 1,150,121 | 3,397,155 | `41752632313211` |
| `sweep-v3.txt` | `c11831f` pattern database through two rotations | 458,634 | 1,345,083 | `21354672313211` |
| `sweep-v4.txt` | `05aafc2` v4: 2-bit pattern database over block codes, three views | 14,723 | 35,794 | `26154372332213` |
| `sweep-r5.txt` | `b5990e0` r5: a child's first face starts from the registers | 14,343 | 34,730 | `26154372332213` |
| `sweep-r6.txt` | `cec1eea` r6: shorter descent and pop | 14,277 | 34,468 | `26154372332213` |
| `sweep-r7.txt` | `f83c138` r7: the walk home skips the face of the step before | 13,887 | 34,035 | `26154372332213` |
| `sweep-r8.txt` | `f803bf3` r8: the rotated views turn only when the first test passes | 13,559 | 32,825 | `26154372332213` |
| `sweep-r9.txt` | `555f1d3` r9: the farthest view of the root is searched first | 13,377 | 26,787 | `21354672313211` |
| `sweep-r10.txt` | r10: one block per face in the walk home | 13,169 | 26,580 | `21354672313211` |

All fifteen sweeps: 2,644 states, 0 failures. The v3 worst case, 1,345,083,
is 2.7% of the 5 x 10^7 limit; the v4 worst case, 35,794, is 0.07%. No
state is slower in `sweep-v3.txt` than in `sweep-r4.txt`; the ratio per
state runs from 1.70 to 3.81. No state is slower in `sweep-v4.txt` than in
`sweep-v3.txt`; that ratio runs from 13.6 to 78.5.

For the nibble experiment, `pattern_dist` was also repacked two entries per
byte, the even `oS` in the low nibble, 8 bytes per permutation (40,320 bytes
instead of 80,640), by `experiments/nibble.py`, which first checks the
packed accessor against the unpacked table at all 80,640 indices, even and
odd (the H4 method). Its docstring gives the commands that reproduce the
1,238,511 below.

Since `74376ec` the sweep script sorts its output; the committed sweeps
up to `sweep-r4.txt` are in completion order, and `sort` makes them
comparable; `sweep-v3.txt` is sorted.

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
| `rubik.s` at `34a5e0f` (unchanged since `f881296`) | 1,048,184 | 1,448 | 121,332 + 392 |
| GUI build at `34a5e0f` (`rubik-gui.s`, renderer in) | | 2,172 | 121,470 + 520 |
| gcc -O2, v3 C (`ripes_ref.c` at `1eb4166`) | 1,461,106 | 2,480 | 119,604 `.rodata` + 19 `.sdata` |
| v3 `c11831f` | 464,819 | 2,316 | 121,348 + 520 |
| v3 GUI build (`rubik-gui.s`, renderer in) | | 3,024 | 121,486 + 648 |
| gcc -O2, v4 C (`ripes_ref.c`) | 49,983 | 2,492 | 123,650 `.rodata` + 35 `.sdata` |
| v4 `rubik.s` at `05aafc2` | 13,540 | 2,436 | 128,172 + 448 |
| r5 `b5990e0` | 13,211 | 2,440 | 128,172 + 448 |
| r6 `cec1eea` | 13,143 | 2,436 | 128,172 + 480 |
| gcc -O2, r7 C | 48,693 | 2,528 | 123,650 `.rodata` + 35 `.sdata` |
| r7 `f83c138` | 12,560 | 2,448 | 128,172 + 480 |
| gcc -O2, r8 C | 42,271 | 2,928 | 123,650 `.rodata` + 35 `.sdata` |
| r8 `f803bf3` | 12,540 | 2,488 | 128,172 + 480 |
| gcc -O2, r9 C | 39,093 | 3,092 | 123,650 `.rodata` + 35 `.sdata` |
| r9 `555f1d3` | 11,957 | 2,692 | 128,172 + 480 |
| r10 | 11,728 | 2,804 | 128,172 + 480 |
| v4 GUI build (`rubik-gui.s`, renderer in) | | 3,128 | 128,310 + 576 |

The GUI build is not measured with `--iret` (the CLI cannot assemble it),
but its static data, 122,134 bytes for v3 and 128,886 for v4, is also
within the 131,072 limit; the v4 CLI build has 128,620. The GUI object is
assembled for its sizes with `--defsym` for the three `LED_MATRIX_0_*`
symbols.

The v4 static data: `pattern` 123,480 bytes (630 rows of 196: three row
addresses, 729 values of 2 bits, one byte of padding), `orient_turn` 4,376,
`next_state` 144, the rotation 16, strings, test cases, move names and
`pair_base` 156; `.bss` is 13 frame slots of 32 bytes and the 32-byte
buffer for the rotated strings. The C build keeps the same 2-bit rows
without the addresses (115,290 bytes) and a separate `block_turn`.

gcc reference: `riscv64-elf-gcc` 16.2.0 (Homebrew; the same compiler as
`riscv64-unknown-elf-gcc`, different target triple name),
`-O2 -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -static`, run on
Ripes as an ELF. On the worst state `41752632313211` the final C retires
10,079,439 instructions against 3,397,155 for `rubik.s` (C at `f881296`:
10,079,394; v2 C: 10,137,571 against 3,455,334). These gcc figures are for
that one state, the worst state of the assembly; the gcc build was not
swept over all 2,644 states (the reviewer's sweep of the `f881296` build
found the same worst state). The v3 C retires 4,232,771 instructions on
`21354672313211`, the worst state of the v3 assembly, against 1,345,083,
and 3,624,686 on `41752632313211`. The v4 C retires 150,890 instructions
on `26154372332213`, the worst state of the v4 assembly, against 35,794.

From v3 on, `ripes_ref.c` loads `gp` first: gcc places the three small
`sym_*` tables in `.sdata`, the linker reaches them through `gp`, and
Ripes starts at `_start` with `gp` not pointing there. Without it the
program does not terminate.

## T7: the test cases on the pipelined models

`make ripes-check RIPES=...`, three test cases (solved, the 3-move
scramble `24173562322133`, the reference vector) in one run. v4:

| Model | Exit | Cycles | iret |
| :--- | :---: | ---: | ---: |
| `RV32_ISS` | 0 | 17,323 | 17,323 |
| `RV32_5S` | 0 | 21,361 | 17,322 |
| `RV32_6S_DUAL` | 0 | 21,115 | 17,322 |

Alone, the solved case takes 1,240 instructions, the 3-move scramble 2,569
and an invalid string (`1234567111111a`) 192, with exit code 2.

v3 (`c11831f`) and before:

| Model | Exit | Cycles | iret | Cycles at `f881296` | Cycles at `80b9446` |
| :--- | :---: | ---: | ---: | ---: | ---: |
| `RV32_ISS` | 0 | 469,171 | 469,171 | 1,049,702 | 1,056,419 |
| `RV32_5S` | 0 | 579,884 | 469,170 | 1,287,143 | 1,277,660 |
| `RV32_6S_DUAL` | 0 | 590,103 | 469,170 | 1,260,325 | 1,253,853 |

r4 (`f881296`) retires 0.6% fewer instructions than `80b9446` but takes
0.7% more cycles on `RV32_5S`. With v3, `RV32_6S_DUAL` takes more cycles
than `RV32_5S`, the reverse of r4.

`RV32_ISS` reports one instruction more than the pipelined models. A
4-instruction test (`addi`, `li a0, 0`, `li a7, 93`, `ecall`, followed by two
more instructions that never run) gives 5 on `RV32_ISS` and 4 on `RV32_5S`
and `RV32_6S_DUAL`; with the `ecall` as the last instruction of `.text`, all
three report 4. So the ISS counts one extra when the halting `ecall` is not
the last instruction, which is the case in `rubik.s`. Every `RV32_ISS`
figure here therefore includes that one extra instruction; it is left in,
since the assignment defines the measure as `--iret` on `RV32_ISS`.

## v4: where the instructions went, and which table

* `experiments/v3-nodes.c`, output `experiments/v3-nodes.txt`: a host model
  of the v3 search loop that adds up the length of every basic block the
  assembly passes. Measured minus model is 2,454 to 2,528 for all 2,644
  states, the part outside the loop. Worst state: 39,719 children, 6,622
  nodes, 33.8 instructions per child; 35.5% the three turn lookups and the
  first test, 35.1% the two rotated lookups, 25.9% spill, pop and face
  loop, 3.5% the orientation test. Nodes peak at depth 4 and 5 of 11. It
  includes the v3 `ida.c`, so it builds at `1e6823e`.
* `experiments/v4-heuristics.c`, output `experiments/v4-heuristics.txt`:
  IDA* node counts over the distance-11 states for tables over other keys,
  each looked up through 2, 3 or 6 symmetric images of the state. Children
  generated for the worst state:

  | Key | Entries | Bytes | Views | Worst | Mean |
  | :--- | ---: | ---: | :---: | ---: | ---: |
  | permutation, twists of cubies 0 3 (v3, no orientation test) | 45,360 | 80,640 | 3 | 42,041 | 14,074 |
  | the same | | | 6 | 27,182 | 8,862 |
  | permutation, twists of 3 cubies | 136,080 | 68,040 at 4 bits | 3 | 12,761 | 4,328 |
  | the same | | | 6 | 8,879 | 2,934 |
  | permutation, twists of 4 cubies | 408,240 | 102,060 at 2 bits | 3 | 4,157 | 1,164 |
  | positions of 3 cubies, orientation | 153,090 | 76,545 at 4 bits | 3 | 9,807 | 1,647 |
  | blocks 3 + 3 + 1 not told apart, parity, orientation | 102,060 | 102,060 | 6 | 4,878 | 1,399 |
  | blocks 3 + 3 + 1, parity, orientation | 204,120 | 102,060 at 4 bits | 3 | 3,654 | 1,130 |
  | the same | | | 6 | 1,679 | 625 |
  | blocks 3 + 2 + 2, parity, orientation | 306,180 | 76,545 at 2 bits | 3 | 1,842 | 598 |
  | the same | | | 6 | 1,002 | 369 |
  | three pairs + 1, orientation (no parity) | 459,270 | 114,818 at 2 bits | 3 | 1,919 | 650 |
  | **pairs {0,6} {1,5} not told apart, {2,4}, 3, parity, orientation (v4)** | **459,270** | **114,818 at 2 bits** | **3** | **903** | **317** |
  | the same | | | 2 | 4,092 | 648 |
  | the same, best labelling for 6 views ({0,3} {1,4}, {5,6}, 2) | 459,270 | | 6 | 786 | 222 |
  | blocks 3 + 2 + 2 and 3 + 3 + 1 together | 510,300 | 127,575 at 2 bits | 3 | 1,224 | 429 |

  Every labelling of each block shape was run (2,520 and 2,870 runs); the
  table has the best of each. The permutation with 4 twists does not fit
  next to its 30,240 bytes of `perm_turn`. With all 6 views the v4 table
  itself gains nothing (903), and the labelling that does gain needs 6
  lookups per node for 13% fewer children.
* `experiments/v4-nodes.c`, output `experiments/v4-nodes.txt`: the same
  kind of model for the v4 loop at `05aafc2`. Worst state: 903 children, 155 nodes;
  27.8% turning the three views (11 per child), 40.1% the tests (9 per view
  reached; a child reaches 1.00, 0.42 and 0.25 of them on average), 22.0%
  per node, and 10.0% (3,591) outside the loop. Over all states the part
  outside the loop is 3,264 to 4,146 instructions, 25.2% of the mean: it
  includes the walks home from the three root keys, 118 keys tried on
  average at 13 instructions each.
* `make gates` at v4: H3 over all states takes 4.6 s (v3: 32.5 s); it also
  checks that the walk home gives the BFS distance for all 459,270 keys.
  Per distance-11 state, 317 children: the state's own view prunes 184
  (57.9%), the view rotated once 56, rotated twice 23, and 57 nodes are
  expanded.

## After v4: shorter paths through the same search

Each row is one commit on top of v4, with the same tables. Up to r8 the
node counts do not change and the worst state is `26154372332213`; r9
changes which view is tested first, and with it the order of the faces.

| Sweep | Change | Counted before | Mean | Worst |
| :--- | :--- | :--- | ---: | ---: |
| `sweep-v4.txt` | | | 14,723 | 35,794 |
| `sweep-r5.txt` | r5: a node that was just a child starts its first face from the registers, at the face after its parent's: no 6 loads, no skip test | 7 for each of 152 descents: -1,064 | 14,343 | 34,730 |
| `sweep-r6.txt` | r6: the face block of a passing child sets the parent's face itself (no `mv`); the pop of the root finds face 3 in the slot in front and goes to `deepen` (no root test in every pop) | 1 for each of 152 descents and of 142 pops (10 nodes are on the answer and never pop), +16 for each of the 2 pops of a root: -262 | 14,277 | 34,468 |
| `sweep-r7.txt` | r7: the walk home from a root key does not try the face of the step before; a closer neighbour is never there. `make gates`: 85 keys tried per state instead of 118, and still the BFS distance for all 459,270 keys | 13 for each key not tried, 2 more for each step: 108 keys become 72 for the worst state, over 23 steps | 13,887 | 34,035 |
| `sweep-r8.txt` | r8: the two rotated views are turned only for a child that passes the test of the state's own view; they then catch up, both together, on the turns they missed (6 a turn, 2 to count). `make gates`: 355 turns of a rotated view per state instead of 635 | worst state: 470 catch-up steps of 8, 1 for each of 304 face blocks and 142 pops, against 6 for each of 903 children: 4,206 against 5,418 | 13,559 | 32,825 |
| `sweep-r9.txt` | r9: the view of the root that is farthest from solved is searched as the state's own view, the one every child is tested with first; of two views equally far, the one whose follower is farther. The other two keep their cyclic order, so this is the search of the rotated cube, and the faces are turned back when the answer is found. `make gates`: the first test prunes 61.2% of the children instead of 57.9% | loop cost in the model, worst and mean over all states: 29,807 and 10,280 with the state's own view first; 29,139 and 10,021 with the first of the farthest views; 23,563 and 10,011 with the tie rule | 13,377 | 26,787 |
| `sweep-r10.txt` | r10: the walk home has one block of code per face, like the search: the turn is a load with a fixed offset, and no face offset and table address are stepped | worst state: the three walks take 1,560 instructions, 12 instead of 13 for each of about 90 keys tried, and less around each face and step | 13,169 | 26,580 |

r7, r8 and r9 also change `ida.c`, so gcc has new figures on the worst
state of the assembly: 149,930 at r7, 119,709 at r8 and 97,416 at r9
(`21354672313211`; v4: 150,890).

At r8 two states, `14325671111111` and `54721631111111`, have root
distances 9, 7, 9. With the first view first they take 32,360 and 32,361
instructions, and every other state at most 26,776; with the third view
first, followed by the first, they take about 15,000. That is what the tie
rule is for, and the mean does not pay for it (13,377 against 13,366
without the rule).

From r9 on, `replay` applies the faces as they are printed to the views of
the state as given, not the views as searched, so a mistake in turning the
faces back fails the case. The printed moves of 43 states were also applied
to the cube arrays of `solver.c` on the host: all solve.

For r8, `experiments/v4-nodes.c` was not rebuilt; the counts are from a
copy of its search with the catch-up added. With one counter for each
rotated view (the second view waits for the first to pass) it counted
5,067, so that form was not built. On `RV32_5S` the three test cases take
20,507 cycles for 16,219 instructions at r8 and 20,190 for 16,275 at r7:
fewer instructions, more cycles.

## Smaller experiments

* `experiments/heuristics.c`, output `experiments/heuristics.txt`: IDA*
  node counts over all distance-11 states for each candidate abstraction,
  the data behind the choice of heuristic. Run from the repository root:
  `cc -O2 -w measurements/experiments/heuristics.c -o heuristics && ./heuristics`.
* `experiments/r1-nodes.c`, output `experiments/r1-nodes.txt`: why r1 was
  slower. A host model of the v1 search loop counts what
  `r1-leaf-loop.patch` changes. On `21345671111111` v1 enters 38,998 nodes
  and 124 of them have `rem = 0`; r1 adds one `beqz` at every node and two
  `mv` at each of the 247 faces of those 124, and saves 1, 4 and 6
  instructions on 678, 60 and 1 of their children: +38,568, the difference
  between `sweep-r1.txt` and `sweep-v1.txt` for that state. For
  `54721631111111` the same count gives +105,171, again the measured
  difference. Run from the repository root:
  `cc -O2 -w measurements/experiments/r1-nodes.c -o r1-nodes && ./r1-nodes`.
* Pruning order, mean per distance-11 state with the v2 heuristic, printed
  by `make gates` at `34a5e0f` (and by a `gates` built with
  `-DPATTERN_FIRST` for the other order): pattern test first prunes 37,978
  of 51,185 children (74.2%) and needs the second lookup 13,207 times;
  orientation test first prunes 23,619 (46.1%) and needs the second lookup
  27,566 times. With 6 and 3 RV32I instructions per test, orientation first
  is cheaper, which r4 confirmed.
* The same with the v3 heuristic, printed by `make gates` (and by a `gates`
  built with `-DORIENT_FIRST`): of 13,387 children, pattern test first
  prunes 7,811 (58.3%) and needs the second lookup 5,576 times; orientation
  test first prunes 2,201 (16.4%) and needs it 11,185 times. Pattern first
  is now cheaper, 6 + 0.42 x 3 = 7.25 instructions a child against
  3 + 0.84 x 6 = 8.01; only that order was built in assembly. After both
  tests, the lookup through one rotation prunes 1,867 children and through
  the other 920, with 16,323 quarter turns of rotated coordinates, and
  2,234 nodes are expanded. H3 over all states takes 29.7 s.
* Bound step, also printed by `make gates`: an IDA* that raised the bound
  to the smallest pruned f instead of by 1 would skip one bound value in
  4,010 of the 3,674,160 states, and in none of the 2,644 at distance 11.
  With the v3 heuristic: 20,955 states, 15 of them at distance 11.
* `experiments/symmetry.c`, output `experiments/symmetry.txt` (it reads
  the v3 `tables.h`, so it builds at `f5d417a`): the 48
  symmetries of the cube applied to every state, the data behind v3. Each
  of them, and inversion, preserves the distance on all 3,674,160 states.
  The states fall into 1,224,828 classes under the 3 rotations about the
  fixed corner, 612,630 under the 6 symmetries that fix it, 77,802 under
  all 48 and 40,296 with inversion as well. Only one map besides the
  identity acts on the index of `pattern_dist`, so the table itself can
  shrink to 22,752 entries at most. Taking the maximum of the heuristic
  over the images instead, generated children per distance-11 state, mean
  and worst: 51,185 and 150,335 without symmetry; 13,387 and 39,719 with
  the two rotations (v3); 8,534 and 26,198 with all 6; 4,028 and 11,408
  with all 48; against 22,845 and 96,510 for the three-cubie pattern
  database, which does not fit. Only the two rotations were built: they
  map each face turn to a face turn, so the rotated coordinates advance
  through the existing turn tables.
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
