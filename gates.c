/* Host gates for ida.c, checked against the exact BFS table of solver.c.
 *
 * For every one of the 3,674,160 states: ida.c's parser agrees with the
 * parity, the block code and the orientation rank read off the cube arrays,
 * the search returns a path whose length equals the exact distance (H3), and
 * that path, applied by solver.c's own apply_move rather than through the
 * generated tables, reaches solved. Before that, for every key of the
 * pattern database: the distance ida.c finds by walking home equals the BFS
 * distance of the key. Also reports operation counts over the distance-11
 * states for the stage 3 argument.
 */
#define main solver_main
#include "solver.c"
#undef main
#define NO_MAIN
#define COUNT_OPS
#include "ida.c"
#include <time.h>

enum { BLOCKS = 630 };

/* The block code computed from the cube arrays, independently of coords:
 * through the position of each cubie and the cycles of the permutation.
 */
static uint16_t block_code(const state_t *state)
{
    int at[CUBIES], index[CUBIES], n = 0, cycles = 0;
    uint8_t seen = 0;
    for (int i = 0; i < CUBIES; ++i)
        at[state->p[i]] = i;
    for (int i = 0; i < CUBIES; ++i)
        if (!(seen >> i & 1)) {
            ++cycles;
            for (int j = i; !(seen >> j & 1); j = state->p[j])
                seen |= (uint8_t) (1U << j);
        }
    for (int i = 0; i < CUBIES; ++i) /* the six positions without cubie 3 */
        index[i] = i == at[3] ? -1 : n++;
    int c1 = index[at[2]], c2 = index[at[4]], pair = 0;
    if (c1 > c2) {
        int t = c1;
        c1 = c2;
        c2 = t;
    }
    for (int i = 0; i < c1; ++i)
        pair += 5 - i;
    pair += c2 - c1 - 1;
    n = 0;
    for (int i = 0; i < CUBIES; ++i) /* the four that hold 0, 6, 1 and 5 */
        index[i] = i == at[3] || i == at[2] || i == at[4] ? -1 : n++;
    int lead = 0; /* the cubie in the first of the four, then its partner */
    for (int i = 0; i < CUBIES; ++i)
        if (index[i] == 0)
            lead = state->p[i];
    int mate = index[at[lead == 0 ? 6 : lead == 6 ? 0 : lead == 1 ? 5 : 1]] - 1;
    /* a permutation of 7 with c cycles is even when 7 - c is */
    return (uint16_t) (((at[3] * 15 + pair) * 3 + mate) * 2 +
                       ((CUBIES - cycles) & 1));
}

/* Distance by walking the baseline's move-toward-solved table home. */
static uint8_t exact_distance(const uint8_t *table, state_t state)
{
    uint8_t d = 0;
    for (uint32_t rank = rank_state(&state); rank; rank = rank_state(&state)) {
        state = apply_move(state, table[rank]);
        ++d;
    }
    return d;
}

/* root_dist for every key against a BFS over the keys. */
static int check_root_dist(void)
{
    static uint8_t dist[BLOCKS][ORIENTATIONS];
    static uint32_t queue[BLOCKS * ORIENTATIONS];
    uint32_t head = 0, tail = 1;
    memset(dist, UINT8_MAX, sizeof dist);
    dist[BLOCK_SOLVED][0] = 0;
    queue[0] = BLOCK_SOLVED * ORIENTATIONS;
    while (head < tail) {
        uint16_t b = (uint16_t) (queue[head] / ORIENTATIONS);
        uint16_t o = (uint16_t) (queue[head++] % ORIENTATIONS);
        for (int face = 0; face < 3; ++face) {
            uint16_t nb = b, no = o;
            for (int t = 0; t < 3; ++t) {
                nb = block_turn[face][nb];
                no = orient_turn[face][no];
                if (dist[nb][no] == UINT8_MAX) {
                    dist[nb][no] = (uint8_t) (dist[b][o] + 1U);
                    queue[tail++] = (uint32_t) nb * ORIENTATIONS + no;
                }
            }
        }
    }
    for (uint16_t b = 0; b < BLOCKS; ++b)
        for (uint16_t o = 0; o < ORIENTATIONS; ++o)
            if (root_dist(b, o) != dist[b][o]) {
                fprintf(stderr, "root_dist failed at (%u, %u)\n", b, o);
                return 0;
            }
    printf("root_dist equals the BFS distance for all %u keys\n", tail);
    return tail == BLOCKS * ORIENTATIONS;
}

int main(void)
{
    uint8_t diameter;
    uint8_t *table = build_table(&diameter);
    if (!table) {
        fputs("could not build complete state table\n", stderr);
        return 1;
    }
    if (!check_root_dist())
        return 1;
    clock_t start = clock();
    uint64_t worst = 0, total = 0, cut[VIEWS] = {0}, passed = 0, tries = 0;
    uint64_t worst_passed = 0;
    uint32_t deepest = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        char text[2 * CUBIES + 1];
        uint16_t vb[VIEWS], vo[VIEWS];
        uint8_t parity, path[MAX_DEPTH];
        unrank_state(rank, &state);
        for (int i = 0; i < CUBIES; ++i) {
            text[i] = (char) ('1' + state.p[i]);
            text[CUBIES + i] = (char) ('1' + state.o[i]);
        }
        text[2 * CUBIES] = '\0';
        if (!parse(text, &parity)) {
            fprintf(stderr, "parse rejects %s\n", text);
            return 1;
        }
        views(text, parity, vb, vo);
        if (vb[0] != block_code(&state) || vo[0] != rank % ORIENTATIONS) {
            fprintf(stderr, "coords disagrees with the cube at %s\n", text);
            return 1;
        }
        uint8_t d = exact_distance(table, state);
        generated = expanded = root_tries = 0;
        pruned[0] = pruned[1] = pruned[2] = 0;
        int length = solve(vb, vo, path);
        if (length != d) {
            fprintf(stderr, "H3 failed at %s: length %d, distance %u\n", text,
                    length, d);
            return 1;
        }
        for (int i = 0; i < length; ++i)
            state = apply_move(state, path[i]);
        if (rank_state(&state)) {
            fprintf(stderr, "path does not solve %s\n", text);
            return 1;
        }
        if (d == 11) {
            ++deepest;
            total += generated;
            for (int k = 0; k < VIEWS; ++k)
                cut[k] += pruned[k];
            passed += expanded;
            tries += root_tries;
            if (generated > worst)
                worst = generated;
            if (expanded > worst_passed)
                worst_passed = expanded;
        }
    }
    free(table);
    printf("H3 optimal for all %u states (%.1f s)\n", STATES,
           (double) (clock() - start) / CLOCKS_PER_SEC);
    printf("distance 11: %u states, generated children mean %.0f, worst %llu\n",
           deepest, (double) total / deepest, (unsigned long long) worst);
    printf("distance 11, mean per state: the state's own view prunes %.0f "
           "(%.1f%%), rotated once %.0f, rotated twice %.0f\n",
           (double) cut[0] / deepest, 100.0 * cut[0] / total,
           (double) cut[1] / deepest, (double) cut[2] / deepest);
    printf("distance 11: nodes expanded mean %.0f, worst %llu; %.0f keys "
           "tried on the way home from the three root keys\n",
           (double) passed / deepest, (unsigned long long) worst_passed,
           (double) tries / deepest);
    return 0;
}
