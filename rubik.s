# Optimal 2x2x2 solver in RV32I for Ripes: IDA* over factored coordinates.
#
# Same algorithm as ida.c. A state is three byte offsets, 2 * permutation
# rank, 2 * orientation rank and 2 * pair code (cubies 0 and 3, see gen.c),
# so every table lookup is one add and one load. The three turns of a face
# are chained, each child from the previous one. The frame being expanded
# lives in registers; it is spilled to a 32-byte slot in `frames` only when
# the search descends.
#
# Preprocess with asmpp.awk (Ripes has no .if or .include):
#   RENDER=0  CLI build, measured with --iret
#   RENDER=1  GUI build, draws the cube on the LED matrix after every move
# The two builds differ only in the code between .if RENDER and .endif.

.equ RENDER, 0
.equ PERM_FACE, 10080           # bytes per face in perm_turn, 2 * 5040
.equ ORIENT_FACE, 1458          # bytes per face in orient_turn, 2 * 729
.equ NO_FACE, 30240             # 3 * PERM_FACE: past the last face
.equ FRAME, 32                  # bytes per slot in frames

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
#   8 face, as its offset into perm_turn     10 same face into orient_turn
#  12 parent's face                          14 turns left on this face (2..0)
#  16 pair code  18 child pair code          20..31 unused
frames: .zero 384
root:   .zero 8                 # p, o, pair code of the state being solved

.text
# Status bits for the exit code: 1 = a result failed validation, 2 = invalid.
main:
    addi sp, sp, -32
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
    mv   a2, a3
    sw   a2, 16(sp)
.if RENDER
    lw   t0, 0(sp)
    lw   a0, 0(t0)
    jal  ra, render_init        # draw the scrambled cube
    lw   a0, 8(sp)
    lw   a1, 12(sp)
    lw   a2, 16(sp)
.endif
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
    addi sp, sp, 32
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

# solve: a0 = 2 * perm rank, a1 = 2 * orient rank, a2 = 2 * pair code
# -> a0 = solution length. Move k of the solution is
# (frames[k].face, frames[k].turns left).
# Registers while searching:
#   a0 pair_turn          a1 this face's pair_turn
#   a2 a3 t1 node         a4 a5 t2 child (p, o, pair code)
#   a6 a7 face offsets    t4 turns left        t6 parent's face
#   s0 s1 turn tables     s2 pattern_dist      s3 orient_dist
#   s4 frame slot         s5 s6 this face's turn tables
#   s7 rem = bound - depth - 1                 s8 bound
#   s9 frames             s10 s11 face strides t5 NO_FACE    t3 scratch
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
    la   s0, perm_turn
    la   s1, orient_turn
    la   s2, pattern_dist
    la   s3, orient_dist
    la   a0, pair_turn
    la   s9, frames
    li   s10, PERM_FACE
    li   s11, ORIENT_FACE
    li   t5, NO_FACE
    lhu  t1, 0(t0)              # t0 = root; bound = max(pattern h, orient h)
    lhu  t2, 4(t0)
    lhu  t3, 2(t0)
    slli t1, t1, 3
    srli t2, t2, 7
    add  t1, t1, t2
    add  t1, t1, s2
    lbu  s8, 0(t1)
    add  t3, t3, s3
    lbu  t3, 0(t3)
    bgeu s8, t3, iteration
    mv   s8, t3
iteration:
    mv   s4, s9
    addi s7, s8, -1
    la   t0, root
    lhu  a2, 0(t0)
    lhu  a3, 2(t0)
    lhu  t1, 4(t0)
    mv   t6, t5                 # the root has no parent face
enter:
    sub  a6, zero, s10          # one face before face 0
    sub  a7, zero, s11
next_face:
    add  a6, a6, s10
    add  a7, a7, s11
    bne  a6, t6, face_ok
    add  a6, a6, s10            # skip the face the parent just turned
    add  a7, a7, s11
