/* Host count behind r14 (TASK.md, N16): what the loop of r13 costs when a
 * child that passes with no slack in any of the three views is cut, and
 * when three equal key distances at the root start the bound one higher.
 * search() is the loop of r13 with the cost of every block; the rule is
 * charged 3 instructions for every child that passes. Each line is the
 * worst and the mean over the 2,644 states of (measured - loop of r13 +
 * loop of the variant).
 *   cc -O2 -w measurements/experiments/r13-rule.c -o r13-rule
 *   ./r13-rule measurements/sweep-r13.txt
 * It needs the tables of r12 or later and reads ida.c only for the
 * coordinates and the key distances.
 */
#define NO_MAIN
#define COUNT_OPS
#include "../../ida.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { uint16_t b[VIEWS], o[VIEWS]; } node_t;
static long cost, children, nodes, tightcut;
static int bound, RULE, CF = 11, CT = 8, XTRA = 3;
static int search(const node_t *n, int depth, int parent, int entered)
{
    int first = 1, left = bound - depth - 1;
    ++nodes;
    for (int f = 0; f < 3; ++f) {
        if (f == parent) { if (!(entered && first)) cost += 1; continue; }
        cost += entered && first ? 2 : 9; first = 0;
        node_t ch = *n; int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f, d[3];
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) { ch.b[k] = block_turn[face][ch.b[k]]; ch.o[k] = orient_turn[face][ch.o[k]]; }
            ++behind; ++children; cost += CF;
            for (k = 0; k < VIEWS; ++k) { if (k == 1) { cost += 8 * behind; behind = 0; } if (k) cost += CT; d[k] = root_dist(ch.b[k], ch.o[k]); if (d[k] > left) break; }
            if (k < VIEWS) { cost += 2; continue; }
            cost += 5; if (!left) return 1;
            if (RULE) { cost += XTRA; if (d[0] == left && d[1] == left && d[2] == left) { ++tightcut; cost += 2; continue; } }
            cost += 14 + (f == 0); if (search(&ch, depth + 1, f, 1)) return 1; cost += 17 + f;
        }
    }
    return 0;
}
int main(int argc, char **argv)
{
    FILE *in = fopen(argv[1], "r"); char text[32]; long measured; int code;
    static char st[2700][16]; static long meas[2700]; int n = 0;
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) { strcpy(st[n], text); meas[n++] = measured; }
    long base[2700];
    for (RULE = 0; RULE < 3; ++RULE) {
        long worst = 0, sumc = 0, sumk = 0, sumn = 0, cut = 0, bumped = 0; const char *wt = "";
        for (int i = 0; i < n; ++i) {
            node_t root; uint16_t vb[3], vo[3]; uint8_t far[3], best = 0; int lead = 0;
            views(st[i], vb, vo); bound = 0;
            for (int k = 0; k < 3; ++k) { far[k] = root_dist(vb[k], vo[k]); if (far[k] > bound) bound = far[k]; }
            for (int k = 0; k < 3; ++k) { uint8_t key = (far[k] << 4) + far[(k + 1) % 3]; if (key > best) { best = key; lead = k; } }
            for (int j = 0, k = lead; j < 3; ++j, k = (k + 1) % 3) { root.b[j] = vb[k]; root.o[j] = vo[k]; }
            /* the rule at the root: all three equal => start one bound higher */
            if (RULE == 2 && far[0] == far[1] && far[1] == far[2]) { ++bound; ++bumped; }
            cost = 0; children = 0; nodes = 0; tightcut = 0;
            for (;; ++bound) { cost += 5; if (search(&root, 0, 3, 0)) break; cost += 25; }
            if (!RULE) base[i] = cost;
            long tot = meas[i] - base[i] + cost;
            sumc += tot; sumk += children; sumn += nodes; cut += tightcut;
            if (tot > worst) { worst = tot; wt = st[i]; }
            if (getenv("PER") && RULE == 2) printf("%s %ld %ld %ld\n", st[i], tot, children, nodes);
        }
        printf("%s: worst %ld (%s) mean %.0f, children %.1f, nodes %.1f, cut by the rule %.1f a state, %ld states start a bound higher\n",
               RULE == 0 ? "r13 loop" : RULE == 1 ? "rule in the search (+3 a passing child)" : "rule in the search and at the root", worst, wt, (double) sumc / n, (double) sumk / n, (double) sumn / n, (double) cut / n, bumped);
    }
    return 0;
}
