#!/usr/bin/env python3
"""The nibble-packed pattern table experiment (dropped), reproducible.

Rewrites pattern_dist in an assembly file built from rubik.s: two entries
per byte, the even oS in the low nibble, 8 bytes per permutation (40,320
bytes instead of 80,640). Before writing, it checks the packed accessor,
(byte[i >> 1] >> 4 * (i & 1)) & 15, against the unpacked table at every
index, even and odd: gate H4.

  git worktree add /tmp/nib 80b9446 && cd /tmp/nib
  patch -p1 < .../measurements/experiments/nibble-pattern.patch
  make rubik-cli.s CASES=21345671111111:11
  python3 .../measurements/experiments/nibble.py rubik-cli.s > nibble.s
"""
import re
import sys

text = open(sys.argv[1]).read()
start = text.index("pattern_dist:\n")
end = text.index("# [2 * rank] -> distance\n", start)
body = text[start + len("pattern_dist:\n"):end]
values = [int(v) for v in re.findall(r"\d+", body)]
assert len(values) == 5040 * 16 and max(values) <= 15
packed = [values[i] | values[i + 1] << 4 for i in range(0, len(values), 2)]
for i, v in enumerate(values):
    assert (packed[i >> 1] >> 4 * (i & 1)) & 15 == v, i
print(f"H4: packed accessor equals the unpacked table at all {len(values)} "
      "indices", file=sys.stderr)
lines = ["pattern_dist:"] + ["    .byte " + ", ".join(map(str, packed[i:i + 16]))
                            for i in range(0, len(packed), 16)]
sys.stdout.write(text[:start] + "\n".join(lines) + "\n" + text[end:])
