/* Optimal 2x2x2 solver shaped for RV32I: IDA* over the factored coordinates.
 *
 * No heap, no recursion, and no multiply or divide in the search loop. The
 * state is the permutation rank p, the orientation rank o, and the pair code
 * c that follows cubies 0 and 3 (see gen.c), each advanced by a quarter-turn
 * table; the three turns of a face are produced in order, each from the
 * previous one, so every child costs one lookup per coordinate. The heuristic
 * is max(pattern_dist[p][c >> 6], orient_dist[o]), tested as two separate
 * comparisons so the second lookup is skipped when the first prunes.
 *
 * A child that passes both is tested twice more with the same pattern_dist,
 * through the two rotations of the cube about the fixed corner (gen.c): each
 * node carries the p and c of its two rotated states, and the rotated child
 * is reached by turning the rotated face as many times.
 */
#include <stdint.h>
#include "tables.h"

enum { CORNERS = 7, MAX_DEPTH = 11, NO_FACE = 3 };

typedef struct {
    uint16_t p, o, c;    /* this node */
    uint16_t cp, co, cc; /* the child being generated */
    uint8_t face;    /* face of that child, NO_FACE before the first */
    uint8_t turn;    /* quarter turns applied: 0 = quarter, 1 = half, 2 = ' */
    uint16_t vp[2], vc[2]; /* p and c of this node rotated once and twice */
} frame_t;

/* Counters for the operation-count argument; compiled out on the target.
 * pruned_first/second: children cut by the first or the second test.
 * pruned_view: children cut by the test through each rotation; view_turns:
 * quarter turns applied to rotated coordinates.
 * skipped_bounds: bound values an IDA* that jumped to the smallest pruned
 * f would have skipped (this one steps by 1).
 */
#ifdef COUNT_OPS
static uint64_t generated, expanded, pruned_first, pruned_second;
static uint64_t pruned_view[2], view_turns;
static uint64_t skipped_bounds;
static unsigned min_pruned_f;
#define COUNT(x) (++(x))
#else
#define COUNT(x) ((void) 0)
#endif

/* The pattern test first. Over the distance-11 states it prunes 58% of the
 * children against 16% for orient_dist, so although it costs 6 RV32I
 * instructions against 3, the pair costs 6 + 0.42 * 3 a child this way and
 * 3 + 0.84 * 6 the other way. ORIENT_FIRST is the other order, for the
 * host measurement only.
 */
#ifdef ORIENT_FIRST
#define FIRST_H(f) orient_dist[(f)->co]
#define SECOND_H(f) pattern_h((f)->cp, (f)->cc)
#else
#define FIRST_H(f) pattern_h((f)->cp, (f)->cc)
#define SECOND_H(f) orient_dist[(f)->co]
#endif

/* Parse "PPPPPPPOOOOOOO" into p, o and the pair code; returns 0 on invalid
 * input.
 */
static int parse(const char *s, uint16_t *p, uint16_t *o, uint16_t *pair)
{
    uint8_t seen = 0, sum = 0, pos0 = 0, pos3 = 0;
    uint32_t prank = 0, orank = 0;
    /* all seven cubie digits first, so the ranking below never reads past
     * the end of a short string */
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t c = (uint8_t) (s[i] - '1');
        if (c >= CORNERS || seen & 1U << c)
            return 0;
        seen |= (uint8_t) (1U << c);
        if (c == 0)
            pos0 = (uint8_t) i;
        if (c == 3)
            pos3 = (uint8_t) i;
    }
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t c = (uint8_t) (s[i] - '1');
        uint8_t smaller = 0;
        for (int j = i + 1; j < CORNERS; ++j)
            smaller += (uint8_t) (s[j] - '1') < c;
        /* smaller * (6 - i)! from a table: gcc -O2 turns both Horner's
         * prank * (7 - i) and a repeated-addition loop into __mulsi3. */
        prank += lehmer_weight[i][smaller];
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
        if (i < CORNERS - 1)
            orank = (orank << 1) + orank + t;
    }
    if (s[2 * CORNERS] != '\0' || sum)
        return 0;
    *p = (uint16_t) prank;
    *o = (uint16_t) orank;
    /* (3 * twist0 + twist3) * 64 + 7 * pos0 + pos3, by shifts and adds */
    uint8_t t0 = (uint8_t) (s[CORNERS + pos0] - '1');
    uint8_t t3 = (uint8_t) (s[CORNERS + pos3] - '1');
    uint16_t twists = (uint16_t) ((t0 << 1) + t0 + t3);
    *pair = (uint16_t) ((twists << 6) + (pos0 << 3) - pos0 + pos3);
    return 1;
}

