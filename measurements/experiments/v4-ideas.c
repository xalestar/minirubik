/* Host model of the search loop of rubik.s at r10, and the counts behind the
 * ideas that were tried or dropped after v4 (TASK.md, section C).
 *
 * search() walks the same tree in the same order as the assembly and adds
 * the length of every basic block of the loop, from `iteration` to the jump
 * to `found`. Options switch on a variant of the loop; each variant is run
 * over every state of a sweep file, and the line printed is the worst and
 * the mean of (measured count - loop of r10 + loop of the variant). The
 * part of a run outside the loop is taken from the measurement, so it is
 * the same in every line.
 *   cc -O2 -w measurements/experiments/v4-ideas.c -o v4-ideas
 *   ./v4-ideas measurements/sweep-r10.txt            the table
 *   ./v4-ideas measurements/sweep-r10.txt STATE...   the loop count of r10
 *                                                    for each state
 */
#define NO_MAIN
#define COUNT_OPS
#include "../../ida.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STATES_MAX = 2644 };

typedef struct {
    uint16_t b[VIEWS], o[VIEWS];
    int8_t s[VIEWS];
} node_t;

/* variants of the loop */
static struct {
    int faces[3];  /* order of the face blocks */
    int swap;      /* test the third view before the second */
    int eager;     /* 1: turn the rotated views for every child (r7);
                      2: one counter for each rotated view */
    int unroll;    /* the first view's three turns unrolled: 1 with a
                      dispatch on the turn, 2 with a jal to the tail */
    int cannot;    /* skip the test of a view with slack 2 or more */
    int least;     /* at every node the view with the least slack first */
} opt;

static int bound;
static long cost, children, nodes, repeats, tests, cannot_tests;
static uint64_t seen[4096];
static int seen_count;

static int search(const node_t *n, int depth, int parent, int entered)
{
    int first = 1, lead = 0;
    if (opt.least)
        for (int k = 1; k < VIEWS; ++k)
            if (n->s[k] / 3 < n->s[lead] / 3)
                lead = k;
    uint64_t id = depth;
    for (int k = 0; k < VIEWS; ++k)
        id = (id * 630 + n->b[k]) * 729 + n->o[k];
    int repeat = 0;
    for (int i = 0; i < seen_count; ++i)
        repeat |= seen[i] == id;
    if (!repeat && seen_count < 4096)
        seen[seen_count++] = id;
    repeats += repeat;
    ++nodes;
    for (int fi = 0; fi < 3; ++fi) {
        int f = opt.faces[fi];
        if (f == parent) {
            if (!(entered && first))
                cost += 1; /* the skip test */
            continue;
        }
        cost += entered && first ? 2 : 9; /* li li, or test and 6 loads too */
        cost += opt.eager == 2 ? 1 : opt.eager == 1 ? -1 : 0; /* counters */
        first = 0;
        node_t child = *n;
        int behind[VIEWS] = {0};
        for (int t = 1; t <= 3; ++t) {
            int k, face = f;
            for (k = 0; k < VIEWS; ++k, face = sym_face[face]) {
                child.b[k] = block_turn[face][child.b[k]];
                child.o[k] = orient_turn[face][child.o[k]];
                ++behind[k];
            }
            ++children;
            cost += 3; /* the first view turns */
            if (opt.eager == 1)
                cost += 6;
            int pruned = 0, failed_at = VIEWS;
            for (k = 0; k < VIEWS; ++k) {
                int v = k ? (opt.swap ? 3 - k : k) : 0;
                v = (v + lead) % VIEWS;
                child.s[v] =
                    next_state[n->s[v]][mod3(child.b[v], child.o[v])];
                if (child.s[v] < 0 && failed_at == VIEWS)
                    failed_at = k;
            }
            pruned = failed_at < VIEWS;
            for (k = 0; k < VIEWS && k <= failed_at; ++k) {
                int v = k ? (opt.swap ? 3 - k : k) : 0;
                v = (v + lead) % VIEWS;
                if (k == 1 && !opt.eager) { /* both catch up together */
                    cost += 8 * behind[1];
                    behind[1] = behind[2] = 0;
                    if (opt.unroll) /* li t4, and j or jal */
                        cost += opt.unroll == 1 && t == 3 ? 1 : 2;
                }
                if (k && opt.eager == 2) { /* each on its own counter */
                    cost += 5 * behind[k];
                    behind[k] = 0;
                }
                if (opt.cannot) {
                    cost += 1; /* compare the row with slack 2 */
                    if (n->s[v] / 3 >= 2 && pruned)
                        continue; /* not tested: it cannot prune */
                }
                ++tests;
                cannot_tests += n->s[v] / 3 >= 2;
                cost += 9;
            }
            if (pruned) {
                if (!opt.unroll)
                    cost += 2; /* next_X */
                else if (failed_at > 0)
                    cost += opt.unroll == 2 ? 1 : 4 - t; /* jr, or dispatch */
                continue;
            }
            cost += 5; /* li, j, sb, sb, beq */
            if (depth + 1 == bound)
                return 1;
            cost += 14 + (f == 0) + (opt.least ? 8 : 0); /* descend */
            if (search(&child, depth + 1, f, 1))
                return 1;
            cost += 15 + f; /* pop */
            cost += opt.eager == 2 ? 1 : opt.eager == 1 ? -1 : 0;
            cost += opt.unroll ? 4 - t : 2; /* dispatch on the turn, or next_X */
        }
    }
    return 0;
}

