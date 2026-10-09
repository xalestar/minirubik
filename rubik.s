# Optimal 2x2x2 solver in RV32I for Ripes: IDA* over three views of one
# pattern database.
#
# Same algorithm as ida.c. A view of the state is two coordinates (gen.c):
# its block code, held as the address of that code's row in `pattern`, and
# 2 * its orientation rank. A row starts with the addresses of the rows the
# code turns into, so a quarter turn is one load for the block code and one
# add and one load for the orientation. The search carries three views, the
# state and its two rotations about the fixed corner; the rotated views turn
# the rotated face. The three turns of a face are chained, each child from
# the previous one, in registers.
#
# After its three addresses a row holds the distance modulo 3 of each
# orientation, 2 bits each. No distance is rebuilt: per view the node keeps
# the address of a 4-byte row of next_state, which stands for (slack,
# distance mod 3) with slack = moves left - distance. Adding the child's
# 2-bit value and loading gives the child's row, or -1 if its slack would be
# negative: pruned. A child that passes with no move left is solved.
#
# Preprocess with asmpp.awk (Ripes has no .if or .include):
#   RENDER=0  CLI build, measured with --iret
#   RENDER=1  GUI build, draws the cube on the LED matrix after every move
# The two builds differ only in the code between .if RENDER and .endif.

.equ RENDER, 0
.equ ORIENT_FACE, 1458          # bytes per face in orient_turn, 2 * 729
.equ FRAME, 32                  # bytes per slot in frames

.data
# No .align anywhere: it means 2^n bytes to GNU as but n bytes to Ripes.
# Instead every block keeps the next one aligned: cases.s is a multiple of
# 16 bytes, tables.s of 4, and the names, messages and pair_base below add
# up to 76, so the words render.s appends start on a 4-byte boundary. The
# rows of `pattern` hold words, so they rely on this too.
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
# pair_base[i] + j - i - 1 numbers the pairs i < j of 6 positions, 0..14.
pair_base:   .byte 0, 5, 9, 12, 14, 0, 0, 0

.bss
# One 32-byte slot per depth, 0..11, and one in front of the root's:
#   0 4 8     the pattern row of this node in each view (words)
#  12 14 16   2 * its orientation rank in each view
#  18 turns left on the face of the child being searched (3..1)
#  19 that face (0 R, 1 B, 2 D); 3 in the slot in front: no parent face
#  20 24 28   the next_state row of this node in each view (words)
# Move k of the answer is bytes 19 and 18 of slot k.
guard:   .zero 32
frames:  .zero 384
rotated: .zero 32               # the state string rotated once, and twice

.text
# Status bits for the exit code: 1 = a result failed validation, 2 = invalid.
main:
    addi sp, sp, -16
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
    mv   s11, a1                # parity, the same in all three views
    la   s9, frames
    lw   t0, 0(sp)
    lw   a0, 0(t0)
    jal  ra, coords
    sw   a2, 0(s9)
    sh   a3, 12(s9)
    lw   t0, 0(sp)              # the same state rotated once, then twice
    lw   a0, 0(t0)
    la   a1, rotated
    jal  ra, rotate
    la   a0, rotated
    mv   a1, s11
    jal  ra, coords
    sw   a2, 4(s9)
    sh   a3, 14(s9)
    la   a0, rotated
    addi a1, a0, 16
    jal  ra, rotate
    la   a0, rotated
    addi a0, a0, 16
    mv   a1, s11
    jal  ra, coords
    sw   a2, 8(s9)
    sh   a3, 16(s9)
.if RENDER
    lw   t0, 0(sp)
    lw   a0, 0(t0)
    jal  ra, render_init        # draw the scrambled cube
.endif
    jal  ra, solve
    sw   a0, 8(sp)              # length
    la   a0, msg_arrow
    li   a7, 4
    ecall
    lw   a0, 8(sp)
    jal  ra, print_path
    lw   a0, 8(sp)
    jal  ra, replay             # a0 = 1 if the path reaches solved
    lw   t0, 0(sp)
    lw   t1, 4(t0)              # expected length, -1 if unknown
    bltz t1, length_ok
    lw   t2, 8(sp)
    beq  t1, t2, length_ok
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
    addi sp, sp, 16
    li   a7, 93
    ecall

