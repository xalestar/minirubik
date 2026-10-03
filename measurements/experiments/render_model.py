#!/usr/bin/env python3
"""Sticker-level 3-D model of the 2x2x2, behind the LED renderer (render.s).

Checks, in order:
 1. Turning the layers of a cube in space (R clockwise seen from +x, B from
    -z, D from -y) reproduces solver.c's source[][] exactly, and following
    each corner's U/D sticker reproduces twist[][] when twist o means "the
    U/D sticker sits -o steps clockwise from the slot's U/D facelet".
 2. The two tables in render.s, r_slot_face and r_slot_xy, equal the ones
    this model derives.
 3. The renderer's formula, colour = slot_face[cubie][(k + twist) % 3], agrees
    with moving the 24 stickers directly, over 2,000 random move sequences.
With "dump FILE STATE", it also compares the two LED memory dumps printed by
led-harness.sh (scrambled STATE, then solved) with the model, LED by LED.

  python3 measurements/experiments/render_model.py
  python3 measurements/experiments/render_model.py dump OUT 24173562322133
"""
import random
import re
import sys

# x = R, y = U, z = F. Positions 0..6 as in solver.c; 7 is the fixed corner.
SLOTS = [(1, 1, 1), (1, -1, 1), (-1, -1, 1), (1, 1, -1), (1, -1, -1),
         (-1, -1, -1), (-1, 1, -1), (-1, 1, 1)]
