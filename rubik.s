# Optimal 2x2x2 solver in RV32I for Ripes: IDA* over three views of one
# pattern database.
#
# Same algorithm as ida.c. A view of the state is two coordinates (gen.c):
# its block code, held as the address of that code's row in `pattern`, and
# its orientation rank o, held as the offset 128 * (o / 16) + 2 * (o % 16).
# A row starts with the addresses of the rows the code turns into, so a
# quarter turn is one load for the block code and one add and one load for
# the orientation. The search carries three views, the state and its two
# rotations about the fixed corner; the rotated views turn the rotated face.
# The three turns of a face are chained, each child from the previous one,
# in registers.
#
# After its three addresses a row holds the distance modulo 3 of each
# orientation, 2 bits each, 16 to a word. The offset shifted right by 5 is
# the byte offset of the word, and a shift by the offset itself, which uses
# its low 5 bits, brings the value down. No distance is rebuilt: per view the
# node keeps the address of a 4-byte row of next_state, which stands for
# (slack, distance mod 3) with slack = moves left - distance. Adding the
# child's 2-bit value and loading gives the offset of the child's row, or -1
# if its slack would be negative: pruned. The rows of slack 0 are the first
# 16 bytes. A child with slack 0 in all three views is solved if no move is
# left, and pruned otherwise: it needs one move more (gen.c).
#
# Preprocess with asmpp.awk (Ripes has no .if or .include):
#   RENDER=0  CLI build, measured with --iret
#   RENDER=1  GUI build, draws the cube on the LED matrix after every move
# The two builds differ only in the code between .if RENDER and .endif.

.equ RENDER, 0
.equ ORIENT_FACE, 32            # from one face to the next in orient_turn
.equ FRAME, 36                  # bytes per slot in frames

.data
# No .align anywhere: it means 2^n bytes to GNU as but n bytes to Ripes.
# Instead every block keeps the next one aligned: cases.s is a multiple of
# 16 bytes, tables.s of 4, and the names, messages, pair_base and farther
# below add up to 84, so the words render.s appends start on a 4-byte
# boundary. The
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
# farther[v - u + 2]: a key with value u turns into one with value v, both
# modulo 3; the key is 1 farther, 0 as far, or -1 (255) closer.
farther:     .byte 1, 255, 0, 1, 255, 0, 0, 0

.bss
# One 36-byte slot per depth, 0..11, and two in front of the root's. The
# first of the two holds the three views of the state as given (bytes
# 0..17) and their distances (bytes 20 24 28); slot 0 holds the views with
# the farthest first. The second is the parent the root is popped to: no
# face (16), and deepen as the place to go on. Its first bytes keep the
# first move with the most nodes below it in this iteration:
#   0 its row and 10 its orientation offset in the first view (the child's)
#   4 where the search went on after it     8 the number of nodes
#   0 4 8     the pattern row of this node in each view (words)
#  12 14 16   its orientation offset in each view
#  18 turns left on the face of the child being searched (3..1)
#  19 that face (0 R, 1 B, 2 D); 3 in the slot in front: no parent face
#  20 24 28   the next_state row of this node in each view (words)
#  32 where the search goes on when the subtree of that child fails
# Move k of the answer is bytes 19 and 18 of slot k.
guard:   .zero 72
frames:  .zero 432
rotated: .zero 32               # the state string rotated once, and twice

.text
# Status bits for the exit code: 1 = a result failed validation, 2 = invalid.
main:
    addi sp, sp, -32
    la   t0, cases
    sw   t0, 0(sp)              # next case
    sw   zero, 4(sp)            # status
