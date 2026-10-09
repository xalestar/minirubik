/* Host-side table generator for the RV32I solver.
 *
 * It reuses quarter_turn, rank_state and unrank_state from solver.c (the
 * first two are covered by its Frama-C proof, unrank_state by its
 * --self-test) so every table comes from the same model the BFS solver uses,
 * and it checks gates H1 (admissibility), H2 (population) and H4 (the packed
 * table) before writing anything.
 * Output: tables.h for the C build, tables.s for the assembly build.
 *
 * Heuristic: one pattern database, looked up for the state and for its two
 * rotations about the fixed corner. Its key keeps
 *   o   the whole orientation, the twists by position (729 values), and
 *   b   a block code (630 values): where cubie 3 sits, where the pair of
 *       cubies {2, 4} sits, how the other four positions split into the
 *       pairs {0, 6} and {1, 5}, and the parity of the permutation.
 * The two cubies of a pair are not told apart, and neither are the pairs
 * {0, 6} and {1, 5}. Eight states share a key: 3,674,160 / 8 = 459,270.
 * The key commutes with the moves (checked below), so its distance in the
 * graph of keys never exceeds the distance of the state.
 * A rotation of the cube about the diagonal through the fixed corner maps
 * states to states at the same distance, so the distance of the rotated key
 * is a lower bound too; it measures the twists against another axis and
 * groups other cubies (sym_pi, sym_tau, sym_face below).
 *
 * The table stores each distance modulo 3, in 2 bits. A move changes the
 * distance of a key by at most 1, so the value of a child modulo 3 and the
 * distance of its parent give the distance of the child. The search does not
 * even rebuild the distance: next_state steps (slack, distance mod 3) of the
 * parent to that of the child, where slack = moves left - distance, and a
 * child whose slack would be negative is pruned.
 */
#define main solver_main
#include "solver.c"
#undef main

enum {
    BLOCKS = 630,     /* block codes: 7 * 15 * 3 * 2 */
    PATTERNS = BLOCKS * ORIENTATIONS,
    ROW_BYTES = 183,  /* 729 values of 2 bits; the last byte holds one */
    ROW_STRIDE = 196, /* tables.s: 3 addresses, the row, 1 pad */
    SLACKS = 12,      /* slack 0..11 */
    SEARCH_STATES = SLACKS * 3
};

static uint16_t perm_turn[3][PERMUTATIONS], orient_turn[3][ORIENTATIONS];
static uint16_t block_turn[3][BLOCKS], block_solved;
static uint8_t pattern_dist[BLOCKS][ORIENTATIONS];
static uint8_t pattern_mod3[BLOCKS][ROW_BYTES];
static int8_t next_state[SEARCH_STATES][4];
/* The 120-degree rotation about the diagonal through the fixed corner, as a
 * map on states: the cubie at position i goes to position sym_pi[i] and is
 * cubie sym_pi[cubie] there. Its twist changes by sym_tau[i] - sym_tau[cubie],
 * because the rotation moves the U/D faces twists are measured against. A
 * quarter turn of face f becomes a quarter turn of face sym_face[f].
 */
static const uint8_t sym_pi[CUBIES] = {2, 5, 6, 1, 4, 3, 0};
static const uint8_t sym_tau[CUBIES] = {0, 1, 0, 1, 0, 1, 0};
static const uint8_t sym_face[3] = {2, 0, 1};
/* The block of each cubie: 0 = {0, 6}, 1 = {1, 5}, 2 = {2, 4}, 3 = {3}. */
static const uint8_t block_of[CUBIES] = {0, 1, 2, 3, 2, 1, 0};
/* pair_base[i] + j - i - 1 numbers the pairs i < j of 6 things, 0..14. */
static const uint8_t pair_base[5] = {0, 5, 9, 12, 14};

static void rotate_state(const state_t *state, state_t *rotated)
{
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t cubie = state->p[i];
        rotated->p[sym_pi[i]] = sym_pi[cubie];
        rotated->o[sym_pi[i]] =
            (uint8_t) ((state->o[i] + sym_tau[i] + 3U - sym_tau[cubie]) % 3U);
    }
}

/* The block code of a state, read off its permutation in one pass:
 *   home    the position of cubie 3,                              7 choices
 *   c1, c2  the positions of cubies 2 and 4 among the other six,  15
 *   mate    which of the last three of the remaining four
 *           positions holds the same pair as the first,           3
 *   parity  of the permutation,                                   2
 */
