/* Optimal 2x2x2 solver shaped for RV32I: IDA* over three views of one
 * pattern database.
 *
 * No heap, no recursion, and no multiply or divide in the search loop. A
 * view of the state is a block code b and the orientation rank o (see
 * gen.c), each advanced by a quarter-turn table. The search carries three
 * views: the state and its two rotations about the fixed corner, whose
 * coordinates advance through the same tables at the rotated face. The three
 * turns of a face are produced in order, each from the previous one. The
 * rotated views are turned only for a child that passes the test of the
 * state's own view; they then catch up on the turns they missed.
 *
 * pattern_mod3 holds the distance of every key (b, o) modulo 3. The search
 * never rebuilds a distance. Per view it keeps a row of next_state, which
 * stands for (slack, distance mod 3) with slack = moves left - distance, and
 * steps it with the 2-bit value of the child. A child whose slack would be
 * negative in some view is pruned.
 *
 * A child with slack 0 in all three views has its three keys exactly as far
 * as there are moves left. With no move left that is distance 0 in all three
 * views, and only the solved state has that. With moves left the child is
 * pruned: in at least one view the key of a state is closer than the state
 * (gen.c, gate H1), so this state needs one move more.
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
    uint8_t behind; /* turns of this face the rotated views have missed */
} frame_t;

/* Counters for the operation-count argument; compiled out on the target.
 * pruned[k]: children cut by the test of view k, after passing the views
 * before it. pruned_tight: children that pass and have slack 0 in every
 * view. rotated_turns: quarter turns of a rotated view.
 */
#ifdef COUNT_OPS
static uint64_t generated, expanded, pruned[VIEWS], pruned_tight, root_tries,
    rotated_turns;
#define COUNT(x) (++(x))
#else
#define COUNT(x) ((void) 0)
#endif

/* Checks "PPPPPPPOOOOOOO"; returns 0 on invalid input. */
static int parse(const char *s)
{
    uint8_t seen = 0, sum = 0;
    /* all seven cubie digits first, so nothing below reads past the end of
     * a short string */
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t c = (uint8_t) (s[i] - '1');
        if (c >= CORNERS || seen & 1U << c)
            return 0;
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
    return s[2 * CORNERS] == '\0' && !sum;
}

/* Block code and orientation rank of a valid state string (gen.c's
 * block_code, read from the digits in one pass).
 */