# parse: a0 = "PPPPPPPOOOOOOO" -> a2 = 1 if the string is a valid state, else
# 0; a1 = the parity of its permutation. For each cubie, the larger cubies
# before it are the bits of `seen` above its own; the parity of all those
# counts together is the parity of the XOR of those bit sets.
parse:
    li   a2, 0
    li   t6, 7
    li   t1, 0                  # i
    li   t2, 0                  # cubies seen, one bit each
    li   a1, 0
parse_cubie:
    add  t4, a0, t1
    lbu  t4, 0(t4)
    addi t4, t4, -49            # cubie c = digit - '1'
    bgeu t4, t6, parse_done     # also catches digits below '1'
    srl  t5, t2, t4
    andi t3, t5, 1
    bnez t3, parse_done         # duplicate cubie
    srli t5, t5, 1
    xor  a1, a1, t5
    li   t3, 1
    sll  t3, t3, t4
    or   t2, t2, t3
    addi t1, t1, 1
    bltu t1, t6, parse_cubie
    li   a3, 0                  # twist sum mod 3
    li   t1, 0
    li   t5, 3
parse_twist:
    add  t4, a0, t1
    lbu  t4, 7(t4)
    addi t4, t4, -49            # twist t = digit - '1'
    bgeu t4, t5, parse_done
    add  a3, a3, t4             # at most 2 + 2: one conditional subtract
    bltu a3, t5, parse_reduced
    sub  a3, a3, t5
parse_reduced:
    addi t1, t1, 1
    bltu t1, t6, parse_twist
    lbu  t4, 14(a0)
    bnez t4, parse_done         # longer than 14 characters
    bnez a3, parse_done         # twist sum not 0 mod 3
    srli t5, a1, 4              # fold the 6 bits into one
    xor  a1, a1, t5
    srli t5, a1, 2
    xor  a1, a1, t5
    srli t5, a1, 1
    xor  a1, a1, t5
    andi a1, a1, 1
    li   a2, 1
parse_done:
    jalr zero, ra, 0

# coords: a0 = a valid state string, a1 = the parity of its permutation ->
# a2 = the pattern row of its block code, a3 = 2 * its orientation rank.
# The block code is ((15 * home + pair) * 3 + mate) * 2 + parity (gen.c).
coords:
    li   a3, 0
    addi t0, a0, 7
    addi t5, a0, 13             # the 7th twist is implied by the sum
coords_twist:
    lbu  t1, 0(t0)
    slli t2, a3, 1
    add  a3, a3, t2
    add  a3, a3, t1             # o = 3 * o + digit
    addi t0, t0, 1
    bltu t0, t5, coords_twist
    li   t1, 17836              # the digits count from '1': 49 * 364
    sub  a3, a3, t1
    slli a3, a3, 1
    li   t2, 0                  # positions so far that do not hold cubie 3
    li   t3, 0                  # positions so far that hold 0, 6, 1 or 5
    li   a7, 0                  # bit k: the k-th of those holds 1 or 5
    li   t6, 3
    mv   t0, a0
    addi t5, a0, 7
coords_cubie:
    lbu  t1, 0(t0)
    addi t1, t1, -49            # cubie c; its block is c or 6 - c
    bgeu t6, t1, coords_block
    addi t1, t1, -6
    sub  t1, zero, t1
coords_block:
    beq  t1, t6, coords_home
    addi t4, t1, -2
    beqz t4, coords_pair
    sll  t4, t1, t3
    or   a7, a7, t4
    addi t3, t3, 1
    j    coords_free
coords_pair:
    mv   a4, a5                 # a4 a5: where cubies 2 and 4 sit, in order
    mv   a5, t2
coords_free:
    addi t2, t2, 1
    j    coords_next
coords_home:
    sub  a6, t0, a0             # where cubie 3 sits
coords_next:
    addi t0, t0, 1
    bltu t0, t5, coords_cubie
    andi t0, a7, 1              # name the block of the first of the four 0;
    beqz t0, coords_mate        # then a7 is 12, 10 or 6: its partner is the
    xori a7, a7, 15             # 1st, 2nd or 3rd of the other three