static uint16_t block_code(const state_t *state)
{
    uint8_t home = 0, c1 = 0, c2 = 0, mate = 0, first = 0, parity = 0;
    uint8_t six = 0, four = 0, twos = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t block = block_of[state->p[i]];
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            parity ^= state->p[j] < state->p[i];
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
    return (uint16_t) (((home * 15U + pair_base[c1] + c2 - c1 - 1U) * 3U +
                        mate) * 2U + parity);
}

/* Returns 0 unless the block code commutes with the quarter turns: every
 * permutation with the same code must turn into the same code.
 */
static int build_transitions(void)
{
    static uint16_t sample[BLOCKS];
    static uint8_t known[BLOCKS];
    uint32_t codes = 0;
    state_t state;
    for (uint32_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state(rank * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            perm_turn[face][rank] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
        uint16_t code = block_code(&state);
        if (code >= BLOCKS)
            return 0;
        if (!known[code]) {
            known[code] = 1;
            sample[code] = (uint16_t) rank;
            ++codes;
        }
    }
    if (codes != BLOCKS)
        return 0;
    for (uint32_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            orient_turn[face][rank] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
    for (uint16_t code = 0; code < BLOCKS; ++code) {
        unrank_state((uint32_t) sample[code] * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            block_turn[face][code] = block_code(&next);
        }
    }
    for (uint32_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state(rank * ORIENTATIONS, &state);
        uint16_t code = block_code(&state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            if (block_code(&next) != block_turn[face][code])
                return 0;
        }
    }
    unrank_state(0, &state);
    block_solved = block_code(&state);
    printf("H2 block code: %u codes over %u permutations, commutes with the "
           "3 quarter turns\n",
           codes, PERMUTATIONS);
    return 1;
}

/* Exact distance in the graph of keys (b, o), by BFS from the solved key.
 * Returns the number of keys reached.
 */
static uint32_t pattern_bfs(void)
{
    uint32_t *queue = malloc(sizeof *queue * PATTERNS);
    uint32_t head = 0, tail = 1;
    if (!queue)
        return 0;
    memset(pattern_dist, UINT8_MAX, sizeof pattern_dist);
    queue[0] = (uint32_t) block_solved * ORIENTATIONS;
    pattern_dist[block_solved][0] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t b = (uint16_t) (here / ORIENTATIONS);
        uint16_t o = (uint16_t) (here % ORIENTATIONS);
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t nb = b, no = o;
            for (uint8_t t = 0; t < 3; ++t) {
                nb = block_turn[face][nb];
                no = orient_turn[face][no];
                if (pattern_dist[nb][no] == UINT8_MAX) {
                    pattern_dist[nb][no] =
                        (uint8_t) (pattern_dist[b][o] + 1U);
                    queue[tail++] = (uint32_t) nb * ORIENTATIONS + no;
                }
            }
        }
    }
    free(queue);
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

/* H2 for the distance table: fully populated, solved entry zero, expected
 * max.
 */
static int check_pattern_dist(uint32_t reached)
{
    uint8_t max = 0;
    for (uint32_t b = 0; b < BLOCKS; ++b)
        for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
            uint8_t d = pattern_dist[b][o];
            if (d == UINT8_MAX) {
                fprintf(stderr, "H2 failed for pattern_dist at (%u, %u)\n", b,
                        o);
                return 0;
            }
            if (d > max)
                max = d;
        }
    if (reached != PATTERNS || pattern_dist[block_solved][0] != 0 ||
        max != 10) {
        fprintf(stderr, "H2 failed for pattern_dist: %u of %u reached, max "
                        "%u\n",
                reached, PATTERNS, max);
        return 0;
    }
    printf("H2 pattern_dist: %u entries, solved 0, max %u\n", PATTERNS, max);
    return 1;
}

/* H2 for a quarter-turn table: each face is a bijection and four quarter
 * turns are the identity. A turn need not move the solved entry: D twists no
 * corner, so it fixes the solved orientation.
 */
static int check_turns(const char *name, const uint16_t *turn, uint32_t size)
{
    static uint8_t hit[PERMUTATIONS];
    for (uint8_t face = 0; face < 3; ++face) {
        const uint16_t *t = turn + face * size;
        memset(hit, 0, size);
        for (uint16_t i = 0; i < size; ++i)
            if (t[i] >= size || hit[t[i]]++ || t[t[t[t[i]]]] != i) {
                fprintf(stderr, "H2 failed for %s face %u at %u\n", name,
                        face, i);
                return 0;
            }
    }
    printf("H2 %s: 3 x %u bijections of order 4\n", name, size);
    return 1;
}

