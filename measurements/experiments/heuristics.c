/* Host experiment behind the choice of heuristic (stage 2): for each
 * candidate abstraction, the IDA* node counts over all 2,644 distance-11
 * states. PS = positions of a subset S of cubies plus the whole orientation;
 * PO = the whole permutation plus the twists of S. Each candidate is
 * combined with max(perm-only, orient-only). For each family and size the
 * subset with the highest mean h is shown.
 *   cc -O2 -w measurements/experiments/heuristics.c -o heuristics && ./heuristics
 */
#define main solver_main
#include "../../solver.c"
#undef main
#include <time.h>

static uint16_t PT[3][PERMUTATIONS], OT[3][ORIENTATIONS];
static uint8_t D[STATES];               /* exact distance */
static uint8_t *H;                      /* heuristic under test, per full rank */

static void tables(void) {
    state_t s;
    for (int r = 0; r < PERMUTATIONS; ++r) { unrank_state((uint32_t) r * ORIENTATIONS, &s);
        for (int f = 0; f < 3; ++f) { state_t n = quarter_turn(s, f); PT[f][r] = rank_state(&n) / ORIENTATIONS; } }
    for (int r = 0; r < ORIENTATIONS; ++r) { unrank_state(r, &s);
        for (int f = 0; f < 3; ++f) { state_t n = quarter_turn(s, f); OT[f][r] = rank_state(&n) % ORIENTATIONS; } }
}
static void exact(void) {
    uint32_t *q = malloc(STATES * 4), h = 0, t = 1;
    memset(D, 255, STATES); D[0] = 0; q[0] = 0;
    while (h < t) { uint32_t x = q[h++]; int p = x / 729, o = x % 729;
        for (int f = 0; f < 3; ++f) { int np = p, no = o;
            for (int k = 0; k < 3; ++k) { np = PT[f][np]; no = OT[f][no]; uint32_t y = np * 729 + no;
                if (D[y] == 255) { D[y] = D[x] + 1; q[t++] = y; } } } }
    free(q);
}
/* Abstraction = projection of a full state to a key; abstract BFS via representatives. */
typedef uint32_t (*proj_t)(const state_t *);
static int K, S[7];
static uint32_t proj_ps(const state_t *s) {  /* positions of cubies S, plus full orientation */
    uint32_t k = 0; for (int i = 0; i < K; ++i) for (int pos = 0; pos < 7; ++pos) if (s->p[pos] == S[i]) k = k * 7 + pos;
    uint32_t o = 0; for (int i = 0; i < 6; ++i) o = o * 3 + s->o[i]; return k * 729 + o;
}
static uint32_t proj_po(const state_t *s) {  /* full permutation, plus orientation of cubies S */
    state_t t = *s; uint32_t p = rank_state(&t) / 729, k = 0;
    for (int i = 0; i < K; ++i) for (int pos = 0; pos < 7; ++pos) if (s->p[pos] == S[i]) k = k * 3 + s->o[pos];
    return p * 27 + k;
}
static uint32_t proj_p(const state_t *s) { state_t t = *s; return rank_state(&t) / 729; }
static uint32_t proj_o(const state_t *s) { state_t t = *s; return rank_state(&t) % 729; }
/* Abstract distance via BFS over the full graph but deduplicated on keys:
 * since projection commutes with moves, the first time a key is reached is its abstract distance. */