coords_mate:
    li   t0, 14
    sub  t0, t0, a7
    srli t0, t0, 2              # mate 0..2
    la   t1, pair_base
    add  t1, t1, a4
    lbu  t1, 0(t1)
    add  t1, t1, a5
    sub  t1, t1, a4
    addi t1, t1, -1             # pair 0..14
    slli t2, a6, 4
    sub  t2, t2, a6
    add  t1, t1, t2             # 15 * home + pair
    slli t2, t1, 1
    add  t1, t1, t2
    add  t1, t1, t0             # * 3 + mate
    slli t1, t1, 1
    add  t1, t1, a1             # * 2 + parity
    slli t0, t1, 7              # a row is 196 = 128 + 64 + 4 bytes
    slli t2, t1, 6
    add  t0, t0, t2
    slli t2, t1, 2
    add  t0, t0, t2
    la   a2, pattern
    add  a2, a2, t0
    jalr zero, ra, 0

# root_dist: a2 = pattern row, a5 = 2 * orientation rank -> a0 = the distance
# of that key, a1 = the distance modulo 3. Walks home: a neighbour one closer
# is one whose value is one less modulo 3. Needs s5 = orient_turn and
# s8 = the solved row.
root_dist:
    li   a0, 0
    srli t0, a5, 3
    add  t0, t0, a2
    lbu  t0, 12(t0)
    andi t1, a5, 6
    srl  t0, t0, t1
    andi a1, t0, 3
    mv   t5, a1
root_step:
    bne  a2, s8, root_search
    beqz a5, root_done
root_search:
    addi t5, t5, -1             # the value to look for
    bgez t5, root_face
    li   t5, 2
root_face:
    li   t2, 0                  # face: 0, 4, 8 into the row
    mv   t3, s5                 # its orient_turn
root_next_face:
    mv   a3, a2
    mv   a6, a5
    li   t4, 3
root_turn:
    add  t0, a3, t2
    lw   a3, 0(t0)
    add  t0, t3, a6
    lhu  a6, 0(t0)
    srli t0, a6, 3
    add  t0, t0, a3
    lbu  t0, 12(t0)
    andi t1, a6, 6
    srl  t0, t0, t1
    andi t0, t0, 3
    beq  t0, t5, root_closer
    addi t4, t4, -1
    bnez t4, root_turn
    addi t2, t2, 4
    addi t3, t3, ORIENT_FACE
    j    root_next_face
root_closer:
    mv   a2, a3
    mv   a5, a6
    addi a0, a0, 1
    j    root_step
root_done:
    jalr zero, ra, 0

# solve: slot 0 of frames holds the three views of the root (bytes 0..17)
# -> a0 = solution length. Move k of the solution is bytes 19 and 18 of slot
# k: the face, and 4 minus the number of quarter turns.
# Registers while searching:
#   a2 a3 a4  the child's pattern row in each view
#   a5 a6 a7  2 * its orientation rank in each view
#   s0 s1 s2  the node's next_state row in each view
#   s3 s10 s11  the child's offset into next_state in each view
#   s5 s6 s7  orient_turn of R, B, D        t2 next_state
#   s4 the node's slot    s9 frames         s8 the last slot that is expanded
#   t4 turns left on this face    t5 this face    t6 the parent's face
#   t3 a1  the constants 1 and 2            t0 t1 scratch
solve:
    mv   s3, ra
    la   s5, orient_turn
    addi s6, s5, ORIENT_FACE
    addi s7, s6, ORIENT_FACE
    la   s8, pattern
    li   t0, BLOCK_SOLVED
    add  s8, s8, t0
    li   t0, 3
    sb   t0, -13(s9)            # byte 19 of the slot in front of the root's
    li   s10, 0                 # the largest of the three distances
    mv   s4, s9                 # s4 t6: the view's row and orientation
    mv   t6, s9
    addi s11, s9, 12
solve_view:
    lw   a2, 0(s4)
    lhu  a5, 12(t6)
    jal  ra, root_dist
    bgeu s10, a0, solve_kept
    mv   s10, a0
solve_kept:
    slli t0, a0, 3              # 4 * (distance mod 3) - 12 * distance, the
    slli t1, a0, 2              # offset into next_state at slack 0
    add  t0, t0, t1
    slli t1, a1, 2
    sub  t0, t1, t0
    sw   t0, 20(s4)
    addi s4, s4, 4
    addi t6, t6, 2
    bltu s4, s11, solve_view
    mv   ra, s3
    mv   a0, s10
    beqz s10, solve_done        # already solved
    la   t2, next_state
    slli t0, s10, 3             # slack = bound - distance: add 12 * bound
    slli t1, s10, 2
    add  t0, t0, t1
    add  t0, t0, t2
    lw   s0, 20(s9)
    lw   s1, 24(s9)
    lw   s2, 28(s9)
    add  s0, s0, t0
    add  s1, s1, t0
    add  s2, s2, t0
    slli s8, s10, 5             # FRAME = 32
    add  s8, s8, s9
    addi s8, s8, -FRAME         # the slot at depth bound - 1
    li   t3, 1
    li   a1, 2
    j    iteration
