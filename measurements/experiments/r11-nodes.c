/* Host model of the search loop of rubik.s at r11: where the retired
 * instructions go, by block of code, by iteration and by depth.
 *
 * search() walks the same tree in the same order as the assembly and adds
 * the length of every basic block it passes, from `iteration` to the jump to
 * `found`:
 *   first   the first view turned and tested                      12 a child
 *   next    the turn counter after a child that is cut, or a pop    2
 *   catch   both rotated views turned once                          8 a step
 *   test    the test of a rotated view                              9 each
 *   node    face heads (1 skipped, 9 entered, 2 for the first face
 *           of a node that was just a child), passed (5), the
 *           descent (14, 15 after an R), the pop (15 + face)
 *   bound   5 an iteration, 7 to deepen, 18 for the pop of the root
 * The rest of a run (parse, coords, rotate, the three walks home, print,
 * replay) is the measured count minus the model, printed as "rest".
 *
 * It also asks the exact distances of solver.c two things about the last
 * iteration (bound 11): how many children of the root are at distance 10,
 * and what the loop would cost if it descended only into children that are
 * one move closer (the oracle), in the same order of faces and turns.
 *   cc -O2 -w measurements/experiments/r11-nodes.c -o r11-nodes
 *   ./r11-nodes measurements/sweep-r11.txt [STATE...]
 */
#define main solver_main
#include "../../solver.c"
#undef main
#define NO_MAIN
#define COUNT_OPS
#include "../../ida.c"

enum { FIRST, NEXT, CATCH, TEST, NODE, BOUND, KINDS };
static const char *const kind_name[KINDS] = {"first", "next", "catch",
                                             "test",  "node", "bound"};
typedef struct {
    uint16_t b[VIEWS], o[VIEWS];
    int8_t s[VIEWS];
} node_t;

static long cost[KINDS], children, nodes, reached[VIEWS], cut[VIEWS];
static long by_depth[MAX_DEPTH + 1], kids_by_depth[MAX_DEPTH + 1];
static long iter_children[MAX_DEPTH + 2], iter_cost[MAX_DEPTH + 2];
static long root_child_cost[9], root_child_kids[9];
static int bound, lead_view, far_as_searched[VIEWS];
static uint8_t *exact; /* distance of every state, by rank */

static long total(void)
{
    long t = 0;
    for (int k = 0; k < KINDS; ++k)
        t += cost[k];
    return t;
}

/* oracle: descend only into a child whose state is one move closer. The
 * state is carried next to the views, turned in the frame of the search.
 */
static int search(const node_t *n, state_t state, int depth, int parent,
                  int entered, int oracle)
{
    int first = 1;
    ++nodes;
    ++by_depth[depth];
    for (int f = 0; f < 3; ++f) {
        if (f == parent) {
            if (!(entered && first))
                cost[NODE] += 1;
            continue;
        }
        cost[NODE] += entered && first ? 2 : 9;
        first = 0;
        node_t child = *n;
        state_t turned = state;
        int behind = 0;
        for (int t = 1; t <= 3; ++t) {
            int k, face = f;
            long before = total(), kids_before = children;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) {
                child.b[k] = block_turn[face][child.b[k]];
                child.o[k] = orient_turn[face][child.o[k]];
            }
            /* the face of the cube as given: add lead_view, as found does */
            turned = quarter_turn(turned, (uint8_t) ((f + lead_view) % 3));
            ++behind;
            ++children;
            ++kids_by_depth[depth];
            cost[FIRST] += 12;
            for (k = 0; k < VIEWS; ++k) {
                if (k == 1) {
                    cost[CATCH] += 8 * behind;
                    behind = 0;
                }
                if (k)
                    cost[TEST] += 9;
                ++reached[k];
                child.s[k] =
                    next_state[n->s[k]][mod3(child.b[k], child.o[k])];
                if (child.s[k] < 0)
                    break;
            }
            if (k < VIEWS) {
                ++cut[k];
                cost[NEXT] += 2;
            } else {
                int closer = exact[rank_state(&turned)] == bound - depth - 1;
                cost[NODE] += 5;
                if (depth + 1 == bound)
                    return 1;
                if (!oracle || closer) {
                    cost[NODE] += 14 + (f == 0);
                    if (search(&child, turned, depth + 1, f, 1, oracle))
                        return 1;
                    cost[NODE] += 15 + f;
                }
                cost[NEXT] += 2;
            }
            if (!depth) {
                root_child_cost[3 * f + t - 1] = total() - before;
                root_child_kids[3 * f + t - 1] = children - kids_before;
            }
        }
    }
    return 0;
}