/* The loop of one state: views as ida.c orders them, or with another
 * choice of the first view (lead_rule 0: as given, 1: the first of the
 * farthest, 2: ida.c's rule).
 */
static long first_children, first_cost;

static long loop(const uint16_t *vb, const uint16_t *vo, int lead_rule)
{
    node_t root;
    uint8_t far[VIEWS], dist[VIEWS], best = 0, lead = 0;
    bound = 0;
    for (int k = 0; k < VIEWS; ++k) {
        far[k] = root_dist(vb[k], vo[k]);
        if (far[k] > bound)
            bound = far[k];
    }
    for (int k = 0; k < VIEWS && lead_rule; ++k) {
        uint8_t key = (uint8_t) ((far[k] << 4) +
                                 (lead_rule == 2 ? far[(k + 1) % VIEWS] : 0));
        if (key > best) {
            best = key;
            lead = (uint8_t) k;
        }
    }
    for (int j = 0; j < VIEWS; ++j) {
        root.b[j] = vb[(lead + j) % VIEWS];
        root.o[j] = vo[(lead + j) % VIEWS];
        dist[j] = far[(lead + j) % VIEWS];
    }
    cost = children = nodes = repeats = tests = cannot_tests = 0;
    first_children = -1;
    first_cost = 0;
    for (;; ++bound) {
        cost += 5; /* iteration */
        seen_count = 0;
        for (int k = 0; k < VIEWS; ++k)
            root.s[k] = (int8_t) (3 * (bound - dist[k]) + dist[k] % 3);
        if (search(&root, 0, 3, 0))
            return cost;
        cost += 18 + 7; /* the pop of the root, deepen */
        if (first_children < 0) {
            first_children = children;
            first_cost = cost;
        }
    }
}

static char text[STATES_MAX][16];
static long measured[STATES_MAX], base[STATES_MAX];
static uint16_t VB[STATES_MAX][VIEWS], VO[STATES_MAX][VIEWS];
static int count;

static void reset(void)
{
    memset(&opt, 0, sizeof opt);
    opt.faces[1] = 1;
    opt.faces[2] = 2;
}

/* One line: the variant over all states. */
static void line(const char *name, int lead_rule, long extra)
{
    long worst = 0, kids = 0;
    double sum = 0;
    int at = 0;
    for (int i = 0; i < count; ++i) {
        long total = measured[i] - base[i] + loop(VB[i], VO[i], lead_rule) +
                     extra;
        sum += total;
        if (total > worst) {
            worst = total;
            at = i;
            kids = children;
        }
    }
    printf("%-58s worst %6ld (%s, %ld children) mean %6.0f\n", name, worst,
           text[at], kids, sum / count);
}

/* C3: when the bound reaches 11, the root's 9 children are built first (45
 * each, 60 around them) and searched in the order of a key of their three
 * slacks; 40 to rebuild the one that is searched. Returns the loop count.
 * key 0: total slack, then largest slack, ascending; 1: total slack
 * ascending, ties in move order; 2: total slack descending; 3: move order.
 */
