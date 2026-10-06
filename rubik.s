# Optimal 2x2x2 solver in RV32I for Ripes: IDA* over factored coordinates.
#
# Same algorithm as ida.c. A state is three byte offsets, 2 * permutation
# rank, 2 * orientation rank and 2 * pair code (cubies 0 and 3, see gen.c),
# so every table lookup is one add and one load. The three turns of a face
# are chained, each child from the previous one. The frame being expanded
# lives in registers; it is spilled to a 32-byte slot in `frames` only when
# the search descends.
#
# A child that passes the two tests is tested twice more with the same
# pattern_dist, through the two rotations of the cube about the fixed corner
# (gen.c). Each slot holds the p and pair code of its node rotated once and
# twice; the rotated child is that node with the rotated face turned as many
# times, so it needs no state of its own between siblings.
#
# Preprocess with asmpp.awk (Ripes has no .if or .include):
#   RENDER=0  CLI build, measured with --iret
#   RENDER=1  GUI build, draws the cube on the LED matrix after every move
# The two builds differ only in the code between .if RENDER and .endif.

.equ RENDER, 0
.equ PERM_FACE, 10080           # bytes per face in perm_turn, 2 * 5040
.equ ORIENT_FACE, 1458          # bytes per face in orient_turn, 2 * 729
.equ FRAME, 32                  # bytes per slot in frames
.equ FACE_TAB, 32               # bytes per face in face_tabs

.data
# No .align anywhere: it means 2^n bytes to GNU as but n bytes to Ripes.
# Instead every block keeps the next one aligned: cases.s is a multiple of
# 16 bytes, tables.s of 4, and the names and messages below add up to 68,
# so the words render.s appends start on a 4-byte boundary.
.include "cases.s"
.include "tables.s"
# Move names, 4 bytes each, indexed by face * 3 + turn.
move_names:
    .byte 82, 0, 0, 0,  82, 50, 0, 0,  82, 39, 0, 0      # R  R2 R'
    .byte 66, 0, 0, 0,  66, 50, 0, 0,  66, 39, 0, 0      # B  B2 B'
    .byte 68, 0, 0, 0,  68, 50, 0, 0,  68, 39, 0, 0      # D  D2 D'
msg_arrow:   .string " -> "
msg_ok:      .string "  ok\n"
msg_fail:    .string "  FAIL\n"
msg_invalid: .string " -> invalid\n"

.bss
# One 32-byte slot per depth, 0..11:
#   0 p   2 o   4 child p   6 child o        (byte offsets)
#   8 face, as the address of its entry in face_tabs (a word)
#  12 parent's face, the same way (a word)
#  16 pair code  18 child pair code
#  20 p  22 pair code of this node rotated once   24 26 rotated twice
#  28 turns left on this face (2..0)         30 unused
frames: .zero 384
root:   .zero 8                 # p, o, pair code of the state being solved
# One 32-byte entry per face, filled in by solve: the addresses of
#   0 its perm_turn   4 its orient_turn   8 its pair_turn
#  12 perm_turn  16 pair_turn of the face it becomes when rotated once
#  20 perm_turn  24 pair_turn of the face it becomes when rotated twice
face_tabs: .zero 96
rotated:   .zero 32             # the state string rotated once, and twice

.text
# Status bits for the exit code: 1 = a result failed validation, 2 = invalid.
main:
    addi sp, sp, -48
    la   t0, cases
    sw   t0, 0(sp)              # next case
    sw   zero, 4(sp)            # status
case_loop:
    lw   t0, 0(sp)
    lw   a0, 0(t0)              # input string, 0 ends the list
    beqz a0, finish
    li   a7, 4
    ecall
    lw   t0, 0(sp)
    lw   a0, 0(t0)
    jal  ra, parse
    beqz a2, case_invalid
    sw   a0, 8(sp)              # root, kept for validation
    sw   a1, 12(sp)
    sw   a3, 16(sp)
    lw   t0, 0(sp)              # the same state rotated once, then twice
    lw   a0, 0(t0)
    la   a1, rotated
    jal  ra, rotate
    la   a0, rotated
    addi a1, a0, 16
    jal  ra, rotate
    la   a0, rotated
    jal  ra, parse
    sw   a0, 20(sp)
    sw   a3, 24(sp)
    la   a0, rotated
    addi a0, a0, 16
    jal  ra, parse
    sw   a0, 28(sp)
    sw   a3, 32(sp)
.if RENDER
    lw   t0, 0(sp)
    lw   a0, 0(t0)
    jal  ra, render_init        # draw the scrambled cube