deepen:
    addi s8, s8, FRAME
    lw   s0, 20(s9)             # one more move: slack + 1 in each view
    lw   s1, 24(s9)
    lw   s2, 28(s9)
    addi s0, s0, 12
    addi s1, s1, 12
    addi s2, s2, 12
iteration:
    sw   s0, 20(s9)
    sw   s1, 24(s9)
    sw   s2, 28(s9)
    mv   s4, s9
    li   t6, 3                  # the root has no parent face
# One block per face: the node's coordinates are loaded, then turned three
# times. Each view turns its own face: the state's face, and the face that
# becomes when the cube is rotated once and twice (R -> D -> B -> R). A
# child is tested view by view, and the first negative row ends it.
face_R:
    beqz t6, face_B             # skip the face the parent just turned
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
    li   t4, 3
turn_R:
    lw   a2, 0(a2)
    add  t0, s5, a5
    lhu  a5, 0(t0)
    lw   a3, 8(a3)
    add  t0, s7, a6
    lhu  a6, 0(t0)
    lw   a4, 4(a4)
    add  t0, s6, a7
    lhu  a7, 0(t0)
    srli t0, a5, 3
    add  t0, t0, a2
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a5, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, next_R
    srli t0, a6, 3
    add  t0, t0, a3
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a6, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, next_R
    srli t0, a7, 3
    add  t0, t0, a4
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a7, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, next_R
    li   t5, 0
    j    passed
next_R:
    addi t4, t4, -1
    bnez t4, turn_R
face_B:
    beq  t6, t3, face_D
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
    li   t4, 3
turn_B:
    lw   a2, 4(a2)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    lw   a3, 0(a3)
    add  t0, s5, a6
    lhu  a6, 0(t0)
    lw   a4, 8(a4)
    add  t0, s7, a7
    lhu  a7, 0(t0)
    srli t0, a5, 3
    add  t0, t0, a2
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a5, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, next_B
    srli t0, a6, 3
    add  t0, t0, a3
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a6, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, next_B
    srli t0, a7, 3
    add  t0, t0, a4
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a7, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, next_B
    li   t5, 1
    j    passed
next_B:
    addi t4, t4, -1
    bnez t4, turn_B
face_D:
    beq  t6, a1, pop
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
    li   t4, 3
turn_D:
    lw   a2, 8(a2)
    add  t0, s7, a5
    lhu  a5, 0(t0)
    lw   a3, 4(a3)
    add  t0, s6, a6
    lhu  a6, 0(t0)
    lw   a4, 0(a4)
    add  t0, s5, a7
    lhu  a7, 0(t0)
    srli t0, a5, 3
    add  t0, t0, a2
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a5, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, next_D
    srli t0, a6, 3
    add  t0, t0, a3
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a6, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, next_D
    srli t0, a7, 3
    add  t0, t0, a4
    lbu  t0, 12(t0)             # the byte of 4 values
    andi t1, a7, 6
    srl  t0, t0, t1
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, next_D
    li   t5, 2
    j    passed
next_D:
    addi t4, t4, -1
    bnez t4, turn_D
pop:
    beq  s4, s9, deepen
    lw   a2, 0(s4)              # this node is the parent's child in progress
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
    addi s4, s4, -FRAME
    lw   s0, 20(s4)
    lw   s1, 24(s4)
    lw   s2, 28(s4)
    lbu  t4, 18(s4)
    lbu  t5, 19(s4)
    lbu  t6, -13(s4)            # the face of the slot before
    beqz t5, next_R
    beq  t5, t3, next_B
    j    next_D
