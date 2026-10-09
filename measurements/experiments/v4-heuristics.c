/* Host experiment behind v4: which pattern databases fit the budget, and how
 * many nodes IDA* needs with each over the 2,644 distance-11 states.
 *
 * A pattern database here is the exact distance of a key that keeps
 *   P          the whole permutation,                          or
 *   S<cubies>  the positions of these cubies,                  or
 *   B<blocks>  which block of cubies sits where (01.23.45: three pairs; the
 *              cubies not named form one more block), with
 *   W          the first two blocks not told apart, and
 *   Q          the parity of the permutation,
 * and
 *   O          the whole orientation (twists by position),     or
 *   T<cubies>  the twists of these cubies, whose positions must be kept.
 * Each of these keys commutes with the moves, so its distance is the
 * smallest exact distance over the states that share the key; that is how
 * the tables are filled here, from the exact BFS distances. The heuristic of
 * a state is the largest table value over a set of images of the state:
 * x2 itself and one rotation about the fixed corner, x3 both rotations, x6
 * the 6 symmetries that fix the corner, x48 all. Tables joined by '+' are
 * used together. The search of symmetry.c takes h = 0 for solved, so a set
 * of views that cannot tell (x1) is reported as NOT OPTIMAL.
 * Reuses the cube model of symmetry.c. Run from the repository root:
 *   cc -O2 -w measurements/experiments/v4-heuristics.c -o v4-heuristics
 *   ./v4-heuristics B06.15.24WQOx3 PT03x3 ...
 * v4-heuristics.txt holds the runs that TASK.md and the README cite; its
 * first line is the command.
 */
#include <stdint.h>
static uint16_t perm_turn[3][5040], orient_turn[3][729];
#define NO_MAIN
#include "symmetry.c"

/* The quarter-turn tables of the two ranks, from solver.c's model. */
static void turn_tables(void)
{
    state_t s;
    for (uint32_t r = 0; r < PERMUTATIONS; ++r) {
        unrank_state(r * ORIENTATIONS, &s);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            perm_turn[f][r] = (uint16_t) (rank_state(&n) / ORIENTATIONS);
        }
    }
    for (uint32_t r = 0; r < ORIENTATIONS; ++r) {
        unrank_state(r, &s);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            orient_turn[f][r] = (uint16_t) (rank_state(&n) % ORIENTATIONS);
        }
    }
}

enum { FULLP = 1, FULLO = 2, PARITY = 4, SWAP = 8 };
typedef struct {
    const char *name;
    int flags;
    unsigned pos, tw; /* cubie sets, one bit per cubie */
    uint8_t block[7]; /* B: cubies with the same number are not told apart */
    int blocks;
} abs_t;

static uint32_t key(const abs_t *a, const state_t *s)
{
    uint8_t at[7];
    state_t t = *s;
    uint32_t rank = rank_state(&t), k = 0;
    for (int i = 0; i < 7; ++i)
        at[s->p[i]] = (uint8_t) i;
    if (a->flags & FULLP)
        k = rank / 729;
    if (a->blocks) {
        int first = 0; /* W: the first two named blocks are not told apart */
        for (int i = 0; i < 7 && !first; ++i)
            if (a->block[s->p[i]] == 1 || a->block[s->p[i]] == 2)
                first = a->block[s->p[i]];
        for (int i = 0; i < 7; ++i) {
            int b = a->block[s->p[i]];
            if (a->flags & SWAP && first == 2 && (b == 1 || b == 2))
                b = 3 - b;
            k = k * a->blocks + b;
        }
    }
    if (a->flags & PARITY) {
        int odd = 0;
        for (int i = 0; i < 7; ++i)
            for (int j = i + 1; j < 7; ++j)
                odd ^= s->p[i] > s->p[j];
        k = k * 2 + odd;
    }
    for (int c = 0; c < 7; ++c)
        if (a->pos >> c & 1)
            k = k * 7 + at[c];
    for (int c = 0; c < 7; ++c)
        if (a->tw >> c & 1)
            k = k * 3 + s->o[at[c]];
    if (a->flags & FULLO)
        k = k * 729 + rank % 729;
    return k;
}

static uint32_t key_space(const abs_t *a)
{
    uint32_t n = a->flags & FULLP ? 5040 : 1;
    for (int i = 0; i < 7 && a->blocks; ++i)
        n *= a->blocks;
    if (a->flags & PARITY)
        n *= 2;
    for (int c = 0; c < 7; ++c) {
        if (a->pos >> c & 1)
            n *= 7;
        if (a->tw >> c & 1)
            n *= 3;
    }
    return a->flags & FULLO ? n * 729 : n;
}

