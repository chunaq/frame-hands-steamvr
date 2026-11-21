"""How much the driver's own smoothing lags behind Frametop's (already filtered) hand, and how
steady it makes it, from a hand recording (one line per hand per frame: time seq right confidence and 63 coordinates).

  python ft_lag_analyze.py recording.txt

The driver smooths the whole hand (wrist and palm) with a One Euro filter (smooth_min_cutoff,
smooth_beta) and the fingers relative to the wrist with a lighter one (finger_point_*). Frametop
0.3.0 already filters its skeleton on the headset, so this checks what the second filter costs
(lag while moving) and buys (less shake while still), for a few settings.
"""
import math, sys, statistics as st


class OneEuro:
    def __init__(self, mincut, beta, dcut=1.0):
        self.mincut, self.beta, self.dcut = mincut, beta, dcut
        self.x = self.dx = None

    @staticmethod
    def alpha(cut, dt):
        tau = 1.0 / (2 * math.pi * cut)
        return 1.0 / (1.0 + tau / dt)

    def __call__(self, x, dt):
        if self.x is None:
            self.x, self.dx = x, 0.0
            return x
        dx = (x - self.x) / dt
        a = self.alpha(self.dcut, dt)
        self.dx = a * dx + (1 - a) * self.dx
        cut = self.mincut + self.beta * abs(self.dx)
        a = self.alpha(cut, dt)
        self.x = a * x + (1 - a) * self.x
        return self.x


rows = {0: [], 1: []}
for line in open(sys.argv[1]):
    v = line.split()
    if len(v) < 67:
        continue
    t, right = float(v[0]), int(v[2])
    p = [tuple(map(float, v[4 + 3 * j:7 + 3 * j])) for j in range(21)]
    rows[right].append((t, p))


def tracks(fr):
    """Split into continuous runs (no gap over 0.3 s)."""
    run = []
    for r in fr:
        if run and r[0] - run[-1][0] > 0.3:
            yield run
            run = []
        run.append(r)
    if run:
        yield run


def evaluate(fr, joint, rel, mincut, beta):
    """Lag (mm behind while moving > 0.2 m/s) and shake (second difference, mm) of a joint."""
    lag, shake_in, shake_out = [], [], []
    for run in tracks(fr):
        if len(run) < 10:
            continue
        f = [OneEuro(mincut, beta) for _ in range(3)]
        prev_t = None
        hist_in, hist_out = [], []
        for t, p in run:
            x = [p[joint][k] - (p[0][k] if rel else 0.0) for k in range(3)]
            dt = 1 / 60 if prev_t is None else max(1 / 240, min(0.1, t - prev_t))
            prev_t = t
            y = [f[k](x[k], dt) for k in range(3)]
            hist_in.append((t, x)); hist_out.append(y)
        for i in range(2, len(hist_in)):
            (t0, a), (t1, b), (t2, c) = hist_in[i - 2], hist_in[i - 1], hist_in[i]
            speed = math.dist(b, c) / max(1e-3, t2 - t1)
            if speed > 0.2:
                lag.append(1000 * math.dist(c, hist_out[i]))
            if speed < 0.05:
                shake_in.append(1000 * math.dist([a[k] - 2 * b[k] + c[k] for k in range(3)], (0, 0, 0)))
                o = hist_out
                shake_out.append(1000 * math.dist([o[i - 2][k] - 2 * o[i - 1][k] + o[i][k] for k in range(3)], (0, 0, 0)))
    med = lambda x: st.median(x) if x else float("nan")
    return med(lag), med(shake_in), med(shake_out)


for r, name in ((0, "LEFT"), (1, "RIGHT")):
    fr = rows[r]
    if len(fr) < 30:
        print(f"{name}: only {len(fr)} frames")
        continue
    dts = [b[0] - a[0] for a, b in zip(fr, fr[1:]) if b[0] - a[0] < 0.3]
    print(f"\n{name}: {len(fr)} frames, {1 / st.median(dts):.1f} fps median, "
          f"gaps over 50 ms {100 * sum(d > 0.05 for d in dts) / len(dts):.0f}%")
    print("  wrist (whole hand)   min_cutoff beta : lag moving / shake still raw -> filtered (mm)")
    for mc, b in ((0.7, 10), (1.5, 10), (3, 15), (99, 0)):
        lag, si, so = evaluate(fr, 0, False, mc, b)
        tag = "  <- now" if (mc, b) == (0.7, 10) else ("  (filter off)" if mc == 99 else "")
        print(f"    {mc:5.1f} {b:4.0f} : {lag:5.1f} / {si:4.1f} -> {so:4.1f}{tag}")
    print("  thumb tip vs wrist   (skeleton_finger_filter 1 would use 4.0 / 25)")
    for mc, b in ((4, 25), (99, 0)):
        lag, si, so = evaluate(fr, 4, True, mc, b)
        tag = "  (off = now)" if mc == 99 else ""
        print(f"    {mc:5.1f} {b:4.0f} : {lag:5.1f} / {si:4.1f} -> {so:4.1f}{tag}")
