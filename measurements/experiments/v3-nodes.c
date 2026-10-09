/* Host model of the v3 search loop (rubik.s at c11831f): where the retired
 * instructions go. It walks the same tree in the same order as the assembly
 * and adds the length of every basic block it passes, split by what the
 * block is for:
 *   child    3 turn lookups and the pattern test of every child      12
 *   orient   the orientation test                                     3
 *   view     the two rotated lookups: 10 + 4k each for turn k, 2 to
 *            store a rotated child that passed, li and j at the end
 *   node     everything a node costs beyond its children: or + beqz,
 *            the spill (16), enter (1), the face loop (16; 22 at the
 *            root), the pop (16, 18 or 19 by the turn it resumes at)
 *   bound    8 per iteration, 3 to deepen, 6 at found
 * The rest of a run (parse, rotate, the set-up of solve, print, replay) is
 * not modelled: it is the difference to the measured count, printed as
 * "rest", and it must be nearly constant for the model to be right.
 * It includes the v3 ida.c and tables.h: build it at the commit that added
 * it. Output: v3-nodes.txt.
 *   cc -O2 -w measurements/experiments/v3-nodes.c -o v3-nodes
 *   ./v3-nodes measurements/sweep-v3.txt
 */
#define NO_MAIN
#include "../../ida.c"
#include <stdio.h>
#include <string.h>

enum { CHILD, ORIENT, VIEW, NODE, BOUND, KINDS };
static const char *const kind_name[KINDS] = {"child", "orient", "view", "node",
                                             "bound"};
static long cost[KINDS], generated, expanded, by_depth[MAX_DEPTH + 1];
static long first_pruned, second_pruned, view_pruned[2];
static int bound;

typedef struct {
    uint16_t p, o, c, vp[2], vc[2];
} node_t;

static int search(const node_t *n, int depth, int parent)
{
    int rem = bound - depth - 1;
    ++expanded;
    ++by_depth[depth];
    cost[NODE] += 1; /* enter */
    for (int f = -1;;) {
        ++f;
        cost[NODE] += 2; /* addi, bne */
        if (f == parent) {
            ++f;
            cost[NODE] += 1;
        }
        cost[NODE] += 1; /* bgeu */
        if (f >= 3)
            return 0;
        cost[NODE] += 3; /* the three table addresses */
        node_t child = *n;
        for (int k = 1; k <= 3; ++k) {
            child.p = perm_turn[f][child.p];
            child.o = orient_turn[f][child.o];
            child.c = pair_turn[f][child.c];
            ++generated;
            cost[CHILD] += 12;
            if (pattern_h(child.p, child.c) > rem) {
                ++first_pruned;
                continue;
            }
            cost[ORIENT] += 3;
            if (orient_dist[child.o] > rem) {
                ++second_pruned;
                continue;
            }
            int cut = 0;
            for (int v = 0; v < 2 && !cut; ++v) {
                uint8_t face = v ? sym_face[sym_face[f]] : sym_face[f];
                uint16_t p = n->vp[v], c = n->vc[v];
                for (int t = 0; t < k; ++t) {
                    p = perm_turn[face][p];
                    c = pair_turn[face][c];
                }
                cost[VIEW] += 10 + 4 * k;
                if (pattern_h(p, c) > rem) {
                    ++view_pruned[v];
                    cut = 1;
                    break;
                }
                cost[VIEW] += 2; /* store the rotated child */
                child.vp[v] = p;
                child.vc[v] = c;
            }
            if (cut)
                continue;
            cost[VIEW] += 1 + (k < 3); /* li t4, j passed */
            cost[NODE] += 2;           /* or, beqz */
            if (!(child.p | child.o)) {
                cost[BOUND] += 6;
                return 1;
            }
            cost[NODE] += 16; /* spill and descend */
            if (search(&child, depth + 1, f))
                return 1;
            cost[NODE] += 15 + (k == 3 ? 1 : k == 2 ? 3 : 4); /* pop */
        }
    }
}

static long model(const char *text)
{
    node_t root;
    if (!parse(text, &root.p, &root.o, &root.c))
        return -1;
    views(text, root.vp, root.vc);
    memset(cost, 0, sizeof cost);
    memset(by_depth, 0, sizeof by_depth);
    generated = expanded = first_pruned = second_pruned = 0;
    view_pruned[0] = view_pruned[1] = 0;
    bound = pattern_h(root.p, root.c);
    if (orient_dist[root.o] > bound)
        bound = orient_dist[root.o];
    for (int v = 0; v < 2; ++v)
        if (pattern_h(root.vp[v], root.vc[v]) > bound)
            bound = pattern_h(root.vp[v], root.vc[v]);
    for (;; ++bound) {
        cost[BOUND] += 8;
        if (search(&root, 0, 3))
            break;
        cost[BOUND] += 3;
    }
    long total = 0;
    for (int k = 0; k < KINDS; ++k)
        total += cost[k];
    return total;
}

static void report(const char *text, long measured)
{
    long total = model(text);
    printf("%s length=%d generated=%ld expanded=%ld pruned: first=%ld "
           "second=%ld view=%ld+%ld\n",
           text, bound, generated, expanded, first_pruned, second_pruned,
           view_pruned[0], view_pruned[1]);
    printf("  expanded by depth:");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %ld", by_depth[d]);
    printf("\n  instructions:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%ld (%.1f%%)", kind_name[k], cost[k],
               100.0 * cost[k] / total);
    printf("\n  model=%ld measured=%ld rest=%ld; %.1f per child, %.1f per "
           "node\n",
           total, measured, measured - total, (double) total / generated,
           (double) total / expanded);
}

int main(int argc, char **argv)
{
    char text[32];
    long measured, sum[KINDS] = {0}, all = 0, gen = 0, exp = 0, states = 0;
    long rest_min = 1L << 30, rest_max = -1, worst = 0;
    long depth_sum[MAX_DEPTH + 1] = {0};
    char worst_text[32] = "";
    int code;
    FILE *in = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (!in) {
        fprintf(stderr, "usage: %s sweep-file\n", argv[0]);
        return 2;
    }
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) {
        long total = model(text), rest = measured - total;
        if (total < 0)
            return 1;
        ++states;
        all += total;
        gen += generated;
        exp += expanded;
        for (int k = 0; k < KINDS; ++k)
            sum[k] += cost[k];
        for (int d = 0; d < MAX_DEPTH; ++d)
            depth_sum[d] += by_depth[d];
        if (rest < rest_min)
            rest_min = rest;
        if (rest > rest_max)
            rest_max = rest;
        if (measured > worst) {
            worst = measured;
            strcpy(worst_text, text);
        }
    }
    fclose(in);
    printf("%ld states: generated mean %.0f, expanded mean %.0f, model mean "
           "%.0f\n",
           states, (double) gen / states, (double) exp / states,
           (double) all / states);
    printf("  rest (measured - model): %ld to %ld\n", rest_min, rest_max);
    printf("  instructions, mean:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%.0f (%.1f%%)", kind_name[k], (double) sum[k] / states,
               100.0 * sum[k] / all);
    printf("\n  %.2f per child, %.1f per node\n", (double) all / gen,
           (double) all / exp);
    printf("  expanded by depth, mean:");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %.1f", (double) depth_sum[d] / states);
    printf("\n");
    report(worst_text, worst);
    return 0;
}
