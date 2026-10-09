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
| `sweep-r10.txt` | `b8612ad` r10: one block per face in the walk home | 13,169 | 26,580 | `21354672313211` |
| `sweep-r11.txt` | r11: shorter set-up around solve | 13,156 | 26,567 | `21354672313211` |
| `sweep-r12.txt` | r12: the key whose relabellings are at most 4 moves from solved | 9,957 | 21,265 | `42651372213311` |
| `sweep-r13.txt` | r13: the 2-bit value read from a word, shifted by the orientation offset itself | 9,530 | 20,273 | `42651372213311` |
| `sweep-r14.txt` | r14: a child with no slack in any of the three views is cut | 7,490 | 15,459 | `32674152113333` |
| `sweep-r15.txt` | r15: the root distances by turning the orientation home | 6,509 | 14,512 | `32674152113333` |
| `sweep-r16.txt` | r16: the first view's three turns unrolled, pops through a stored address | 6,378 | 13,986 | `32674152113333` |
| `sweep-r17.txt` | r17: at bound 11 the search starts at the first move with the most nodes below it at bound 10 | 6,432 | 13,064 | `73542163321133` |

All twenty-two sweeps of kept or earlier versions: 2,644 states, 0 failures. The v3 worst case, 1,345,083,
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
| r10 `b8612ad` | 11,728 | 2,804 | 128,172 + 480 |
| r11 | 11,715 | 2,760 | 128,172 + 480 |
| gcc -O2, r12 C | 15,422 | 3,040 | |
| r12 | 5,396 | 2,740 | 128,180 + 480 |
| r13 (gcc as at r12: the C did not change) | 5,209 | 2,704 | 129,660 + 480 |
| gcc -O2, r14 C | 13,457 | 3,068 | |
| r14 | 4,716 | 2,780 | 129,644 + 480 |
| gcc -O2, r15 C | 11,202 | 2,996 | |
| r15 | 3,822 | 2,644 | 129,684 + 480 |
| r16 (gcc as at r15: the C did not change) | 3,806 | 2,964 | 129,684 + 536 |
| gcc -O2, r17 C | 12,199 | 3,588 | |
| r17 | 3,831 | 3,100 | 129,684 + 536 |
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

r10, the same run:

| Model | Exit | Cycles | iret |
| :--- | :---: | ---: | ---: |
| `RV32_ISS` | 0 | 15,365 | 15,365 |
| `RV32_5S` | 0 | 19,675 | 15,364 |
| `RV32_6S_DUAL` | 0 | 19,605 | 15,364 |

Alone at r10: solved 1,255, the 3-move scramble 2,408, invalid 192. At r11
the three cases together take 15,327 instructions on `RV32_ISS`, 19,631
cycles on `RV32_5S` and 19,567 on `RV32_6S_DUAL`.

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
  kind of model for the v4 loop at `05aafc2`; it reads that commit's
  `ida.c`, so it builds there (`v4-ideas.c` below models the loop at r10).
  Worst state: 903 children, 155 nodes;
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
| `sweep-r11.txt` | r11: main keeps the input string and the buffer for the rotated strings in registers; `root_dist` returns through `a7`, so `solve` does not save `ra`, and takes the constant 1 from `solve`; `replay` reads the solved row that `solve` stored | 13 instructions a run, 11 instructions of code | 13,156 | 26,567 |

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

On `RV32_5S` the three test cases take 20,507 cycles for 16,219
instructions at r8 and 20,190 for 16,275 at r7: fewer instructions, more
cycles.

### Built, swept and dropped

Six variants were built on r10 (`b8612ad`) as changes to `rubik.s` only and
swept on Ripes. Each patch is in `experiments/` and applies to that commit.
All six sweeps: 2,644 states, 0 failures.