/* H2 for the rotation: it maps every state to a valid state, the solved
 * state to itself, three applications are the identity, and it commutes
 * with the moves: rotating after a quarter turn of face f equals a quarter
 * turn of sym_face[f] after rotating. Together these make it a symmetry of
 * the move graph that fixes solved, so it preserves the distance.
 */
static int check_rotation(void)
{
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state, once, twice, thrice;
        unrank_state(rank, &state);
        rotate_state(&state, &once);
        rotate_state(&once, &twice);
        rotate_state(&twice, &thrice);
        uint32_t image = valid(&once) ? rank_state(&once) : STATES;
        int ok = image < STATES && (rank != 0 || image == 0) &&
                 !memcmp(&state, &thrice, sizeof state);
        for (uint8_t face = 0; ok && face < 3; ++face) {
            state_t turned = quarter_turn(state, face), rotated;
            uint8_t to = sym_face[face];
            rotate_state(&turned, &rotated);
            ok = rank_state(&rotated) ==
                 (uint32_t) perm_turn[to][image / ORIENTATIONS] * ORIENTATIONS +
                     orient_turn[to][image % ORIENTATIONS];
        }
        if (!ok) {
            fprintf(stderr, "H2 failed for the rotation at rank %u\n", rank);
            return 0;
        }
    }
    printf("H2 rotation: order 3, fixes solved, commutes with the 3 quarter "
           "turns on %u states\n",
           STATES);
    return 1;
}

static uint8_t pattern_of(const state_t *state)
{
    state_t copy = *state;
    return pattern_dist[block_code(state)][rank_state(&copy) % ORIENTATIONS];
}

/* H1: the heuristic, the largest pattern_dist over the state and its two
 * rotations, never exceeds the exact distance, and it is 0 for the solved
 * state only: the search takes a child with h = 0 for solved.
 */
static int check_admissible(const uint8_t *exact)
{
    uint32_t tight = 0, zero = 0;
    uint64_t sum = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state, once, twice;
        unrank_state(rank, &state);
        rotate_state(&state, &once);
        rotate_state(&once, &twice);
        uint8_t h = 0;
        const state_t *view[] = {&state, &once, &twice};
        for (uint8_t k = 0; k < 3; ++k)
            if (pattern_of(view[k]) > h)
                h = pattern_of(view[k]);
        if (exact[rank] == UINT8_MAX || h > exact[rank]) {
            fprintf(stderr, "H1 failed at rank %u: h %u, d %u\n", rank, h,
                    exact[rank]);
            return 0;
        }
        tight += h == exact[rank];
        zero += h == 0;
        sum += h;
    }
    if (zero != 1) {
        fprintf(stderr, "H1 failed: h = 0 for %u states\n", zero);
        return 0;
    }
    printf("H1 h <= d over %u states (%u exact, mean h %.3f), h = 0 for the "
           "solved state only\n",
           STATES, tight, (double) sum / STATES);
    return 1;
}

static uint8_t mod3_at(uint16_t b, uint16_t o)
{
    return pattern_mod3[b][o >> 2] >> ((o & 3U) << 1) & 3U;
}

/* next_state[3 * slack + d % 3][v]: a node whose key is at distance d with
 * `slack` moves to spare has a child whose key is at distance v modulo 3.
 * The child is at d - 1, d or d + 1, whichever matches v, and one move is
 * used up, so its slack is slack - 0, 1 or 2; -1 if that is negative.
 */
static void build_next_state(void)
{
    for (int slack = 0; slack < SLACKS; ++slack)
        for (int m = 0; m < 3; ++m)
            for (int v = 0; v < 4; ++v) {
                int used = (v - m + 4) % 3; /* 0 closer, 1 same, 2 farther */
                next_state[3 * slack + m][v] =
                    (int8_t) (v == 3 || used > slack ? -1
                                                     : 3 * (slack - used) + v);
            }
}

/* H4: the 2-bit accessor agrees with the unpacked table modulo 3 at every
 * index, even and odd, and next_state rebuilds from it exactly what the
 * unpacked distances give, for every key, move and slack.
 */