static void coords(const char *s, uint16_t *b, uint16_t *o)
{
    uint8_t home = 0, c1 = 0, c2 = 0, six = 0, ring = 0, last = 0;
    uint16_t orank = 0;
    for (uint8_t i = 0; i < CORNERS - 1; ++i)
        orank = (uint16_t) ((orank << 1) + orank + s[CORNERS + i] - '1');
    for (uint8_t i = 0; i < CORNERS; ++i) {
        uint8_t kind = kind_of[s[i] - '1'];
        if (kind == KIND_HOME) {
            home = i;
            continue;
        }
        if (kind >= KIND_PAIR) {
            c1 = c2;
            c2 = six;
            last = kind;
        } else {
            ring = (uint8_t) (ring << 2 | kind);
        }
        ++six;
    }
    /* the places of the second and the third ring cubie after the first,
     * in the mirror image if cubie 0 is the second of the pair */
    uint8_t first = ring >> 6;
    uint8_t second = (uint8_t) ((ring >> 4) - first) & 3U;
    uint8_t third = (uint8_t) ((ring >> 2) - first) & 3U;
    uint8_t t = (uint8_t) ((second << 1) + second + third);
    if (last == KIND_PAIR)
        t = (uint8_t) (16U - t);
    t = (uint8_t) (t - 6U + (t < 8));
    /* (15 * home + pair) * 6 + ring, by shifts and adds */
    uint16_t code = (uint16_t) ((home << 4) - home + pair_base[c1] + c2 - c1 -
                                1);
    code = (uint16_t) ((code << 1) + code);
    *b = (uint16_t) ((code << 1) + t);
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
 * (1) and twice (2).
 */
static void views(const char *s, uint16_t *vb, uint16_t *vo)
{
    char once[2 * CORNERS + 1], twice[2 * CORNERS + 1];
    rotate(s, once);
    rotate(once, twice);
    coords(s, &vb[0], &vo[0]);
    coords(once, &vb[1], &vo[1]);
    coords(twice, &vb[2], &vo[2]);
}

/* The distance of key (b, o) modulo 3. */
static uint8_t mod3(uint16_t b, uint16_t o)
{
    return pattern_mod3[b][o >> 2] >> ((o & 3U) << 1) & 3U;
}

/* The distance of a key. The key is turned until its orientation is home,
 * each time by the quarter turn that orient_home names for the orientation;
 * home_dist has the distance of every block code there. On the way the
 * values modulo 3 tell how the distance changes: a quarter turn changes it
 * by at most 1.
 */
static uint8_t root_dist(uint16_t b, uint16_t o)
{
    /* by (value after - value before) + 2: farther, closer, same, ... */
    static const int8_t change[5] = {1, -1, 0, 1, -1};
    uint8_t v = mod3(b, o);
    int8_t farther = 0;
    while (o) {
        uint8_t face = orient_home[o];
        b = block_turn[face][b];
        o = orient_turn[face][o];
        uint8_t w = mod3(b, o);
        farther = (int8_t) (farther + change[w - v + 2]);
        v = w;
        COUNT(root_tries);
    }
    return (uint8_t) (home_dist[b] - farther);
}

/* Writes moves as face * 3 + turn into path; returns the solution length.
 * vb and vo are the coordinates from views.
 */
static int solve(const uint16_t *vb, const uint16_t *vo, uint8_t *path)
{
    frame_t stack[MAX_DEPTH + 1];
    uint8_t far[VIEWS], dist[VIEWS], bound = 0, lead = 0, best = 0;
    for (uint8_t k = 0; k < VIEWS; ++k) {
        far[k] = root_dist(vb[k], vo[k]);
        if (far[k] > bound)
            bound = far[k];
    }
    if (!bound)
        return 0;
    /* three keys equally far: the state is one move farther */
    if (far[0] == far[1] && far[1] == far[2])
        ++bound;
    /* The view that is farthest from solved goes first: its test prunes the
     * most, and it is the one every child pays for. The other two follow in
     * the same cyclic order, so of two views equally far, the one whose
     * follower is farther goes first. This is the search of the cube
     * rotated `lead` times, where a face f is called sym_face[f], `lead`
     * times over.
     */
    for (uint8_t k = 0; k < VIEWS; ++k) {
        uint8_t key = (uint8_t) ((far[k] << 4) + far[k == 2 ? 0 : k + 1]);
        if (key > best) {
            best = key;
            lead = k;
        }
    }
    for (uint8_t j = 0, k = lead; j < VIEWS; ++j) {
        stack[0].b[j] = vb[k];
        stack[0].o[j] = vo[k];
        dist[j] = far[k];
        if (++k == VIEWS)
            k = 0;
    }
    /* When the bound is MAX_DEPTH an answer is certain, and it pays to
     * start with a good first move: the one that had the most nodes below
     * it in the iteration before is most often the first move of an answer.
     * The search starts at that first move and goes on with those after it;
     * if they all fail, all first moves are searched in order.
     */
    uint8_t first_face = NO_FACE, first_turn = 0;
    for (;; ++bound) {
        uint8_t most_face = NO_FACE, most_turn = 0;
        uint16_t most = 0, below = 0;
        /* row 3 * slack + distance mod 3; the table has the latter */
        for (int k = 0; k < VIEWS; ++k) {
            uint8_t slack = (uint8_t) (bound - dist[k]);
            stack[0].s[k] = (int8_t) ((slack << 1) + slack +
                                      mod3(stack[0].b[k], stack[0].o[k]));
        }
        for (int only = bound == MAX_DEPTH && first_face != NO_FACE, all = 0;
             !all; all = !only, only = 0) {
        int depth = 0;
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
                    if (!depth) { /* a first move failed */
                        if (below > most) {
                            most = below;
                            most_face = stack[0].face;
                            most_turn = stack[0].turn;
                        }
                        below = 0;
                    }
                    continue;
                }
                f->face = face;
                f->turn = 0;
                f->behind = 0;
                for (k = 0; k < VIEWS; ++k) {
                    next->b[k] = f->b[k];
                    next->o[k] = f->o[k];
                }
            }
            next->b[0] = block_turn[f->face][next->b[0]];
            next->o[0] = orient_turn[f->face][next->o[0]];
            ++f->behind;
            if (!depth && only > 0) { /* turn past the first moves before it */
                if (f->face != first_face || f->turn != first_turn)
                    continue;
                only = -1;
            }
            COUNT(generated);
            int8_t s = next_state[f->s[0]][mod3(next->b[0], next->o[0])];
            if (s < 0) {
                COUNT(pruned[0]);
                continue;
            }
            next->s[0] = s;
            /* the rotated views turn the rotated face */
            for (; f->behind; --f->behind)
                for (uint8_t k2 = 1, face = sym_face[f->face]; k2 < VIEWS;
                     ++k2, face = sym_face[face]) {
                    next->b[k2] = block_turn[face][next->b[k2]];
                    next->o[k2] = orient_turn[face][next->o[k2]];
                    COUNT(rotated_turns);
                }
            for (k = 1; k < VIEWS; ++k) {
                s = next_state[f->s[k]][mod3(next->b[k], next->o[k])];
                if (s < 0)
                    break;
                next->s[k] = s;
            }
            if (k < VIEWS) {
                COUNT(pruned[k]);
                continue;
            }
            /* rows 0..2 are slack 0 */
            if (next->s[0] < 3 && next->s[1] < 3 && next->s[2] < 3) {
                if (depth + 1 != bound) {
                    COUNT(pruned_tight);
                    continue;
                }
                /* back to the faces of the cube as given: sym_face takes a
                 * face to the one before it, so add `lead` */
                for (int i = 0; i <= depth; ++i) {
                    uint8_t face = (uint8_t) (stack[i].face + lead);
                    if (face >= 3)
                        face -= 3;
                    path[i] = (uint8_t) ((face << 1) + face + stack[i].turn);
                }
                return bound;
            }
            COUNT(expanded);
            ++below;
            ++depth;
            next->face = NO_FACE;
        }
        }
        first_face = most_face;
        first_turn = most_turn;
    }
}

#ifndef NO_MAIN
#include <stdio.h>

static const char *const move_names[9] = {"R",  "R2", "R'", "B", "B2",
                                          "B'", "D",  "D2", "D'"};

int main(int argc, char **argv)
{
    uint16_t vb[VIEWS], vo[VIEWS];
    uint8_t path[MAX_DEPTH];
    if (argc != 2 || !parse(argv[1])) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }
    views(argv[1], vb, vo);
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