face_ok:
    bgeu a6, t5, pop
    add  s5, s0, a6
    add  s6, s1, a7
    add  a1, a0, a7             # pair_turn rows are orient_turn-sized
# The three turns of the face, unrolled: a pruned child falls through to
# the next turn with no counter to update or test. t4, the turns left, is
# set only for a child that passes, since found and the spilled frame need it.
turn_1:
    add  t0, s5, a2
    lhu  a4, 0(t0)
    add  t0, s6, a3
    lhu  a5, 0(t0)
    add  t0, a1, t1
    lhu  t2, 0(t0)
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, turn_2         # orient h > rem: 3 instructions
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, turn_2         # pattern h > rem: 6 instructions
    li   t4, 2
    j    passed
turn_2:
    add  t0, s5, a4
    lhu  a4, 0(t0)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    add  t0, a1, t2
    lhu  t2, 0(t0)
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, turn_3
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, turn_3
    li   t4, 1
    j    passed
turn_3:
    add  t0, s5, a4
    lhu  a4, 0(t0)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    add  t0, a1, t2
    lhu  t2, 0(t0)
    add  t0, s3, a5
    lbu  t0, 0(t0)
    bltu s7, t0, next_face
    slli t0, a4, 3              # pattern_dist[16 * p + oS]:
    srli t3, t2, 7              # 2p << 3 = 16p, 2c >> 7 = c >> 6 = oS
    add  t0, t0, t3
    add  t0, t0, s2
    lbu  t0, 0(t0)
    bltu s7, t0, next_face
    li   t4, 0
passed:
    or   t0, a4, a5
    beqz t0, found              # only a child with h = 0 can be solved
    sh   a2, 0(s4)              # descend: spill this frame
    sh   a3, 2(s4)
    sh   a4, 4(s4)
    sh   a5, 6(s4)
    sh   a6, 8(s4)
    sh   a7, 10(s4)
    sh   t6, 12(s4)
    sh   t4, 14(s4)
    sh   t1, 16(s4)
    sh   t2, 18(s4)
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
    lhu  a6, 8(s4)
    lhu  a7, 10(s4)
    lhu  t6, 12(s4)
    lhu  t4, 14(s4)
    lhu  t1, 16(s4)
    lhu  t2, 18(s4)
    add  s5, s0, a6
    add  s6, s1, a7
    add  a1, a0, a7
    beqz t4, next_face          # resume after the child just searched
    addi t0, t4, -1
    beqz t0, turn_3
    j    turn_2
deepen:
    addi s8, s8, 1
    j    iteration
found:
    sh   a6, 8(s4)              # the last move is still in registers
    sh   a7, 10(s4)
    sh   t4, 14(s4)
    sub  a0, s4, s9
    srli a0, a0, 5              # FRAME = 32
    addi a0, a0, 1
    jalr zero, ra, 0

# move_index: a0 = frame slot -> a0 = face * 3 + turn, for move_names.
# Used once per move of the answer, so a compare chain is cheap enough.
move_index:
    lhu  t0, 8(a0)
    lhu  t1, 14(a0)
    li   t2, 2
    sub  t1, t2, t1             # turn = 2 - turns left
    beqz t0, move_face_known
    addi t1, t1, 3
    li   t2, PERM_FACE
    beq  t0, t2, move_face_known
    addi t1, t1, 3
move_face_known:
    mv   a0, t1
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
    la   s3, perm_turn
    la   s4, orient_turn
    mv   s5, a0
    mv   s6, a1
    mv   s7, a2
replay_move:
    beqz s7, replay_done
    lhu  t0, 8(s2)
    add  t0, t0, s3             # this face's perm_turn
    lhu  t1, 10(s2)
    add  t1, t1, s4             # this face's orient_turn
    lhu  t2, 14(s2)
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
    lhu  a0, 8(s2)              # redraw after every move of the answer
    lhu  a1, 14(s2)
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
