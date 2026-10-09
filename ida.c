/* Optimal 2x2x2 solver shaped for RV32I: IDA* over three views of one
 * pattern database.
 *
 * No heap, no recursion, and no multiply or divide in the search loop. A
 * view of the state is a block code b and the orientation rank o (see
 * gen.c), each advanced by a quarter-turn table. The search carries three
 * views: the state and its two rotations about the fixed corner, whose
 * coordinates advance through the same tables at the rotated face. The three
 * turns of a face are produced in order, each from the previous one.
 *
 * pattern_mod3 holds the distance of every key (b, o) modulo 3. The search
 * never rebuilds a distance. Per view it keeps a row of next_state, which
 * stands for (slack, distance mod 3) with slack = moves left - distance, and
 * steps it with the 2-bit value of the child. A child whose slack would be
 * negative in some view is pruned; a child that passes with no move left has
 * distance 0 in all three views, and only the solved state has that (gate H1
 * in gen.c).
 */
#include <stdint.h>
#include "tables.h"

enum { CORNERS = 7, MAX_DEPTH = 11, NO_FACE = 3, VIEWS = 3 };

typedef struct {
    uint16_t b[VIEWS], o[VIEWS]; /* each view of this node */
    int8_t s[VIEWS];             /* its next_state row in each view */
    uint8_t face; /* face of the child being generated, NO_FACE before the
                     first */
    uint8_t turn; /* quarter turns applied: 0 = quarter, 1 = half, 2 = ' */
} frame_t;

/* Counters for the operation-count argument; compiled out on the target.
 * pruned[k]: children cut by the test of view k, after passing the views
 * before it.
 */
#ifdef COUNT_OPS
static uint64_t generated, expanded, pruned[VIEWS], root_tries;
#define COUNT(x) (++(x))
#else
#define COUNT(x) ((void) 0)
#endif

/* Checks "PPPPPPPOOOOOOO"; returns 0 on invalid input. *parity is the parity
 * of the permutation: for each cubie, the cubies before it that are larger
 * are the bits of `seen` above its own, and the parity of a sum of bit
 * counts is the parity of the XOR of the bit sets.
 */
static int parse(const char *s, uint8_t *parity)
{
    uint8_t seen = 0, sum = 0, odd = 0;
    /* all seven cubie digits first, so nothing below reads past the end of
     * a short string */
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t c = (uint8_t) (s[i] - '1');
        if (c >= CORNERS || seen & 1U << c)
            return 0;
        odd ^= (uint8_t) (seen >> c >> 1);
        seen |= (uint8_t) (1U << c);
    }
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t t = (uint8_t) (s[CORNERS + i] - '1');
        if (t > 2)
            return 0;
        /* sum stays in 0..2: each step adds at most 2, so one conditional
         * subtract is exact, and gcc emits no __umodsi3 for it */
        sum += t;
        if (sum >= 3)
            sum -= 3;
    }
    if (s[2 * CORNERS] != '\0' || sum)
        return 0;
    odd ^= odd >> 4;
    odd ^= odd >> 2;
    odd ^= odd >> 1;
    *parity = odd & 1U;
    return 1;
}

/* Block code and orientation rank of a valid state string (gen.c's
 * block_code, read from the digits in one pass).
 */
static void coords(const char *s, uint8_t parity, uint16_t *b, uint16_t *o)
{
    uint8_t home = 0, c1 = 0, c2 = 0, mate = 0, first = 0;
    uint8_t six = 0, four = 0, twos = 0;
    uint16_t orank = 0;
    for (uint8_t i = 0; i < CORNERS - 1; ++i)
        orank = (uint16_t) ((orank << 1) + orank + s[CORNERS + i] - '1');
    for (uint8_t i = 0; i < CORNERS; ++i) {
        uint8_t block = block_of[s[i] - '1'];
        if (block == 3) {
            home = i;
            continue;
        }
        if (block == 2) {
            if (twos++)
                c2 = six;
            else
                c1 = six;
        } else {
            if (!four)
                first = block;
            else if (block == first)
                mate = (uint8_t) (four - 1U);
            ++four;
        }
        ++six;
    }
    /* ((15 * home + pair) * 3 + mate) * 2 + parity, by shifts and adds */
    uint16_t code = (uint16_t) ((home << 4) - home + pair_base[c1] + c2 - c1 -
                                1);
    code = (uint16_t) ((code << 1) + code + mate);
    *b = (uint16_t) ((code << 1) + parity);
    *o = orank;
}

/* The state string of the cube rotated about the fixed corner (gen.c); s
 * must have passed parse.
 */
static void rotate(const char *s, char *t)
{
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t cubie = (uint8_t) (s[i] - '1'), to = sym_pi[i];
        uint8_t twist = (uint8_t) (s[CORNERS + i] - '1' + sym_tau[i] + 3 -
                                   sym_tau[cubie]);
        /* 1..7: two conditional subtracts, no __umodsi3 */
        if (twist >= 3)
            twist -= 3;
        if (twist >= 3)
            twist -= 3;
        t[to] = (char) ('1' + sym_pi[cubie]);
        t[CORNERS + to] = (char) ('1' + twist);
    }
    t[2 * CORNERS] = '\0';
}