SOURCE = [[1, 4, 2, 0, 3, 5, 6], [0, 1, 2, 4, 5, 6, 3], [0, 2, 5, 3, 1, 4, 6]]
TWIST = [[1, 2, 0, 2, 1, 0, 0], [0, 0, 0, 1, 2, 1, 2], [0, 0, 0, 0, 0, 0, 0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def rotation(axis, quarter):
    c, s = [(1, 0), (0, 1), (-1, 0), (0, -1)][quarter % 4]
    m = [[[1, 0, 0], [0, c, -s], [0, s, c]],
         [[c, 0, s], [0, 1, 0], [-s, 0, c]],
         [[c, -s, 0], [s, c, 0], [0, 0, 1]]][axis]
    return lambda v: tuple(dot(r, v) for r in m)


# (rotation, axis, side of the turning layer) for R, B, D
MOVES = [(rotation(0, -1), 0, 1), (rotation(2, 1), 2, -1), (rotation(1, 1), 1, -1)]


def turn(face, v, n=None):
    """Move position v (and sticker normal n) by one quarter turn of face."""
    rot, axis, side = MOVES[face]
    if v[axis] != side:
        return (v, n)
    return (rot(v), rot(n) if n else None)


def facelets(v):
    """The three face normals at corner v, clockwise seen from outside,
    starting with the U or D face."""
    a = (0, v[1], 0)
    for b, c in [((v[0], 0, 0), (0, 0, v[2])), ((0, 0, v[2]), (v[0], 0, 0))]:
        if dot(cross(a, b), v) < 0:
            return [a, b, c]


FACE = {(0, 1, 0): 0, (-1, 0, 0): 1, (0, 0, 1): 2, (1, 0, 0): 3,
        (0, 0, -1): 4, (0, -1, 0): 5}               # U L F R B D
NET = {0: (1, 0), 1: (0, 1), 2: (1, 1), 3: (2, 1), 4: (3, 1), 5: (1, 2)}
PALETTE = [0xFFFFFF, 0xFF8000, 0x00B000, 0xFF0000, 0x0000FF, 0xFFFF00]


def pixel(v, n):
    """Top-left LED (x, y) of the facelet of corner v with normal n."""
    x, y, z = v
    f = FACE[n]
    row, col = {
        0: (z > 0, x > 0),          # U, seen from above, B at the top
        1: (y < 0, z > 0),          # L, seen from the left
        2: (y < 0, x > 0),          # F
        3: (y < 0, z < 0),          # R, seen from the right
        4: (y < 0, x < 0),          # B, seen from behind
        5: (z < 0, x > 0),          # D, seen from below, F at the top
    }[f]
    c, r = NET[f]
    return (9 * c + 4 * col, 7 * r + 3 * row)


SLOT_FACE = [[FACE[n] for n in facelets(s)] for s in SLOTS]
SLOT_XY = [[pixel(s, n) for n in facelets(s)] for s in SLOTS]


def check_model():
    for f in range(3):
        for d in range(7):
            s = next(i for i in range(7) if turn(f, SLOTS[i])[0] == SLOTS[d])
            assert s == SOURCE[f][d], ("source", f, d)
            ud = facelets(SLOTS[s])[0]
            k = facelets(SLOTS[d]).index(turn(f, SLOTS[s], ud)[1])
            assert (-k) % 3 == TWIST[f][d], ("twist", f, d)
    print("model: R/B/D rotations reproduce solver.c source[][] and twist[][]")


def check_render_s(path):
    text = open(path).read()

    def table(label):
        """Values of a .byte table: the label's line and the indented
        .byte lines that follow it."""
        values, inside = [], False
        for line in text.splitlines():
            code = line.split("#")[0]
            if code.startswith(label + ":"):
                inside = True
            elif inside and not (code.startswith((" ", "\t")) and ".byte" in code):
                break
            if inside:
                values += [int(v) for v in re.findall(r"\d+", code.split(".byte", 1)[1])]
        return values

    face = table("r_slot_face")
    xy = table("r_slot_xy")
    assert face == [f for row in SLOT_FACE for f in row], "r_slot_face"
    assert xy == [c for row in SLOT_XY for p in row for c in p], "r_slot_xy"
    print("render.s: r_slot_face and r_slot_xy equal the model's tables")


def image_from_state(p, o):
    """LED colours by the renderer's formula from cubie/twist arrays."""
    img = {}
    for i in range(8):
        c, t = (p[i], o[i]) if i < 7 else (7, 0)
        for k in range(3):
            img[SLOT_XY[i][k]] = SLOT_FACE[c][(k + t) % 3]
    return img


def image_from_stickers(moves):
    """LED colours by moving the 24 sticker normals directly."""
    st = {(s, n): FACE[n] for s in SLOTS for n in facelets(s)}
    for f, q in moves:
        for _ in range(q):
            st = {turn(f, s, n): col for (s, n), col in st.items()}
    return {pixel(s, n): col for (s, n), col in st.items()}


def apply_solver(p, o, f, q):
    for _ in range(q):
        p = [p[SOURCE[f][d]] for d in range(7)]
        o = [(o[SOURCE[f][d]] + TWIST[f][d]) % 3 for d in range(7)]
    return p, o


def check_formula(trials=2000):
    random.seed(1)
    for _ in range(trials):
        seq = [(random.randrange(3), random.randrange(1, 4))
               for _ in range(random.randrange(12))]
        p, o = list(range(7)), [0] * 7
        for f, q in seq:
            p, o = apply_solver(p, o, f, q)
        assert image_from_state(p, o) == image_from_stickers(seq), seq
    print(f"formula: agrees with moving stickers on {trials} random sequences")


def leds(img):
    out = [0] * (35 * 25)
    for (x, y), f in img.items():
        for dy in range(3):
            for dx in range(4):
                out[(y + dy) * 35 + x + dx] = PALETTE[f]
    return out


def check_dump(path, state):
    dumps = [d for d in open(path).read().split("|") if "0x" in d][:2]
    p = [int(ch) - 1 for ch in state[:7]]
    o = [int(ch) - 1 for ch in state[7:]]
    for name, (pp, oo), dump in [("scrambled", (p, o), dumps[0]),
                                 ("solved", (list(range(7)), [0] * 7), dumps[1])]:
        got = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", dump)]
        exp = leds(image_from_state(pp, oo))
        diff = sum(a != b for a, b in zip(got, exp))
        assert len(got) == 875 and diff == 0, (name, len(got), diff)
        print(f"LED dump {name}: all {len(got)} LEDs match the model")


if __name__ == "__main__":
    check_model()
    check_render_s("render.s")
    check_formula()
    if len(sys.argv) == 4 and sys.argv[1] == "dump":
        check_dump(sys.argv[2], sys.argv[3])
