/* Host model of the v1 search loop (rubik.s at d21851e), behind the account
 * of why r1 was slower: it counts the nodes and children that
 * r1-leaf-loop.patch treats differently and adds up the change in retired
 * instructions. Per state:
 *   + 1  beqz s7 at every node entered
 *   + 2  two mv per face of a node with rem = 0
 *   - 1  child of such a node pruned by the first test (or, beqz for 3)
 *   - 4  child of such a node pruned by the second test (2 for 6)
 *   - 6  the solved child (2 for 8)
 *   cc -O2 -w measurements/experiments/r1-nodes.c -o r1-nodes && ./r1-nodes
 */
#define main solver_main
#include "../../solver.c"
#undef main

static uint16_t PT[3][PERMUTATIONS], OT[3][ORIENTATIONS];
static uint8_t HP[PERMUTATIONS], HO[ORIENTATIONS];

static void distances(uint8_t *dist, const uint16_t *turn, int n)
{
    static uint16_t queue[PERMUTATIONS];
    int head = 0, tail = 1;
    memset(dist, UINT8_MAX, (size_t) n);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        int x = queue[head++];
        for (int f = 0; f < 3; ++f) {
            int y = x;
            for (int k = 0; k < 3; ++k) {
                y = turn[f * n + y];
                if (dist[y] == UINT8_MAX) {
                    dist[y] = (uint8_t) (dist[x] + 1);
                    queue[tail++] = (uint16_t) y;
                }
            }
        }
    }
}

static void tables(void)
{
    state_t s;
    for (int r = 0; r < PERMUTATIONS; ++r) {
        unrank_state((uint32_t) r * ORIENTATIONS, &s);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            PT[f][r] = (uint16_t) (rank_state(&n) / ORIENTATIONS);
        }
    }
    for (int r = 0; r < ORIENTATIONS; ++r) {
        unrank_state((uint32_t) r, &s);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            OT[f][r] = (uint16_t) (rank_state(&n) % ORIENTATIONS);
        }
    }
    distances(HP, &PT[0][0], PERMUTATIONS);
    distances(HO, &OT[0][0], ORIENTATIONS);
}

static long nodes, children, leaf_nodes, leaf_faces, leaf_first, leaf_second;
static int bound;

/* The order of v1: faces 0..2 without the parent's, three chained turns,
 * hp test, ho test, solved test, descend.
 */
static int search(int p, int o, int depth, int parent)
{
    int rem = bound - depth - 1;
    ++nodes;
    leaf_nodes += !rem;
    for (int f = 0; f < 3; ++f) {
        if (f == parent)
            continue;
        leaf_faces += !rem;
        int cp = p, co = o;
        for (int k = 0; k < 3; ++k) {
            cp = PT[f][cp];
            co = OT[f][co];
            ++children;
            if (HP[cp] > rem) {
                leaf_first += !rem;
                continue;
            }
            if (HO[co] > rem) {
                leaf_second += !rem;
                continue;
            }
            if (!(cp | co) || search(cp, co, depth + 1, f))
                return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const char *const defaults[] = {"", "21345671111111",
                                           "54721631111111"};
    int near = 0;
    if (argc < 2) {
        argc = 3;
        argv = (char **) defaults;
    }
    tables();
    for (int r = 0; r < PERMUTATIONS; ++r)
        near += HP[r] <= 1;
    printf("permutations with perm_dist <= 1: %d of %d\n", near, PERMUTATIONS);
    for (int i = 1; i < argc; ++i) {
        state_t s;
        if (!parse_state(argv[i], &s))
            return 2;
        uint32_t rank = rank_state(&s);
        int p = (int) (rank / ORIENTATIONS), o = (int) (rank % ORIENTATIONS);
        if (!rank)
            continue;
        nodes = children = leaf_nodes = leaf_faces = 0;
        leaf_first = leaf_second = 0;
        bound = HP[p] > HO[o] ? HP[p] : HO[o];
        while (!search(p, o, 0, -1))
            ++bound;
        printf("%s length=%d nodes=%ld children=%ld rem0: nodes=%ld faces=%ld "
               "pruned_first=%ld pruned_second=%ld  r1-v1=%+ld\n",
               argv[i], bound, nodes, children, leaf_nodes, leaf_faces,
               leaf_first, leaf_second,
               nodes + 2 * leaf_faces - leaf_first - 4 * leaf_second - 6);
    }
    return 0;
}