case_loop:
    lw   t0, 0(sp)
    lw   s8, 0(t0)              # input string, 0 ends the list
    beqz s8, finish
    mv   a0, s8
    li   a7, 4
    ecall
    mv   a0, s8
    jal  ra, parse
    beqz a2, case_invalid
    la   s9, frames
    la   s10, rotated
    mv   a0, s8
    jal  ra, coords
    sw   a2, -72(s9)            # the root's views, in the first slot in front
    sh   a3, -60(s9)
    mv   a0, s8                 # the same state rotated once, then twice
    mv   a1, s10
    jal  ra, rotate
    mv   a0, s10
    jal  ra, coords
    sw   a2, -68(s9)
    sh   a3, -58(s9)
    mv   a0, s10
    addi a1, s10, 16
    jal  ra, rotate
    addi a0, s10, 16
    jal  ra, coords
    sw   a2, -64(s9)
    sh   a3, -56(s9)
.if RENDER
    mv   a0, s8
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
    addi sp, sp, 32
    li   a7, 93
    ecall

# parse: a0 = "PPPPPPPOOOOOOO" -> a2 = 1 if the string is a valid state, else
# 0.
parse:
    li   a2, 0
    li   t6, 7
    li   t1, 0                  # i
    li   t2, 0                  # cubies seen, one bit each
parse_cubie:
    add  t4, a0, t1
    lbu  t4, 0(t4)
    addi t4, t4, -49            # cubie c = digit - '1'
    bgeu t4, t6, parse_done     # also catches digits below '1'
    srl  t5, t2, t4
    andi t3, t5, 1
    bnez t3, parse_done         # duplicate cubie
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
    li   a2, 1
parse_done:
    jalr zero, ra, 0

# coords: a0 = a valid state string -> a2 = the pattern row of its block
# code, a3 = its orientation offset. The block code is
# (15 * home + pair) * 6 + ring (gen.c).
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
    andi t1, a3, 15             # the offset: 128 * (o / 16) + 2 * (o % 16)
    srli a3, a3, 4
    slli a3, a3, 7
    slli t1, t1, 1
    or   a3, a3, t1
    li   t2, 0                  # positions so far that do not hold cubie 3
    li   a7, 0                  # the ring places so far, 2 bits each
    la   t6, kind_of
    li   t3, 4                  # a kind below 4 is a place in the ring,
    li   a1, 6                  # 6 is cubie 3, 4 and 5 are cubies 0 and 6
    mv   t0, a0
    addi t5, a0, 7
coords_cubie:
    lbu  t1, 0(t0)
    add  t1, t1, t6
    lbu  t1, -49(t1)            # the kind of the cubie, by digit - '1'
    bltu t1, t3, coords_ring
    beq  t1, a1, coords_home
    mv   a4, a5                 # a4 a5: where cubies 0 and 6 sit, in order
    mv   a5, t2
    mv   t4, t1                 # the second of the two
    j    coords_free
coords_home:
    sub  a6, t0, a0             # where cubie 3 sits
    j    coords_next
coords_ring:
    slli a7, a7, 2
    or   a7, a7, t1
coords_free:
    addi t2, t2, 1
coords_next:
    addi t0, t0, 1
    bltu t0, t5, coords_cubie
    srli t0, a7, 6              # the place of the first ring cubie
    srli t1, a7, 4
    sub  t1, t1, t0
    andi t1, t1, 3              # the second's place after the first's
    srli t2, a7, 2
    sub  t2, t2, t0
    andi t2, t2, 3              # the third's
    slli t0, t1, 1
    add  t0, t0, t1
    add  t0, t0, t2             # 3 * second + third: 5 6 7 9 10 11
    bne  t4, t3, coords_ranked
    li   t1, 16                 # cubie 0 is the second of the pair: the
    sub  t0, t1, t0             # mirror image of the ring
coords_ranked:
    slti t1, t0, 8
    add  t0, t0, t1
    addi t0, t0, -6             # ring 0..5
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
    slli t1, t1, 1              # * 6
    add  t1, t1, t0             # + ring
    slli t0, t1, 7              # a row is 196 = 128 + 64 + 4 bytes
    slli t2, t1, 6
    add  t0, t0, t2
    slli t2, t1, 2
    add  t0, t0, t2
    la   a2, pattern
    add  a2, a2, t0
    jalr zero, ra, 0