static uint8_t *abstract_bfs(proj_t pr, uint32_t keys, uint32_t *nkeys) {
    uint8_t *ad = malloc(keys); memset(ad, 255, keys);
    state_t *q = malloc(sizeof(state_t) * keys); uint32_t h = 0, t = 1, n = 1;
    state_t s0 = {{0,1,2,3,4,5,6},{0}}; q[0] = s0; ad[pr(&s0)] = 0;
    while (h < t) { state_t x = q[h++]; uint8_t dx = ad[pr(&x)];
        for (int f = 0; f < 3; ++f) { state_t y = x;
            for (int k = 0; k < 3; ++k) { y = quarter_turn(y, f); uint32_t ky = pr(&y);
                if (ad[ky] == 255) { ad[ky] = dx + 1; q[t++] = y; ++n; } } } }
    free(q); *nkeys = n; return ad;
}
static void lift(uint8_t *ad, proj_t pr, int combine) { /* H[r] = max(H[r], ad[pr(r)]) */
    state_t s; for (uint32_t r = 0; r < STATES; ++r) { unrank_state(r, &s); uint8_t v = ad[pr(&s)];
        if (!combine || v > H[r]) H[r] = v; }
}
static uint64_t nodes, gen;
static int ida(uint32_t root) {   /* returns solution length; counts expanded nodes */
    struct { int p, o, f, k, cp, co; } st[16];
    int p0 = root / 729, o0 = root % 729;
    for (int bound = H[root];; ++bound) {
        int d = 0; st[0].p = p0; st[0].o = o0; st[0].f = -1;
        if (root == 0) return 0;
        ++nodes;
        for (;;) {
            /* advance current frame to next child */
            int *f = &st[d].f, *k = &st[d].k;
            if (*f >= 0 && *k < 2) { ++*k; st[d].cp = PT[*f][st[d].cp]; st[d].co = OT[*f][st[d].co]; }
            else { do ++*f; while (*f < 3 && d > 0 && *f == st[d-1].f);
                   if (*f >= 3) { if (d == 0) break; --d; continue; }
                   *k = 0; st[d].cp = PT[*f][st[d].p]; st[d].co = OT[*f][st[d].o]; }
            ++gen; uint32_t c = st[d].cp * 729 + st[d].co;
            if (c == 0) return d + 1;
            if (d + 1 + H[c] > bound) continue;
            ++nodes; ++d; st[d].p = st[d-1].cp; st[d].o = st[d-1].co; st[d].f = -1;
        }
    }
}
static double meanh(void) { double s = 0; for (uint32_t r = 0; r < STATES; ++r) s += H[r]; return s / STATES; }
static void run(const char *name, size_t bytes) {
    uint64_t worst = 0, tot = 0, ref = 0, gw = 0, gt = 0; int n = 0;
    for (uint32_t r = 0; r < STATES; ++r) if (D[r] == 11) { nodes = 0; gen = 0; int L = ida(r); if (gen > gw) gw = gen; gt += gen;
        if (L != 11) { printf("NOT OPTIMAL %u\n", r); exit(1); }
        if (nodes > worst) worst = nodes; tot += nodes; ++n; }
    state_t s; parse_state("21345671111111", &s); nodes = 0; ida(rank_state(&s)); ref = nodes;
    printf("%-44s mean_h=%.3f exp_mean=%7.0f exp_worst=%7llu gen_mean=%8.0f gen_worst=%8llu ref_exp=%llu\n", name, meanh(), (double) tot / n, (unsigned long long) worst, (double) gt / n, (unsigned long long) gw, (unsigned long long) ref);
}
int main(int argc, char **argv) {
    tables(); exact(); H = malloc(STATES); uint32_t n;
    double dm = 0; for (uint32_t r = 0; r < STATES; ++r) dm += D[r]; printf("mean true distance %.3f\n", dm / STATES);
    uint8_t *hp = abstract_bfs(proj_p, PERMUTATIONS, &n), *ho = abstract_bfs(proj_o, ORIENTATIONS, &n);
    int mp = 0, mo = 0; for (int i = 0; i < 5040; ++i) if (hp[i] > mp) mp = hp[i]; for (int i = 0; i < 729; ++i) if (ho[i] > mo) mo = ho[i];
    printf("max hp=%d max ho=%d\n", mp, mo);
    lift(hp, proj_p, 0); lift(ho, proj_o, 1); run("max(hp,ho)", 5040 + 729);
    uint8_t *base = malloc(STATES); memcpy(base, H, STATES);
    /* scan subsets by mean h, then IDA* on the best of each family */
    for (int fam = 0; fam < 2; ++fam) for (K = 1; K <= 3; ++K) {
        double best = -1; int bs = 0;
        for (int m = 0; m < 128; ++m) { if (__builtin_popcount(m) != K) continue;
            int j = 0; for (int c = 0; c < 7; ++c) if (m >> c & 1) S[j++] = c;
            uint32_t keys = fam == 0 ? 343 * 729 : 5040 * 27;
            uint8_t *ad = abstract_bfs(fam == 0 ? proj_ps : proj_po, keys, &n);
            memcpy(H, base, STATES); lift(ad, fam == 0 ? proj_ps : proj_po, 1); free(ad);
            double mh = meanh(); if (mh > best) { best = mh; bs = m; } }
        int j = 0; for (int c = 0; c < 7; ++c) if (bs >> c & 1) S[j++] = c;
        uint8_t *ad = abstract_bfs(fam == 0 ? proj_ps : proj_po, fam == 0 ? 343 * 729 : 5040 * 27, &n);
        int mx = 0; for (uint32_t i = 0; i < (fam == 0 ? 343u * 729 : 5040u * 27); ++i) if (ad[i] != 255 && ad[i] > mx) mx = ad[i];
        memcpy(H, base, STATES); lift(ad, fam == 0 ? proj_ps : proj_po, 1); free(ad);
        char name[64]; snprintf(name, sizeof name, "%s k=%d S=0x%02x (+hp,ho) entries=%u max=%d", fam ? "PO" : "PS", K, bs, n, mx);
        run(name, (size_t) n);
    }
    return 0;
}
