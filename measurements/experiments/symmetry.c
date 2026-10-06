/* Host experiment behind the rotated lookups: the symmetries of the cube
 * acting on the fixed-corner state space.
 *
 * It builds a 24-facelet model of the cube, fits it to solver.c (twist sign
 * and the three layer rotations, then checked on every state and face), and
 * applies each of the 48 symmetries of the cube to every state: conjugate,
 * then turn the whole cube until cubie 0 is home again. It reports
 *   - whether each symmetry, and inversion, preserves the distance;
 *   - the number of classes of states under the 3 rotations about the fixed
 *     corner, the 6 symmetries fixing it, all 48, and all 48 with inversion;
 *   - which of these maps act on the index of pattern_dist;
 *   - IDA* node counts over the 2,644 distance-11 states when the heuristic
 *     is the maximum of the v2 heuristic over each of those sets of images,
 *     next to the three-cubie pattern database without symmetry.
 * Needs tables.h (make tables.h). Run from the repository root:
 *   cc -O2 -w measurements/experiments/symmetry.c -o symmetry && ./symmetry
 */
#define main solver_main
#include "../../solver.c"
#undef main
#include "../../tables.h"

/* ---- geometry: 8 corner positions, 24 facelet slots = 3 * pos + axis ---- */
static const int V[8][3] = { /* x: L->R, y: D->U, z: B->F; report numbering */
    {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}, {-1, -1, 1},
    {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}};
static int pos_of(const int *v)
{
    for (int i = 0; i < 8; ++i)
        if (V[i][0] == v[0] && V[i][1] == v[1] && V[i][2] == v[2])
            return i;
    abort();
}
static int G[48][24], DET[48], NSYM;
static void build_syms(void)
{
    static const int perms[6][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1},
                                    {0, 2, 1}, {2, 1, 0}, {1, 0, 2}};
    for (int pi = 0; pi < 6; ++pi)
        for (int sg = 0; sg < 8; ++sg) {
            int m = NSYM++, det = pi < 3 ? 1 : -1;
            for (int a = 0; a < 3; ++a)
                if (sg >> a & 1)
                    det = -det;
            DET[m] = det;
            for (int pos = 0; pos < 8; ++pos) {
                int w[3];
                for (int a = 0; a < 3; ++a)
                    w[perms[pi][a]] = (sg >> a & 1 ? -1 : 1) * V[pos][a];
                for (int a = 0; a < 3; ++a)
                    G[m][3 * pos + a] = 3 * pos_of(w) + perms[pi][a];
            }
        }
}
/* axes of a corner in a fixed rotational sense, starting with the U/D one */
static const int *ord(int pos)
{
    static const int a[3] = {1, 2, 0}, b[3] = {1, 0, 2};
    return V[pos][0] * V[pos][1] * V[pos][2] > 0 ? a : b;
}
static int SGN = 1;
static void encode(const state_t *s, uint8_t *Z)
{
    Z[0] = 0, Z[1] = 1, Z[2] = 2;
    for (int i = 0; i < 7; ++i) {
        int pos = i + 1, c = s->p[i] + 1, t = s->o[i];
        for (int j = 0; j < 3; ++j)
            Z[3 * pos + ord(pos)[(j + SGN * t + 3) % 3]] =
                (uint8_t) (3 * c + ord(c)[j]);
    }
}
static void decode(const uint8_t *Z, state_t *s)
{
    for (int i = 0; i < 7; ++i) {
        int pos = i + 1, c = Z[3 * pos] / 3, k = 0;
        while (Z[3 * pos + ord(pos)[k]] != 3 * c + ord(c)[0])
            ++k;
        s->p[i] = (uint8_t) (c - 1);
        s->o[i] = (uint8_t) (SGN > 0 ? k : (3 - k) % 3);
    }
}
static void conj(const uint8_t *Z, int m, uint8_t *Y)
{
    for (int s = 0; s < 24; ++s)
        Y[G[m][s]] = (uint8_t) G[m][Z[s]];
}
/* rotate the whole cube so that cubie 0 is home; returns the rotation */
static int normalize(const uint8_t *Y, uint8_t *W)
{
    int at[3] = {0};
    for (int s = 0; s < 24; ++s)
        if (Y[s] < 3)
            at[Y[s]] = s;
    for (int r = 0; r < 48; ++r)
        if (DET[r] > 0 && G[r][at[0]] == 0 && G[r][at[1]] == 1 &&
            G[r][at[2]] == 2) {
            for (int s = 0; s < 24; ++s)
                W[G[r][s]] = Y[s];
            return r;
        }
    abort();
}
static int in_layer(int face, int slot) /* 0 R, 1 B, 2 D */
{
    const int *v = V[slot / 3];
    return face == 0 ? v[0] > 0 : face == 1 ? v[2] < 0 : v[1] < 0;
}
static void layer_turn(const uint8_t *Z, int face, int g, uint8_t *Y)
{
    memcpy(Y, Z, 24);
    for (int s = 0; s < 24; ++s)
        if (in_layer(face, s))
            Y[G[g][s]] = Z[s];
}
/* fit the geometric model to solver.c: twist sign and the three rotations */
static int TURN[3];
static int fit(void)
{
    for (SGN = 1; SGN >= -1; SGN -= 2) {
        int found = 0;
        for (int face = 0; face < 3; ++face) {
            TURN[face] = -1;
            for (int g = 0; g < 48 && TURN[face] < 0; ++g) {
                int ok = DET[g] > 0;
                for (uint32_t r = 1; ok && r < STATES; r += 9973) {
                    state_t s, t, want;
                    uint8_t Z[24], Y[24];
                    unrank_state(r, &s);
                    want = quarter_turn(s, (uint8_t) face);
                    encode(&s, Z);
                    int stays = 1; /* g must keep the layer in the layer */
                    for (int k = 0; k < 24; ++k)
                        if (in_layer(face, k) != in_layer(face, G[g][k]))
                            stays = 0;
                    if (!stays) {
                        ok = 0;
                        break;
                    }
                    layer_turn(Z, face, g, Y);
                    if (Y[0] != 0 || Y[1] != 1 || Y[2] != 2) {
                        ok = 0;
                        break;
                    }
                    decode(Y, &t);
                    ok = !memcmp(&t, &want, sizeof t);
                }
                if (ok)
                    TURN[face] = g;
            }
            found += TURN[face] >= 0;
        }
        if (found == 3)
            return 1;
    }
    return 0;
}

