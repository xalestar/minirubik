/* Host-side table generator for the RV32I solver.
 *
 * It reuses the proven quarter_turn, rank_state and unrank_state from solver.c
 * so every table comes from the same model the BFS solver uses, and it checks
 * gates H1 (admissibility) and H2 (population) before writing anything.
 * Output: tables.h for the C build, tables.s for the assembly build.
 *
 * Heuristic: h = max(pattern_dist, orient_dist).
 *   orient_dist[o]          exact distance of the orientation alone (729)
 *   pattern_dist[p][oS]     exact distance of the abstraction that keeps the
 *                           whole permutation p and the twists of cubies 0
 *                           and 3, oS = 3 * twist0 + twist3 (5040 x 9)
 * The search tracks oS through a small coordinate c of its own:
 *   c = oS * 64 + 7 * pos0 + pos3   (pos = where cubie 0 / 3 sits; 576 codes)
 * so oS = c >> 6 and the pattern index needs no multiply.
 */
#define main solver_main
#include "solver.c"
#undef main

enum {
    PAIR_CODES = 576,    /* c < 9 * 64 */
    PAIR_STATES = 378,   /* 7 * 6 positions x 9 twists */
    PATTERN_ROW = 16,    /* pattern_dist row: 9 used, padded to 16 */
    PATTERNS = PERMUTATIONS * 9,
    PAIR_SOLVED = 3      /* cubie 0 at 0, cubie 3 at 3, untwisted */
};

static uint16_t perm_turn[3][PERMUTATIONS], orient_turn[3][ORIENTATIONS];
static uint16_t pair_turn[3][PAIR_CODES];
static uint8_t orient_dist[ORIENTATIONS];
static uint8_t pattern_dist[PERMUTATIONS][PATTERN_ROW];
/* lehmer_weight[i][k] = k * (6 - i)!, the factoradic digit weights */
static uint16_t lehmer_weight[CUBIES][CUBIES];

static int pair_valid(uint16_t c)
{
    uint8_t pos0 = (uint8_t) (c % 64 / 7), pos3 = (uint8_t) (c % 64 % 7);
    return c < PAIR_CODES && c % 64 < 49 && pos0 != pos3;
}

/* The pair code of a full state: where cubies 0 and 3 are, and their twists. */
static uint16_t pair_of(const state_t *state)
{
    uint8_t pos0 = 0, pos3 = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] == 0)
            pos0 = i;
        if (state->p[i] == 3)
            pos3 = i;
    }
    return (uint16_t) ((3 * state->o[pos0] + state->o[pos3]) * 64 + 7 * pos0 +
                       pos3);
}

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
    /* Pair codes move like the cubies they describe: build a state with
     * cubies 0 and 3 in place, turn it, and read them back. The other
     * cubies and twists are arbitrary and do not affect the result.
     */
    for (uint16_t c = 0; c < PAIR_CODES; ++c) {
        if (!pair_valid(c))
            continue;
        uint8_t pos0 = (uint8_t) (c % 64 / 7), pos3 = (uint8_t) (c % 64 % 7);
        uint8_t others[] = {1, 2, 4, 5, 6}, k = 0;
        for (uint8_t i = 0; i < CUBIES; ++i) {
            state.p[i] = i == pos0 ? 0 : i == pos3 ? 3 : others[k++];
            state.o[i] = 0;
        }
        state.o[pos0] = (uint8_t) (c / 64 / 3);
        state.o[pos3] = (uint8_t) (c / 64 % 3);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            pair_turn[face][c] = pair_of(&next);
        }
    }
    for (uint16_t i = 0, f = 720; i < CUBIES; f = i < 5 ? f / (6 - i) : 1, ++i)
        for (uint16_t k = 0; k < CUBIES; ++k)
            lehmer_weight[i][k] = (uint16_t) (k * f);
}

/* Exact distance in the orientation-only graph. The orientation evolves
 * independently of the permutation (report.md section 4), so its own
 * transition table is the projected move. Returns the entries reached.
 */
static uint32_t orient_bfs(void)
{
    uint16_t queue[ORIENTATIONS];
    uint32_t head = 0, tail = 1;
    memset(orient_dist, UINT8_MAX, sizeof orient_dist);
    orient_dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t there = here;
            for (uint8_t t = 0; t < 3; ++t) {
                there = orient_turn[face][there];
                if (orient_dist[there] == UINT8_MAX) {
                    orient_dist[there] = (uint8_t) (orient_dist[here] + 1U);
                    queue[tail++] = there;
                }
            }
        }
    }
    return tail;
}

/* Exact distance in the pattern graph, by BFS over (p, c). Given p, the
 * positions inside c are determined, so (p, c) and (p, oS) name the same
 * abstract state; the first visit of either is its distance. Returns the
 * number of (p, oS) entries reached, or 0 if two visits disagree.
 */