| Sweep | Patch | Mean | Worst | Worst state | `.text` | Why dropped |
| :--- | :--- | ---: | ---: | :--- | ---: | :--- |
| `sweep-r10.txt` | (r10) | 13,169 | 26,580 | `21354672313211` | 2,804 | |
| `sweep-c9-tail-order.txt` | `c9-tail-order.patch`: the third view tested before the second | 13,190 | 26,895 | `21354672313211` | 2,804 | slower: +315 |
| `sweep-c9-face-order.txt` | `c9-face-order.patch`: face blocks in the order R, D, B | 13,203 | 26,656 | `45312672313211` | 2,804 | slower: +76 |
| `sweep-c10-unroll.txt` | `c10-unroll.patch`: the first view's three turns unrolled, dispatch on the turn | 12,882 | 26,180 | `21354672313211` | 3,152 | faster by 400 (-1.5%), but `.text` is over gcc's 3,092: check 5 fails |
| `sweep-c1-separate-counters.txt` | `c1-separate-counters.patch`: one catch-up counter for each rotated view | 13,353 | 26,856 | `21354672313211` | 2,844 | slower: +276 |
| `sweep-a9-two-views.txt` | `a9-two-views.patch`: the third view is carried but not tested | 18,648 | 97,400 | `12354761112323` | 2,672 | slower: 3.7 times |
| `sweep-c2-cannot-prune.txt` | `c2-cannot-prune.patch`: a view with slack 2 or more is tested only for a child that passes the others | 14,265 | 28,874 | `21354672313211` | 3,204 | slower: +2,294, and `.text` over gcc's |

Three more were built on r11 (`d1b8fed`), one after the other:

| Sweep | Patch | Mean | Worst | Worst state | `.text` | Why dropped |
| :--- | :--- | ---: | ---: | :--- | ---: | :--- |
| `sweep-r11.txt` | (r11) | 13,156 | 26,567 | `21354672313211` | 2,760 | |
| `sweep-c10-unroll-r11.txt` | `c10-unroll-r11.patch`: the unrolled first view again, on the shorter r11 | 12,869 | 26,167 | `21354672313211` | 3,108 | faster by 400, but `.text` is still 16 bytes over gcc's 3,092: check 5 fails |
| `sweep-c9-face-order-dbr.txt` | `c9-face-order-dbr.patch`: face blocks in the order D, B, R | 13,274 | 27,813 | `13246571111111` | 2,760 | slower: +1,246 |
| `sweep-c9-face-order-brd.txt` | `c9-face-order-brd.patch`: face blocks in the order B, R, D | 13,270 | 32,430 | `52341671111111` | 2,760 | slower: +5,863 |

The model below had 26,895, 26,694, 26,420, 26,853 and 27,434 for the
first four and the last; the unrolled loop does better than its count by
240 because the count kept the `li t4, 3` at the head of every face block,
which the unrolled loop does not need. The last one is worse
than its count because the test left out has to be flagged (a `li` for
every view reached) and looked at again for every child that passes.

### Counted and not built

`experiments/v4-ideas.c`, output `experiments/v4-ideas.txt`: a model of
the loop at r10 with a switch for each variant. For three states its loop
count equals the count of an instruction-level profile of the assembly
(23,433, 8,412 and 23,300). Each line is the worst and the mean over the
2,644 states of the measured count with the loop replaced by the variant's.

| Variant | Worst | Mean |
| :--- | ---: | ---: |
| r10 | 26,580 | 13,169 |
| the first view as given (before r9) | 32,743 | 13,438 |
| the first of the farthest views first (r9 without the tie rule) | 32,293 | 13,180 |
| every child turns the rotated views (before r8) | 27,051 | 13,546 |
| one catch-up counter for each rotated view | 26,853 | 13,351 |
| skip the test of a view with slack 2 or more, which cannot prune | 27,434 | 13,630 |
| the view with the least slack first at every node | 26,499 | 13,305 |
| the other 11 orders of the faces and of the two rotated tests | 26,694 to 32,696 | 13,190 to 13,323 |
| first view's three turns unrolled, dispatch on the turn | 26,420 | 12,991 |
| the same with a `jal` to the shared tail | 26,292 | 12,962 |
| root's children at bound 11 by least slack, ties by the largest slack | 26,217 | 13,585 |
| the same, ties in move order | 30,018 | 13,647 |
| the same, most slack first | 29,487 | 13,643 |
| the first iteration skipped, paid with three more root keys | 28,197 | 14,078 |
| the inverse state when its root keys are farther | 28,430 | 14,739 |

A state has 314.6 children on average, 31.4 of them in the first
iteration; 0.39 of its 56.8 nodes were reached before in the same
iteration; 44.9 of its 509.7 tests are of a view that cannot prune. The
unrolled forms need 108 more instructions, which would make `.text` 3,236
bytes against gcc's 3,092. Ordering the root's children moves the worst
case by -1% or +13% depending on how ties are broken, and the mean up by
3%: the order in which the last iteration meets a solution is luck, and no
key of the slacks predicts it.