.endif
    lw   a0, 8(sp)
    lw   a1, 12(sp)
    lw   a2, 16(sp)
    lw   a3, 20(sp)
    lw   a4, 24(sp)
    lw   a5, 28(sp)
    lw   a6, 32(sp)
    jal  ra, solve
    mv   s0, a0                 # length
    la   a0, msg_arrow
    li   a7, 4
    ecall
    mv   a0, s0
    jal  ra, print_path
    lw   a0, 8(sp)
    lw   a1, 12(sp)
    mv   a2, s0
    jal  ra, replay             # a0 = 1 if the path reaches solved
    lw   t0, 0(sp)
    lw   t1, 4(t0)              # expected length, -1 if unknown
    bltz t1, length_ok
    beq  t1, s0, length_ok
    li   a0, 0
length_ok:
    beqz a0, case_fail
    la   a0, msg_ok
    j    case_next
case_fail:
    lw   t0, 4(sp)
    ori  t0, t0, 1
    sw   t0, 4(sp)
    la   a0, msg_fail
    j    case_next
case_invalid:
    lw   t0, 4(sp)
    ori  t0, t0, 2
    sw   t0, 4(sp)
    la   a0, msg_invalid
case_next:
    li   a7, 4
    ecall
    lw   t0, 0(sp)
    addi t0, t0, 8
    sw   t0, 0(sp)
    j    case_loop
finish:
    lw   a0, 4(sp)
    addi sp, sp, 48
    li   a7, 93
    ecall

# parse: a0 = "PPPPPPPOOOOOOO" -> a0 = 2 * perm rank, a1 = 2 * orient rank,
# a3 = 2 * pair code, a2 = 1 if the string is a valid state, else 0.
parse:
    mv   t0, a0
    li   a2, 0
    li   t6, 7
    li   t1, 0                  # i
    li   t2, 0                  # cubies seen, one bit each
    li   a0, 0
    la   t3, lehmer_weight      # row i, 7 halves per row
perm_digit:
    add  t4, t0, t1
    lbu  t4, 0(t4)
    addi t4, t4, -49            # cubie c = digit - '1'
    bgeu t4, t6, parse_done     # also catches digits below '1'
    li   t5, 1
    sll  t5, t5, t4
    and  a3, t2, t5
    bnez a3, parse_done         # duplicate cubie
    or   t2, t2, t5
    li   a3, 0                  # cubies after i that are smaller than c
    addi a4, t1, 1
count_smaller:
    bgeu a4, t6, add_weight
    add  a5, t0, a4
    lbu  a5, 0(a5)
    addi a5, a5, -49
    sltu a5, a5, t4
    add  a3, a3, a5
    addi a4, a4, 1
    j    count_smaller
add_weight:
    slli a3, a3, 1
    add  a3, a3, t3
    lhu  a3, 0(a3)              # 2 * smaller * (6 - i)!
    add  a0, a0, a3
    addi t3, t3, 14
    addi t1, t1, 1
    bltu t1, t6, perm_digit
    li   a1, 0
    li   a3, 0                  # twist sum mod 3
    li   t1, 0
    li   t5, 3
    li   a6, 6
orient_digit:
    add  t4, t0, t1
    lbu  t4, 7(t4)
    addi t4, t4, -49            # twist t = digit - '1'
    bgeu t4, t5, parse_done
    add  a3, a3, t4             # at most 2 + 2: one conditional subtract
    bltu a3, t5, sum_reduced
    sub  a3, a3, t5
sum_reduced:
    bgeu t1, a6, orient_next    # the 7th twist is implied by the sum
    slli a4, a1, 1
    add  a1, a1, a4
    add  a1, a1, t4             # o = 3 * o + t
orient_next:
    addi t1, t1, 1
    bltu t1, t6, orient_digit
    lbu  t4, 14(t0)
    bnez t4, parse_done         # longer than 14 characters
    bnez a3, parse_done         # twist sum not 0 mod 3
    slli a1, a1, 1
    li   t1, 0                  # find cubies 0 and 3, digits '1' and '4'
pair_scan:
    add  t4, t0, t1
    lbu  t5, 0(t4)
    li   t2, 49
    bne  t5, t2, pair_not0
    mv   a5, t1                 # pos0
    lbu  a6, 7(t4)              # its twist digit
pair_not0:
    li   t2, 52
    bne  t5, t2, pair_not3
    mv   a7, t1                 # pos3
    lbu  t3, 7(t4)
