/* Host gates for ida.c, checked against the exact BFS table of solver.c.
 *
 * For every one of the 3,674,160 states: ida.c's parser agrees with
 * rank_state, the search returns a path whose length equals the exact
 * distance (H3), and that path, applied by solver.c's own apply_move rather
 * than through the generated tables, reaches solved. Also reports operation
 * counts over the distance-11 states for the stage 3 argument.
 */
#define main solver_main
#include "solver.c"
#undef main
#define NO_MAIN
#define COUNT_OPS
#include "ida.c"
#include <time.h>

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
    uint64_t worst = 0, total = 0;
    uint32_t deepest = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        char text[2 * CUBIES + 1];
        uint16_t p, o;
        uint8_t path[MAX_DEPTH];
        unrank_state(rank, &state);
        for (int i = 0; i < CUBIES; ++i) {
            text[i] = (char) ('1' + state.p[i]);
            text[CUBIES + i] = (char) ('1' + state.o[i]);
        }
        text[2 * CUBIES] = '\0';
        if (!parse(text, &p, &o) || (uint32_t) p * ORIENTATIONS + o != rank) {
            fprintf(stderr, "parse disagrees with rank_state at %s\n", text);
            return 1;
        }
        uint8_t d = exact_distance(table, state);
        generated = 0;
        int length = solve(p, o, path);
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
            if (generated > worst)
                worst = generated;
        }
    }
    free(table);
    printf("H3 optimal for all %u states (%.1f s)\n", STATES,
           (double) (clock() - start) / CLOCKS_PER_SEC);
    printf("distance 11: %u states, generated children mean %.0f, worst %llu\n",
           deepest, (double) total / deepest, (unsigned long long) worst);
    return 0;
}