Two more variants were counted in throwaway host runs on r8 and not kept
as files: a budget per first move at bound 11 with a restart in a rotated
frame (25,786 in the loop at the best budget, against 29,807; r9 reaches
23,563 in the same count), and children ordered by the size of their
subtree in the iteration before (34,592: at the root 99.2% of the largest
subtrees are a first move of a solution, but they are also the dearest to
search).

## Round 2: where the instructions of r11 go, and which key

Each row is one attempt of round 2: one change, one full sweep on Ripes
(2,644 states, 0 failures in each). Kept rows are commits; the patch of a
dropped row is in `experiments/` and applies to the row above it that was
kept.

| Sweep | Change | Counted before | Mean | Worst | Worst state | `.text` | Kept |
| :--- | :--- | :--- | ---: | ---: | :--- | ---: | :--- |
| `sweep-r11.txt` | (r11) | | 13,156 | 26,567 | `21354672313211` | 2,760 | |
| `sweep-r12.txt` | r12 (N1): the block code keeps where cubie 3 sits, where the pair {0, 6} sits, and the ring of cubies 1, 2, 5, 4 up to a turn of D, its mirror image counting as the same when 0 and 6 are swapped. The table has the same size; its largest distance is 11 (one key). `parse` no longer computes the parity of the permutation, which the old key used | `r11-keys.c`: loop worst 18,093 and mean 6,733 against 23,433 and 9,943, so about 21,300 and 9,950 | 9,957 | 21,265 | `42651372213311` | 2,740 | yes |
| `sweep-r13.txt` | r13 (N2): an orientation rank o is kept as the offset 128 * (o / 16) + 2 * (o % 16). Shifted right by 5 it is the byte offset of the word of the row that holds the value; a shift by the offset itself uses its low 5 bits, the position in the word. `orient_turn` is laid out for these offsets (a block of 128 bytes for 16 ranks, 32 of them unused): 5,856 bytes instead of 4,376. The rows of the table are the same bytes | 1 instruction less for each lookup: 601 first tests, about 350 rotated tests and 90 keys on the walks home for the worst state; 12 more in `coords` | 9,530 | 20,273 | `42651372213311` | 2,704 | yes |
| `sweep-r14.txt` | r14 (N16): the turns of D are among the relabellings of the key, and those of R and B in the rotated views. A shortest sequence that makes a state starts with a turn of some face, so in the view of that face the key is closer than the state. So a child whose three keys are all exactly as far as there are moves left needs one move more: it is cut, unless no move is left (then it is the solved state). At the root, three equal key distances start the bound one higher. `next_state` has 16 bytes a slack, so a row offset below 16 means slack 0, and 8 slacks instead of 12 (`gen` checks that a key is at most 4 closer than its state) | host model on r13: worst 15,749, mean 7,580; 9.0 children a state are cut this way and take their subtrees with them: 134.7 children a state instead of 212.8 | 7,490 | 15,459 | `32674152113333` | 2,780 | yes |
| `sweep-r15.txt` | r15 (N17): the distance of a root key is no longer found by a search for closer neighbours. `orient_turn` names for every orientation the quarter turn that brings it one quarter turn closer to home (in the 32 bytes of each block that were unused), the key is turned that way until its orientation is home, and the values modulo 3 give the change of the distance on the way. The last byte of each row, a pad until now, is the distance of the row's code at the home orientation | host count over all 459,270 keys: 6.73 quarter turns a key (at most 10) of 18 instructions, always the BFS distance; the search for neighbours tried 90 keys of 10 instructions a state, with about 26 steps around them | 6,509 | 14,512 | `32674152113333` | 2,644 | yes |
| `sweep-r16.txt` | r16 (N3, N9): each of the three turns of a face has its own copy of the first view's turn and test, so a child that fails it costs 11 instructions and no loop counter. A child that passes calls the rest of its face (`rest_R`: catch-up and the two rotated tests) with `jal`; a cut returns through that register to the next turn or the next face. The register is kept in the node's slot when a child is searched (slots are 36 bytes now), and the pop of the child jumps through it: no dispatch on the face and the turn, and after a third turn the pop lands on the next face. The bound is kept in `tp` for the length of the answer | round 1: -400 on r11 for the unrolled turns alone, with `.text` over gcc's; pops: 3 to 5 instructions less each; r15 left 352 bytes of `.text` under gcc's | 6,378 | 13,986 | `32674152113333` | 2,964 | yes |
| `sweep-r17.txt` | r17 (N6): an iteration counts the nodes below each first move and keeps the first move with the most. When the bound reaches 11, where an answer is certain, the search starts at that first move and goes on with the first moves after it; if they all fail, all first moves are searched in order. The root is told from other nodes by its parent face (16), so a pop to the root costs one branch more, and a descent one increment | `r11-ideas.txt`: the first move with the most children at bound 10 is at distance 10 in 2,624 of 2,644 states (the first in order: 2,501). Host model on r14: 14,487 to 14,787 against 15,459. C built first: 374 children for the worst state against 392, and gcc's `.text` 3,588 | 6,432 | 13,064 | `73542163321133` | 3,100 | yes |