pair_not3:
    addi t1, t1, 1
    bltu t1, t6, pair_scan
    addi a6, a6, -49
    addi t3, t3, -49
    slli t2, a6, 1
    add  t2, t2, a6
    add  t2, t2, t3             # oS = 3 * twist0 + twist3
    slli t2, t2, 6
    slli t4, a5, 3
    sub  t4, t4, a5             # 7 * pos0
    add  t2, t2, t4
    add  t2, t2, a7             # pair code = oS * 64 + 7 * pos0 + pos3
    slli a3, t2, 1
    li   a2, 1
parse_done:
    jalr zero, ra, 0

# solve: a0 = 2 * perm rank, a1 = 2 * orient rank, a2 = 2 * pair code,
# a3 a4 = 2 * perm rank and 2 * pair code of the state rotated once,
# a5 a6 = the same rotated twice -> a0 = solution length. Move k of the
# solution is (frames[k].face, frames[k].turns left).
# Registers while searching:
#   a2 a3 t1 node         a4 a5 t2 child (p, o, pair code)
#   a6 this face's entry in face_tabs          a7 one entry before the first
#   t5 one entry past the last                 t6 parent's face entry
#   s5 s6 a1 this face's perm, orient and pair turn tables
#   s2 pattern_dist       s3 orient_dist       t4 turns left
#   s4 frame slot         s9 frames
#   s7 rem = bound - depth - 1                 s8 bound
#   t0 t3 s0 s1 scratch
solve:
    or   t0, a0, a1
    bnez t0, solve_setup
    li   a0, 0                  # already solved
    jalr zero, ra, 0
solve_setup:
    la   t0, root
    sh   a0, 0(t0)
    sh   a1, 2(t0)
    sh   a2, 4(t0)
    la   s9, frames
    sh   a3, 20(s9)             # the root's rotated coordinates
    sh   a4, 22(s9)
    sh   a5, 24(s9)
    sh   a6, 26(s9)
    la   s2, pattern_dist
    la   s3, orient_dist
    la   s0, perm_turn          # s0 s1 s10: perm_turn of R, B, D
    li   t0, PERM_FACE
    add  s1, s0, t0
    add  s10, s1, t0
    la   t1, orient_turn        # t1 t2 t3: orient_turn of R, B, D
    li   t0, ORIENT_FACE
    add  t2, t1, t0
    add  t3, t2, t0
    la   a3, pair_turn          # a3 a4 a5: pair_turn, rows of the same size
    add  a4, a3, t0
    add  a5, a4, t0
    la   t0, face_tabs
    sw   s0, 0(t0)              # R: rotated once it is D, twice B
    sw   t1, 4(t0)
    sw   a3, 8(t0)
    sw   s10, 12(t0)
    sw   a5, 16(t0)
    sw   s1, 20(t0)
    sw   a4, 24(t0)
    sw   s1, 32(t0)             # B: rotated once it is R, twice D
    sw   t2, 36(t0)
    sw   a4, 40(t0)
    sw   s0, 44(t0)
    sw   a3, 48(t0)
    sw   s10, 52(t0)
    sw   a5, 56(t0)
    sw   s10, 64(t0)            # D: rotated once it is B, twice R
    sw   t3, 68(t0)
    sw   a5, 72(t0)
    sw   s1, 76(t0)
    sw   a4, 80(t0)
    sw   s0, 84(t0)
    sw   a3, 88(t0)
    addi a7, t0, -FACE_TAB
    addi t5, t0, 96
    slli t1, a0, 3              # bound = max of the four heuristics
    srli t2, a2, 7
    add  t1, t1, t2
    add  t1, t1, s2
    lbu  s8, 0(t1)
    add  t1, a1, s3
    lbu  t1, 0(t1)
    bgeu s8, t1, bound_once
    mv   s8, t1
bound_once:
    lhu  t1, 20(s9)
    lhu  t2, 22(s9)
    slli t1, t1, 3
    srli t2, t2, 7
    add  t1, t1, t2
    add  t1, t1, s2
    lbu  t1, 0(t1)
    bgeu s8, t1, bound_twice
    mv   s8, t1
bound_twice:
    lhu  t1, 24(s9)
    lhu  t2, 26(s9)
    slli t1, t1, 3
    srli t2, t2, 7
    add  t1, t1, t2
    add  t1, t1, s2
    lbu  t1, 0(t1)
    bgeu s8, t1, iteration
    mv   s8, t1
iteration:
    mv   s4, s9
    addi s7, s8, -1
    la   t0, root
    lhu  a2, 0(t0)
    lhu  a3, 2(t0)
    lhu  t1, 4(t0)
    mv   t6, t5                 # the root has no parent face
enter:
    mv   a6, a7                 # one entry before face R
next_face:
    addi a6, a6, FACE_TAB
    bne  a6, t6, face_ok
    addi a6, a6, FACE_TAB       # skip the face the parent just turned