static int pack_and_check(void)
{
    uint32_t agree[2] = {0};
    for (uint32_t b = 0; b < BLOCKS; ++b)
        for (uint32_t o = 0; o < ORIENTATIONS; ++o)
            pattern_mod3[b][o >> 2] |=
                (uint8_t) (pattern_dist[b][o] % 3U << ((o & 3U) << 1));
    for (uint32_t b = 0; b < BLOCKS; ++b)
        for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
            if (mod3_at((uint16_t) b, (uint16_t) o) != pattern_dist[b][o] % 3U) {
                fprintf(stderr, "H4 failed for pattern_mod3 at (%u, %u)\n", b,
                        o);
                return 0;
            }
            ++agree[o & 1U];
        }
    printf("H4 pattern_mod3: %u even and %u odd indices agree with "
           "pattern_dist modulo 3\n",
           agree[0], agree[1]);
    build_next_state();
    uint64_t steps = 0;
    for (uint32_t b = 0; b < BLOCKS; ++b)
        for (uint32_t o = 0; o < ORIENTATIONS; ++o)
            for (uint8_t face = 0; face < 3; ++face) {
                uint16_t nb = (uint16_t) b, no = (uint16_t) o;
                int d = pattern_dist[b][o];
                for (uint8_t t = 0; t < 3; ++t) {
                    nb = block_turn[face][nb];
                    no = orient_turn[face][no];
                    int nd = pattern_dist[nb][no];
                    for (int slack = 0; slack < SLACKS; ++slack) {
                        int left = slack - (nd - d + 1);
                        int want = left < 0 ? -1 : 3 * left + nd % 3;
                        if (nd - d > 1 || d - nd > 1 ||
                            next_state[3 * slack + d % 3][mod3_at(nb, no)] !=
                                want) {
                            fprintf(stderr,
                                    "H4 failed for next_state at (%u, %u)\n",
                                    b, o);
                            return 0;
                        }
                        ++steps;
                    }
                }
            }
    printf("H4 next_state: %llu steps (key, move, slack) agree with "
           "pattern_dist\n",
           (unsigned long long) steps);
    return 1;
}

/* The number of states at each distance: the BFS levels that establish the
 * diameter. Level 11 is non-empty and the levels add up to every state.
 */
static int print_distribution(const uint8_t *exact)
{
    uint32_t level[16] = {0}, total = 0;
    uint64_t sum = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (exact[rank] > 15)
            return 0;
        ++level[exact[rank]];
        sum += exact[rank];
    }
    for (uint8_t d = 0; d < 16 && level[d]; ++d) {
        total += level[d];
        printf("distance %2u: %9u states, %9u cumulative\n", d, level[d], total);
    }
    printf("mean distance %.4f\n", (double) sum / STATES);
    return total == STATES && level[11] && !level[12];
}

/* The 2,644 states at distance 11, one input string per line, for the
 * worst-case sweep on Ripes (ripes-sweep.sh).
 */
static int write_deepest(const uint8_t *exact, const char *path)
{
    FILE *out = fopen(path, "w");
    uint32_t count = 0;
    if (!out)
        return 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        if (exact[rank] != 11)
            continue;
        unrank_state(rank, &state);
        for (uint8_t i = 0; i < CUBIES; ++i)
            fputc('1' + state.p[i], out);
        for (uint8_t i = 0; i < CUBIES; ++i)
            fputc('1' + state.o[i], out);
        fputc('\n', out);
        ++count;
    }
    printf("%u states at distance 11 written to %s\n", count, path);
    return fclose(out) == 0 && count == 2644;
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

