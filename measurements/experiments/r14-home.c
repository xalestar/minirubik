/* Host count behind r15 (TASK.md, N17): the distance of a key without a
 * search for closer neighbours. Every key is turned until its orientation
 * is home, by any move that brings the orientation closer (first line) or
 * by clockwise quarter turns only (second line); the values modulo 3 give
 * the change of the distance on the way, and the distance at the home
 * orientation is taken as known. "wrong" counts the keys for which this is
 * not the distance.
 *   cc -O2 -w measurements/experiments/r14-home.c -o r14-home && ./r14-home
 */
#define NO_MAIN
#include "../../ida.c"
#include <stdio.h>
#include <string.h>
static uint8_t od[729], oq[729];
int main(void)
{
    static uint16_t q[729]; int head = 0, tail = 1, hist[16] = {0}, maxd = 0;
    memset(od, 255, sizeof od); od[0] = 0; q[0] = 0;
    while (head < tail) { int o = q[head++]; for (int f = 0; f < 3; ++f) { int n = o; for (int t = 0; t < 3; ++t) { n = orient_turn[f][n]; if (od[n] == 255) { od[n] = od[o] + 1; q[tail++] = n; } } } }
    double sum = 0; for (int o = 0; o < 729; ++o) { ++hist[od[o]]; sum += od[o]; if (od[o] > maxd) maxd = od[o]; }
    printf("orientations by moves from home:"); for (int d = 0; d <= maxd; ++d) printf(" %d:%d", d, hist[d]); printf("  mean %.2f\n", sum / 729);
    /* clockwise quarter turns only: turns needed to bring o home = BFS from home over the inverse turns */
    static uint16_t inv[3][729]; for (int f = 0; f < 3; ++f) for (int o = 0; o < 729; ++o) inv[f][orient_turn[f][o]] = o;
    memset(oq, 255, sizeof oq); oq[0] = 0; q[0] = 0; head = 0; tail = 1; memset(hist, 0, sizeof hist); maxd = 0; sum = 0;
    while (head < tail) { int o = q[head++]; for (int f = 0; f < 3; ++f) { int n = inv[f][o]; if (oq[n] == 255) { oq[n] = oq[o] + 1; q[tail++] = n; } } }
    for (int o = 0; o < 729; ++o) { ++hist[oq[o]]; sum += oq[o]; if (oq[o] > maxd) maxd = oq[o]; }
    printf("orientations by clockwise quarter turns to home:"); for (int d = 0; d <= maxd; ++d) printf(" %d:%d", d, hist[d]); printf("  mean %.2f\n", sum / 729);
    for (int mode = 0; mode < 2; ++mode) {
        long steps = 0, quarters = 0, keys = 0, bad = 0; int maxsteps = 0, maxabs = 0;
        for (int b = 0; b < 630; ++b) for (int o = 0; o < 729; ++o) {
            int cb = b, co = o, acc = 0, v = mod3(b, o), n = 0;
            while (co) { int bf = -1, bt = 0, bo = 0;
                if (mode == 0) { for (int f = 0; f < 3 && bf < 0; ++f) { int x = co; for (int t = 1; t <= 3; ++t) { x = orient_turn[f][x]; if (od[x] == od[co] - 1) { bf = f; bt = t; bo = x; break; } } } }
                else { for (int f = 0; f < 3; ++f) if (oq[orient_turn[f][co]] == oq[co] - 1) { bf = f; bt = 1; bo = orient_turn[f][co]; break; } }
                for (int t = 0; t < bt; ++t) cb = block_turn[bf][cb];
                co = bo; int nv = mod3(cb, co), dl = (nv - v + 4) % 3 - 1; acc += dl; v = nv; ++n; quarters += bt; if (acc > maxabs) maxabs = acc; if (-acc > maxabs) maxabs = -acc; }
            if (root_dist(cb, 0) - acc != root_dist(b, o)) ++bad;
            steps += n; ++keys; if (n > maxsteps) maxsteps = n;
        }
        printf("%s: %ld keys: %.2f steps and %.2f quarter turns a key, most steps %d, largest change of the distance on the way %d, wrong %ld\n", mode ? "clockwise quarter turns" : "any move", keys, (double) steps / keys, (double) quarters / keys, maxsteps, maxabs, bad);
    }
    return 0;
}
