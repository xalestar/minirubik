/* Host-side table generator for the RV32I solver.
 *
 * It reuses the proven quarter_turn, rank_state and unrank_state from solver.c
 * so the transition tables come from the same model the BFS solver uses, and
 * it checks gates H1 (admissibility) and H2 (population) before writing
 * anything. Output: tables.h for the C build, tables.s for the assembly build.
 */
#define main solver_main
#include "solver.c"
#undef main

static uint16_t perm_turn[3][PERMUTATIONS], orient_turn[3][ORIENTATIONS];
static uint8_t perm_dist[PERMUTATIONS], orient_dist[ORIENTATIONS];
/* lehmer_weight[i][k] = k * (6 - i)!, the factoradic digit weights */
static uint16_t lehmer_weight[CUBIES][CUBIES];

static void build_transitions(void)
{
    state_t state;
    for (uint32_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state(rank * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            perm_turn[face][rank] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }
    for (uint32_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            orient_turn[face][rank] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
    for (uint16_t i = 0, f = 720; i < CUBIES; f = i < 5 ? f / (6 - i) : 1, ++i)
        for (uint16_t k = 0; k < CUBIES; ++k)
            lehmer_weight[i][k] = (uint16_t) (k * f);
}

/* Exact distance in the projected graph. Each table is a pattern database
 * over one factor; the factors evolve independently (report.md section 4), so
 * the factor's own transition table is the projected move. Returns the number
 * of entries reached.
 */
static uint32_t factor_bfs(const uint16_t *turn, uint32_t size, uint8_t *dist)
{
    uint16_t queue[PERMUTATIONS];
    uint32_t head = 0, tail = 1;
    memset(dist, UINT8_MAX, size);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t there = here;
            for (uint8_t t = 0; t < 3; ++t) {
                there = turn[face * size + there];
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    queue[tail++] = there;
                }
            }
        }
    }
    return tail;
}

/* Exact distance of every full state, for gate H1. */
static uint8_t *exact_distances(void)
{
    uint8_t *dist = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 1;
    if (!dist || !queue) {
        free(dist);
        free(queue);
        return NULL;
    }
    memset(dist, UINT8_MAX, STATES);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t p = (uint16_t) (here / ORIENTATIONS);
        uint16_t o = (uint16_t) (here % ORIENTATIONS);
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t np = p, no = o;
            for (uint8_t t = 0; t < 3; ++t) {
                np = perm_turn[face][np];
                no = orient_turn[face][no];
                uint32_t there = (uint32_t) np * ORIENTATIONS + no;
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    queue[tail++] = there;
                }
            }
        }
    }
    free(queue);
    return dist;
}

static uint8_t max_of(const uint8_t *table, uint32_t size)
{
    uint8_t max = 0;
    for (uint32_t i = 0; i < size; ++i)
        if (table[i] > max)
            max = table[i];
    return max;
}

/* H2: fully populated, solved entry zero, and the maximum we expect. */
static int check_populated(const char *name, const uint8_t *table,
                           uint32_t size, uint32_t reached, uint8_t max)
{
    if (reached != size || table[0] != 0 || max_of(table, size) != max) {
        fprintf(stderr, "H2 failed for %s: %u of %u reached, max %u\n", name,
                reached, size, max_of(table, size));
        return 0;
    }
    printf("H2 %s: %u entries, solved 0, max %u\n", name, size, max);
    return 1;
}

/* H2 for a quarter-turn table: each face is a bijection on [0, size) and
 * four quarter turns are the identity. A turn need not move the solved
 * entry: D twists no corner, so it fixes the solved orientation.
 */
static int check_turns(const char *name, const uint16_t *turn, uint32_t size)
{
    static uint8_t hit[PERMUTATIONS];
    for (uint8_t face = 0; face < 3; ++face) {
        const uint16_t *t = turn + face * size;
        memset(hit, 0, size);
        for (uint32_t i = 0; i < size; ++i) {
            if (t[i] >= size || hit[t[i]]++ ||
                t[t[t[t[i]]]] != i) {
                fprintf(stderr, "H2 failed for %s face %u at %u\n", name,
                        face, i);
                return 0;
            }
        }
    }
    printf("H2 %s: 3 x %u bijections of order 4\n", name, size);
    return 1;
}

/* H2 for the Lehmer weights: the largest digits sum to 7! - 1 and the
 * solved permutation, all digits zero, ranks to 0.
 */
static int check_lehmer(void)
{
    uint32_t max = 0, solved = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        max += lehmer_weight[i][CUBIES - 1 - i];
        solved += lehmer_weight[i][0];
    }
    if (max != PERMUTATIONS - 1 || solved) {
        fprintf(stderr, "H2 failed for lehmer_weight: max %u\n", max);
        return 0;
    }
    printf("H2 lehmer_weight: solved 0, max %u\n", max);
    return 1;
}