static uint32_t *IMG[48]; /* IMG[m][rank] = rank of the image under m */
static int FIX[48];

static void images(int all)
{
    for (int m = 0; m < 48; ++m) {
        FIX[m] = G[m][0] / 3 == 0;
        if (all || FIX[m])
            IMG[m] = malloc(sizeof(uint32_t) * STATES);
    }
    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s, t;
        uint8_t Z[24], Y[24], W[24];
        unrank_state(r, &s);
        encode(&s, Z);
        for (int m = 0; m < 48; ++m) {
            if (!IMG[m])
                continue;
            conj(Z, m, Y);
            normalize(Y, W);
            decode(W, &t);
            IMG[m][r] = rank_state(&t);
        }
    }
}

/* table of a over its keys; returns the number of keys in use and the max */
static uint8_t *table(const abs_t *a, uint32_t *used, int *max)
{
    uint32_t n = key_space(a);
    uint8_t *t = malloc(n);
    memset(t, 255, n);
    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s;
        unrank_state(r, &s);
        uint32_t k = key(a, &s);
        if (D[r] < t[k])
            t[k] = D[r];
    }
    *used = 0, *max = 0;
    for (uint32_t k = 0; k < n; ++k)
        if (t[k] != 255) {
            ++*used;
            if (t[k] > *max)
                *max = t[k];
        }
    return t;
}

/* views: 1 the state only, 2 and one rotation, 3 both rotations, 6 all that
 * fix the corner, 48 all
 */
static void add(uint8_t *h, const abs_t *a, int views, uint32_t *used,
                int *max)
{
    uint8_t *t = table(a, used, max);
    uint32_t *k = malloc(sizeof *k * STATES);
    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s;
        unrank_state(r, &s);
        k[r] = key(a, &s);
    }
    int rotations = 0; /* views 2: the state and one rotation of it */
    for (int m = 0; m < 48; ++m) {
        int rot = m && FIX[m] && DET[m] > 0;
        int use = views == 48 || (views == 6 && FIX[m]) ||
                  (views == 3 && rot) || (views == 2 && rot && !rotations) ||
                  m == 0;
        rotations += rot;
        if (!use || !IMG[m])
            continue;
        for (uint32_t r = 0; r < STATES; ++r) {
            uint8_t v = t[k[IMG[m][r]]];
            if (v > h[r])
                h[r] = v;
        }
    }
    free(k);
    free(t);
}

/* One run per argument, tables joined by '+': P, S<cubies> or B<blocks>,
 * then W if the first two blocks may swap, then Q for the parity of the
 * permutation, then O or T<cubies>, then
 * x<views>, as in PT036x3+S036Ox3. B01.23.45 keeps where each pair of
 * cubies sits without telling the two apart; the cubies it does not name
 * form one more block.
 */
static void try_spec(const char *spec)
{
    uint8_t *h = calloc(STATES, 1);
    char name[160];
    size_t at = (size_t) snprintf(name, sizeof name, "%s [", spec);
    for (const char *c = spec; *c;) {
        abs_t a = {spec, 0, 0, 0, {0}, 0};
        uint32_t used;
        int max, views;
        if (*c == 'P')
            a.flags |= FULLP, ++c;
        else if (*c == 'B') {
            a.blocks = 2;
            for (++c; (*c >= '0' && *c <= '6') || *c == '.'; ++c)
                if (*c == '.')
                    ++a.blocks;
                else
                    a.block[*c - '0'] = (uint8_t) (a.blocks - 1);
        } else
            for (++c; *c >= '0' && *c <= '6'; ++c)
                a.pos |= 1u << (*c - '0');
        if (*c == 'W')
            a.flags |= SWAP, ++c;
        if (*c == 'Q')
            a.flags |= PARITY, ++c;
        if (*c == 'O')
            a.flags |= FULLO, ++c;
        else
            for (++c; *c >= '0' && *c <= '6'; ++c)
                a.tw |= 1u << (*c - '0');
        if (a.flags & FULLP)
            a.pos = 0;
        views = atoi(++c);
        while (*c && *c != '+')
            ++c;
        if (*c)
            ++c;
        add(h, &a, views, &used, &max);
        at += (size_t) snprintf(name + at, sizeof name - at, "%u max %d%s",
                                used, max, *c ? ", " : "]");
    }
    run(name, h);
    free(h);
}

int main(int argc, char **argv)
{
    int all = 0;
    for (int i = 1; i < argc; ++i)
        all |= strstr(argv[i], "x48") != NULL;
    turn_tables();
    build_syms();
    if (!fit())
        return puts("geometric model does not fit solver.c"), 1;
    exact();
    images(all);
    for (int i = 1; i < argc; ++i)
        try_spec(argv[i]);
    return 0;
}