static int write_header(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out)
        return 0;
    fputs("/* Generated by gen.c; do not edit. */\n#include <stdint.h>\n",
          out);
    fprintf(out, "enum { BLOCK_SOLVED = %u };\n", block_solved);
    emit_c_u16(out, "orient_turn", &orient_turn[0][0], 3, ORIENTATIONS);
    emit_c_u16(out, "block_turn", &block_turn[0][0], 3, BLOCKS);
    fprintf(out, "static const uint8_t pattern_mod3[%u][%u] = {\n", BLOCKS,
            ROW_BYTES);
    for (uint32_t b = 0; b < BLOCKS; ++b) {
        fputs("    {", out);
        for (uint32_t i = 0; i < ROW_BYTES; ++i)
            fprintf(out, "%s%u", i ? "," : "", pattern_mod3[b][i]);
        fputs("},\n", out);
    }
    fprintf(out, "};\nstatic const int8_t next_state[%u][4] = {\n",
            SEARCH_STATES);
    for (uint32_t s = 0; s < SEARCH_STATES; ++s)
        fprintf(out, "    {%d,%d,%d,%d},\n", next_state[s][0],
                next_state[s][1], next_state[s][2], next_state[s][3]);
    fputs("};\n", out);
    const struct {
        const char *name;
        const uint8_t *table;
        uint32_t size;
    } small[] = {{"sym_pi", sym_pi, CUBIES},     {"sym_tau", sym_tau, CUBIES},
                 {"sym_face", sym_face, 3},      {"block_of", block_of, CUBIES},
                 {"pair_base", pair_base, 5}};
    for (uint32_t k = 0; k < 5; ++k) {
        fprintf(out, "static const uint8_t %s[%u] = {", small[k].name,
                small[k].size);
        for (uint32_t i = 0; i < small[k].size; ++i)
            fprintf(out, "%s%u", i ? "," : "", small[k].table[i]);
        fputs("};\n", out);
    }
    return fclose(out) == 0;
}

/* The values of one .byte or .half table, 16 a line. */
static void emit_s(FILE *out, const char *directive, const uint32_t *value,
                   uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        fprintf(out, "%s%u%s", i % 16 ? ", " : directive, value[i],
                i % 16 == 15 || i + 1 == count ? "\n" : "");
}

/* The assembly indexes by byte offset, so orientation ranks are stored
 * doubled: a looked-up offset is used as is, and it is also the bit offset
 * of the rank in a pattern row. A block code is stored as the address of its
 * row, and each row starts with the addresses of the three rows its code
 * turns into, so advancing a block code is one load. next_state is stored
 * times 4, the offset of a row of itself, with 255 for -1.
 */
static int write_asm(const char *path)
{
    static uint32_t value[3 * ORIENTATIONS + 1];
    FILE *out = fopen(path, "w");
    if (!out)
        return 0;
    fputs("# Generated by gen.c; do not edit.\n"
          "# Ripes has no .rodata, so the read-only tables live in .data.\n"
          ".data\n"
          "# [face][2 * rank] -> 2 * rank after one quarter turn; 1 half of "
          "padding\n"
          "orient_turn:\n",
          out);
    for (uint32_t i = 0; i < 3 * ORIENTATIONS; ++i)
        value[i] = 2U * orient_turn[i / ORIENTATIONS][i % ORIENTATIONS];
    value[3 * ORIENTATIONS] = 0;
    emit_s(out, "    .half ", value, 3 * ORIENTATIONS + 1);
    fprintf(out,
            "# one %u-byte row per block code: the rows of the code after a "
            "quarter turn\n"
            "# of R, B, D, then the distance modulo 3 of each orientation, "
            "2 bits each\n"
            "pattern:\n",
            ROW_STRIDE);
    for (uint32_t b = 0; b < BLOCKS; ++b) {
        fprintf(out, "    .word pattern+%u, pattern+%u, pattern+%u\n",
                ROW_STRIDE * block_turn[0][b], ROW_STRIDE * block_turn[1][b],
                ROW_STRIDE * block_turn[2][b]);
        for (uint32_t i = 0; i < ROW_BYTES; ++i)
            value[i] = pattern_mod3[b][i];
        value[ROW_BYTES] = 0;
        emit_s(out, "    .byte ", value, ROW_BYTES + 1);
    }
    fputs("# [12 * slack + 4 * (distance mod 3) + value] -> the same offset "
          "for the child\n"
          "next_state:\n",
          out);
    for (uint32_t i = 0; i < SEARCH_STATES * 4; ++i)
        value[i] = next_state[i / 4][i % 4] < 0
                       ? 255U
                       : 4U * (uint32_t) next_state[i / 4][i % 4];
    emit_s(out, "    .byte ", value, SEARCH_STATES * 4);
    fputs("# the rotation about the fixed corner: [i] -> position and cubie\n"
          "# map, twist offset; 7 bytes each, padded to 8\n",
          out);
    for (uint32_t i = 0; i < CUBIES + 1; ++i)
        value[i] = i < CUBIES ? sym_pi[i] : 0;
    fputs("sym_pi:\n", out);
    emit_s(out, "    .byte ", value, CUBIES + 1);
    for (uint32_t i = 0; i < CUBIES + 1; ++i)
        value[i] = i < CUBIES ? sym_tau[i] : 0;
    fputs("sym_tau:\n", out);
    emit_s(out, "    .byte ", value, CUBIES + 1);
    fprintf(out, ".equ BLOCK_SOLVED, %u\n", ROW_STRIDE * block_solved);
    return fclose(out) == 0;
}