At r12 `make gates` counts 213 children a state (r11: 315), 40 nodes
(57) and 90 keys tried on the walks home (85); H3 over all states takes
3.7 s. `gen` reports 2,207,542 states where the heuristic is exact (r11:
2,000,558) and a mean of 8.340 (8.234). On the worst state of the
assembly, `42651372213311`, the gcc build retires 74,872 instructions
against 21,265. The three test cases together take 8,825 instructions on
`RV32_ISS`, 11,554 cycles on `RV32_5S` and 11,455 on `RV32_6S_DUAL`.

At r13 the static data is 130,140 bytes for the CLI build and 130,406 for
the GUI build. `gen` checks the word accessor against the unpacked table
at all 459,270 indices, next to the byte accessor of the C build (H4). The
three test cases take 8,605 instructions on `RV32_ISS`, 11,590 cycles on
`RV32_5S` and 11,511 on `RV32_6S_DUAL`: 220 fewer instructions than r12,
36 more cycles on `RV32_5S`, where the shift now waits for the load of the
word.

At r14 gate H1 covers both parts of the heuristic: the largest of the
three key distances, and the smallest plus one. It holds for all
3,674,160 states, is exact for 2,776,830 of them (r13: 2,207,542) and has
a mean of 8.504 (8.340); the three keys are equally far in 605,930
states. `make gates`: 135 children a state, 27 nodes, H3 in 3.8 s. The gcc
build retires 55,808 instructions on the worst state of the assembly,
`32674152113333`, against 15,459. Three test cases: 8,104 instructions
on `RV32_ISS`, 10,944 cycles on `RV32_5S`, 10,862 on `RV32_6S_DUAL`.

The first build of r14 set the face register on the way to the cut, so a
node could turn the face its parent had turned. Every answer was still
optimal and every check passed; 26 states took up to 2,415 instructions
more than the host model. Since then each build is also compared with
`ida.c` by the number of children it generates (the executions of the
three first-view turns, counted by the host interpreter): r14 equals
`ida.c` for all 2,644 states.

At r15 the three root distances of the worst state take 357 instructions
(r14: 1,343), and `.text` is 2,644 bytes (r14: 2,780).
`make gates` checks it against a BFS for all 459,270 keys; a state at
distance 11 needs 18.8 quarter turns for its three keys. The gcc build
retires 53,381 instructions on `32674152113333` against 14,512. Three
test cases: 6,942 instructions on `RV32_ISS`, 9,228 cycles on `RV32_5S`,
9,165 on `RV32_6S_DUAL`. Static data: 130,164 bytes (CLI), 130,430 (GUI).

At r16 `.text` is 2,964 bytes against gcc's 2,996, so 32 bytes are left:
the unrolled copies are 75 instructions. Three test cases: 6,926
instructions on `RV32_ISS`, 9,227 cycles on `RV32_5S`, 9,188 on
`RV32_6S_DUAL`. Static data: 130,220 bytes (CLI), 130,486 (GUI); the 14
slots of 36 bytes are 56 bytes more than at r15.

At r17 a state has 133.9 children on average (r16: 134.7) and at most 374
(392). The mean rises by 54 instructions: every descent and every pop pay
one instruction, and in 915 states the first move that is tried first
costs more children than the first move in order; in 882 it costs fewer.
The first form of this idea, in C only, tried that first move first in
every iteration: 139 children on average and 429 at most, because in an
iteration that fails its subtree is searched twice. The gcc build retires
56,356 instructions on `73542163321133` against 13,064. Three test
cases: 6,956 instructions on `RV32_ISS`, 9,265 cycles on `RV32_5S`, 9,189
on `RV32_6S_DUAL`.