# root_dist: a2 = pattern row, a5 = orientation offset -> a0 = the distance
# of that key, a1 = the distance modulo 3. The key is turned until its
# orientation is home, each time by the quarter turn that orient_turn names
# for the orientation (bytes 96.. of its block: the offset after the turn,
# and 32 * the face); the last byte of a row is the distance of its code
# there. On the way the values modulo 3 tell how the distance changes: a
# quarter turn changes it by at most 1. Needs s5 = orient_turn and t2 =
# farther; returns through a7.
root_dist:
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi a1, t0, 3
    mv   t5, a1                 # the value before the next turn
    li   a0, 0                  # how much farther the key is by now
    beqz a5, root_home
root_turn:
    add  t0, s5, a5
    lhu  t1, 96(t0)
    andi t0, t1, 96
    srli t0, t0, 3              # 4 * face: where the row keeps that turn
    add  t0, t0, a2
    lw   a2, 0(t0)
    andi a5, t1, -97            # the offset without the face
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3
    sub  t1, t0, t5             # -2..2
    add  t1, t1, t2
    lb   t1, 2(t1)              # 1 farther, 0 the same, -1 closer
    add  a0, a0, t1
    mv   t5, t0
    bnez a5, root_turn
root_home:
    lbu  t0, 195(a2)            # the distance at the home orientation
    sub  a0, t0, a0
    jalr zero, a7, 0

# solve: the first slot in front of frames holds the three views of the
# root (bytes 0..17) -> a0 = solution length. Move k of the solution is bytes
# 19 and 18 of slot k: the face, and 4 minus the number of quarter turns.
# The view that is farthest from solved is searched as the state's own view:
# its test prunes the most, and every child pays for it. The other two
# follow in the same cyclic order, so of two views equally far the one whose
# follower is farther goes first. This is the search of the cube rotated
# that many times; last turns the faces back. Writes the count to 16(sp)
# and the solved row to 20(sp).
# Registers while searching:
#   a2 a3 a4  the child's pattern row in each view
#   a5 a6 a7  its orientation offset in each view
#   s0 s1 s2  the node's next_state row in each view
#   s3 s10 s11  the child's offset into next_state in each view
#   s5 s6 s7  orient_turn of R, B, D        t2 next_state
#   s4 the node's slot    s9 frames         s8 the last slot that is expanded
#   t4 turns left on this face    t6 the parent's face, then this face for
#   a child that passes           t5 where a child that is cut goes back to
#   t3 a1 gp  the constants 1, 2 and 16     t0 t1 scratch    tp the bound
#   ra the nodes below the first move that is being searched
#   a0 turns left when the rotated views were last turned
solve:
    sw   ra, 24(sp)             # ra counts the nodes below a first move
    la   s5, orient_turn
    addi s6, s5, ORIENT_FACE
    addi s7, s6, ORIENT_FACE
    la   s8, pattern
    li   t0, BLOCK_SOLVED
    add  s8, s8, t0
    sw   s8, 20(sp)             # replay compares with it
    li   t3, 1
    li   gp, 16                 # the rows of next_state below 16: slack 0
    sb   gp, -17(s9)            # the slot in front of the root's: no face,
    la   t0, deepen             # and a pop of the root goes on at deepen
    sw   t0, -4(s9)
    la   t2, farther            # for root_dist
    li   s10, 0                 # the largest of the three distances
    mv   s4, s9                 # s4 t6: the view's row and orientation
    mv   t6, s9
    addi s11, s9, 12
solve_view:
    lw   a2, -72(s4)
    lhu  a5, -60(t6)
    sw   a2, 0(s4)
    sh   a5, 12(t6)
    jal  a7, root_dist
    sb   a0, -52(s4)            # kept in the slot in front, bytes 20 24 28
    bgeu s10, a0, solve_kept
    mv   s10, a0
