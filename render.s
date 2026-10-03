# LED matrix renderer, assembled only when RENDER = 1 (included by rubik.s).
#
# Draws the cube as an unfolded net on a 35 x 25 LED matrix: U above,
# L F R B across, D below. A facelet is 4 x 3 pixels, a face 8 x 6, with a
# 1-pixel gap between faces: 4 * 8 + 3 = 35 wide, 3 * 6 + 2 = 20 tall.
#
# The renderer keeps its own copy of the cube as cubie and twist arrays,
# parsed from the same input string, and applies each move the solver
# returns with the source/twist tables of solver.c. The search state (two
# ranks) cannot be decoded without division, and an independent copy means
# the picture shows what the moves actually do.
#
# Slot k-faces are listed clockwise seen from outside, starting with the
# U or D face. A cubie c with twist o in slot i shows, on slot face k, the
# colour of its own home face (k + o) mod 3, that is slot_face[c][(k+o)%3].

# Iterations of a 2-instruction wait after each frame; tune to the speed
# Ripes reaches on your machine when run with the fast-forward button.
.equ RENDER_DELAY, 16000000

.data
# 24-bit RGB per face: U white, L orange, F green, R red, B blue, D yellow
r_palette:   .word 0xFFFFFF, 0xFF8000, 0x00B000, 0xFF0000, 0x0000FF, 0xFFFF00
# slot_face[slot][k]: face of the slot's k-th facelet; slot 7 is the fixed
# front-upper-left corner, which never moves
r_slot_face: .byte 0, 3, 2,  5, 2, 3,  5, 1, 2,  0, 4, 3
             .byte 5, 3, 4,  5, 4, 1,  0, 1, 4,  0, 2, 1
# slot_xy[slot][k]: top-left pixel (x, y) of that facelet
r_slot_xy:   .byte 13, 3, 18, 7, 13, 7,    13, 14, 13, 10, 18, 10
             .byte 9, 14, 4, 10, 9, 10,    13, 0, 27, 7, 22, 7
             .byte 13, 17, 22, 10, 27, 10, 9, 17, 31, 10, 0, 10
             .byte 9, 0, 0, 7, 31, 7,      9, 3, 9, 7, 4, 7
# solver.c's quarter turns: destination i takes the cubie from source[f][i]
# and adds twist[f][i]
r_source:    .byte 1, 4, 2, 0, 3, 5, 6,  0, 1, 2, 4, 5, 6, 3,  0, 2, 5, 3, 1, 4, 6
r_twist:     .byte 1, 2, 0, 2, 1, 0, 0,  0, 0, 0, 1, 2, 1, 2,  0, 0, 0, 0, 0, 0, 0

.bss
r_addr:      .zero 96           # LED address of each facelet, slot * 3 + k
r_cubie:     .zero 8
r_twists:    .zero 8
r_next:      .zero 16           # scratch for one quarter turn

.text
# render_init: a0 = input string. Computes facelet addresses, clears the
# matrix, and draws the starting state. Uses only a and t registers.
render_init:
    mv   t6, ra
    li   t0, 0
render_init_digits:
    add  t1, a0, t0
    lbu  t2, 0(t1)
    addi t2, t2, -49
    la   t3, r_cubie
    add  t3, t3, t0
    sb   t2, 0(t3)
    lbu  t2, 7(t1)
    addi t2, t2, -49
    la   t3, r_twists
    add  t3, t3, t0
    sb   t2, 0(t3)
    addi t0, t0, 1
    li   t1, 7
    bltu t0, t1, render_init_digits
    li   a1, LED_MATRIX_0_WIDTH
    slli a1, a1, 2              # bytes per LED row
    la   a2, r_slot_xy
    la   a3, r_addr
    li   a4, 24
render_init_addr:
    lbu  t0, 0(a2)              # x
    lbu  t1, 1(a2)              # y
    li   t2, LED_MATRIX_0_BASE
    slli t0, t0, 2
    add  t2, t2, t0
render_init_row:
    beqz t1, render_init_store  # y * row bytes by repeated addition:
    add  t2, t2, a1             # at most 17 adds, 24 times, once
    addi t1, t1, -1
    j    render_init_row
render_init_store:
    sw   t2, 0(a3)
    addi a2, a2, 2
    addi a3, a3, 4
    addi a4, a4, -1
    bnez a4, render_init_addr
    li   t0, LED_MATRIX_0_BASE  # clear: HEIGHT rows of row bytes
    li   t1, LED_MATRIX_0_HEIGHT
render_clear_row:
    add  t2, t0, a1