static long ordered_root(const uint16_t *vb, const uint16_t *vo, int key)
{
    node_t root, kid[9];
    uint8_t far[VIEWS], dist[VIEWS], best = 0, lead = 0;
    long rank[9], total;
    int face[9], kids = 0;
    bound = 0;
    for (int k = 0; k < VIEWS; ++k) {
        far[k] = root_dist(vb[k], vo[k]);
        if (far[k] > bound)
            bound = far[k];
    }
    for (int k = 0; k < VIEWS; ++k) {
        uint8_t v = (uint8_t) ((far[k] << 4) + far[(k + 1) % VIEWS]);
        if (v > best) {
            best = v;
            lead = (uint8_t) k;
        }
    }
    for (int j = 0; j < VIEWS; ++j) {
        root.b[j] = vb[(lead + j) % VIEWS];
        root.o[j] = vo[(lead + j) % VIEWS];
        dist[j] = far[(lead + j) % VIEWS];
    }
    cost = 0;
    for (; bound < 11; ++bound) {
        cost += 5;
        seen_count = 0;
        for (int k = 0; k < VIEWS; ++k)
            root.s[k] = (int8_t) (3 * (bound - dist[k]) + dist[k] % 3);
        if (search(&root, 0, 3, 0))
            return cost; /* not a distance-11 state */
        cost += 18 + 7;
    }
    for (int k = 0; k < VIEWS; ++k)
        root.s[k] = (int8_t) (3 * (bound - dist[k]) + dist[k] % 3);
    total = cost + 5 + 9 * 45 + 60;
    for (int f = 0; f < 3; ++f) {
        node_t child = root;
        for (int t = 0; t < 3; ++t) {
            int k, fc = f, sum = 0, high = 0;
            for (k = 0; k < VIEWS; ++k, fc = sym_face[fc]) {
                child.b[k] = block_turn[fc][child.b[k]];
                child.o[k] = orient_turn[fc][child.o[k]];
                child.s[k] =
                    next_state[root.s[k]][mod3(child.b[k], child.o[k])];
                if (child.s[k] < 0)
                    break;
                sum += child.s[k] / 3;
                high = child.s[k] / 3 > high ? child.s[k] / 3 : high;
            }
            if (k < VIEWS)
                continue;
            kid[kids] = child;
            face[kids] = f;
            rank[kids] = key == 0 ? sum * 100 + high : key == 1 ? sum
                         : key == 2 ? -sum : 0;
            ++kids;
        }
    }
    for (int done = 0; done < kids; ++done) {
        int pick = -1;
        for (int i = 0; i < kids; ++i)
            if (face[i] >= 0 && (pick < 0 || rank[i] < rank[pick]))
                pick = i;
        int f = face[pick];
        face[pick] = -1;
        cost = 0;
        seen_count = 0;
        int found = search(&kid[pick], 1, f, 1);
        total += 40 + cost;
        if (found)
            break;
    }
    return total;
}

/* The state string of the inverse of a state. */
static void invert(const char *s, char *t)
{
    for (int i = 0; i < CORNERS; ++i) {
        int cubie = s[i] - '1';
        t[cubie] = (char) ('1' + i);
        t[CORNERS + cubie] = (char) ('1' + (3 - (s[CORNERS + i] - '1')) % 3);
    }
    t[2 * CORNERS] = '\0';
}