solve_kept:
    slli t0, a0, 4              # 4 * (distance mod 3) - 16 * distance, the
    slli t1, a1, 2              # offset into next_state at slack 0
    sub  t0, t1, t0
    sw   t0, 20(s4)
    addi s4, s4, 4
    addi t6, t6, 2
    bltu s4, s11, solve_view
    mv   a0, s10
    beqz s10, solve_done        # already solved
    lbu  t0, -52(s9)            # the first view: the farthest, and of two
    lbu  t1, -48(s9)            # equally far the one whose follower is
    lbu  t2, -44(s9)            # farther
    bne  t0, t1, solve_order    # three keys equally far: the state is one
    bne  t1, t2, solve_order    # move farther (gen.c)
    addi s10, s10, 1
solve_order:
    slli a2, t0, 4
    add  a2, a2, t1
    slli a3, t1, 4
    add  a3, a3, t2
    slli a4, t2, 4
    add  a4, a4, t0
    li   s0, 0
    bgeu a2, a3, solve_second
    li   s0, 1
    mv   a2, a3
solve_second:
    bgeu a2, a4, solve_lead
    li   s0, 2
solve_lead:
    sw   s0, 16(sp)
    beqz s0, solve_first
solve_rotate:                   # every view moves up one place
    lw   t0, 0(s9)
    lw   t1, 4(s9)
    lw   t2, 8(s9)
    sw   t1, 0(s9)
    sw   t2, 4(s9)
    sw   t0, 8(s9)
    lhu  t0, 12(s9)
    lhu  t1, 14(s9)
    lhu  t2, 16(s9)
    sh   t1, 12(s9)
    sh   t2, 14(s9)
    sh   t0, 16(s9)
    lw   t0, 20(s9)
    lw   t1, 24(s9)
    lw   t2, 28(s9)
    sw   t1, 20(s9)
    sw   t2, 24(s9)
    sw   t0, 28(s9)
    addi s0, s0, -1
    bnez s0, solve_rotate
solve_first:
    la   t2, next_state
    slli t0, s10, 4             # slack = bound - distance: add 16 * bound
    add  t0, t0, t2
    lw   s0, 20(s9)
    lw   s1, 24(s9)
    lw   s2, 28(s9)
    add  s0, s0, t0
    add  s1, s1, t0
    add  s2, s2, t0
    slli s8, s10, 5             # FRAME = 36
    slli t0, s10, 2
    add  s8, s8, t0
    add  s8, s8, s9
    addi s8, s8, -FRAME         # the slot at depth bound - 1
    mv   tp, s10                # the bound
    li   a1, 2
    j    iteration
# With 11 moves an answer is certain, and it pays to start with a good
# first move: the one with the most nodes below it when the bound was 10 is
# most often the first move of an answer. The search starts at the test of
# that first move, 40 bytes before the place it went on from, and goes on
# with the first moves after it. If they all fail, the root is popped to
# deepen with the bound at 11: then all first moves are searched in order.
deepen:
    li   t0, 11
    lw   s0, 20(s9)
    lw   s1, 24(s9)
    lw   s2, 28(s9)
    beq  tp, t0, iteration
    addi tp, tp, 1
    addi s8, s8, FRAME
    addi s0, s0, 16             # one more move: slack + 1 in each view
    addi s1, s1, 16
    addi s2, s2, 16
    bne  tp, t0, iteration
    lhu  t1, -28(s9)
    beqz t1, iteration          # no first move had a node below it
    sw   s0, 20(s9)
    sw   s1, 24(s9)
    sw   s2, 28(s9)
    sh   zero, -28(s9)
    mv   s4, s9
    mv   t6, gp
    li   ra, 0
    lw   a2, -36(s9)            # the first view after that first move,
    lhu  a5, -26(s9)
    lw   a3, 4(s9)              # the rotated views of the root
    lw   a4, 8(s9)
    lhu  a6, 14(s9)
    lhu  a7, 16(s9)
    li   a0, 4
    lw   t0, -32(s9)
start_there:
    jalr zero, t0, -40
