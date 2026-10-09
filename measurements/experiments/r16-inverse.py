"""Host count for searching the inverse state (TASK.md, N15), at r16.

The inverse of a distance-11 state is a distance-11 state, so its cost is in
the sweep. For each state: its cost and root distances, and those of its
inverse; then what choosing between them by the root distances would give,
with 1,300 instructions for the coordinates and the root distances of the
inverse.
    cc -O2 -w measurements/experiments/r16-far.c -o r16-far
    python3 measurements/experiments/r16-inverse.py measurements/sweep-r16.txt ./r16-far
"""
import sys, subprocess
sweep = {}
for l in open(sys.argv[1]):
    s, n, c = l.split(); sweep[s] = int(n)
def inv(s):
    p = [int(ch) - 1 for ch in s[:7]]; o = [int(ch) - 1 for ch in s[7:]]
    q = [0] * 7; t = [0] * 7
    for i in range(7):
        q[p[i]] = i; t[p[i]] = (3 - o[i]) % 3
    return "".join(str(x + 1) for x in q) + "".join(str(x + 1) for x in t)
# root distances from the far tool
out = subprocess.run([sys.argv[2]] + list(sweep), capture_output=True, text=True).stdout
far = {}
for l in out.splitlines():
    f = l.split(); far[f[0]] = tuple(int(x) for x in f[2:5])
def h(k):
    return max(max(k), min(k) + 1)
self_inv = sum(1 for s in sweep if inv(s) == s)
missing = sum(1 for s in sweep if inv(s) not in sweep)
print("states", len(sweep), "equal to their inverse", self_inv, "inverse not in the list", missing)
OVER = 1300
worst = max(sweep.values()); mean = sum(sweep.values()) / len(sweep)
print("as built: worst %d mean %.0f" % (worst, mean))
orc = [min(sweep[s], sweep[inv(s)] + OVER) for s in sweep]
print("oracle, the cheaper of the state and its inverse (+%d for the inverse): worst %d mean %.0f" % (OVER, max(orc), sum(orc) / len(orc)))
for name, pick in (("larger h, then larger sum of the keys", lambda s: (h(far[inv(s)]), sum(far[inv(s)])) > (h(far[s]), sum(far[s]))),
                   ("larger sum of the keys", lambda s: sum(far[inv(s)]) > sum(far[s])),
                   ("larger h only", lambda s: h(far[inv(s)]) > h(far[s]))):
    c = [(sweep[inv(s)] if pick(s) else sweep[s]) + OVER for s in sweep]
    n = sum(1 for s in sweep if pick(s))
    print("inverse when it has the %s (all pay +%d): worst %d mean %.0f, inverse taken for %d states" % (name, OVER, max(c), sum(c) / len(c), n))
hs = {}
for s in sweep:
    k = (h(far[s]), h(far[inv(s)])); hs[k] = hs.get(k, 0) + 1
print("h of the state and of its inverse:", sorted(hs.items()))
top = sorted(sweep, key=lambda s: -sweep[s])[:8]
for s in top: print(s, sweep[s], far[s], "inverse", inv(s), sweep[inv(s)], far[inv(s)])