int main(int argc, char **argv)
{
    int code;
    FILE *in = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (!in) {
        fprintf(stderr, "usage: %s sweep-file [state...]\n", argv[0]);
        return 2;
    }
    reset();
    if (argc > 2) { /* the loop count alone, to compare with a profile */
        for (int i = 2; i < argc; ++i) {
            uint16_t vb[VIEWS], vo[VIEWS];
            uint8_t parity;
            if (!parse(argv[i], &parity))
                return 2;
            views(argv[i], parity, vb, vo);
            long c = loop(vb, vo, 2);
            printf("%s loop %ld children %ld nodes %ld\n", argv[i], c,
                   children, nodes);
        }
        return 0;
    }
    while (count < STATES_MAX &&
           fscanf(in, "%15s %ld %d", text[count], &measured[count], &code) ==
               3) {
        uint8_t parity;
        if (!parse(text[count], &parity))
            return 1;
        views(text[count], parity, VB[count], VO[count]);
        ++count;
    }
    fclose(in);
    long low = 1L << 30, high = 0, all_kids = 0, all_first = 0, all_rep = 0;
    long all_nodes = 0, all_tests = 0, all_cannot = 0, worst_first = 0;
    for (int i = 0; i < count; ++i) {
        base[i] = loop(VB[i], VO[i], 2);
        long rest = measured[i] - base[i];
        low = rest < low ? rest : low;
        high = rest > high ? rest : high;
        all_kids += children;
        all_first += first_children;
        all_rep += repeats;
        all_nodes += nodes;
        all_tests += tests;
        all_cannot += cannot_tests;
        worst_first = first_children > worst_first ? first_children
                                                   : worst_first;
    }
    printf("%d states; outside the loop: %ld to %ld instructions\n", count,
           low, high);
    printf("per state: %.1f children, %.1f in the first iteration (most: "
           "%ld); %.1f nodes, %.2f of them reached before in the same "
           "iteration; %.1f tests, %.1f of them of a view with slack 2 or "
           "more\n",
           (double) all_kids / count, (double) all_first / count, worst_first,
           (double) all_nodes / count, (double) all_rep / count,
           (double) all_tests / count, (double) all_cannot / count);

    line("r10", 2, 0);
    line("C13 the first view as given", 0, 0);
    line("C13 the first of the farthest views first", 1, 0);
    opt.eager = 1;
    line("C1  every child turns the rotated views", 2, 0);
    opt.eager = 2;
    line("C1  one counter for each rotated view", 2, 0);
    reset();
    opt.cannot = 1;
    line("C2  skip the test of a view that cannot prune", 2, 0);
    reset();
    opt.least = 1;
    line("C2  the view with the least slack first at every node", 2, 0);
    reset();
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 2; ++b) {
            static const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                            {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
            char name[64];
            if (!a && !b)
                continue;
            memcpy(opt.faces, order[a], sizeof opt.faces);
            opt.swap = b;
            snprintf(name, sizeof name, "C9  faces %c%c%c, %s view tested "
                                        "second",
                     "RBD"[order[a][0]], "RBD"[order[a][1]],
                     "RBD"[order[a][2]], b ? "third" : "second");
            line(name, 2, 0);
        }
    reset();
    opt.unroll = 1;
    line("C10 first view unrolled, dispatch on the turn", 2, 0);
    opt.unroll = 2;
    line("C10 first view unrolled, jal to the tail", 2, 0);
    reset();
    for (int key = 0; key < 4; ++key) {
        static const char *const name[4] = {
            "C3  root's children at bound 11: least slack, then largest",
            "C3  root's children at bound 11: least slack, in move order",
            "C3  root's children at bound 11: most slack first",
            "C3  root's children at bound 11: move order, same overhead"};
        long worst = 0;
        double sum = 0;
        int at = 0;
        for (int i = 0; i < count; ++i) {
            long total = measured[i] - base[i] +
                         ordered_root(VB[i], VO[i], key);
            sum += total;
            if (total > worst) {
                worst = total;
                at = i;
            }
        }
        printf("%-58s worst %6ld (%s) mean %6.0f\n", name[key], worst,
               text[at], sum / count);
    }

    /* C11: a bound at the root that is one higher, from three more root
     * keys (1,850 as in C4), when the state has three iterations or more */
    {
        long worst = 0;
        double sum = 0;
        for (int i = 0; i < count; ++i) {
            long all = loop(VB[i], VO[i], 2);
            long total = measured[i] - base[i] + all - first_cost + 1850;
            sum += total;
            worst = total > worst ? total : worst;
        }
        printf("%-58s worst %6ld mean %6.0f\n",
               "C11 the first iteration skipped, three more root keys", worst,
               sum / count);
    }

    /* C4: the inverse state instead, when its root keys are farther, and the
     * better of the two with hindsight; 500 more for its coordinates and
     * 1,350 for its walks home */
    long worst[2] = {0};
    double mean[2] = {0};
    for (int i = 0; i < count; ++i) {
        char inverse[16];
        uint16_t vb[VIEWS], vo[VIEWS];
        uint8_t parity;
        int far[2] = {0};
        invert(text[i], inverse);
        if (!parse(inverse, &parity))
            return 1;
        views(inverse, parity, vb, vo);
        for (int k = 0; k < VIEWS; ++k) {
            far[0] += root_dist(VB[i][k], VO[i][k]);
            far[1] += root_dist(vb[k], vo[k]);
        }
        long own = base[i], other = loop(vb, vo, 2);
        long rest = measured[i] - base[i] + 1850;
        long rule = rest + (far[1] > far[0] ? other : own);
        long hindsight = rest + (other < own ? other : own);
        worst[0] = rule > worst[0] ? rule : worst[0];
        worst[1] = hindsight > worst[1] ? hindsight : worst[1];
        mean[0] += rule;
        mean[1] += hindsight;
    }
    printf("%-58s worst %6ld mean %6.0f\n",
           "C4  the inverse when its root keys are farther", worst[0],
           mean[0] / count);
    printf("%-58s worst %6ld mean %6.0f\n",
           "C4  the better of the state and its inverse, in hindsight",
           worst[1], mean[1] / count);
    return 0;
}
