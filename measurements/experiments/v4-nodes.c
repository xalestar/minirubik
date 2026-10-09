/* Host model of the v4 search loop: where the retired instructions of
 * rubik.s go. It walks the same tree in the same order as the assembly and
 * adds the length of every basic block it passes, split by what the block
 * is for:
 *   turn   the 3 views turned once, and the turn counter              9 + 2
 *   test   the tests of the views a child reaches                     9 each
 *   node   everything a node costs beyond its children: the face
 *          blocks (1 skipped, 8 entered), passed (5), the descent
 *          (15), the pop (15 to 17)
 *   bound  5 per iteration, 7 to deepen, 4 at found
 * The rest of a run (parse, coords, rotate, the three walks home, print,
 * replay) is not modelled: it is the difference to the measured count,
 * printed as "rest" with the keys tried on the walks home.
 *   cc -O2 -w measurements/experiments/v4-nodes.c -o v4-nodes
 *   ./v4-nodes measurements/sweep-v4.txt
 */
#define NO_MAIN
#define COUNT_OPS
#include "../../ida.c"
#include <stdio.h>
#include <string.h>

enum { TURN, TEST, NODE, BOUND, KINDS };
static const char *const kind_name[KINDS] = {"turn", "test", "node", "bound"};
static long cost[KINDS], children, nodes, by_depth[MAX_DEPTH + 1];
static long cut[VIEWS], reached[VIEWS];
static int bound;

typedef struct {
    uint16_t b[VIEWS], o[VIEWS];
    int8_t s[VIEWS];
} node_t;

static int search(const node_t *n, int depth, int parent)
{
    ++nodes;
    ++by_depth[depth];
    for (int f = 0; f < 3; ++f) {
        cost[NODE] += 1; /* the skip test */
        if (f == parent)
            continue;
        cost[NODE] += 7; /* load the node, li */
        node_t child = *n;
        for (int t = 0; t < 3; ++t) {
            int k, face = f;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) {
                child.b[k] = block_turn[face][child.b[k]];
                child.o[k] = orient_turn[face][child.o[k]];
            }
            ++children;
            cost[TURN] += 11;
            for (k = 0; k < VIEWS; ++k) {
                ++reached[k];
                cost[TEST] += 9;
                child.s[k] =
                    next_state[n->s[k]][mod3(child.b[k], child.o[k])];
                if (child.s[k] < 0)
                    break;
            }
            if (k < VIEWS) {
                ++cut[k];
                continue;
            }
            cost[NODE] += 5; /* li, j, sb, sb, beq */
            if (depth + 1 == bound) {
                cost[BOUND] += 4;
                return 1;
            }
            cost[NODE] += 15; /* descend */
            if (search(&child, depth + 1, f))
                return 1;
            cost[NODE] += 15 + f; /* pop, and 1 to 3 to find the face */
        }
    }
    return 0;
}

static long model(const char *text)
{
    node_t root;
    uint8_t parity, dist[VIEWS];
    if (!parse(text, &parity))
        return -1;
    views(text, parity, root.b, root.o);
    memset(cost, 0, sizeof cost);
    memset(by_depth, 0, sizeof by_depth);
    memset(cut, 0, sizeof cut);
    memset(reached, 0, sizeof reached);
    children = nodes = bound = 0;
    root_tries = 0;
    for (int k = 0; k < VIEWS; ++k) {
        dist[k] = root_dist(root.b[k], root.o[k]);
        if (dist[k] > bound)
            bound = dist[k];
    }
    for (;; ++bound) {
        cost[BOUND] += 5;
        for (int k = 0; k < VIEWS; ++k)
            root.s[k] = (int8_t) (3 * (bound - dist[k]) + dist[k] % 3);
        if (search(&root, 0, 3))
            break;
        cost[BOUND] += 1 + 7; /* the pop that finds the root, deepen */
    }
    long total = 0;
    for (int k = 0; k < KINDS; ++k)
        total += cost[k];
    return total;
}

static void report(const char *text, long measured)
{
    long total = model(text);
    printf("%s length=%d children=%ld nodes=%ld cut by view: %ld %ld %ld\n",
           text, bound, children, nodes, cut[0], cut[1], cut[2]);
    printf("  nodes by depth:");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %ld", by_depth[d]);
    printf("\n  instructions:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%ld (%.1f%%)", kind_name[k], cost[k],
               100.0 * cost[k] / measured);
    printf("\n  model=%ld measured=%ld rest=%ld (%.1f%%), %llu keys tried on "
           "the walks home\n",
           total, measured, measured - total,
           100.0 * (measured - total) / measured,
           (unsigned long long) root_tries);
}

int main(int argc, char **argv)
{
    char text[32], worst_text[32] = "";
    long measured, sum[KINDS] = {0}, all = 0, got = 0, kids = 0, exp = 0;
    long states = 0, rest_min = 1L << 30, rest_max = -1, worst = 0;
    long tries = 0, view[VIEWS] = {0};
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
        got += measured;
        kids += children;
        exp += nodes;
        tries += (long) root_tries;
        for (int k = 0; k < KINDS; ++k)
            sum[k] += cost[k];
        for (int k = 0; k < VIEWS; ++k)
            view[k] += reached[k];
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
    printf("%ld states: children mean %.0f, nodes mean %.0f, measured mean "
           "%.0f\n",
           states, (double) kids / states, (double) exp / states,
           (double) got / states);
    printf("  views reached per child: %.2f %.2f %.2f\n",
           (double) view[0] / kids, (double) view[1] / kids,
           (double) view[2] / kids);
    printf("  instructions, mean:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%.0f (%.1f%%)", kind_name[k], (double) sum[k] / states,
               100.0 * sum[k] / got);
    printf("\n  rest (measured - model): %ld to %ld, mean %.0f (%.1f%%); "
           "%.0f keys tried on the walks home\n",
           rest_min, rest_max, (double) (got - all) / states,
           100.0 * (got - all) / got, (double) tries / states);
    report(worst_text, worst);
    return 0;
}
