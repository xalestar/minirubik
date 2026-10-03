/* Optimal 2x2x2 solver shaped for RV32I: IDA* over the factored coordinates.
 *
 * No heap, no recursion, and no multiply or divide in the search loop. The
 * state is a pair (permutation rank, orientation rank) advanced by the
 * quarter-turn tables from gen.c; the three turns of a face are produced in
 * order, each from the previous one, so every child costs one lookup per
 * coordinate. The heuristic is max(perm_dist, orient_dist), tested as two
 * separate comparisons so the second load is skipped when the first prunes.
 */
#include <stdint.h>
#include "tables.h"

enum { CORNERS = 7, MAX_DEPTH = 11, NO_FACE = 3 };

typedef struct {
    uint16_t p, o;   /* this node */
    uint16_t cp, co; /* the child being generated */
    uint8_t face;    /* face of that child, NO_FACE before the first */
    uint8_t turn;    /* quarter turns applied: 0 = quarter, 1 = half, 2 = ' */
} frame_t;

/* Counters for the operation-count argument; compiled out on the target. */
#ifdef COUNT_OPS
static uint64_t generated, expanded;
#define COUNT(x) (++(x))
#else
#define COUNT(x) ((void) 0)
#endif

/* Parse "PPPPPPPOOOOOOO" into the two ranks; returns 0 on invalid input. */
static int parse(const char *s, uint16_t *p, uint16_t *o)
{
    uint8_t seen = 0, sum = 0;
    uint32_t prank = 0, orank = 0;
    for (int i = 0; i < CORNERS; ++i) {
        uint8_t c = (uint8_t) (s[i] - '1');
        if (c >= CORNERS || seen & 1U << c)
            return 0;
        seen |= (uint8_t) (1U << c);
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
    return 1;
}

/* Writes moves as face * 3 + turn into path; returns the solution length. */
static int solve(uint16_t p, uint16_t o, uint8_t *path)
{
    frame_t stack[MAX_DEPTH + 1];
    if (!(p | o))
        return 0;
    uint8_t bound = perm_dist[p] > orient_dist[o] ? perm_dist[p]
                                                  : orient_dist[o];
    for (;; ++bound) {
        int depth = 0;
        stack[0].p = p;
        stack[0].o = o;
        stack[0].face = NO_FACE;
        COUNT(expanded);
        for (;;) {
            frame_t *f = &stack[depth];
            if (f->face != NO_FACE && f->turn < 2) {
                /* next turn of the same face, from the previous child */
                ++f->turn;
                f->cp = perm_turn[f->face][f->cp];
                f->co = orient_turn[f->face][f->co];
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
            }
            COUNT(generated);
            if (!(f->cp | f->co)) {
                for (int i = 0; i <= depth; ++i)
                    path[i] = (uint8_t) (stack[i].face * 3U + stack[i].turn);
                return depth + 1;
            }
            /* children of f sit at depth + 1; descend only if they fit */
            uint8_t rem = (uint8_t) (bound - depth - 1);
            if (perm_dist[f->cp] > rem || orient_dist[f->co] > rem)
                continue;
            COUNT(expanded);
            frame_t *next = &stack[++depth];
            next->p = f->cp;
            next->o = f->co;
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
    uint16_t p, o;
    uint8_t path[MAX_DEPTH];
    if (argc != 2 || !parse(argv[1], &p, &o)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }
    int length = solve(p, o, path);
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