/* H2 for the assembly encoding: read tables.s back and check every value
 * against the tables above, with the doubling, the row addresses and the
 * padding applied, so a slip in write_asm cannot reach the target unnoticed.
 */
static int check_asm(const char *path)
{
    enum { TABLES = 5 };
    static const char *const label[TABLES] = {"orient_turn", "pattern",
                                              "next_state", "sym_pi",
                                              "sym_tau"};
    static const uint32_t count[TABLES] = {3 * ORIENTATIONS + 1,
                                           BLOCKS * (3 + ROW_BYTES + 1),
                                           SEARCH_STATES * 4, CUBIES + 1,
                                           CUBIES + 1};
    uint32_t seen[TABLES] = {0};
    int table = -1;
    char line[512];
    FILE *in = fopen(path, "r");
    if (!in)
        return 0;
    while (fgets(line, sizeof line, in)) {
        char *t = line;
        for (int k = 0; k < TABLES; ++k)
            if (!strncmp(line, label[k], strlen(label[k])) &&
                line[strlen(label[k])] == ':')
                table = k;
        int word = strstr(line, ".word") != NULL;
        if (word)
            t = strstr(line, ".word") + 5;
        else if (strstr(line, ".half"))
            t = strstr(line, ".half") + 5;
        else if (strstr(line, ".byte"))
            t = strstr(line, ".byte") + 5;
        else
            continue;
        for (char *end; table >= 0; t = end + (*end == ',')) {
            if (word) { /* an address: pattern+offset */
                t += strspn(t, " ");
                if (strncmp(t, "pattern+", 8))
                    break;
                t += 8;
            }
            long v = strtol(t, &end, 10);
            if (end == t)
                break;
            uint32_t i = seen[table]++, expect = 0;
            uint32_t b = i / (3 + ROW_BYTES + 1), at = i % (3 + ROW_BYTES + 1);
            switch (table) {
            case 0:
                expect = i < 3 * ORIENTATIONS
                             ? 2U * orient_turn[i / ORIENTATIONS]
                                               [i % ORIENTATIONS]
                             : 0;
                break;
            case 1:
                if (word != (at < 3))
                    expect = UINT32_MAX;
                else if (at < 3)
                    expect = b < BLOCKS ? ROW_STRIDE * block_turn[at][b] : 0;
                else
                    expect = b < BLOCKS && at < 3 + ROW_BYTES
                                 ? pattern_mod3[b][at - 3]
                                 : 0;
                break;
            case 2:
                expect = i < SEARCH_STATES * 4
                             ? (uint8_t) (next_state[i / 4][i % 4] < 0
                                              ? 255
                                              : 4 * next_state[i / 4][i % 4])
                             : 0;
                break;
            case 3:
                expect = i < CUBIES ? sym_pi[i] : 0;
                break;
            case 4:
                expect = i < CUBIES ? sym_tau[i] : 0;
                break;
            }
            if (i >= count[table] || v != (long) expect) {
                fprintf(stderr, "H2 failed for %s in %s at %u\n",
                        label[table], path, i);
                fclose(in);
                return 0;
            }
        }
    }
    fclose(in);
    for (int k = 0; k < TABLES; ++k)
        if (seen[k] != count[k]) {
            fprintf(stderr, "H2 failed for %s in %s: %u values\n", label[k],
                    path, seen[k]);
            return 0;
        }
    printf("H2 %s: every value matches, doubled, addressed and padded as "
           "encoded\n",
           path);
    return 1;
}

int main(void)
{
    if (!build_transitions()) {
        fputs("H2 failed for the block code\n", stderr);
        return 1;
    }
    uint32_t pattern_reached = pattern_bfs();
    if (!check_turns("orient_turn", &orient_turn[0][0], ORIENTATIONS) ||
        !check_turns("block_turn", &block_turn[0][0], BLOCKS) ||
        !check_pattern_dist(pattern_reached) || !check_rotation() ||
        !pack_and_check())
        return 1;
    uint8_t *exact = exact_distances();
    if (!exact) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    int ok = print_distribution(exact) && check_admissible(exact) &&
             write_deepest(exact, "distance11.txt");
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
    if (!check_asm("tables.s"))
        return 1;
    return 0;
}
