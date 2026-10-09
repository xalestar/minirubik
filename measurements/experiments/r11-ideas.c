/* Host counts behind the ideas of round 2 (TASK.md, N4 to N10): what the
 * loop of r11 would cost with another policy at the root of the last
 * iteration, with bounds 10 and 11 in one pass, with a shorter pop, and
 * with another order of the moves on the walk home.
 *
 * search() is the loop of r11 with the cost of every block, as in
 * r11-nodes.c. For the last iteration (bound 11) each first move is also
 * searched on its own, with and without a budget of pops, so a policy is a
 * sum over the first moves in its order. Each line is the worst and the
 * mean over the 2,644 states of (measured - loop of r11 + loop of the
 * variant); the fixed cost of a policy is in its name.
 *   cc -O2 -w measurements/experiments/r11-ideas.c -o r11-ideas
 *   ./r11-ideas measurements/sweep-r11.txt
 *   DUMP=1 ./r11-ideas measurements/sweep-r11.txt   one line a state: each
 *       first move as distance : key distances : F found or - failed, cost
 * It reads the ida.c and tables.h of r11, so it builds at d1b8fed.
 */
#define main solver_main
#include "../../solver.c"
#undef main
#define NO_MAIN
#define COUNT_OPS
#include "../../ida.c"
typedef struct { uint16_t b[VIEWS], o[VIEWS]; int8_t s[VIEWS]; } node_t;
static long cost, children, nodes_left, nodes_seen; static int max_depth;
static int bound, lead_view, far_s[VIEWS], aborted;
static uint8_t *exact;
/* per-test cost knobs so the m2 lookup (one instruction less) can be modelled */
static int C_FIRST = 12, C_TEST = 9, POPX = 0;
static int search(const node_t *n, int depth, int parent, int entered)
{
    int first = 1;
    ++nodes_seen; if (depth > max_depth) max_depth = depth;
    for (int f = 0; f < 3; ++f) {
        if (f == parent) { if (!(entered && first)) cost += 1; continue; }
        cost += entered && first ? 2 : 9;
        first = 0;
        node_t child = *n;
        int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) {
                child.b[k] = block_turn[face][child.b[k]];
                child.o[k] = orient_turn[face][child.o[k]];
            }
            ++behind; ++children; cost += C_FIRST;
            for (k = 0; k < VIEWS; ++k) {
                if (k == 1) { cost += 8 * behind; behind = 0; }
                if (k) cost += C_TEST;
                child.s[k] = next_state[n->s[k]][mod3(child.b[k], child.o[k])];
                if (child.s[k] < 0) break;
            }
            if (k < VIEWS) { cost += 2; continue; }
            cost += 5;
            if (depth + 1 == bound) return 1;
            cost += 14 + (f == 0);
            if (search(&child, depth + 1, f, 1)) return 1;
            if (aborted) return 0;
            if (--nodes_left == 0) { aborted = 1; return 0; }
            cost += 15 + f + 2 + POPX;
        }
    }
    return 0;
}
static long pops_n, pops3; static int tighten, found_len, far_s3[3];
/* DFBnB: when a solution is found at bound 11, go on at bound 10 */
static int search_bnb(const node_t *n, int depth, int parent, int entered, int bnb)
{
    int first = 1;
    for (int f = 0; f < 3; ++f) {
        if (f == parent) { if (!(entered && first)) cost += 1; continue; }
        cost += entered && first ? 2 : 9; first = 0;
        node_t child = *n; int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) { child.b[k] = block_turn[face][child.b[k]]; child.o[k] = orient_turn[face][child.o[k]]; }
            ++behind; ++children; cost += 12;
            for (k = 0; k < VIEWS; ++k) {
                if (k == 1) { cost += 8 * behind; behind = 0; }
                if (k) cost += 9;
                /* slack with the bound as it is now */
                int d = root_dist(child.b[k], child.o[k]);
                if (d > bound - tighten - depth - 1) break;
            }
            if (k < VIEWS) { cost += 2; continue; }
            cost += 5;
            if (depth + 1 == bound - tighten) { if (!bnb || tighten) return 1; found_len = bound; tighten = 1; cost += 60; return 2; }
            cost += 14 + (f == 0);
            int r = search_bnb(&child, depth + 1, f, 1, bnb);
            if (r == 1) return 1;
            cost += 15 + f + 2; ++pops_n; pops3 += t == 3;
            if (r == 2) { /* unwinding after the first solution: is this node still alive at the tighter bound? */
                int alive = 1; for (k = 0; k < VIEWS; ++k) if (root_dist(n->b[k], n->o[k]) > bound - tighten - depth) alive = 0;
                if (!alive) return 2; }
        }
    }
    return 0;
}
static void more(const char *path)
{
    /* (i) the elements of the subgroup of the new key, as states without twist */
    static const char *const els[8] = {"0123456", "0253146", "0413526", "0543216", "6143250", "6213540", "6453120", "6523410"};
    printf("new key, distance of each element of the subgroup:");
    for (int k = 0; k < 8; ++k) { state_t s; for (int i = 0; i < 7; ++i) { s.p[i] = els[k][i] - '0'; s.o[i] = 0; } printf(" %s:%d", els[k], exact[rank_state(&s)]); }
    static const char *const old[8] = {"0123456", "0543216", "1023465", "1643205", "5043261", "5623401", "6143250", "6523410"};
    printf("\nr11 key, distance of each element of the subgroup:");
    for (int k = 0; k < 8; ++k) { state_t s; for (int i = 0; i < 7; ++i) { s.p[i] = old[k][i] - '0'; s.o[i] = 0; } printf(" %s:%d", old[k], exact[rank_state(&s)]); }
    { state_t s; unrank_state(0, &s); s = apply_move(s, 1); s = apply_move(s, 4); s = apply_move(s, 1); printf("\nR2 B2 R2 = "); for (int i = 0; i < 7; ++i) printf("%d", s.p[i]); printf(" twists "); for (int i = 0; i < 7; ++i) printf("%d", s.o[i]); printf("\n"); }
    /* (iv) keys tried per step of the walk home, by order of the 9 moves, over all keys of distance 7..10 */
    {
        static const int ord[4][9] = {{0,1,2,3,4,5,6,7,8},{1,0,2,4,3,5,7,6,8},{0,2,1,3,5,4,6,8,7},{1,4,7,0,3,6,2,5,8}};
        static const char *const nm[4] = {"quarter, half, back (as built)", "half first", "quarter, back, half", "all half turns, then quarters"};
        for (int q = 0; q < 4; ++q) { long tries = 0, steps = 0;
            for (int b = 0; b < 630; b += 7) for (int o = 0; o < 729; o += 5) { int cb = b, co = o, last = 3, d = root_dist(b, o); if (d < 7) continue;
                while (d) { for (int j = 0; j < 9; ++j) { int m = ord[q][j], f = m / 3; if (f == last) continue; int nb = cb, no = co; for (int t = 0; t <= m % 3; ++t) { nb = block_turn[f][nb]; no = orient_turn[f][no]; }
                        ++tries; if (root_dist(nb, no) == d - 1) { cb = nb; co = no; last = f; break; } } --d; ++steps; } }
            printf("walk home, %s: %.2f keys tried a step\n", nm[q], (double) tries / steps); }
    }
    FILE *in = fopen(path, "r"); char text[32]; long measured; int code, n = 0;
    long sum_n = 0, sum_b = 0, worst_n = 0, worst_b = 0, sp = 0, sp3 = 0, wp = 0, wp3 = 0;
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) {
        node_t root; uint16_t vb[3], vo[3]; uint8_t parity, far[3], best = 0; int lead = 0;
        parse(text, &parity); views(text, parity, vb, vo); bound = 0;
        for (int k = 0; k < 3; ++k) { far[k] = root_dist(vb[k], vo[k]); if (far[k] > bound) bound = far[k]; }
        for (int k = 0; k < 3; ++k) { uint8_t key = (far[k] << 4) + far[(k + 1) % 3]; if (key > best) { best = key; lead = k; } }
        for (int j = 0, k = lead; j < 3; ++j, k = (k + 1) % 3) { root.b[j] = vb[k]; root.o[j] = vo[k]; far_s3[j] = far[k]; }
        int b0 = bound; long normal = 0, bnb = 0;
        cost = 0; pops_n = pops3 = 0; tighten = 0;
        for (bound = b0;; ++bound) { cost += 5; if (search_bnb(&root, 0, 3, 0, 0)) break; cost += 25; }
        normal = cost; sp += pops_n; sp3 += pops3;
        if (measured > wp) { wp = measured; wp3 = pops3 * 1000 + pops_n; }
        /* DFBnB: bounds up to 9 as usual, then bound 11 at once, tightened to 10 after the first solution */
        cost = 0; tighten = 0;
        for (bound = b0; bound < 10; ++bound) { cost += 5; if (search_bnb(&root, 0, 3, 0, 0)) break; cost += 25; }
        bound = 11; cost += 5; search_bnb(&root, 0, 3, 0, 1);
        bnb = cost;
        long rest = measured - normal;
        sum_n += normal + rest; sum_b += bnb + rest; if (normal + rest > worst_n) worst_n = normal + rest; if (bnb + rest > worst_b) worst_b = bnb + rest; ++n;
    }
    printf("as built: worst %ld mean %.0f; bounds 10 and 11 in one pass (branch and bound, +60 at the first answer): worst %ld mean %.0f\n", worst_n, (double) sum_n / n, worst_b, (double) sum_b / n);
    printf("pops per state %.1f, after a third turn %.1f; worst state: %ld pops, %ld after a third turn\n", (double) sp / n, (double) sp3 / n, wp3 % 1000, wp3 / 1000);
    fclose(in);
}
enum { NK = 12 };
static const long budgets[NK] = {1L << 40, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 28};
typedef struct {
    char text[16]; long measured, rest, failed;
    int far[3], d[9], pass[9], kd[9][3];
    long prev_kids[9], prev_nodes[9], prev_cost[9]; int prev_depth[9];
    long gen[9];              /* cost to generate and test root child m (no descent) */
    long sub[NK][9];          /* cost under root child m with budget k (from descent to return) */
    int found[NK][9];
    long full_last;           /* model of the normal last iteration */
} rec_t;
static rec_t recs[2700]; static int nrec;
int main(int argc, char **argv)
{
    if (argc > 2) { C_FIRST = 11; C_TEST = 8; }
    uint8_t diameter; uint8_t *table = build_table(&diameter);
    exact = malloc(STATES); memset(exact, 255, STATES); exact[0] = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint32_t trail[16], at = rank; int n = 0;
        while (exact[at] == 255) { state_t s; trail[n++] = at; unrank_state(at, &s); s = apply_move(s, table[at]); at = rank_state(&s); }
        for (int i = n - 1; i >= 0; --i) exact[trail[i]] = exact[at] + n - i;
    }
    FILE *in = fopen(argv[1], "r"); char text[32]; long measured; int code;
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) {
        rec_t *r = &recs[nrec++];
        node_t root; state_t state; uint16_t vb[3], vo[3]; uint8_t parity, far[3], best = 0;
        strcpy(r->text, text); r->measured = measured;
        parse_state(text, &state); parse(text, &parity); views(text, parity, vb, vo);
        bound = lead_view = 0;
        for (int k = 0; k < 3; ++k) { far[k] = root_dist(vb[k], vo[k]); if (far[k] > bound) bound = far[k]; }
        for (int k = 0; k < 3; ++k) { uint8_t key = (far[k] << 4) + far[(k + 1) % 3]; if (key > best) { best = key; lead_view = k; } }
        for (int j = 0, k = lead_view; j < 3; ++j, k = (k + 1) % 3) { root.b[j] = vb[k]; root.o[j] = vo[k]; far_s[j] = far[k]; r->far[j] = far[k]; }
        long total12 = 0, total = 0; /* total12: loop cost with 12/9 for the rest computation */
        int cf = C_FIRST, ct = C_TEST;
        for (;; ++bound) {
            for (int k = 0; k < 3; ++k) root.s[k] = 3 * (bound - far_s[k]) + far_s[k] % 3;
            C_FIRST = 12; C_TEST = 9; cost = 5; nodes_left = 1L << 40; aborted = 0;
            int done = search(&root, 0, 3, 0); if (!done) cost += 25; total12 += cost;
            C_FIRST = cf; C_TEST = ct; cost = 5; nodes_left = 1L << 40; aborted = 0;
            done = search(&root, 0, 3, 0); if (!done) cost += 25; total += cost;
            if (done) { r->full_last = cost; break; }
            r->failed += cost;
        }
        r->rest = measured - total12;
        /* root children at bound 11, each on its own */
        for (int m = 0; m < 9; ++m) {
            int f = m / 3, t = m % 3;
            state_t c = apply_move(state, ((f + lead_view) % 3) * 3 + t);
            node_t child = root; int face = f, k;
            for (k = 0; k < 3; ++k, face = sym_face[face])
                for (int q = 0; q <= t; ++q) { child.b[k] = block_turn[face][child.b[k]]; child.o[k] = orient_turn[face][child.o[k]]; }
            r->d[m] = exact[rank_state(&c)];
            for (k = 0; k < 3; ++k) r->kd[m][k] = root_dist(child.b[k], child.o[k]);
            /* generation cost as in the loop: first, catch (t+1 steps if first passes: approximated by 8), tests */
            r->gen[m] = C_FIRST; r->pass[m] = 1;
            for (k = 0; k < 3; ++k) {
                if (k == 1) r->gen[m] += 8;
                if (k) r->gen[m] += C_TEST;
                child.s[k] = 3 * (10 - r->kd[m][k]) + r->kd[m][k] % 3;
                if (r->kd[m][k] > 10) { r->pass[m] = 0; break; }
            }
            r->gen[m] += r->pass[m] ? 5 : 2;
            { /* the same child in the iteration before: 9 moves left */
                node_t c10 = child; int ok = 1;
                for (k = 0; k < 3; ++k) { if (r->kd[m][k] > 9) ok = 0; c10.s[k] = 3 * (9 - r->kd[m][k]) + r->kd[m][k] % 3; }
                r->prev_kids[m] = r->prev_nodes[m] = r->prev_cost[m] = 0; r->prev_depth[m] = 0;
                if (ok) { cost = 0; children = 0; nodes_seen = 0; max_depth = 0; nodes_left = 1L << 40; aborted = 0; bound = 10;
                    search(&c10, 1, f, 1); r->prev_kids[m] = children; r->prev_nodes[m] = nodes_seen; r->prev_cost[m] = cost; r->prev_depth[m] = max_depth; }
            }
            for (int bi = 0; bi < NK; ++bi) {
                r->sub[bi][m] = 0; r->found[bi][m] = 0;
                if (!r->pass[m]) continue;
                cost = 14 + (f == 0); nodes_left = budgets[bi]; aborted = 0; bound = 11;
                int done = search(&child, 1, f, 1);
                if (!done && !aborted) cost += 15 + f + 2;
                r->sub[bi][m] = cost; r->found[bi][m] = done;
            }
        }
    }
    if (getenv("DUMP")) { for (int i = 0; i < nrec; ++i) { rec_t *r = &recs[i];
        printf("%s tot %ld failed %ld last %ld far %d%d%d |", r->text, r->rest + r->failed + r->full_last, r->failed, r->full_last, r->far[0], r->far[1], r->far[2]);
        for (int m = 0; m < 9; ++m) printf(" %d:%d%d%d:%s%ld", r->d[m], r->kd[m][0], r->kd[m][1], r->kd[m][2], !r->pass[m] ? "x" : r->found[0][m] ? "F" : "-", r->sub[0][m]);
        printf("\n"); } return 0; }
    /* policies */
    #define EVAL(name, expr_cost) do { long worst = 0, sum = 0; const char *wt = ""; \
        for (int i = 0; i < nrec; ++i) { rec_t *r = &recs[i]; long last = (expr_cost); long tot = r->rest + r->failed + last; \
            sum += tot; if (tot > worst) { worst = tot; wt = r->text; } } \
        printf("%-64s worst %6ld (%s) mean %6.0f\n", name, worst, wt, (double) sum / nrec); } while (0)
    long tmp;
    EVAL("normal order (model of the loop as built)", r->full_last);
    /* sequential by root child, natural order, composed from parts (check of the decomposition) */
    #define SEQ(order_expr, extra) ({ long c = 5 + (extra); int ord[9]; order_expr; \
        for (int j = 0; j < 9; ++j) { int m = ord[j]; c += r->gen[m] + (j % 3 == 0 ? 9 : 0); if (!r->pass[m]) continue; c += r->sub[0][m]; if (r->found[0][m]) break; } c; })
    EVAL("natural order, composed from parts", SEQ(for (int j = 0; j < 9; ++j) ord[j] = j, 0));
    EVAL("oracle at the root: first child at distance 10", SEQ(({ int n = 0; for (int j = 0; j < 9; ++j) if (r->d[j] == 10) ord[n++] = j; for (int j = 0; j < 9; ++j) if (r->d[j] != 10) ord[n++] = j; }), 0));
    EVAL("oracle at the root: cheapest child that finds", ({ long best = 1L << 40; for (int m = 0; m < 9; ++m) if (r->pass[m] && r->found[0][m] && r->sub[0][m] < best) best = r->sub[0][m]; best + 5 + 9 * 20; }));
    /* order by a key over the three key distances */
    #define BYKEY(keyexpr) ({ int n = 0, used[9] = {0}; for (int j = 0; j < 9; ++j) { int bm = -1; long bk = 1L << 40; \
        for (int m = 0; m < 9; ++m) { if (used[m]) continue; int *kd = r->kd[m]; long key = (keyexpr); if (key < bk) { bk = key; bm = m; } } used[bm] = 1; ord[n++] = bm; } })
    EVAL("all nine by least sum of key distances (+350)", SEQ(BYKEY(kd[0] + kd[1] + kd[2]), 350));
    EVAL("all nine by largest sum (+350)", SEQ(BYKEY(-(kd[0] + kd[1] + kd[2])), 350));
    EVAL("all nine by least max, then least sum (+350)", SEQ(BYKEY(100 * (kd[0] > kd[1] ? (kd[0] > kd[2] ? kd[0] : kd[2]) : (kd[1] > kd[2] ? kd[1] : kd[2])) + kd[0] + kd[1] + kd[2]), 350));
    EVAL("all nine by least first view, then sum (+350)", SEQ(BYKEY(100 * kd[0] + kd[1] + kd[2]), 350));
    EVAL("all nine by least min, then sum (+350)", SEQ(BYKEY(100 * (kd[0] < kd[1] ? (kd[0] < kd[2] ? kd[0] : kd[2]) : (kd[1] < kd[2] ? kd[1] : kd[2])) + kd[0] + kd[1] + kd[2]), 350));
    /* first = least sum, then natural order from the start (rerun) */
    EVAL("first by least sum, then the natural order again (+350)", ({ int bm = 0, bs = 99; for (int m = 0; m < 9; ++m) { int s = r->kd[m][0] + r->kd[m][1] + r->kd[m][2]; if (r->pass[m] && s < bs) { bs = s; bm = m; } } \
        r->found[0][bm] ? 350 + r->sub[0][bm] : 350 + r->sub[0][bm] + r->full_last; }));
    /* statistics of the predictor */
    { int good = 0, tot = 0; long hist[2][40] = {{0}};
      for (int i = 0; i < nrec; ++i) { rec_t *r = &recs[i]; int bm = 0, bs = 99;
        for (int m = 0; m < 9; ++m) { int s = r->kd[m][0] + r->kd[m][1] + r->kd[m][2]; ++hist[r->d[m] == 11][s]; if (r->pass[m] && s < bs) { bs = s; bm = m; } }
        good += r->d[bm] == 10; ++tot; }
      printf("least-sum child is at distance 10 in %d of %d states\n", good, tot);
      printf("sum of key distances of the 9 children: sum: at distance 10 / at distance 11\n");
      for (int s = 18; s <= 30; ++s) if (hist[0][s] + hist[1][s]) printf("  %d: %ld / %ld\n", s, hist[0][s], hist[1][s]); }
    EVAL("one turn of each face first: R B D, R2 B2 D2, R' B' D' (+150)", SEQ(({ static const int o2[9] = {0,3,6,1,4,7,2,5,8}; for (int j = 0; j < 9; ++j) ord[j] = o2[j]; }), 150));
    EVAL("half turns first: R2 B2 D2, R B D, R' B' D' (+150)", SEQ(({ static const int o2[9] = {1,4,7,0,3,6,2,5,8}; for (int j = 0; j < 9; ++j) ord[j] = o2[j]; }), 150));
    EVAL("faces D B R at the root only (+20)", SEQ(({ static const int o2[9] = {6,7,8,3,4,5,0,1,2}; for (int j = 0; j < 9; ++j) ord[j] = o2[j]; }), 20));
    EVAL("back turns first at the root: R' R2 R ... (+20)", SEQ(({ static const int o2[9] = {2,1,0,5,4,3,8,7,6}; for (int j = 0; j < 9; ++j) ord[j] = o2[j]; }), 20));
    { long pos[9] = {0}; for (int i = 0; i < nrec; ++i) for (int m = 0; m < 9; ++m) pos[m] += recs[i].d[m] == 11;
      printf("first moves that lead to a distance-11 state, by place in the order R R2 R' B B2 B' D D2 D':"); for (int m = 0; m < 9; ++m) printf(" %ld", pos[m]); printf("\n"); }
    #define BYREC(keyexpr) ({ int n = 0, used[9] = {0}; for (int j = 0; j < 9; ++j) { int bm = -1; long bk = 1L << 40; \
        for (int m = 0; m < 9; ++m) { if (used[m]) continue; long key = (keyexpr); if (key < bk) { bk = key; bm = m; } } used[bm] = 1; ord[n++] = bm; } })
    EVAL("all nine by most children in the iteration before (+400)", SEQ(BYREC(-r->prev_kids[m]), 400));
    EVAL("all nine by fewest children in the iteration before (+400)", SEQ(BYREC(r->prev_kids[m]), 400));
    EVAL("all nine by most nodes in the iteration before (+400)", SEQ(BYREC(-r->prev_nodes[m]), 400));
    EVAL("all nine by deepest node in the iteration before (+400)", SEQ(BYREC(-r->prev_depth[m]), 400));
    EVAL("all nine by deepest, then fewest children (+400)", SEQ(BYREC(-1000000L * r->prev_depth[m] + r->prev_kids[m]), 400));
    EVAL("all nine by deepest, then most children (+400)", SEQ(BYREC(-1000000L * r->prev_depth[m] - r->prev_kids[m]), 400));
    { long good[4] = {0}; long dh[2][12] = {{0}};
      for (int i = 0; i < nrec; ++i) { rec_t *r = &recs[i]; int a = 0, b = 0, c = 0;
        for (int m = 1; m < 9; ++m) { if (r->prev_kids[m] > r->prev_kids[a]) a = m; if (r->prev_depth[m] > r->prev_depth[b]) b = m; if (r->prev_nodes[m] > r->prev_nodes[c]) c = m; }
        for (int m = 0; m < 9; ++m) ++dh[r->d[m] == 11][r->prev_depth[m]];
        good[0] += r->d[a] == 10; good[1] += r->d[b] == 10; good[2] += r->d[c] == 10; }
      printf("child with most children / deepest node / most nodes before is at distance 10 in %ld / %ld / %ld of %d\n", good[0], good[1], good[2], nrec);
      printf("deepest node under a root child in the iteration before: depth: at distance 10 / 11\n");
      for (int d = 0; d < 12; ++d) if (dh[0][d] + dh[1][d]) printf("  %d: %ld / %ld\n", d, dh[0][d], dh[1][d]); }
    for (int bi = 1; bi < NK; ++bi) {
        char name[128]; snprintf(name, sizeof name, "budget %ld pops a first move, then no budget (+2 a pop)", budgets[bi]);
        EVAL(name, ({ long c = 5; int ok = 0; for (int m = 0; m < 9 && !ok; ++m) { c += r->gen[m] + (m % 3 == 0 ? 9 : 0); if (!r->pass[m]) continue; c += r->sub[bi][m] + 4; ok = r->found[bi][m]; } if (!ok) c += r->full_last + 25; c + 2 * (r->failed + r->full_last) / 190; }));
        snprintf(name, sizeof name, "budget %ld pops, then only the first moves that were cut", budgets[bi]);
        EVAL(name, ({ long c = 5; int ok = 0; for (int m = 0; m < 9 && !ok; ++m) { c += r->gen[m] + (m % 3 == 0 ? 9 : 0); if (!r->pass[m]) continue; c += r->sub[bi][m] + 4; ok = r->found[bi][m]; }
            if (!ok) { c += 30; for (int m = 0; m < 9 && !ok; ++m) { c += r->gen[m] + (m % 3 == 0 ? 9 : 0); if (!r->pass[m]) continue; if (r->sub[bi][m] == r->sub[0][m] && !r->found[0][m]) continue; c += r->sub[0][m]; ok = r->found[0][m]; } }
            c + 2 * (r->failed + r->full_last) / 190; }));
    }
    for (int bi = 1; bi + 3 < NK; ++bi) {
        char name[128]; snprintf(name, sizeof name, "budgets %ld, %ld pops in turn, then no budget", budgets[bi], budgets[bi + 3]);
        EVAL(name, ({ long c = 0; int ok = 0; int seq[2] = {bi, bi + 3}; for (int q = 0; q < 2 && !ok; ++q) { c += 5; for (int m = 0; m < 9 && !ok; ++m) { c += r->gen[m] + (m % 3 == 0 ? 9 : 0); if (!r->pass[m]) continue; c += r->sub[seq[q]][m] + 4; ok = r->found[seq[q]][m]; } if (!ok) c += 25; } if (!ok) c += r->full_last; c + 2 * (r->failed + r->full_last) / 190; }));
    }
    more(argv[1]);
    return 0;
}

