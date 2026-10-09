/* Which key for the pattern database: every subgroup of order 8, counted.
 *
 * A key that commutes with the moves is a left coset H s of a subgroup H of
 * the group of states: the states of a key are s after one of |H|
 * relabellings of the cubies, and the distance of a key is the least
 * distance among them. 459,270 keys at 2 bits fill the memory, so |H| = 8.
 * A subgroup of order 8 is a subgroup P of order 8 of S7 acting on the
 * cubie labels, with the twists measured in some frame c: the key is the
 * coset of the permutation under P and o'[i] = o[i] + c[cubie at i]. (All
 * complements of the twists in the group they make with P are conjugate.)
 * Frames that differ by a function that is constant on the orbits of P give
 * the same key, and the sum of c is fixed at 0.
 *
 * For each of the 1,575 subgroups P and each frame the program builds the
 * table by BFS over the keys and runs the search of r11 (three rotated
 * views, the farthest first, the cost of every block of the loop as in
 * r11-nodes.c) over the 2,644 distance-11 states. A key is refused when the
 * three views do not pin the solved state, or when a search ends before
 * bound 11.
 *   cc -O2 -w measurements/experiments/r11-keys.c -o r11-keys
 *   ./r11-keys list                 count the subgroups
 *   ./r11-keys run I N FRAMES CAP   subgroups with index % N == I; FRAMES 0:
 *                                   the frame of the twists as stored, 1:
 *                                   every frame; a table is abandoned when
 *                                   its mean is over CAP children after 300
 *                                   states
 *   ./r11-keys one G C              subgroup G in frame C (7 digits): other
 *                                   orders of the views, faces and tests,
 *                                   the key distances, and whether two
 *                                   views pin the solved state
 * r11-keys.txt has the commands that were run and the best lines.
 */