From r13 on, every one of the 3,674,160 states was also run through
`rubik-cli.s` on the host interpreter, with its exact distance as the
expected length of the test case: all exit with code 0. The worst state
overall is a distance-11 state (r13: 20,273; the worst at distance 10 is
16,297, at distance 9 12,435; r14: 15,459, 12,677 and 10,681; r15:
14,512 at distance 11; r16: 13,986; r17: 13,064).

The host programs of round 2 (`r11-nodes.c`, `r11-keys.c`, `r11-ideas.c`)
read the `ida.c` and `tables.h` of r11 (`d1b8fed`); `r11-keys.c` needs only
`solver.c` and builds at any commit.

* `experiments/r11-nodes.c`, output `experiments/r11-nodes.txt`: the model
  of the loop at r11, by block of code, by iteration and by depth. Measured
  minus model is 2,723 to 3,652 for all 2,644 states. A state has 314.6
  children and 56.8 nodes on average, 31.6 instructions a child in the
  loop; 78.2 of the children are in the last iteration (bound 11). Of the
  2,644 states, 192 start at bound 10, 2,192 at bound 9 and 260 at bound 8.
  With the exact distances of `solver.c` it also shows what the last
  iteration costs: 1,144 states have 9 neighbours at distance 10, 1,008
  have 8, 432 have 7 and 60 have 4 to 6; in 143 states the first move that
  is searched at bound 11 leads to another distance-11 state. Worst state
  `21354672313211`: root distances 8, 8, 8; bounds 8 to 10 take 3,050
  instructions, bound 11 takes 20,383, of which 18,884 under the four first
  moves R, R2, R', B, which all lead to distance-11 states. If the last
  iteration descended only into children that are one move closer, the worst
  state would take 19,442 and the mean would be 11,811: the rest is the
  iterations that fail (465 children at bound 10 for a state with root
  distances 9, 8, 8).
* `experiments/r11-keys.c`, output `experiments/r11-keys.txt`: every key
  that fits the table. A key that commutes with the moves is a left coset
  of a subgroup, and at 2 bits a key the subgroup has order 8. All 1,575
  subgroups of order 8 of S7, each with the twists measured in every frame
  (116,235 tables), were built and searched over the 2,644 states with the
  loop of r11. The r11 key is one of them (subgroup 1267: loop mean 9,943,
  worst 23,433, as measured). The best is subgroup 936 with the twists as
  stored: loop mean 6,733, worst 18,093, 212.8 children a state. The same
  key seen along the other two axes follows (18,550 and 18,553), then
  another family at 19,200. The 8 relabellings of subgroup 936 are the
  turns of D, R2 B2 R2 and three more at 4 moves from solved
  (`experiments/r11-ideas.txt`), so the distance of a key is at most 4
  below the distance of each of its states; the r11 key has four
  relabellings at 5 moves. 15 subgroups are refused: their three views do
  not pin the solved state.
* `experiments/r11-ideas.c`, output `experiments/r11-ideas.txt`: policies
  for the last iteration and smaller ideas, as the worst and the mean of
  (measured - loop of r11 + loop of the variant):

  | Variant | Worst | Mean |
  | :--- | ---: | ---: |
  | r11 | 26,567 | 13,156 |
  | oracle: the first move is one that is a move closer | 22,492 | 12,802 |
  | oracle: the first move with the cheapest search | 19,259 | 11,835 |
  | first moves by least sum of their key distances | 29,456 | 13,543 |
  | first moves by most children in the iteration before | 29,454 | 13,493 |
  | one turn of each face first | 27,802 | 13,309 |
  | half turns first | 25,161 | 13,386 |
  | a budget of 5 pops a first move, then no budget | 24,208 | 13,161 |
  | the same with 4, 8, 16 pops | 25,935, 26,222, 30,309 | |
  | bounds 10 and 11 in one pass (branch and bound) | 25,687 | 13,068 |

  A half turn leads from a distance-11 state to another one in 381 to 390
  states, a quarter turn in 143 to 176. On the walk home the order quarter,
  back, half tries 3.36 keys a step against 3.43 as built. Of the 107 pops
  of the worst state, 28 follow a third turn (15.1 of 43.8 on average).
* A host interpreter of RV32I (not kept as a file; the linked image as in
  `reference_ripes_cli`: `riscv64-elf-ld --no-relax -Ttext=0
  -Tdata=0x10000000`) gives the Ripes count of all 2,644 states of r11
  exactly, in 0.13 s. It was used to try changes before a Ripes sweep.

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