static uint32_t pattern_bfs(void)
{
    uint8_t *seen = calloc((size_t) PERMUTATIONS * PAIR_CODES, 1);
    uint32_t *queue = malloc(sizeof *queue * PATTERNS);
    uint8_t *dist = malloc((size_t) PERMUTATIONS * PAIR_CODES);
    uint32_t head = 0, tail = 1, filled = 0;
    if (!seen || !queue || !dist) {
        free(seen);
        free(queue);
        free(dist);
        return 0;
    }
    memset(pattern_dist, UINT8_MAX, sizeof pattern_dist);
    queue[0] = PAIR_SOLVED;
    seen[PAIR_SOLVED] = 1;
    dist[PAIR_SOLVED] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t p = (uint16_t) (here / PAIR_CODES);
        uint16_t c = (uint16_t) (here % PAIR_CODES);
        uint8_t *slot = &pattern_dist[p][c / 64];
        if (*slot == UINT8_MAX) {
            *slot = dist[here];
            ++filled;
        } else if (*slot != dist[here]) {
            fprintf(stderr, "pattern (%u, %u) reached at %u and %u\n", p,
                    c / 64, *slot, dist[here]);
            filled = 0;
            break;
        }
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t np = p, nc = c;
            for (uint8_t t = 0; t < 3; ++t) {
                np = perm_turn[face][np];
                nc = pair_turn[face][nc];
                uint32_t there = (uint32_t) np * PAIR_CODES + nc;
                if (!seen[there]) {
                    seen[there] = 1;
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    if (tail == PATTERNS) {
                        fputs("pattern graph larger than expected\n", stderr);
                        filled = 0;
                        goto done;
                    }
                    queue[tail++] = there;
                }
            }
        }
    }
done:
    free(seen);
    free(queue);
    free(dist);
    return filled;
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

/* H2 for a distance table: fully populated, solved entry zero, expected max. */
static int check_dist(const char *name, uint32_t reached, uint32_t size,
                      uint8_t solved, uint8_t max, uint8_t expected_max)
{
    if (reached != size || solved != 0 || max != expected_max) {
        fprintf(stderr, "H2 failed for %s: %u of %u reached, max %u\n", name,
                reached, size, max);
        return 0;
    }
    printf("H2 %s: %u entries, solved 0, max %u\n", name, size, max);
    return 1;
}

static int check_orient_dist(uint32_t reached)
{
    uint8_t max = 0;
    for (uint32_t i = 0; i < ORIENTATIONS; ++i)
        if (orient_dist[i] > max)
            max = orient_dist[i];
    return check_dist("orient_dist", reached, ORIENTATIONS, orient_dist[0],
                      max, 6);
}

/* Every used cell (oS < 9) filled, the padding never written. */
static int check_pattern_dist(uint32_t reached)
{
    uint8_t max = 0;
    for (uint32_t p = 0; p < PERMUTATIONS; ++p)
        for (uint32_t s = 0; s < PATTERN_ROW; ++s) {
            uint8_t d = pattern_dist[p][s];
            if ((s < 9) != (d != UINT8_MAX)) {
                fprintf(stderr, "H2 failed for pattern_dist at (%u, %u)\n",
                        p, s);
                return 0;
            }
            if (s < 9 && d > max)
                max = d;
        }
    return check_dist("pattern_dist", reached, PATTERNS, pattern_dist[0][0],
                      max, 8);
}

/* H2 for a quarter-turn table: each face is a bijection on its valid
 * entries and four quarter turns are the identity. A turn need not move the
 * solved entry: D twists no corner, so it fixes the solved orientation.
 */
static int check_turns(const char *name, const uint16_t *turn, uint32_t size,
                       int (*valid)(uint16_t), uint32_t expected)
{
    static uint8_t hit[PERMUTATIONS];
    for (uint8_t face = 0; face < 3; ++face) {
        const uint16_t *t = turn + face * size;
        uint32_t count = 0;
        memset(hit, 0, size);
        for (uint16_t i = 0; i < size; ++i) {
            if (valid && !valid(i))
                continue;
            ++count;
            if (t[i] >= size || (valid && !valid(t[i])) || hit[t[i]]++ ||
                t[t[t[t[i]]]] != i) {
                fprintf(stderr, "H2 failed for %s face %u at %u\n", name,
                        face, i);
                return 0;
            }
        }
        if (count != expected) {
            fprintf(stderr, "H2 failed for %s: %u valid entries\n", name,
                    count);
            return 0;
        }
    }
    printf("H2 %s: 3 x %u bijections of order 4\n", name, expected);
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

/* H1: max(pattern_dist, orient_dist) never exceeds the exact distance. */
static int check_admissible(const uint8_t *exact)
{
    uint32_t tight = 0;
    uint64_t sum = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        unrank_state(rank, &state);
        uint8_t hp = pattern_dist[rank / ORIENTATIONS][pair_of(&state) / 64];
        uint8_t ho = orient_dist[rank % ORIENTATIONS];
        uint8_t h = hp > ho ? hp : ho;
        if (exact[rank] == UINT8_MAX || h > exact[rank]) {
            fprintf(stderr, "H1 failed at rank %u: h %u, d %u\n", rank, h,
                    exact[rank]);
            return 0;
        }
        tight += h == exact[rank];
        sum += h;
    }
    printf("H1 h <= d over %u states (%u exact, mean h %.3f)\n", STATES, tight,
           (double) sum / STATES);
    return 1;
}