face_ok:
    bgeu a6, t5, pop
    lw   s5, 0(a6)
    lw   s6, 4(a6)
    lw   a1, 8(a6)
# The three turns of the face, unrolled: a pruned child falls through to
# the next turn with no counter to update or test. t4, the turns left, is
# set only for a child that passes, since found and the spilled frame need it.
# Each turn tests the pattern distance, the orientation distance, and then
# the pattern distance of the child rotated once and twice. A rotated child
# is computed from the node's rotated coordinates in this slot, so turn k
# takes k lookups per coordinate; it is written to the child's slot, which
# is used only if the child passes.
turn_1:
    add  t0, s5, a2
    lhu  a4, 0(t0)
    add  t0, s6, a3
    lhu  a5, 0(t0)
    add  t0, a1, t1
    lhu  t2, 0(t0)
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, turn_2         # pattern h > rem: 6 instructions
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, turn_2         # orient h > rem: 3 instructions
    lhu  t0, 20(s4)             # rotated once: p, 1 quarter turn
    lw   s0, 12(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 22(s4)             # and its pair code
    lw   s0, 16(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, turn_2
    sh   t0, 52(s4)             # the child's slot
    sh   t3, 54(s4)
    lhu  t0, 24(s4)             # rotated twice: p, 1 quarter turn
    lw   s0, 20(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 26(s4)             # and its pair code
    lw   s0, 24(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, turn_2
    sh   t0, 56(s4)             # the child's slot
    sh   t3, 58(s4)
    li   t4, 2
    j    passed
turn_2:
    add  t0, s5, a4
    lhu  a4, 0(t0)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    add  t0, a1, t2
    lhu  t2, 0(t0)
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, turn_3
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, turn_3
    lhu  t0, 20(s4)             # rotated once: p, 2 quarter turns
    lw   s0, 12(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 22(s4)             # and its pair code
    lw   s0, 16(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, turn_3
    sh   t0, 52(s4)             # the child's slot
    sh   t3, 54(s4)
    lhu  t0, 24(s4)             # rotated twice: p, 2 quarter turns
    lw   s0, 20(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 26(s4)             # and its pair code
    lw   s0, 24(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, turn_3
    sh   t0, 56(s4)             # the child's slot
    sh   t3, 58(s4)
    li   t4, 1
    j    passed
turn_3:
    add  t0, s5, a4
    lhu  a4, 0(t0)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    add  t0, a1, t2
    lhu  t2, 0(t0)
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, next_face
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, next_face
    lhu  t0, 20(s4)             # rotated once: p, 3 quarter turns
    lw   s0, 12(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 22(s4)             # and its pair code
    lw   s0, 16(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, next_face
    sh   t0, 52(s4)             # the child's slot
    sh   t3, 54(s4)
    lhu  t0, 24(s4)             # rotated twice: p, 3 quarter turns
    lw   s0, 20(a6)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    add  t0, s0, t0
    lhu  t0, 0(t0)
    lhu  t3, 26(s4)             # and its pair code
    lw   s0, 24(a6)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    add  t3, s0, t3
    lhu  t3, 0(t3)
    slli s0, t0, 3
    srli s1, t3, 7
    add  s0, s0, s1
    add  s0, s0, s2
    lbu  s0, 0(s0)
    bltu s7, s0, next_face
    sh   t0, 56(s4)             # the child's slot
    sh   t3, 58(s4)
    li   t4, 0
passed:
    or   t0, a4, a5
    beqz t0, found              # only a child with h = 0 can be solved
    sh   a2, 0(s4)              # descend: spill this frame
    sh   a3, 2(s4)
    sh   a4, 4(s4)
    sh   a5, 6(s4)
    sw   a6, 8(s4)
    sw   t6, 12(s4)
    sh   t1, 16(s4)
    sh   t2, 18(s4)
    sh   t4, 28(s4)
    addi s4, s4, FRAME
    addi s7, s7, -1
    mv   t6, a6
    mv   a2, a4
    mv   a3, a5
    mv   t1, t2
    j    enter
pop:
    beq  s4, s9, deepen
    addi s4, s4, -FRAME
    addi s7, s7, 1
    lhu  a2, 0(s4)
    lhu  a3, 2(s4)
    lhu  a4, 4(s4)
    lhu  a5, 6(s4)
    lw   a6, 8(s4)
    lw   t6, 12(s4)
    lhu  t1, 16(s4)
    lhu  t2, 18(s4)
    lhu  t4, 28(s4)
    lw   s5, 0(a6)
    lw   s6, 4(a6)
    lw   a1, 8(a6)
    beqz t4, next_face          # resume after the child just searched
    addi t0, t4, -1
    beqz t0, turn_3
    j    turn_2
deepen:
    addi s8, s8, 1
    j    iteration
found:
    sw   a6, 8(s4)              # the last move is still in registers
    sh   t4, 28(s4)
    sub  a0, s4, s9
    srli a0, a0, 5              # FRAME = 32
    addi a0, a0, 1
    jalr zero, ra, 0

# rotate: a0 = a valid state string, a1 = 15-byte buffer for the string of
# the cube rotated about the fixed corner: the cubie at position i goes to
# sym_pi[i] as cubie sym_pi[cubie], its twist plus sym_tau[i] - sym_tau[cubie].
rotate:
    la   a2, sym_pi
    la   a3, sym_tau
    li   t0, 0                  # i
    li   t5, 7
rotate_digit:
    add  t1, a0, t0
    lbu  t2, 0(t1)
    addi t2, t2, -49            # cubie
    lbu  t3, 7(t1)              # twist digit
    add  t4, a3, t0
    lbu  t4, 0(t4)
    add  t3, t3, t4
    add  t4, a3, t2
    lbu  t4, 0(t4)
    sub  t3, t3, t4             # '1' - 2 .. '3' + 2
    li   t4, 49
    bgeu t3, t4, rotate_not_low
    addi t3, t3, 3
rotate_not_low:
    li   t4, 52
    bltu t3, t4, rotate_not_high
    addi t3, t3, -3
rotate_not_high:
    add  t4, a2, t2
    lbu  t2, 0(t4)
    addi t2, t2, 49             # the cubie's digit after the rotation
    add  t4, a2, t0
    lbu  t4, 0(t4)
    add  t4, a1, t4             # its new position
    sb   t2, 0(t4)
    sb   t3, 7(t4)
    addi t0, t0, 1
    bltu t0, t5, rotate_digit
    sb   zero, 14(a1)
    jalr zero, ra, 0

# move_index: a0 = frame slot -> a0 = face * 3 + turn, for move_names.
move_index:
    lw   t0, 8(a0)
    la   t2, face_tabs
    sub  t0, t0, t2
    srli t0, t0, 5              # face: FACE_TAB = 32
    slli t2, t0, 1
    add  t0, t0, t2             # 3 * face
    lhu  t1, 28(a0)
    li   t2, 2
    sub  t1, t2, t1             # turn = 2 - turns left
    add  a0, t0, t1
    jalr zero, ra, 0

# print_path: a0 = length; prints the moves from frames, then nothing else.
print_path:
    mv   s1, ra
    mv   s2, a0
    la   s3, frames
print_move:
    beqz s2, print_done
    mv   a0, s3
    jal  ra, move_index
    slli a0, a0, 2
    la   t0, move_names
    add  a0, a0, t0
    li   a7, 4
    ecall
    addi s2, s2, -1
    beqz s2, print_done
    li   a0, 32
    li   a7, 11
    ecall
    addi s3, s3, FRAME
    j    print_move
print_done:
    mv   ra, s1
    jalr zero, ra, 0

# replay: a0, a1 = root offsets, a2 = length -> a0 = 1 if applying the moves
# in frames reaches solved, else 0. This is gate T5 on the target.
replay:
    mv   s1, ra
    la   s2, frames
    mv   s5, a0
    mv   s6, a1
    mv   s7, a2
replay_move:
    beqz s7, replay_done
    lw   t0, 8(s2)              # this face's entry in face_tabs
    lw   t1, 4(t0)              # its orient_turn
    lw   t0, 0(t0)              # its perm_turn
    lhu  t2, 28(s2)
    li   t3, 3
    sub  t2, t3, t2             # quarter turns = 3 - turns left
replay_turn:
    add  t3, t0, s5
    lhu  s5, 0(t3)
    add  t3, t1, s6
    lhu  s6, 0(t3)
    addi t2, t2, -1
    bnez t2, replay_turn
.if RENDER
    lw   a0, 8(s2)              # redraw after every move of the answer
    la   t0, face_tabs
    sub  a0, a0, t0
    srli a0, a0, 5              # face 0..2: FACE_TAB = 32
    lhu  a1, 28(s2)
    li   t0, 3
    sub  a1, t0, a1
    jal  ra, render_move
.endif
    addi s2, s2, FRAME
    addi s7, s7, -1
    j    replay_move
replay_done:
    or   t0, s5, s6
    sltiu a0, t0, 1
    mv   ra, s1
    jalr zero, ra, 0

.if RENDER
.include "render.s"
.endif