/* ---- union-find ---- */
static uint32_t *uf_new(void)
{
    uint32_t *u = malloc(sizeof *u * STATES);
    for (uint32_t i = 0; i < STATES; ++i)
        u[i] = i;
    return u;
}
static uint32_t uf_find(uint32_t *u, uint32_t x)
{
    while (u[x] != x)
        x = u[x] = u[u[x]];
    return x;
}
static void uf_join(uint32_t *u, uint32_t a, uint32_t b)
{
    a = uf_find(u, a), b = uf_find(u, b);
    if (a != b)
        u[a > b ? a : b] = a > b ? b : a;
}
static uint32_t uf_count(uint32_t *u)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < STATES; ++i)
        n += uf_find(u, i) == i;
    return n;
}

/* ---- exact distances and IDA* node counts (as in heuristics.c) ---- */
static uint8_t D[STATES];
static void exact(void)
{
    uint32_t *q = malloc(STATES * 4), h = 0, t = 1;
    memset(D, 255, STATES);
    D[0] = 0, q[0] = 0;
    while (h < t) {
        uint32_t x = q[h++];
        int p = x / 729, o = x % 729;
        for (int f = 0; f < 3; ++f) {
            int np = p, no = o;
            for (int k = 0; k < 3; ++k) {
                np = perm_turn[f][np], no = orient_turn[f][no];
                uint32_t y = np * 729 + no;
                if (D[y] == 255)
                    D[y] = D[x] + 1, q[t++] = y;
            }
        }
    }
    free(q);
}
static uint8_t *H;
static uint64_t nodes, gen;
static int ida(uint32_t root)
{
    struct { int p, o, f, k, cp, co; } st[16];
    int p0 = root / 729, o0 = root % 729;
    if (root == 0)
        return 0;
    for (int bound = H[root];; ++bound) {
        int d = 0;
        st[0].p = p0, st[0].o = o0, st[0].f = -1;
        ++nodes;
        for (;;) {
            int *f = &st[d].f, *k = &st[d].k;
            if (*f >= 0 && *k < 2) {
                ++*k;
                st[d].cp = perm_turn[*f][st[d].cp];
                st[d].co = orient_turn[*f][st[d].co];
            } else {
                do
                    ++*f;
                while (*f < 3 && d > 0 && *f == st[d - 1].f);
                if (*f >= 3) {
                    if (d == 0)
                        break;
                    --d;
                    continue;
                }
                *k = 0;
                st[d].cp = perm_turn[*f][st[d].p];
                st[d].co = orient_turn[*f][st[d].o];
            }
            ++gen;
            uint32_t c = st[d].cp * 729 + st[d].co;
            if (c == 0)
                return d + 1;
            if (d + 1 + H[c] > bound)
                continue;
            ++nodes, ++d;
            st[d].p = st[d - 1].cp, st[d].o = st[d - 1].co, st[d].f = -1;
        }
    }
}
static void run(const char *name, uint8_t *h)
{
    uint64_t ew = 0, et = 0, gw = 0, gt = 0, tight = 0;
    uint32_t n = 0, worst_at = 0;
    double sum = 0;
    H = h;
    for (uint32_t r = 0; r < STATES; ++r) {
        sum += h[r];
        tight += h[r] == D[r];
        if (h[r] > D[r]) {
            printf("%s NOT ADMISSIBLE at %u\n", name, r);
            exit(1);
        }
    }
    for (uint32_t r = 0; r < STATES; ++r)
        if (D[r] == 11) {
            nodes = gen = 0;
            if (ida(r) != 11) {
                printf("NOT OPTIMAL %u\n", r);
                exit(1);
            }
            if (gen > gw)
                gw = gen, worst_at = r;
            if (nodes > ew)
                ew = nodes;
            gt += gen, et += nodes, ++n;
        }
    state_t s;
    parse_state("21345671111111", &s);
    nodes = gen = 0;
    ida(rank_state(&s));
    printf("%-26s mean_h=%.3f exact=%7llu | d11 gen mean %7.0f worst %7llu "
           "| exp mean %6.0f worst %6llu | ref gen %7llu exp %6llu\n",
           name, sum / STATES, (unsigned long long) tight, (double) gt / n,
           (unsigned long long) gw, (double) et / n, (unsigned long long) ew,
           (unsigned long long) gen, (unsigned long long) nodes);
    fflush(stdout);
}