static void emit_c_u16(FILE *out, const char *name, const uint16_t *table,
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
                      uint32_t rows, uint32_t cols)
{
    fprintf(out, "static const uint8_t %s[%u][%u] = {\n", name, rows, cols);
    for (uint32_t r = 0; r < rows; ++r) {
        fputs("    {", out);
        for (uint32_t c = 0; c < cols; ++c) {
            uint8_t v = table[r * cols + c];
            fprintf(out, "%s%u", c ? "," : "", v == UINT8_MAX ? 0 : v);
        }
        fputs("},\n", out);
    }
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
    emit_c_u16(out, "pair_turn", &pair_turn[0][0], 3, PAIR_CODES);
    emit_c_u8(out, "pattern_dist", &pattern_dist[0][0], PERMUTATIONS,
              PATTERN_ROW);
    fprintf(out, "static const uint8_t orient_dist[%u] = {", ORIENTATIONS);
    for (uint32_t i = 0; i < ORIENTATIONS; ++i)
        fprintf(out, "%s%u", i ? "," : "", orient_dist[i]);
    fputs("};\n", out);
    emit_c_u16(out, "lehmer_weight", &lehmer_weight[0][0], CUBIES, CUBIES);
    return fclose(out) == 0;
}

/* .half rows, 16 values a line, each value times scale; a row shorter than
 * stride is padded with zeros so rows start stride entries apart.
 */
static void emit_s_u16(FILE *out, const char *label, const uint16_t *table,
                       uint32_t rows, uint32_t cols, uint32_t stride,
                       uint16_t scale)
{
    fprintf(out, "%s:\n", label);
    for (uint32_t r = 0; r < rows; ++r)
        for (uint32_t i = 0; i < stride; ++i) {
            uint16_t v = i < cols ? (uint16_t) (table[r * cols + i] * scale)
                                  : 0;
            fprintf(out, "%s%u%s", i % 16 ? ", " : "    .half ", v,
                    i % 16 == 15 || i + 1 == stride ? "\n" : "");
        }
}

static void emit_s_u8(FILE *out, const char *label, const uint8_t *table,
                      uint32_t size, uint32_t spacing)
{
    fprintf(out, "%s:\n", label);
    for (uint32_t i = 0; i < size; ++i) {
        uint8_t v = table[i] == UINT8_MAX ? 0 : table[i];
        fprintf(out, "%s%u%s%s", i % 16 ? ", " : "    .byte ", v,
                spacing == 2 ? ", 0" : "",
                i % 16 == 15 || i + 1 == size ? "\n" : "");
    }
}

/* The assembly indexes by byte offset, so ranks and pair codes are stored
 * doubled and orient_dist gets a 2-byte stride: a looked-up offset is used
 * as is. pair_turn rows are padded to the orient_turn row length, so one
 * face offset register serves both tables.
 */
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
    emit_s_u16(out, "perm_turn", &perm_turn[0][0], 3, PERMUTATIONS,
               PERMUTATIONS, 2);
    emit_s_u16(out, "orient_turn", &orient_turn[0][0], 3, ORIENTATIONS,
               ORIENTATIONS, 2);
    fputs("# [face][2 * c] -> 2 * c, rows padded to the orient_turn row\n",
          out);
    emit_s_u16(out, "pair_turn", &pair_turn[0][0], 3, PAIR_CODES,
               ORIENTATIONS, 2);
    fputs("# [i][k] = 2 * k * (6 - i)!\n", out);
    emit_s_u16(out, "lehmer_weight", &lehmer_weight[0][0], 1,
               CUBIES * CUBIES, CUBIES * CUBIES, 2);
    fputs("# [p * 16 + oS] -> distance; 9 of each 16 bytes used\n", out);
    emit_s_u8(out, "pattern_dist", &pattern_dist[0][0],
              PERMUTATIONS * PATTERN_ROW, 1);
    fputs("# [2 * rank] -> distance\n", out);
    emit_s_u8(out, "orient_dist", orient_dist, ORIENTATIONS, 2);
    return fclose(out) == 0;
}

int main(void)
{
    build_transitions();
    uint32_t orient_reached = orient_bfs();
    uint32_t pattern_reached = pattern_bfs();
    if (!check_turns("perm_turn", &perm_turn[0][0], PERMUTATIONS, NULL,
                     PERMUTATIONS) ||
        !check_turns("orient_turn", &orient_turn[0][0], ORIENTATIONS, NULL,
                     ORIENTATIONS) ||
        !check_turns("pair_turn", &pair_turn[0][0], PAIR_CODES, pair_valid,
                     PAIR_STATES) ||
        !check_lehmer() || !check_orient_dist(orient_reached) ||
        !check_pattern_dist(pattern_reached))
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