iteration:
    sw   s0, 20(s9)
    sw   s1, 24(s9)
    sw   s2, 28(s9)
    sh   zero, -28(s9)          # no first move has nodes below it yet
    li   ra, 0
    mv   s4, s9
    mv   t6, gp                 # the root has no parent face
# One block per face: the node's coordinates are loaded, then the state's
# own view is turned three times, each turn with its own copy of the test.
# A child that fails it costs nothing more. A child that passes goes
# through the rest of its face (rest_R): the rotated views catch up and are
# tested. They turn the face that the state's face becomes when the cube is
# rotated once and twice (R -> D -> B -> R); a0 is the value t4 had when
# they were last up to date (4: at the node), and they turn until it equals
# t4. The first negative row sends the search back through t5, to the next
# turn or the next face. A child that passes with no slack in any view is
# solved or cut (tight_R). When a child is searched, t5 is kept in the
# node's slot, and the pop of the child comes back through it. A node is
# entered at first_R or first_B, after the loads: it was just a child.
face_R:
    beqz t6, face_B             # skip the face the parent just turned
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
first_R:
    li   a0, 4
R_1:
    lw   a2, 0(a2)
    add  t0, s5, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, R_2
    li   t4, 3
    jal  t5, rest_R
R_2:
    lw   a2, 0(a2)
    add  t0, s5, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, R_3
    li   t4, 2
    jal  t5, rest_R
R_3:
    lw   a2, 0(a2)
    add  t0, s5, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, face_B
    li   t4, 1
    jal  t5, rest_R
face_B:
    beq  t6, t3, face_D
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
first_B:
    li   a0, 4
B_1:
    lw   a2, 4(a2)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, B_2
    li   t4, 3
    jal  t5, rest_B
B_2:
    lw   a2, 4(a2)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, B_3
    li   t4, 2
    jal  t5, rest_B
B_3:
    lw   a2, 4(a2)
    add  t0, s6, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, face_D
    li   t4, 1
    jal  t5, rest_B
face_D:
    beq  t6, a1, pop
    lw   a2, 0(s4)
    lw   a3, 4(s4)
    lw   a4, 8(s4)
    lhu  a5, 12(s4)
    lhu  a6, 14(s4)
    lhu  a7, 16(s4)
    li   a0, 4
D_1:
    lw   a2, 8(a2)
    add  t0, s7, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, D_2
    li   t4, 3
    jal  t5, rest_D
D_2:
    lw   a2, 8(a2)
    add  t0, s7, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, D_3
    li   t4, 2
    jal  t5, rest_D
D_3:
    lw   a2, 8(a2)
    add  t0, s7, a5
    lhu  a5, 0(t0)
    srli t0, a5, 5
    add  t0, t0, a2
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a5             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s0
    lb   s3, 0(t0)
    bltz s3, pop
    li   t4, 1
    jal  t5, rest_D
pop:
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
    lbu  a0, 18(s4)             # the child had all three views up to date
    lw   t5, 32(s4)
    lbu  t6, -17(s4)            # the face of the slot before
    beq  t6, gp, popped_first   # none: a first move failed
back:
    jalr zero, t5, 0            # the next turn, the next face, or deepen
popped_first:
    lhu  t0, -28(s9)            # the most nodes below a first move so far
    bgeu t0, ra, popped_fewer
    sh   ra, -28(s9)
    sw   t5, -32(s9)
    sw   a2, -36(s9)
    sh   a5, -26(s9)
popped_fewer:
    li   ra, 0
    jalr zero, t5, 0
passed:
    sb   t4, 18(s4)             # this move, for the answer
    sb   t6, 19(s4)
    sw   t5, 32(s4)             # where to go on if the child's subtree fails
    addi ra, ra, 1
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
    bnez t6, first_R            # the child's coordinates are in the registers:
    j    first_B                # its first face, R, or B after an R