/* The loop of one state; oracle applies to the last iteration only. */
static long model(const char *text, int oracle)
{
    node_t root;
    state_t state;
    uint16_t vb[VIEWS], vo[VIEWS];
    uint8_t parity, far[VIEWS], best = 0;
    if (!parse_state(text, &state) || !parse(text, &parity))
        return -1;
    views(text, parity, vb, vo);
    memset(cost, 0, sizeof cost);
    memset(by_depth, 0, sizeof by_depth);
    memset(kids_by_depth, 0, sizeof kids_by_depth);
    memset(cut, 0, sizeof cut);
    memset(reached, 0, sizeof reached);
    memset(iter_children, 0, sizeof iter_children);
    memset(iter_cost, 0, sizeof iter_cost);
    children = nodes = bound = lead_view = 0;
    root_tries = 0;
    for (int k = 0; k < VIEWS; ++k) {
        far[k] = root_dist(vb[k], vo[k]);
        if (far[k] > bound)
            bound = far[k];
    }
    for (int k = 0; k < VIEWS; ++k) {
        uint8_t key = (uint8_t) ((far[k] << 4) + far[(k + 1) % VIEWS]);
        if (key > best) {
            best = key;
            lead_view = k;
        }
    }
    for (int j = 0, k = lead_view; j < VIEWS; ++j, k = (k + 1) % VIEWS) {
        root.b[j] = vb[k];
        root.o[j] = vo[k];
        far_as_searched[j] = far[k];
    }
    int d = exact[rank_state(&state)];
    for (;; ++bound) {
        long before = total(), kids = children;
        cost[BOUND] += 5;
        for (int k = 0; k < VIEWS; ++k)
            root.s[k] = (int8_t) (3 * (bound - far_as_searched[k]) +
                                  far_as_searched[k] % 3);
        int done = search(&root, state, 0, 3, 0, oracle && bound == d);
        if (!done)
            cost[BOUND] += 18 + 7;
        iter_children[bound] = children - kids;
        iter_cost[bound] = total() - before;
        if (done)
            break;
    }
    return total();
}

static void report(const char *text, long measured)
{
    long loop = model(text, 0);
    state_t state;
    parse_state(text, &state);
    printf("%s length=%d root distances as searched %d %d %d (view %d "
           "first)\n",
           text, bound, far_as_searched[0], far_as_searched[1],
           far_as_searched[2], lead_view);
    printf("  children=%ld nodes=%ld, cut by view: %ld %ld %ld; "
           "%.1f instructions a child, %.1f a node\n",
           children, nodes, cut[0], cut[1], cut[2], (double) loop / children,
           (double) loop / nodes);
    printf("  children by iteration:");
    for (int b = 0; b <= MAX_DEPTH; ++b)
        if (iter_children[b])
            printf(" bound %d: %ld (%ld instructions)", b, iter_children[b],
                   iter_cost[b]);
    printf("\n  nodes by depth:   ");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %ld", by_depth[d]);
    printf("\n  children by depth:");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %ld", kids_by_depth[d]);
    printf("\n  instructions:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%ld (%.1f%%)", kind_name[k], cost[k],
               100.0 * cost[k] / measured);
    printf("\n  loop=%ld measured=%ld rest=%ld (%.1f%%), %llu keys tried on "
           "the walks home\n",
           loop, measured, measured - loop,
           100.0 * (measured - loop) / measured,
           (unsigned long long) root_tries);
    printf("  last iteration, children of the root (distance, children "
           "below, instructions):");
    for (int m = 0; m < 9; ++m) {
        state_t c = apply_move(state, (uint8_t) (((m / 3 + lead_view) % 3) * 3 + m % 3));
        if (m / 3 * 3 + m % 3 > 8 || !root_child_kids[m])
            continue;
        printf(" %s:%d,%ld,%ld", move_names[m], exact[rank_state(&c)],
               root_child_kids[m], root_child_cost[m]);
        if (exact[rank_state(&c)] == bound - 1 && root_child_cost[m] &&
            m == 8)
            break;
    }
    long with_oracle = model(text, 1);
    printf("\n  with the oracle in the last iteration: loop=%ld, "
           "children=%ld\n",
           with_oracle, children);
}