#define main solver_main
#include "../../solver.c"
#undef main
static const uint8_t sym_pi[7] = {2, 5, 6, 1, 4, 3, 0}, sym_tau[7] = {0, 1, 0, 1, 0, 1, 0}, sym_face[3] = {2, 0, 1};
static void rotate_state(const state_t *s, state_t *r)
{
    for (int i = 0; i < 7; ++i) { int c = s->p[i]; r->p[sym_pi[i]] = sym_pi[c]; r->o[sym_pi[i]] = (s->o[i] + sym_tau[i] + 3 - sym_tau[c]) % 3; }
}
static uint16_t orient_turn[3][729];
static uint8_t P[5040][7];
static int prank(const uint8_t *p) { state_t s; memcpy(s.p, p, 7); memset(s.o, 0, 7); return rank_state(&s) / 729; }
typedef struct { uint16_t e[8]; } grp_t;
static grp_t groups[20000]; static int ngroups;
static void compose(const uint8_t *a, const uint8_t *b, uint8_t *c) { for (int i = 0; i < 7; ++i) c[i] = a[b[i]]; }
static int closure(const int *gens, int ng, uint16_t *out)
{
    int n = 1; out[0] = 0; /* identity has rank 0 */
    for (int head = 0; head < n; ++head)
        for (int g = 0; g < ng; ++g) {
            uint8_t c[7]; compose(P[out[head]], P[gens[g]], c); int r = prank(c), seen = 0;
            for (int i = 0; i < n; ++i) seen |= out[i] == r;
            if (!seen) { if (n == 8) return 9; out[n++] = r; }
        }
    return n;
}
static int cmp16(const void *a, const void *b) { return *(const uint16_t *) a - *(const uint16_t *) b; }
static void add_group(uint16_t *e)
{
    qsort(e, 8, 2, cmp16);
    for (int i = 0; i < ngroups; ++i) if (!memcmp(groups[i].e, e, 16)) return;
    memcpy(groups[ngroups++].e, e, 16);
}
static void conj(const grp_t *g, const uint8_t *pi, grp_t *out)
{
    uint8_t inv[7]; for (int i = 0; i < 7; ++i) inv[pi[i]] = i;
    for (int k = 0; k < 8; ++k) { uint8_t t[7], u[7]; compose(pi, P[g->e[k]], t); compose(t, inv, u); out->e[k] = prank(u); }
    qsort(out->e, 8, 2, cmp16);
}
static void enumerate(void)
{
    static int el[2000]; int nel = 0, inv2[400], n2 = 0;
    for (int r = 1; r < 5040; ++r) {
        uint8_t a[7], b[7]; compose(P[r], P[r], a); compose(a, a, b);
        if (prank(b) == 0) { el[nel++] = r; if (prank(a) == 0) inv2[n2++] = r; }
    }
    for (int i = 0; i < nel; ++i) for (int j = i + 1; j < nel; ++j) {
        int gens[2] = {el[i], el[j]}; uint16_t out[9];
        if (closure(gens, 2, out) == 8) add_group(out);
    }
    for (int i = 0; i < n2; ++i) for (int j = i + 1; j < n2; ++j) {
        int g2[2] = {inv2[i], inv2[j]}; uint16_t out[9];
        if (closure(g2, 2, out) != 4) continue;
        for (int k = j + 1; k < n2; ++k) { int g3[3] = {inv2[i], inv2[j], inv2[k]}; if (closure(g3, 3, out) == 8) add_group(out); }
    }
}
/* one candidate */
static uint16_t coset_of[5040], block_turn[3][630];
static uint8_t dist[630][729];
static int ncoset, solved_b, solved_o;
static int build(const grp_t *g, const uint8_t *c)
{
    static int canon[5040], id[5040];
    memset(id, -1, sizeof id); ncoset = 0;
    for (int r = 0; r < 5040; ++r) { int m = 5040; for (int k = 0; k < 8; ++k) { uint8_t q[7]; for (int i = 0; i < 7; ++i) q[i] = P[g->e[k]][P[r][i]]; int x = prank(q); if (x < m) m = x; } canon[r] = m; }
    static uint16_t repr[630];
    for (int r = 0; r < 5040; ++r) { if (id[canon[r]] < 0) { if (ncoset == 630) return 0; repr[ncoset] = canon[r]; id[canon[r]] = ncoset++; } coset_of[r] = id[canon[r]]; }
    if (ncoset != 630) return 0;
    for (int b = 0; b < 630; ++b) for (int f = 0; f < 3; ++f) { state_t s; memcpy(s.p, P[repr[b]], 7); memset(s.o, 0, 7); s = quarter_turn(s, f); block_turn[f][b] = coset_of[prank(s.p)]; }
    solved_b = coset_of[0]; solved_o = 0; for (int i = 0; i < 6; ++i) solved_o = solved_o * 3 + c[i];
    static uint32_t queue[630 * 729]; int head = 0, tail = 1;
    memset(dist, 255, sizeof dist); dist[solved_b][solved_o] = 0; queue[0] = solved_b * 729 + solved_o;
    while (head < tail) { int b = queue[head] / 729, o = queue[head] % 729; ++head;
        for (int f = 0; f < 3; ++f) { int nb = b, no = o; for (int t = 0; t < 3; ++t) { nb = block_turn[f][nb]; no = orient_turn[f][no];
            if (dist[nb][no] == 255) { dist[nb][no] = dist[b][o] + 1; queue[tail++] = nb * 729 + no; } } } }
    if (tail != 630 * 729) return 0;
    /* the 7 other states of the solved key of view 0: do views 1 and 2 tell them from solved? */
    for (int k = 0; k < 8; ++k) {
        state_t s, r1, r2; int sum = 0;
        if (!g->e[k]) continue;
        for (int i = 0; i < 7; ++i) { s.p[i] = P[g->e[k]][i]; s.o[i] = (c[i] + 3 - c[s.p[i]]) % 3; sum += s.o[i]; }
        if (sum % 3) continue; /* not a state: the class of solved is smaller */
        rotate_state(&s, &r1); rotate_state(&r1, &r2);
        int o1 = 0, o2 = 0; for (int i = 0; i < 6; ++i) { o1 = o1 * 3 + (r1.o[i] + c[r1.p[i]]) % 3; o2 = o2 * 3 + (r2.o[i] + c[r2.p[i]]) % 3; }
        if (coset_of[prank(r1.p)] == solved_b && o1 == solved_o && coset_of[prank(r2.p)] == solved_b && o2 == solved_o) return -2;
    }
    return 1;
}
typedef struct { uint16_t b[3], o[3]; } node_t;
static long cost, children, kinds[6]; static int bound;
static int face_order[3] = {0, 1, 2}, swap_tail, tie_rule = 1, lead_mode = 0, CF = 12, CT = 9;
static int search(const node_t *n, int depth, int parent, int entered)
{
    int first = 1, left = bound - depth - 1;
    for (int fi = 0; fi < 3; ++fi) {
        int f = face_order[fi];
        if (f == parent) { if (!(entered && first)) { cost += 1; kinds[4] += 1; } continue; }
        cost += entered && first ? 2 : 9; kinds[4] += entered && first ? 2 : 9; first = 0;
        node_t ch = *n; int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f;
            for (k = 0; k < 3; ++k, face = sym_face[face]) { ch.b[k] = block_turn[face][ch.b[k]]; ch.o[k] = orient_turn[face][ch.o[k]]; }
            ++behind; ++children; cost += CF; kinds[0] += CF;
            for (k = 0; k < 3; ++k) { int v = k ? (swap_tail ? 3 - k : k) : 0;
                if (k == 1) { cost += 8 * behind; kinds[2] += 8 * behind; behind = 0; } if (k) { cost += CT; kinds[3] += CT; } if (dist[ch.b[v]][ch.o[v]] > left) break; }
            if (k < 3) { cost += 2; kinds[1] += 2; continue; }
            cost += 5; kinds[4] += 5; if (!left) return 1;
            cost += 14 + (fi == 0); kinds[4] += 14 + (fi == 0); if (search(&ch, depth + 1, f, 1)) return 1; cost += 17 + fi; kinds[4] += 15 + fi; kinds[1] += 2;
        }
    }
    return 0;
}
static state_t anti[2644][3]; static int nanti;
static int key_o(const state_t *s, const uint8_t *c) { int o = 0; for (int i = 0; i < 6; ++i) o = o * 3 + (s->o[i] + c[s->p[i]]) % 3; return o; }
static int evaluate(const grp_t *g, const uint8_t *c, long cap, double *res)
{
    { int rc = build(g, c); if (rc != 1) return rc; }
    long sumk = 0, sumc = 0, worstk = 0, worstc = 0, failed_worst = 0; double sumh = 0; int worst_i = 0;
    for (int i = 0; i < nanti; ++i) {
        node_t v, root; int far[3], lead = 0, best = -1; bound = 0;
        for (int k = 0; k < 3; ++k) { v.b[k] = coset_of[prank(anti[i][k].p)]; v.o[k] = key_o(&anti[i][k], c); far[k] = dist[v.b[k]][v.o[k]]; if (far[k] > bound) bound = far[k]; }
        for (int k = 0; k < 3; ++k) { int key = lead_mode == 1 ? -k : lead_mode == 2 ? (far[k] << 4) + far[(k + 2) % 3] : lead_mode == 3 ? (far[k] << 4) - far[(k + 1) % 3] + 15 : (far[k] << 4) + (tie_rule ? far[(k + 1) % 3] : 0); if (key > best) { best = key; lead = k; } }
        for (int j = 0, k = lead; j < 3; ++j, k = (k + 1) % 3) { root.b[j] = v.b[k]; root.o[j] = v.o[k]; }
        sumh += bound; cost = 0; children = 0; long failed = 0;
        for (;; ++bound) { cost += 5; long before = cost; if (search(&root, 0, 3, 0)) break; cost += 25; failed = cost; if (bound > 11) return 0; }
        if (bound != 11) return -3;
        sumk += children; sumc += cost; if (children > worstk) worstk = children; if (cost > worstc) { worstc = cost; worst_i = i; } if (failed > failed_worst) failed_worst = failed;
        if (cap && i >= 300 && sumk > cap * (i + 1)) return -1;
    }
    res[6] = worst_i;
    res[0] = (double) sumk / nanti; res[1] = worstk; res[2] = (double) sumc / nanti; res[3] = worstc; res[4] = sumh / nanti; res[5] = failed_worst;
    return 1;
}
int main(int argc, char **argv)
{
    for (int r = 0; r < 5040; ++r) { state_t s; unrank_state(r * 729, &s); memcpy(P[r], s.p, 7); }
    for (int r = 0; r < 729; ++r) { state_t s; unrank_state(r, &s); for (int f = 0; f < 3; ++f) { state_t n = quarter_turn(s, f); orient_turn[f][r] = rank_state(&n) % 729; } }
    FILE *in = fopen("distance11.txt", "r"); char text[32];
    while (fscanf(in, "%31s", text) == 1) { parse_state(text, &anti[nanti][0]); rotate_state(&anti[nanti][0], &anti[nanti][1]); rotate_state(&anti[nanti][1], &anti[nanti][2]); ++nanti; }
    enumerate();
    /* Conjugating P by the rotation is another key, not the same one seen
     * from another view: the twists stay in the frame of the state. So
     * every subgroup is counted. */
    static int cls[20000]; int ncls = ngroups;
    for (int i = 0; i < ngroups; ++i) cls[i] = i;
    fprintf(stderr, "%d subgroups of order 8 of S7, %d states at distance 11\n", ngroups, nanti);
    if (argc > 1 && !strcmp(argv[1], "list")) return 0;
    if (argc > 3 && !strcmp(argv[1], "one")) {
        const grp_t *g = &groups[atoi(argv[2])]; uint8_t c[7]; for (int i = 0; i < 7; ++i) c[i] = argv[3][i] - '0';
        double res[8];
        static const int orders[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        if (argc > 4) { CF = 11; CT = 8; }
        for (int lm = 0; lm < 4; ++lm) for (int tr = 0; tr < (lm ? 1 : 2); ++tr) for (int fo = 0; fo < 6; ++fo) for (int sw = 0; sw < 2; ++sw) {
            lead_mode = lm; tie_rule = 1 - tr; memcpy(face_order, orders[fo], sizeof face_order); swap_tail = sw; memset(kinds, 0, sizeof kinds);
            int rc = evaluate(g, c, 0, res);
            printf("lead %d tie %d faces %d%d%d swap %d: rc %d kids mean %.1f worst %.0f | loop mean %.0f worst %.0f | failed worst %.0f | first %.0f next %.0f catch %.0f test %.0f node %.0f\n", lm, tie_rule, face_order[0], face_order[1], face_order[2], sw, rc,
                   res[0], res[1], res[2], res[3], res[5], (double) kinds[0] / nanti, (double) kinds[1] / nanti, (double) kinds[2] / nanti, (double) kinds[3] / nanti, (double) kinds[4] / nanti);
        }
        { /* which pairs of views tell every state of the solved key of each view from solved */
          build(g, c);
          int pin[3] = {1, 1, 1}; /* pin[k]: views k and k+1 */
          for (int k = 1; k < 8; ++k) { state_t s[3]; int sum = 0, at[3];
            for (int i = 0; i < 7; ++i) { s[0].p[i] = P[g->e[k]][i]; s[0].o[i] = (c[i] + 3 - c[s[0].p[i]]) % 3; sum += s[0].o[i]; }
            if (sum % 3) continue;
            rotate_state(&s[0], &s[1]); rotate_state(&s[1], &s[2]);
            for (int v = 0; v < 3; ++v) at[v] = coset_of[prank(s[v].p)] == solved_b && key_o(&s[v], c) == solved_o;
            /* s[0] is solved in view 0; s rotated back gives the states solved in the other views */
            if (at[1]) pin[0] = 0; if (at[2]) pin[2] = 0;
            { state_t b1, b2; rotate_state(&s[0], &b1); rotate_state(&b1, &b2); /* b2 = s rotated twice = rotated back once */
              state_t t[3]; t[0] = b2; rotate_state(&t[0], &t[1]); rotate_state(&t[1], &t[2]);
              /* t[1] = s: solved in view 1 of t[0]; is t[0] solved in view 2 too? */
              if (coset_of[prank(t[2].p)] == solved_b && key_o(&t[2], c) == solved_o) pin[1] = 0; }
          }
          printf("two views pin the solved state: views 0+1 %d, 1+2 %d, 2+0 %d\n", pin[0], pin[1], pin[2]); }
        int hist[12] = {0}, maxd = 0; for (int b = 0; b < 630; ++b) for (int o = 0; o < 729; ++o) { ++hist[dist[b][o]]; if (dist[b][o] > maxd) maxd = dist[b][o]; }
        printf("key distances:"); for (int d = 0; d <= maxd; ++d) printf(" %d:%d", d, hist[d]); printf("\n");
        return 0;
    }
    int I = argc > 3 ? atoi(argv[2]) : 0, N = argc > 3 ? atoi(argv[3]) : 1, frames = argc > 4 ? atoi(argv[4]) : 0;
    long cap = argc > 5 ? atol(argv[5]) : 1200;
    for (int q = I; q < ncls; q += N) {
        const grp_t *g = &groups[cls[q]];
        /* orbits of P on the labels */
        int orb[7]; for (int i = 0; i < 7; ++i) { orb[i] = i; for (int k = 0; k < 8; ++k) if (P[g->e[k]][i] < orb[i]) orb[i] = P[g->e[k]][i]; }
        int nf = frames ? 2187 : 1;
        for (int fr = 0; fr < nf; ++fr) {
            uint8_t c[7]; int x = fr, sum = 0, ok = 1;
            for (int i = 0; i < 7; ++i) { c[i] = x % 3; x /= 3; sum += c[i]; }
            if (sum % 3) continue;
            /* frames that differ by a function constant on the orbits are the same key: keep c = 0 on each orbit's first label */
            for (int i = 0; i < 7; ++i) if (orb[i] == i && c[i]) ok = 0;
            if (frames && !ok) { /* the sum-0 condition couples them: keep instead those with c[orbit minimum] = 0 except one free orbit */
                int firsts = 0, bad = 0; for (int i = 0; i < 7; ++i) if (orb[i] == i) { ++firsts; if (firsts > 1 && c[i]) bad = 1; }
                if (bad) continue; }
            double res[6]; int rc = evaluate(g, c, cap, res);
            printf("%d ", cls[q]); for (int k = 0; k < 8; ++k) { printf("%s", k ? "," : ""); for (int i = 0; i < 7; ++i) printf("%d", P[g->e[k]][i]); }
            printf(" c="); for (int i = 0; i < 7; ++i) printf("%d", c[i]);
            if (rc == 1) printf(" kids mean %.1f worst %.0f | loop mean %.0f worst %.0f | root h %.3f | failed worst %.0f\n", res[0], res[1], res[2], res[3], res[4], res[5]);
            else printf(rc == -1 ? " abandoned (weak)\n" : rc == -2 ? " views do not pin the state\n" : rc == -3 ? " ends before 11\n" : " not a 630-coset key\n");
            fflush(stdout);
        }
    }
    return 0;
}
