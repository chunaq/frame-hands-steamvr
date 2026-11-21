"""Compare thumb-to-finger touch stability, right hand vs left, from a passive recording.

  python ft_touch_analyze.py recording.txt

Finds moments where the thumb tip comes close to a fingertip (natural touches while playing)
and measures, per hand: how many touches, how often the touch flickers on/off, how much the
thumb tip shakes while touching, the noise of the thumb-finger gap, tracker frame rate and
dropped frames during touches, and confidence.
"""
import sys, math, statistics as st

ON, OFF = 0.25, 0.35          # gap / palm length: touching below ON, released above OFF
TIPS = {8: "index", 12: "middle", 16: "ring", 20: "pinky"}

def d(a, b):
    return math.dist(a, b)

rows = {0: [], 1: []}
for line in open(sys.argv[1]):
    v = line.split()
    if len(v) < 67:
        continue
    t, right, conf = float(v[0]), int(v[2]), float(v[3])
    p = [tuple(map(float, v[4 + 3 * j:7 + 3 * j])) for j in range(21)]
    rows[right].append((t, conf, p))

def analyze(name, fr):
    if len(fr) < 30:
        print(f"{name}: only {len(fr)} frames"); return
    dur = fr[-1][0] - fr[0][0]
    touching, start, touches, flick = None, 0, [], 0
    last_end = {}
    shake, gapnoise, dts, confs, gaps_in = [], [], [], [], []
    for i, (t, conf, p) in enumerate(fr):
        palm = d(p[0], p[9]) or 1e-6
        g = {k: d(p[4], p[k]) / palm for k in TIPS}
        k = min(g, key=g.get)
        if touching is None:
            if g[k] < ON:
                touching, start = k, i
                # a re-touch of the same finger within 0.3 s of letting go = flicker
                if k in last_end and t - last_end[k] < 0.3:
                    flick += 1
        else:
            if g[touching] > OFF:
                touches.append((touching, start, i))
                last_end[touching] = t
                touching = None
    if touching is not None:
        touches.append((touching, start, len(fr) - 1))
    held = 0.0
    for k, a, b in touches:
        held += fr[b][0] - fr[a][0]
        seg = fr[a:b + 1]
        for j in range(1, len(seg)):
            dts.append(seg[j][0] - seg[j - 1][0])
        for j in range(2, len(seg)):
            # thumb tip relative to the wrist (head motion removed), second difference
            q = [[seg[j - m][2][4][c] - seg[j - m][2][0][c] for c in range(3)] for m in (2, 1, 0)]
            shake.append(1000 * math.dist([q[0][c] - 2 * q[1][c] + q[2][c] for c in range(3)], (0, 0, 0)))
        gs = [d(s[2][4], s[2][k]) * 1000 for s in seg]
        if len(gs) > 2:
            gapnoise.append(st.pstdev(gs))
        confs += [s[1] for s in seg]
    per = {TIPS[k]: sum(1 for x in touches if x[0] == k) for k in TIPS}
    big = sum(1 for x in dts if x > 0.1)
    med = lambda x: st.median(x) if x else float("nan")
    print(f"\n{name}: {len(fr)} frames over {dur:.0f} s ({len(fr) / dur:.1f} fps overall)")
    print(f"  touches {len(touches)} {per}, held {held:.1f} s, flickers {flick}"
          f" ({100 * flick / max(1, len(touches)):.0f}% of touches)")
    if dts:
        print(f"  during touches: {1 / med(dts):.1f} fps, gaps >100 ms {big} ({100 * big / len(dts):.1f}%)")
    print(f"  thumb tip shake (median / 90th pct) {med(shake):.1f} / "
          f"{sorted(shake)[int(.9 * len(shake))] if shake else float('nan'):.1f} mm")
    print(f"  thumb-finger gap noise (median per touch) {med(gapnoise):.1f} mm")
    print(f"  confidence while touching {med(confs):.2f}")

for r, name in ((0, "LEFT"), (1, "RIGHT")):
    analyze(name, rows[r])