passed:
    sb   t4, 18(s4)             # this move, for the resume and the answer
    sb   t5, 19(s4)
    beq  s4, s8, found          # no move left: distance 0 in every view
    addi s4, s4, FRAME          # descend: the child gets its own slot
    sw   a2, 0(s4)
    sw   a3, 4(s4)
    sw   a4, 8(s4)
    sh   a5, 12(s4)
    sh   a6, 14(s4)
    sh   a7, 16(s4)
    add  s0, t2, s3
    add  s1, t2, s10
    add  s2, t2, s11
    sw   s0, 20(s4)
    sw   s1, 24(s4)
    sw   s2, 28(s4)
    mv   t6, t5
    j    face_R
found:
    sub  a0, s4, s9
    srli a0, a0, 5              # FRAME = 32
    addi a0, a0, 1
solve_done:
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

# print_path: a0 = length; prints the moves from frames, then nothing else.
# Keeps s5 s6 s7 s9 of solve for replay.
print_path:
    mv   s2, s9
    slli t0, a0, 5              # FRAME = 32
    add  s1, s9, t0             # the slot after the last move
print_move:
    beq  s2, s1, print_done
    lbu  t0, 19(s2)             # face
    lbu  t1, 18(s2)             # 4 - quarter turns
    slli a0, t0, 1
    add  a0, a0, t0
    sub  a0, a0, t1
    addi a0, a0, 3              # face * 3 + turn
    slli a0, a0, 2
    la   t0, move_names
    add  a0, a0, t0
    li   a7, 4
    ecall
    addi s2, s2, FRAME
    beq  s2, s1, print_done
    li   a0, 32
    li   a7, 11
    ecall
    j    print_move
print_done:
    jalr zero, ra, 0

# replay: a0 = length -> a0 = 1 if applying the moves in frames to the three
# views of the root reaches the solved key in all of them, else 0. This is
# gate T5 on the target. Needs s5 s6 s7 s9 as solve left them.
replay:
    sw   ra, 12(sp)
    lw   s3, 0(s9)
    lw   s4, 4(s9)
    lw   s8, 8(s9)
    lhu  s10, 12(s9)
    lhu  s11, 14(s9)
    lhu  s0, 16(s9)
    mv   s2, s9
    slli t0, a0, 5              # FRAME = 32
    add  s1, s9, t0
replay_move:
    beq  s2, s1, replay_done
    lbu  t0, 19(s2)             # face
    lbu  t1, 18(s2)
    li   t2, 4
    sub  t1, t2, t1             # quarter turns
    beqz t0, replay_R
    addi t0, t0, -1
    beqz t0, replay_B
replay_D:
    lw   s3, 8(s3)
    add  t0, s7, s10
    lhu  s10, 0(t0)
    lw   s4, 4(s4)
    add  t0, s6, s11
    lhu  s11, 0(t0)
    lw   s8, 0(s8)
    add  t0, s5, s0
    lhu  s0, 0(t0)
    addi t1, t1, -1
    bnez t1, replay_D
    j    replay_next
replay_B:
    lw   s3, 4(s3)
    add  t0, s6, s10
    lhu  s10, 0(t0)
    lw   s4, 0(s4)
    add  t0, s5, s11
    lhu  s11, 0(t0)
    lw   s8, 8(s8)
    add  t0, s7, s0
    lhu  s0, 0(t0)
    addi t1, t1, -1
    bnez t1, replay_B
    j    replay_next
replay_R:
    lw   s3, 0(s3)
    add  t0, s5, s10
    lhu  s10, 0(t0)
    lw   s4, 8(s4)
    add  t0, s7, s11
    lhu  s11, 0(t0)
    lw   s8, 4(s8)
    add  t0, s6, s0
    lhu  s0, 0(t0)
    addi t1, t1, -1
    bnez t1, replay_R
replay_next:
.if RENDER
    lbu  a0, 19(s2)             # redraw after every move of the answer
    lbu  a1, 18(s2)
    li   t0, 4
    sub  a1, t0, a1
    jal  ra, render_move
.endif
    addi s2, s2, FRAME
    j    replay_move
replay_done:
    la   t0, pattern
    li   t1, BLOCK_SOLVED
    add  t0, t0, t1
    xor  t1, s3, t0
    xor  t2, s4, t0
    or   t1, t1, t2
    xor  t2, s8, t0
    or   t1, t1, t2
    or   t1, t1, s10
    or   t1, t1, s11
    or   t1, t1, s0
    sltiu a0, t1, 1
    lw   ra, 12(sp)
    jalr zero, ra, 0

.if RENDER
.include "render.s"
.endif