static uint8_t pattern_h(uint16_t p, uint16_t c)
{
    return pattern_dist[p][c >> 6];
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

/* p and pair code of a valid state rotated once (index 0) and twice (1). */
static void views(const char *s, uint16_t *vp, uint16_t *vc)
{
    char once[2 * CORNERS + 1], twice[2 * CORNERS + 1];
    uint16_t o;
    rotate(s, once);
    rotate(once, twice);
    parse(once, &vp[0], &o, &vc[0]);
    parse(twice, &vp[1], &o, &vc[1]);
}

/* Heuristic of the child of f through rotation k, whose coordinates are left
 * in next: the rotated node, with the rotated face turned as often.
 */
static uint8_t view_h(const frame_t *f, frame_t *next, int k)
{
    uint8_t face = k ? sym_face[sym_face[f->face]] : sym_face[f->face];
    uint16_t p = f->vp[k], c = f->vc[k];
    for (uint8_t t = 0; t <= f->turn; ++t) {
        p = perm_turn[face][p];
        c = pair_turn[face][c];
        COUNT(view_turns);
    }
    next->vp[k] = p;
    next->vc[k] = c;
    return pattern_h(p, c);
}

#ifdef COUNT_OPS
static void note_pruned(const frame_t *f, int depth)
{
    unsigned a = orient_dist[f->co], b = pattern_h(f->cp, f->cc);
    frame_t scratch;
    uint64_t turns = view_turns;
    for (int k = 0; k < 2; ++k) {
        unsigned v = view_h(f, &scratch, k);
        b = v > b ? v : b;
    }
    view_turns = turns;
    unsigned fv = (unsigned) depth + 1 + (a > b ? a : b);
    if (fv < min_pruned_f)
        min_pruned_f = fv;
}
#endif

/* Writes moves as face * 3 + turn into path; returns the solution length.
 * vp and vc are the rotated coordinates from views.
 */
static int solve(uint16_t p, uint16_t o, uint16_t c, const uint16_t *vp,
                 const uint16_t *vc, uint8_t *path)
{
    frame_t stack[MAX_DEPTH + 1];
    if (!(p | o))
        return 0;
    uint8_t bound = pattern_h(p, c) > orient_dist[o] ? pattern_h(p, c)
                                                     : orient_dist[o];
    for (int k = 0; k < 2; ++k) {
        stack[0].vp[k] = vp[k];
        stack[0].vc[k] = vc[k];
        if (pattern_h(vp[k], vc[k]) > bound)
            bound = pattern_h(vp[k], vc[k]);
    }
#ifdef COUNT_OPS
    int searched = 0; /* a bound has been tried and failed */
#endif
    for (;; ++bound) {
#ifdef COUNT_OPS
        if (searched && min_pruned_f > bound)
            skipped_bounds += min_pruned_f - bound;
        searched = 1;
        min_pruned_f = UINT8_MAX;
#endif
        int depth = 0;
        stack[0].p = p;
        stack[0].o = o;
        stack[0].c = c;
        stack[0].face = NO_FACE;
        COUNT(expanded);
        for (;;) {
            frame_t *f = &stack[depth];
            if (f->face != NO_FACE && f->turn < 2) {
                /* next turn of the same face, from the previous child */
                ++f->turn;
                f->cp = perm_turn[f->face][f->cp];
                f->co = orient_turn[f->face][f->co];
                f->cc = pair_turn[f->face][f->cc];
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
                f->cp = perm_turn[face][f->p];
                f->co = orient_turn[face][f->o];
                f->cc = pair_turn[face][f->c];
            }
            COUNT(generated);
            if (!(f->cp | f->co)) {
                for (int i = 0; i <= depth; ++i)
                    path[i] = (uint8_t) (stack[i].face * 3U + stack[i].turn);
                return depth + 1;
            }
            /* children of f sit at depth + 1; descend only if they fit */
            uint8_t rem = (uint8_t) (bound - depth - 1);
            if (FIRST_H(f) > rem) {
                COUNT(pruned_first);
#ifdef COUNT_OPS
                note_pruned(f, depth);
#endif
                continue;
            }
            if (SECOND_H(f) > rem) {
                COUNT(pruned_second);
#ifdef COUNT_OPS
                note_pruned(f, depth);
#endif
                continue;
            }
            frame_t *next = &stack[depth + 1];
            if (view_h(f, next, 0) > rem) {
                COUNT(pruned_view[0]);
#ifdef COUNT_OPS
                note_pruned(f, depth);
#endif
                continue;
            }
            if (view_h(f, next, 1) > rem) {
                COUNT(pruned_view[1]);
#ifdef COUNT_OPS
                note_pruned(f, depth);
#endif
                continue;
            }
            COUNT(expanded);
            ++depth;
            next->p = f->cp;
            next->o = f->co;
            next->c = f->cc;
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
    uint16_t p, o, c, vp[2], vc[2];
    uint8_t path[MAX_DEPTH];
    if (argc != 2 || !parse(argv[1], &p, &o, &c)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }
    views(argv[1], vp, vc);
    int length = solve(p, o, c, vp, vc, path);
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
