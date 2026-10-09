/* Host count for bounds 10 and 11 in one pass (TASK.md, N8), with the
 * loop of r16: the search runs at bound 11, and after the first answer it
 * goes on at bound 10, to find a shorter answer or none. The first answer
 * is charged 120 instructions for keeping the path and tightening the
 * slots.
 *   cc -O2 -w measurements/experiments/r16-bnb.c -o r16-bnb
 *   ./r16-bnb measurements/sweep-r16.txt
 * Its model of the loop of r16 is approximate; it builds from r15 on.
 */
#define NO_MAIN
#include "../../ida.c"
#include <stdio.h>
#include <string.h>
typedef struct { uint16_t b[VIEWS], o[VIEWS]; } node_t;
static long cost; static int bound, tighten;
static uint8_t dist_of(uint16_t b, uint16_t o) { static uint8_t memo[630][729]; static char have[630][729]; if (!have[b][o]) { memo[b][o] = root_dist(b, o); have[b][o] = 1; } return memo[b][o]; }
/* returns 1 found (final), 2 found the first answer in branch-and-bound mode, 0 failed */
static int search(const node_t *n, int depth, int parent, int entered, int bnb)
{
    int first = 1;
    for (int f = 0; f < 3; ++f) {
        if (f == parent) { if (!(entered && first)) cost += 1; continue; }
        cost += entered && first ? 1 : 8; first = 0;
        node_t ch = *n; int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f, d[3], left = bound - tighten - depth - 1;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) { ch.b[k] = block_turn[face][ch.b[k]]; ch.o[k] = orient_turn[face][ch.o[k]]; }
            ++behind; cost += 11;
            for (k = 0; k < VIEWS; ++k) { if (k == 1) { cost += 2 + 8 * behind; behind = 0; } if (k) cost += 8; d[k] = dist_of(ch.b[k], ch.o[k]); if (d[k] > left) break; }
            if (k < VIEWS) { if (k) cost += 1; continue; }
            int tight = d[0] == left && d[1] == left && d[2] == left;
            cost += 1; if (d[2] == left) cost += 2 + tight;
            if (tight && left == 0) { if (!bnb || tighten) return 1; tighten = 1; cost += 120; return 2; }
            if (tight) { cost += 1; continue; }
            cost += 2 + 17 + (f == 0);
            int r = search(&ch, depth + 1, f, 1, bnb);
            if (r == 1) return 1;
            cost += 14;
            if (r == 2) { /* is this node still alive one move tighter? its own keys against the moves it has left */
                int alive = 1, own = bound - tighten - depth, all = 1;
                for (k = 0; k < VIEWS; ++k) { int dk = dist_of(n->b[k], n->o[k]); if (dk > own) alive = 0; if (dk != own) all = 0; }
                if (depth && (!alive || all)) return 2;
                if (!depth && !alive) return 2; }
        }
    }
    return 0;
}
int main(int argc, char **argv)
{
    FILE *in = fopen(argv[1], "r"); char text[32]; long measured; int code, n = 0;
    long sum_n = 0, sum_b = 0, worst_n = 0, worst_b = 0, sum_m = 0, worst_m = 0; char wb[32] = "";
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) {
        node_t root; uint16_t vb[3], vo[3]; uint8_t far[3], best = 0; int lead = 0, b0 = 0;
        views(text, vb, vo);
        for (int k = 0; k < 3; ++k) { far[k] = root_dist(vb[k], vo[k]); if (far[k] > b0) b0 = far[k]; }
        for (int k = 0; k < 3; ++k) { uint8_t key = (far[k] << 4) + far[(k + 1) % 3]; if (key > best) { best = key; lead = k; } }
        for (int j = 0, k = lead; j < 3; ++j, k = (k + 1) % 3) { root.b[j] = vb[k]; root.o[j] = vo[k]; }
        if (far[0] == far[1] && far[1] == far[2]) ++b0;
        cost = 0; tighten = 0;
        for (bound = b0;; ++bound) { cost += 5; if (search(&root, 0, 3, 0, 0)) break; cost += 20; }
        long normal = cost;
        cost = 0; tighten = 0;
        for (bound = b0; bound < 10; ++bound) { cost += 5; if (search(&root, 0, 3, 0, 0)) break; cost += 20; }
        bound = 11; cost += 5; search(&root, 0, 3, 0, 1);
        long bnb = cost, rest = measured - normal;
        sum_m += measured; if (measured > worst_m) worst_m = measured;
        sum_n += normal + rest; sum_b += bnb + rest; if (bnb + rest > worst_b) { worst_b = bnb + rest; strcpy(wb, text); } ++n;
        if (argc > 2) printf("%s %ld %ld %ld\n", text, measured, normal, bnb);
    }
    printf("r16 measured: worst %ld mean %.0f; bounds 10 and 11 in one pass (+120 at the first answer): worst %ld (%s) mean %.0f\n", worst_m, (double) sum_m / n, worst_b, wb, (double) sum_b / n);
    return 0;
}