int main(int argc, char **argv)
{
    char text[32], worst_text[32] = "";
    long measured, sum[KINDS] = {0}, all = 0, got = 0, kids = 0, exp = 0;
    long states = 0, rest_min = 1L << 30, rest_max = -1, worst = 0;
    long tries = 0, view[VIEWS] = {0}, last_kids = 0, last_cost = 0;
    long oracle_sum = 0, oracle_worst = 0, oracle_kids = 0;
    long depth_nodes[MAX_DEPTH + 1] = {0}, depth_kids[MAX_DEPTH + 1] = {0};
    long near[10] = {0}, lead_hist[MAX_DEPTH + 1] = {0}, iters[8] = {0};
    long wrong_first = 0;
    int code;
    uint8_t diameter;
    FILE *in = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (!in) {
        fprintf(stderr, "usage: %s sweep-file [STATE...]\n", argv[0]);
        return 2;
    }
    /* distances by rank from the table of moves toward solved */
    uint8_t *table = build_table(&diameter);
    exact = malloc(STATES);
    if (!table || !exact)
        return 1;
    memset(exact, UINT8_MAX, STATES);
    exact[0] = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint32_t trail[16], at = rank;
        int n = 0;
        while (exact[at] == UINT8_MAX) {
            state_t s;
            trail[n++] = at;
            unrank_state(at, &s);
            s = apply_move(s, table[at]);
            at = rank_state(&s);
        }
        for (int i = n - 1; i >= 0; --i)
            exact[trail[i]] = (uint8_t) (exact[at] + n - i);
    }
    free(table);
    while (fscanf(in, "%31s %ld %d", text, &measured, &code) == 3) {
        long loop = model(text, 0), rest = measured - loop;
        state_t state;
        if (loop < 0)
            return 1;
        parse_state(text, &state);
        ++states;
        all += loop;
        got += measured;
        kids += children;
        exp += nodes;
        tries += (long) root_tries;
        last_kids += iter_children[bound];
        last_cost += iter_cost[bound];
        ++lead_hist[far_as_searched[0]];
        int n_iter = 0;
        for (int b = 0; b <= MAX_DEPTH; ++b)
            n_iter += iter_children[b] > 0;
        ++iters[n_iter];
        for (int k = 0; k < KINDS; ++k)
            sum[k] += cost[k];
        for (int k = 0; k < VIEWS; ++k)
            view[k] += reached[k];
        for (int d = 0; d <= MAX_DEPTH; ++d) {
            depth_nodes[d] += by_depth[d];
            depth_kids[d] += kids_by_depth[d];
        }
        int closer = 0;
        for (uint8_t m = 0; m < 9; ++m) {
            state_t c = apply_move(state, m);
            closer += exact[rank_state(&c)] == bound - 1;
        }
        ++near[closer];
        /* the first child of the root that passes: is it one move closer? */
        for (int m = 0; m < 9; ++m)
            if (root_child_kids[m] > 1 || (m == 8 && root_child_kids[m])) {
                state_t c = apply_move(
                    state, (uint8_t) (((m / 3 + lead_view) % 3) * 3 + m % 3));
                wrong_first += exact[rank_state(&c)] != bound - 1;
                break;
            }
        if (rest < rest_min)
            rest_min = rest;
        if (rest > rest_max)
            rest_max = rest;
        if (measured > worst) {
            worst = measured;
            strcpy(worst_text, text);
        }
        long with_oracle = model(text, 1) + rest;
        oracle_sum += with_oracle;
        oracle_kids += children;
        if (with_oracle > oracle_worst)
            oracle_worst = with_oracle;
    }
    fclose(in);
    printf("%ld states: children mean %.1f, nodes mean %.1f, measured mean "
           "%.0f; %.1f instructions a child, %.1f a node in the loop\n",
           states, (double) kids / states, (double) exp / states,
           (double) got / states, (double) all / kids, (double) all / exp);
    printf("  views reached per child: %.2f %.2f %.2f\n",
           (double) view[0] / kids, (double) view[1] / kids,
           (double) view[2] / kids);
    printf("  instructions, mean:");
    for (int k = 0; k < KINDS; ++k)
        printf(" %s=%.0f (%.1f%%)", kind_name[k], (double) sum[k] / states,
               100.0 * sum[k] / got);
    printf("\n  rest (measured - loop): %ld to %ld, mean %.0f (%.1f%%); "
           "%.0f keys tried on the walks home\n",
           rest_min, rest_max, (double) (got - all) / states,
           100.0 * (got - all) / got, (double) tries / states);
    printf("  last iteration (bound 11): %.1f children, %.0f instructions "
           "a state: %.1f%% of the loop\n",
           (double) last_kids / states, (double) last_cost / states,
           100.0 * last_cost / all);
    printf("  iterations per state:");
    for (int i = 1; i < 8; ++i)
        if (iters[i])
            printf(" %d: %ld states", i, iters[i]);
    printf("\n  distance of the first view at the root:");
    for (int d = 0; d <= MAX_DEPTH; ++d)
        if (lead_hist[d])
            printf(" %d: %ld states", d, lead_hist[d]);
    printf("\n  mean nodes by depth:   ");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %.1f", (double) depth_nodes[d] / states);
    printf("\n  mean children by depth:");
    for (int d = 0; d < MAX_DEPTH; ++d)
        printf(" %.1f", (double) depth_kids[d] / states);
    printf("\n  neighbours at distance 10, of 9:");
    for (int c = 0; c < 10; ++c)
        if (near[c])
            printf(" %d: %ld states", c, near[c]);
    printf("\n  the first child of the root that is searched at bound 11 is "
           "not one move closer in %ld states\n",
           wrong_first);
    printf("  with the oracle in the last iteration: worst %ld, mean %.0f, "
           "children mean %.1f\n",
           oracle_worst, (double) oracle_sum / states,
           (double) oracle_kids / states);
    report(worst_text, worst);
    for (int i = 2; i < argc; ++i) {
        FILE *again = fopen(argv[1], "r");
        while (fscanf(again, "%31s %ld %d", text, &measured, &code) == 3)
            if (!strcmp(text, argv[i]))
                report(text, measured);
        fclose(again);
    }
    free(exact);
    return 0;
}