/* The coordinates of a valid state (index 0) and of the state rotated once
 * (1) and twice (2). A rotation keeps the parity of the permutation.
 */
static void views(const char *s, uint8_t parity, uint16_t *vb, uint16_t *vo)
{
    char once[2 * CORNERS + 1], twice[2 * CORNERS + 1];
    rotate(s, once);
    rotate(once, twice);
    coords(s, parity, &vb[0], &vo[0]);
    coords(once, parity, &vb[1], &vo[1]);
    coords(twice, parity, &vb[2], &vo[2]);
}

/* The distance of key (b, o) modulo 3. */
static uint8_t mod3(uint16_t b, uint16_t o)
{
    return pattern_mod3[b][o >> 2] >> ((o & 3U) << 1) & 3U;
}

/* The distance of a key, by walking home: a neighbour one closer is the one
 * whose value is one less modulo 3, since neighbours differ by at most 1.
 */
static uint8_t root_dist(uint16_t b, uint16_t o)
{
    uint8_t d = 0, v = mod3(b, o);
    while (b != BLOCK_SOLVED || o) {
        uint8_t want = v ? (uint8_t) (v - 1U) : 2, found = 0;
        for (uint8_t face = 0; face < 3 && !found; ++face) {
            uint16_t nb = b, no = o;
            for (uint8_t t = 0; t < 3 && !found; ++t) {
                nb = block_turn[face][nb];
                no = orient_turn[face][no];
                COUNT(root_tries);
                if (mod3(nb, no) == want) {
                    b = nb;
                    o = no;
                    found = 1;
                }
            }
        }
        v = want;
        ++d;
    }
    return d;
}

/* Writes moves as face * 3 + turn into path; returns the solution length.
 * vb and vo are the coordinates from views.
 */
static int solve(const uint16_t *vb, const uint16_t *vo, uint8_t *path)
{
    frame_t stack[MAX_DEPTH + 1];
    uint8_t dist[VIEWS], bound = 0;
    for (int k = 0; k < VIEWS; ++k) {
        stack[0].b[k] = vb[k];
        stack[0].o[k] = vo[k];
        dist[k] = root_dist(vb[k], vo[k]);
        if (dist[k] > bound)
            bound = dist[k];
    }
    if (!bound)
        return 0;
    for (;; ++bound) {
        int depth = 0;
        /* row 3 * slack + distance mod 3; the table has the latter */
        for (int k = 0; k < VIEWS; ++k) {
            uint8_t slack = (uint8_t) (bound - dist[k]);
            stack[0].s[k] =
                (int8_t) ((slack << 1) + slack + mod3(vb[k], vo[k]));
        }
        stack[0].face = NO_FACE;
        COUNT(expanded);
        for (;;) {
            /* the child is built in the next frame, which is its own if the
             * search descends, and the previous turn of the face if not */
            frame_t *f = &stack[depth], *next = f + 1;
            int k;
            if (f->face != NO_FACE && f->turn < 2) {
                ++f->turn;
            } else {
                /* next face, skipping the one the parent just turned */
                uint8_t face = (uint8_t) (f->face + 1U) & 3U;
                if (depth && face == stack[depth - 1].face)
                    ++face;
                if (face >= NO_FACE) {
                    if (!depth--)
                        break;
                    continue;
                }
                f->face = face;
                f->turn = 0;
                for (k = 0; k < VIEWS; ++k) {
                    next->b[k] = f->b[k];
                    next->o[k] = f->o[k];
                }
            }
            /* the rotated views turn the rotated face */
            for (uint8_t k2 = 0, face = f->face; k2 < VIEWS;
                 ++k2, face = sym_face[face]) {
                next->b[k2] = block_turn[face][next->b[k2]];
                next->o[k2] = orient_turn[face][next->o[k2]];
            }
            COUNT(generated);
            for (k = 0; k < VIEWS; ++k) {
                int8_t s = next_state[f->s[k]][mod3(next->b[k], next->o[k])];
                if (s < 0)
                    break;
                next->s[k] = s;
            }
            if (k < VIEWS) {
                COUNT(pruned[k]);
                continue;
            }
            if (depth + 1 == bound) {
                for (int i = 0; i <= depth; ++i)
                    path[i] = (uint8_t) (stack[i].face * 3U + stack[i].turn);
                return bound;
            }
            COUNT(expanded);
            ++depth;
            next->face = NO_FACE;
        }
    }
}

#ifndef NO_MAIN
#include <stdio.h>

static const char *const move_names[9] = {"R",  "R2", "R'", "B", "B2",
                                          "B'", "D",  "D2", "D'"};

int main(int argc, char **argv)
{
    uint16_t vb[VIEWS], vo[VIEWS];
    uint8_t parity, path[MAX_DEPTH];
    if (argc != 2 || !parse(argv[1], &parity)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }
    views(argv[1], parity, vb, vo);
    int length = solve(vb, vo, path);
    for (int i = 0; i < length; ++i)
        printf("%s%s", i ? " " : "", move_names[path[i]]);
    putchar('\n');
#ifdef COUNT_OPS
    fprintf(stderr, "generated %llu expanded %llu\n",
            (unsigned long long) generated, (unsigned long long) expanded);
#endif
    return fflush(stdout) != 0 || ferror(stdout);
}
#endif