/* H1: max(perm_dist, orient_dist) never exceeds the exact distance. */
static int check_admissible(const uint8_t *exact)
{
    uint32_t tight = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint8_t hp = perm_dist[rank / ORIENTATIONS];
        uint8_t ho = orient_dist[rank % ORIENTATIONS];
        uint8_t h = hp > ho ? hp : ho;
        if (exact[rank] == UINT8_MAX || h > exact[rank]) {
            fprintf(stderr, "H1 failed at rank %u: h %u, d %u\n", rank, h,
                    exact[rank]);
            return 0;
        }
        tight += h == exact[rank];
    }
    printf("H1 h <= d over %u states (%u exact)\n", STATES, tight);
    return 1;
}

static void emit_c_u16(FILE *out, const char *name, uint16_t *table,
                       uint32_t rows, uint32_t cols)
{
    fprintf(out, "static const uint16_t %s[%u][%u] = {\n", name, rows, cols);
    for (uint32_t r = 0; r < rows; ++r) {
        fputs("    {", out);
        for (uint32_t c = 0; c < cols; ++c)
            fprintf(out, "%s%u", c ? "," : "", table[r * cols + c]);
        fputs("},\n", out);
    }
    fputs("};\n", out);
}

static void emit_c_u8(FILE *out, const char *name, const uint8_t *table,
                      uint32_t size)
{
    fprintf(out, "static const uint8_t %s[%u] = {", name, size);
    for (uint32_t i = 0; i < size; ++i)
        fprintf(out, "%s%u", i ? "," : "", table[i]);
    fputs("};\n", out);
}

static int write_header(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out)
        return 0;
    fputs("/* Generated by gen.c; do not edit. */\n#include <stdint.h>\n",
          out);
    emit_c_u16(out, "perm_turn", &perm_turn[0][0], 3, PERMUTATIONS);
    emit_c_u16(out, "orient_turn", &orient_turn[0][0], 3, ORIENTATIONS);
    emit_c_u8(out, "perm_dist", perm_dist, PERMUTATIONS);
    emit_c_u8(out, "orient_dist", orient_dist, ORIENTATIONS);
    emit_c_u16(out, "lehmer_weight", &lehmer_weight[0][0], CUBIES, CUBIES);
    return fclose(out) == 0;
}

/* The assembly indexes by byte offset, so every rank is stored doubled and
 * the distance tables get a 2-byte stride: a looked-up offset is used as is,
 * with no shift per lookup.
 */
static void emit_s_u16(FILE *out, const char *label, const uint16_t *table,
                       uint32_t size, uint16_t scale)
{
    fprintf(out, "%s:\n", label);
    for (uint32_t i = 0; i < size; ++i)
        fprintf(out, "%s%u%s", i % 16 ? ", " : "    .half ",
                table[i] * scale, i % 16 == 15 || i + 1 == size ? "\n" : "");
}

static void emit_s_dist(FILE *out, const char *label, const uint8_t *table,
                        uint32_t size)
{
    fprintf(out, "%s:\n", label);
    for (uint32_t i = 0; i < size; ++i)
        fprintf(out, "%s%u, 0%s", i % 16 ? ", " : "    .byte ", table[i],
                i % 16 == 15 || i + 1 == size ? "\n" : "");
}

static int write_asm(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out)
        return 0;
    fputs("# Generated by gen.c; do not edit.\n"
          "# Ripes has no .rodata, so the read-only tables live in .data.\n"
          ".data\n.align 2\n"
          "# [face][2 * rank] -> 2 * rank after one quarter turn\n",
          out);
    emit_s_u16(out, "perm_turn", &perm_turn[0][0], 3 * PERMUTATIONS, 2);
    emit_s_u16(out, "orient_turn", &orient_turn[0][0], 3 * ORIENTATIONS, 2);
    fputs("# [i][k] = 2 * k * (6 - i)!\n", out);
    emit_s_u16(out, "lehmer_weight", &lehmer_weight[0][0], CUBIES * CUBIES,
               2);
    fputs("# [2 * rank] -> distance of the projected state\n", out);
    emit_s_dist(out, "perm_dist", perm_dist, PERMUTATIONS);
    emit_s_dist(out, "orient_dist", orient_dist, ORIENTATIONS);
    return fclose(out) == 0;
}

int main(void)
{
    build_transitions();
    uint32_t perm_reached =
        factor_bfs(&perm_turn[0][0], PERMUTATIONS, perm_dist);
    uint32_t orient_reached =
        factor_bfs(&orient_turn[0][0], ORIENTATIONS, orient_dist);
    if (!check_turns("perm_turn", &perm_turn[0][0], PERMUTATIONS) ||
        !check_turns("orient_turn", &orient_turn[0][0], ORIENTATIONS) ||
        !check_lehmer() ||
        !check_populated("perm_dist", perm_dist, PERMUTATIONS, perm_reached,
                         7) ||
        !check_populated("orient_dist", orient_dist, ORIENTATIONS,
                         orient_reached, 6))
        return 1;
    uint8_t *exact = exact_distances();
    if (!exact) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    int ok = check_admissible(exact);
    free(exact);
    if (!ok)
        return 1;
    if (!write_header("tables.h")) {
        perror("tables.h");
        return 1;
    }
    if (!write_asm("tables.s")) {
        perror("tables.s");
        return 1;
    }
    return 0;
}
