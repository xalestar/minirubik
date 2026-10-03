/* Host gates for ida.c, checked against the exact BFS table of solver.c.
 *
 * For every one of the 3,674,160 states: ida.c's parser agrees with
 * rank_state and with the pair code read off the cube arrays, the search
 * returns a path whose length equals the exact distance (H3), and that path,
 * applied by solver.c's own apply_move rather than through the generated
 * tables, reaches solved. Also reports operation
 * counts over the distance-11 states for the stage 3 argument.
 */
#define main solver_main
#include "solver.c"
#undef main
#define NO_MAIN
#define COUNT_OPS
#include "ida.c"
#include <time.h>

/* The pair code computed from the cube arrays, independently of parse. */
static uint16_t pair_code(const state_t *state)
{
    int pos0 = 0, pos3 = 0;
    for (int i = 0; i < CUBIES; ++i) {
        if (state->p[i] == 0)
            pos0 = i;
        if (state->p[i] == 3)
            pos3 = i;
    }
    return (uint16_t) ((3 * state->o[pos0] + state->o[pos3]) * 64 + 7 * pos0 +
                       pos3);
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

int main(void)
{
    uint8_t diameter;
    uint8_t *table = build_table(&diameter);
    if (!table) {
        fputs("could not build complete state table\n", stderr);
        return 1;
    }
    clock_t start = clock();
    uint64_t worst = 0, total = 0, first = 0, second = 0;
    uint32_t deepest = 0, skipping = 0, skipping11 = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        char text[2 * CUBIES + 1];
        uint16_t p, o, c;
        uint8_t path[MAX_DEPTH];
        unrank_state(rank, &state);
        for (int i = 0; i < CUBIES; ++i) {
            text[i] = (char) ('1' + state.p[i]);
            text[CUBIES + i] = (char) ('1' + state.o[i]);
        }
        text[2 * CUBIES] = '\0';
        if (!parse(text, &p, &o, &c) ||
            (uint32_t) p * ORIENTATIONS + o != rank || c != pair_code(&state)) {
            fprintf(stderr, "parse disagrees with the cube at %s\n", text);
            return 1;
        }
        uint8_t d = exact_distance(table, state);
        generated = pruned_first = pruned_second = skipped_bounds = 0;
        int length = solve(p, o, c, path);
        skipping += skipped_bounds != 0;
        skipping11 += skipped_bounds != 0 && d == 11;
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
            first += pruned_first;
            second += pruned_second;
            if (generated > worst)
                worst = generated;
        }
    }
    free(table);
    printf("H3 optimal for all %u states (%.1f s)\n", STATES,
           (double) (clock() - start) / CLOCKS_PER_SEC);
    printf("distance 11: %u states, generated children mean %.0f, worst %llu\n",
           deepest, (double) total / deepest, (unsigned long long) worst);
    printf("distance 11, mean per state: first test prunes %.0f (%.1f%%), "
           "second lookup %.0f times, second test prunes %.0f\n",
           (double) first / deepest, 100.0 * first / total,
           (double) (total - first) / deepest, (double) second / deepest);
    printf("a jump to the smallest pruned f would skip a bound in %u states "
           "(%u at distance 11)\n",
           skipping, skipping11);
    return 0;
}