static uint32_t key_of(const state_t *s) /* pattern_dist index: p * 9 + oS */
{
    int pos0 = 0, pos3 = 0;
    for (int i = 0; i < 7; ++i) {
        if (s->p[i] == 0)
            pos0 = i;
        if (s->p[i] == 3)
            pos3 = i;
    }
    state_t t = *s;
    return rank_state(&t) / 729 * 9 + 3 * s->o[pos0] + s->o[pos3];
}

/* PO k=3 on cubies {0,3,6}, for comparison (heuristics.c's best k=3) */
static uint32_t key3(const state_t *s)
{
    static const int S[3] = {0, 3, 6};
    state_t t = *s;
    uint32_t k = 0;
    for (int i = 0; i < 3; ++i)
        for (int pos = 0; pos < 7; ++pos)
            if (s->p[pos] == S[i])
                k = k * 3 + s->o[pos];
    return rank_state(&t) / 729 * 27 + k;
}

int main(void)
{
    build_syms();
    if (!fit()) {
        puts("geometric model does not fit solver.c");
        return 1;
    }
    printf("model fits solver.c: twist sign %+d, turn syms R=%d B=%d D=%d\n",
           SGN, TURN[0], TURN[1], TURN[2]);
    /* full check of the fit on every state and face */
    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s, t, want;
        uint8_t Z[24], Y[24];
        unrank_state(r, &s);
        encode(&s, Z);
        decode(Z, &t);
        if (memcmp(&s, &t, sizeof s))
            return puts("encode/decode mismatch"), 1;
        for (int f = 0; f < 3; ++f) {
            want = quarter_turn(s, (uint8_t) f);
            layer_turn(Z, f, TURN[f], Y);
            decode(Y, &t);
            if (memcmp(&t, &want, sizeof t))
                return puts("turn mismatch"), 1;
        }
    }
    puts("all 3,674,160 x 3 quarter turns agree with solver.c");
    exact();

    int fixes[48], nfix = 0, nrot = 0;
    for (int m = 0; m < 48; ++m) {
        fixes[m] = G[m][0] / 3 == 0;
        nfix += fixes[m];
        nrot += fixes[m] && DET[m] > 0;
    }
    printf("symmetries: 48 total, %d fix the FUL corner (%d rotations)\n",
           nfix, nrot);

    uint8_t *base = malloc(STATES), *pd = malloc(STATES);
    uint32_t *key = malloc(sizeof *key * STATES);
    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s;
        unrank_state(r, &s);
        key[r] = key_of(&s);
        pd[r] = pattern_dist[key[r] / 9][key[r] % 9];
        uint8_t ho = orient_dist[r % 729];
        base[r] = pd[r] > ho ? pd[r] : ho;
    }
    uint8_t *h_c3pd = malloc(STATES), *h_c3 = malloc(STATES),
            *h_c3v = malloc(STATES), *h_48 = malloc(STATES),
            *h_96 = malloc(STATES);
    memcpy(h_c3pd, base, STATES), memcpy(h_c3, base, STATES);
    memcpy(h_c3v, base, STATES), memcpy(h_48, base, STATES);
    uint32_t *u3 = uf_new(), *u6 = uf_new(), *u48 = uf_new(), *u96 = uf_new();
    uint32_t *inv = malloc(sizeof *inv * STATES);
    uint64_t *seen = calloc(STATES, sizeof *seen);
    uint64_t dist_bad = 0;
    /* does symmetry m act on pattern keys? keymap[m][key] = key', or clash */
    enum { KEYS = 5040 * 9 };
    static int32_t keymap[49][KEYS];
    static int key_ok[49];
    memset(keymap, -1, sizeof keymap);
    for (int m = 0; m < 49; ++m)
        key_ok[m] = 1;

    for (uint32_t r = 0; r < STATES; ++r) {
        state_t s, t;
        uint8_t Z[24], Y[24], W[24];
        unrank_state(r, &s);
        encode(&s, Z);
        for (int m = 0; m < 48; ++m) {
            conj(Z, m, Y);
            int rot = normalize(Y, W);
            if (fixes[m] && rot != 0) /* sym 0 is the identity */
                return puts("corner-fixing symmetry needed a rotation"), 1;
            decode(W, &t);
            if (!valid(&t))
                return puts("image is not a valid state"), 1;
            uint32_t img = rank_state(&t);
            if (seen[img] >> m & 1)
                return puts("symmetry is not a bijection"), 1;
            seen[img] |= 1ull << m;
            dist_bad += D[img] != D[r];
            uf_join(u48, r, img), uf_join(u96, r, img);
            if (fixes[m]) {
                uf_join(u6, r, img);
                if (base[img] > h_c3v[r])
                    h_c3v[r] = base[img];
                if (DET[m] > 0) {
                    uf_join(u3, r, img);
                    if (base[img] > h_c3[r])
                        h_c3[r] = base[img];
                    if (pd[img] > h_c3pd[r])
                        h_c3pd[r] = pd[img];
                }
            }
            if (base[img] > h_48[r])
                h_48[r] = base[img];
            int32_t *km = &keymap[m][key[r]];
            if (*km < 0)
                *km = (int32_t) key[img];
            else if (*km != (int32_t) key[img])
                key_ok[m] = 0;
        }
        for (int k = 0; k < 24; ++k)
            Y[Z[k]] = (uint8_t) k;
        decode(Y, &t);
        inv[r] = rank_state(&t);
        dist_bad += D[inv[r]] != D[r];
        uf_join(u96, r, inv[r]);
        int32_t *km = &keymap[48][key[r]];
        if (*km < 0)
            *km = (int32_t) key[inv[r]];
        else if (*km != (int32_t) key[inv[r]])
            key_ok[48] = 0;
    }
    for (uint32_t r = 0; r < STATES; ++r)
        h_96[r] = h_48[r] > h_48[inv[r]] ? h_48[r] : h_48[inv[r]];
    printf("distance mismatches over 49 maps x %u states: %llu\n", STATES,
           (unsigned long long) dist_bad);
    uint32_t n48 = uf_count(u48), n96 = uf_count(u96);
    printf("classes: 3 rotations %u | 6 fixing the corner %u | all 48 %u | 48 + inverse "
           "%u | no symmetry %u\n",
           uf_count(u3), uf_count(u6), n48, n96, STATES);
    printf("4-bit exact table over classes: 48 -> %u B, 48+inv -> %u B\n",
           (n48 + 1) / 2, (n96 + 1) / 2);

    /* S2a: which maps act on the pattern keys, and how far that shrinks */
    {
        int acting = 0;
        static uint32_t ku[KEYS];
        for (uint32_t i = 0; i < KEYS; ++i)
            ku[i] = i;
        for (int m = 0; m < 49; ++m) {
            if (!key_ok[m])
                continue;
            ++acting;
            for (uint32_t i = 0; i < KEYS; ++i) {
                uint32_t a = i, b = (uint32_t) keymap[m][i];
                while (ku[a] != a)
                    a = ku[a];
                while (ku[b] != b)
                    b = ku[b];
                if (a != b)
                    ku[a > b ? a : b] = a > b ? b : a;
            }
        }
        uint32_t classes = 0;
        for (uint32_t i = 0; i < KEYS; ++i)
            classes += ku[i] == i;
        printf("maps (of 48 syms + inverse) that act on the pattern_dist "
               "index: %d (identity included); entries %d -> %u\n",
               acting, KEYS, classes);
        printf("   acting maps:");
        for (int m = 0; m < 49; ++m)
            if (key_ok[m])
                printf(" %d%s", m, m == 48 ? "(inverse)" : fixes[m] ? "(fix)" : "");
        puts("");
    }

    /* S2b: node counts */
    run("v2 max(PD,ho)", base);
    run("2 rotations, PD only", h_c3pd);
    run("2 rotations, PD and ho", h_c3);
    run("6 fixing the corner", h_c3v);
    run("all 48", h_48);
    run("48 + inverse", h_96);
    {
        /* PO k=3 by abstract BFS, as heuristics.c does */
        enum { K3 = 5040 * 27 };
        uint8_t *ad = malloc(K3), *h3 = malloc(STATES);
        state_t *q = malloc(sizeof *q * K3);
        uint32_t qh = 0, qt = 1, n = 1;
        state_t s0 = {{0, 1, 2, 3, 4, 5, 6}, {0}};
        memset(ad, 255, K3);
        q[0] = s0, ad[key3(&s0)] = 0;
        while (qh < qt) {
            state_t x = q[qh++];
            uint8_t dx = ad[key3(&x)];
            for (int f = 0; f < 3; ++f) {
                state_t y = x;
                for (int k = 0; k < 3; ++k) {
                    y = quarter_turn(y, (uint8_t) f);
                    uint32_t ky = key3(&y);
                    if (ad[ky] == 255)
                        ad[ky] = dx + 1, q[qt++] = y, ++n;
                }
            }
        }
        for (uint32_t r = 0; r < STATES; ++r) {
            state_t s;
            unrank_state(r, &s);
            uint8_t v = ad[key3(&s)], ho = orient_dist[r % 729];
            h3[r] = v > ho ? v : ho;
        }
        printf("PO k=3 {0,3,6}: %u entries\n", n);
        run("PO k=3 (no sym)", h3);
    }
    return 0;
}