rest_R:
    lw   a3, 8(a3)
    add  t0, s7, a6
    lhu  a6, 0(t0)
    lw   a4, 4(a4)
    add  t0, s6, a7
    lhu  a7, 0(t0)
    addi a0, a0, -1
    bne  a0, t4, rest_R
    srli t0, a6, 5
    add  t0, t0, a3
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a6             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, back
    srli t0, a7, 5
    add  t0, t0, a4
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a7             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, back
    bltu s11, gp, tight_R       # no slack in the third view
pass_R:
    li   t6, 0                  # the child's parent face
    j    passed
tight_R:
    or   t0, s3, s10
    bgeu t0, gp, pass_R         # slack in another view
    bne  s4, s8, back           # moves left: one move more is needed
    li   t6, 0                  # no move left: solved
    j    last
rest_B:
    lw   a3, 0(a3)
    add  t0, s5, a6
    lhu  a6, 0(t0)
    lw   a4, 8(a4)
    add  t0, s7, a7
    lhu  a7, 0(t0)
    addi a0, a0, -1
    bne  a0, t4, rest_B
    srli t0, a6, 5
    add  t0, t0, a3
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a6             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, back
    srli t0, a7, 5
    add  t0, t0, a4
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a7             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, back
    bltu s11, gp, tight_B       # no slack in the third view
pass_B:
    li   t6, 1                  # the child's parent face
    j    passed
tight_B:
    or   t0, s3, s10
    bgeu t0, gp, pass_B         # slack in another view
    bne  s4, s8, back           # moves left: one move more is needed
    li   t6, 1                  # no move left: solved
    j    last
rest_D:
    lw   a3, 4(a3)
    add  t0, s6, a6
    lhu  a6, 0(t0)
    lw   a4, 0(a4)
    add  t0, s5, a7
    lhu  a7, 0(t0)
    addi a0, a0, -1
    bne  a0, t4, rest_D
    srli t0, a6, 5
    add  t0, t0, a3
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a6             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s1
    lb   s10, 0(t0)
    bltz s10, back
    srli t0, a7, 5
    add  t0, t0, a4
    lw   t0, 12(t0)             # the word of 16 values
    srl  t0, t0, a7             # by the low 5 bits
    andi t0, t0, 3              # the child's distance modulo 3
    add  t0, t0, s2
    lb   s11, 0(t0)
    bltz s11, back
    bltu s11, gp, tight_D       # no slack in the third view
pass_D:
    li   t6, 2                  # the child's parent face
    j    passed
tight_D:
    or   t0, s3, s10
    bgeu t0, gp, pass_D         # slack in another view
    bne  s4, s8, back           # moves left: one move more is needed
    li   t6, 2                  # no move left: solved
last:
    sb   t4, 18(s4)             # the last move of the answer
    sb   t6, 19(s4)
    lw   t1, 16(sp)             # the faces of the cube as given: a rotation
    beqz t1, found_length       # takes a face to the one before it
    mv   t0, s9
    li   a2, 3
found_face:
    lbu  t2, 19(t0)
    add  t2, t2, t1
    bltu t2, a2, found_kept
    addi t2, t2, -3
found_kept:
    sb   t2, 19(t0)
    addi t0, t0, FRAME
    bgeu s4, t0, found_face
found_length:
    mv   a0, tp
    lw   ra, 24(sp)
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
    slli t0, a0, 5              # FRAME = 36
    slli t1, a0, 2
    add  t0, t0, t1
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

# replay: a0 = length -> a0 = 1 if applying the moves in frames, as they are
# printed, to the three views of the state as given reaches the solved key in
# all of them, else 0. This is gate T5 on the target. Needs s5 s6 s7 s9 as
# solve left them.
replay:
    sw   ra, 12(sp)
    lw   s3, -72(s9)            # the views as given, not as searched
    lw   s4, -68(s9)
    lw   s8, -64(s9)
    lhu  s10, -60(s9)
    lhu  s11, -58(s9)
    lhu  s0, -56(s9)
    mv   s2, s9
    slli t0, a0, 5              # FRAME = 36
    slli t1, a0, 2
    add  t0, t0, t1
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
    lw   t0, 20(sp)             # the solved row
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