render_clear_led:
    sw   zero, 0(t0)
    addi t0, t0, 4
    bne  t0, t2, render_clear_led
    addi t1, t1, -1
    bnez t1, render_clear_row
    li   a2, 7                  # the fixed corner, drawn once
    li   a3, 0
render_fixed:
    mv   a0, a2
    mv   a5, a2                 # it is its own cubie, never twisted
    li   a6, 0
    jal  ra, render_facelet
    addi a3, a3, 1
    li   t0, 3
    bltu a3, t0, render_fixed
    mv   ra, t6
    j    render_draw

# render_move: a0 = the move's face as its offset into perm_turn (0, 10080,
# 20160), a1 = quarter turns. Applies it to the arrays and redraws.
render_move:
    li   t0, 0
    beqz a0, render_face_known
    li   t0, 7
    li   t1, PERM_FACE
    beq  a0, t1, render_face_known
    li   t0, 14
render_face_known:
    la   a2, r_source
    add  a2, a2, t0             # this face's row of source and twist
    la   a3, r_twist
    add  a3, a3, t0
render_quarter:
    la   a4, r_cubie
    la   a5, r_twists
    la   a6, r_next
    li   t0, 0
render_quarter_slot:
    add  t1, a2, t0
    lbu  t1, 0(t1)              # from = source[f][i]
    add  t2, a4, t1
    lbu  t2, 0(t2)
    add  t3, a6, t0
    sb   t2, 0(t3)              # next cubie
    add  t2, a5, t1
    lbu  t2, 0(t2)
    add  t3, a3, t0
    lbu  t3, 0(t3)
    add  t2, t2, t3             # twist + twist, at most 4
    li   t3, 3
    bltu t2, t3, render_twist_reduced
    addi t2, t2, -3
render_twist_reduced:
    add  t3, a6, t0
    sb   t2, 8(t3)              # next twist
    addi t0, t0, 1
    li   t1, 7
    bltu t0, t1, render_quarter_slot
    li   t0, 0
render_quarter_copy:
    add  t1, a6, t0
    lbu  t2, 0(t1)
    lbu  t3, 8(t1)
    add  t1, a4, t0
    sb   t2, 0(t1)
    add  t1, a5, t0
    sb   t3, 0(t1)
    addi t0, t0, 1
    li   t1, 7
    bltu t0, t1, render_quarter_copy
    addi a1, a1, -1
    bnez a1, render_quarter

# render_draw: paints the 21 facelets of the seven movable slots, then
# waits, so each move stays on screen when the GUI runs at full speed.
render_draw:
    mv   t6, ra
    li   a2, 0                  # slot
render_draw_slot:
    la   t0, r_cubie
    add  t0, t0, a2
    lbu  a5, 0(t0)              # cubie
    la   t0, r_twists
    add  t0, t0, a2
    lbu  a6, 0(t0)              # twist
    li   a3, 0                  # k
render_draw_facelet:
    mv   a0, a2
    jal  ra, render_facelet
    addi a3, a3, 1
    li   t0, 3
    bltu a3, t0, render_draw_facelet
    addi a2, a2, 1
    li   t0, 7
    bltu a2, t0, render_draw_slot
    li   t0, RENDER_DELAY
render_wait:
    addi t0, t0, -1
    bnez t0, render_wait
    mv   ra, t6
    jalr zero, ra, 0

# render_facelet: a0 = slot, a3 = k, a5 = cubie, a6 = twist. Fills the
# 4 x 3 facelet with slot_face[cubie][(k + twist) % 3]. Clobbers t0-t5.
render_facelet:
    add  t0, a3, a6
    li   t1, 3
    bltu t0, t1, render_side_reduced
    addi t0, t0, -3
render_side_reduced:
    slli t1, a5, 1
    add  t1, t1, a5             # cubie * 3
    add  t0, t0, t1
    la   t1, r_slot_face
    add  t0, t0, t1
    lbu  t0, 0(t0)              # face of that sticker
    slli t0, t0, 2
    la   t1, r_palette
    add  t0, t0, t1
    lw   t2, 0(t0)              # colour
    slli t0, a0, 1
    add  t0, t0, a0             # slot * 3
    add  t0, t0, a3
    slli t0, t0, 2
    la   t1, r_addr
    add  t0, t0, t1
    lw   t3, 0(t0)              # top-left LED
    li   t4, LED_MATRIX_0_WIDTH
    slli t4, t4, 2
    li   t5, 3
render_facelet_row:
    sw   t2, 0(t3)
    sw   t2, 4(t3)
    sw   t2, 8(t3)
    sw   t2, 12(t3)
    add  t3, t3, t4
    addi t5, t5, -1
    bnez t5, render_facelet_row
    jalr zero, ra, 0
