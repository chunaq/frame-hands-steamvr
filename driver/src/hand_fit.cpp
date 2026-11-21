#include "hand_fit.h"

#include <cmath>
#include <cstring>

namespace {
struct V { double x, y, z; };
V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V operator*(V a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Len(V a) { return std::sqrt(Dot(a, a)); }
bool Unit(V& a)
{
	const double l = Len(a);
	if (l < 1e-9) return false;
	a = a * (1.0 / l);
	return true;
}
V P(const float in[21][3], int i) { return { in[i][0], in[i][1], in[i][2] }; }

constexpr double D2R = 3.14159265358979 / 180.0;
// Same ranges as the aimed-finger limits (hand_simulation.cpp kFingerLimits), radians.
// Bends are measured from the palm's plane (MCP) or the previous segment (PIP, DIP).
const double kLo[4] = { -25 * D2R, -30 * D2R, -10 * D2R, -15 * D2R };
const double kHi[4] = {  25 * D2R,  95 * D2R, 115 * D2R,  90 * D2R };

// One finger's frame, from the palm: knuckle position, forward (the metacarpal's
// direction flattened into the palm's plane), toward the palm, and sideways.
struct FingerFrame { V base, fwd, palm, side; };

// Joint positions (PIP, DIP, tip) for angles th and lengths len.
void Forward(const FingerFrame& F, const double th[4], const double len[3], V out[3])
{
	const double ca = std::cos(th[0]), sa = std::sin(th[0]);
	const V fa = F.fwd * ca + Cross(F.palm, F.fwd) * sa;   // forward, spread about the palm normal
	double phi = 0.0;
	V p = F.base;
	for (int k = 0; k < 3; ++k) {
		phi += th[1 + k];
		const V s = fa * std::cos(phi) + F.palm * std::sin(phi);   // bending toward the palm
		p = p + s * len[k];
		out[k] = p;
	}
}

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Solves the 4x4 system A x = b (A symmetric positive definite) by Gaussian elimination.
bool Solve4(double A[4][4], double b[4], double x[4])
{
	double M[4][5];
	for (int i = 0; i < 4; ++i) { for (int j = 0; j < 4; ++j) M[i][j] = A[i][j]; M[i][4] = b[i]; }
	for (int c = 0; c < 4; ++c) {
		int piv = c;
		for (int r = c + 1; r < 4; ++r) if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
		if (std::fabs(M[piv][c]) < 1e-14) return false;
		if (piv != c) for (int j = 0; j < 5; ++j) { const double t = M[c][j]; M[c][j] = M[piv][j]; M[piv][j] = t; }
		for (int r = 0; r < 4; ++r) {
			if (r == c) continue;
			const double f = M[r][c] / M[c][c];
			for (int j = c; j < 5; ++j) M[r][j] -= f * M[c][j];
		}
	}
	for (int i = 0; i < 4; ++i) x[i] = M[i][4] / M[i][i];
	return true;
}
}

void HandFitter::Reset()
{
	// A re-found hand starts its angles fresh but keeps the finger lengths it has learned:
	// relearning them from the first frames after a dropout (often a half-hidden or wrongly
	// tracked hand) is what produced impossible fingers.
	for (Finger& f : fingers_) {
		f.init = false;
		for (int j = 0; j < 4; ++j) { f.th[j] = 0; f.vel[j] = 0; }
	}
}

namespace {
// Believable segment lengths (metres): MCP->PIP, PIP->DIP, DIP->tip, any finger.
const double kLenLo[3] = { 0.020, 0.012, 0.010 };
const double kLenHi[3] = { 0.065, 0.045, 0.035 };
double Median(const double* v, int n)
{
	double s[31];
	for (int i = 0; i < n; ++i) s[i] = v[i];
	for (int i = 1; i < n; ++i)   // insertion sort, n <= 31
		for (int j = i; j > 0 && s[j - 1] > s[j]; --j) { const double t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
	return n % 2 ? s[n / 2] : 0.5 * (s[n / 2 - 1] + s[n / 2]);
}
}

double HandFitter::Fit(const float in[21][3], bool right, const Params& prm, float out[21][3])
{
	float tmp[21][3];
	std::memcpy(tmp, in, sizeof(tmp));

	// Palm: across = pinky->index knuckles, up = wrist->middle knuckle. Their cross product
	// points out of the palm of a right hand and out of the back of a left one.
	V across = P(tmp, 5) - P(tmp, 17), up = P(tmp, 9) - P(tmp, 0);
	V normal = Cross(across, up);
	if (!Unit(across) || !Unit(up) || !Unit(normal)) {
		if (out != in) std::memcpy(out, in, sizeof(tmp));
		return -1.0;
	}
	const V toward_palm = normal * (right ? 1.0 : -1.0);

	static const int kBase[4] = { 5, 9, 13, 17 };
	double sq = 0.0;
	int nres = 0;
	for (int f = 0; f < 4; ++f) {
		const int b = kBase[f];
		FingerFrame F;
		F.base = P(tmp, b);
		V meta = F.base - P(tmp, 0);
		meta = meta - toward_palm * Dot(meta, toward_palm);
		if (!Unit(meta)) continue;
		F.fwd = meta;
		F.palm = toward_palm;
		F.side = Cross(F.fwd, F.palm);
		const V obs[3] = { P(tmp, b + 1), P(tmp, b + 2), P(tmp, b + 3) };

		// Segment lengths: the median of the first 31 believable frames, kept from then on
		// (see Finger). Frames with any segment outside the believable range don't count.
		Finger& fg = fingers_[f];
		double l[3] = { Len(obs[0] - F.base), Len(obs[1] - obs[0]), Len(obs[2] - obs[1]) };
		bool plausible = true;
		for (int k = 0; k < 3; ++k) plausible = plausible && l[k] >= kLenLo[k] && l[k] <= kLenHi[k];
		if (!fg.learned && plausible) {
			for (int k = 0; k < 3; ++k) {
				fg.samples[k][fg.nsamp] = l[k];
				fg.len[k] = Median(fg.samples[k], fg.nsamp + 1);
			}
			if (++fg.nsamp >= 31) fg.learned = true;
		}
		if (fg.nsamp == 0) continue;   // no believable frame yet: this finger stays as tracked
		// A finger whose whole size is far off its learned length (a stretched or shrunk hand,
		// as after a bad dropout) isn't a pose to fit: keep its tracked points this frame.
		// One wild point or a joint bent the wrong way changes the size much less, and is
		// exactly what the robust, limited fit below is for.
		{
			const double have = l[0] + l[1] + l[2], want = fg.len[0] + fg.len[1] + fg.len[2];
			const double ratio = want > 1e-6 ? have / want : 1.0;
			if (ratio > 1.6 || ratio < 0.6) { fg.init = false; continue; }
		}

		// Start from last frame's angles; the first time, from a straight finger.
		double th[4], prev[4];
		std::memcpy(th, fg.th, sizeof(th));
		std::memcpy(prev, fg.th, sizeof(prev));
		const double lo3 = f == 3 ? -30 * D2R : kLo[0], hi3 = f == 3 ? 30 * D2R : kHi[0];
		const double lo[4] = { lo3, kLo[1], kLo[2], kLo[3] }, hi[4] = { hi3, kHi[1], kHi[2], kHi[3] };
		const int iters = fg.init ? 4 : 12;
		// Signed changes average out when it's only noise and build up when the finger
		// really moves, so this tells a curl from jitter within a few frames.
		double speed = 0.0;
		for (int j = 0; j < 4; ++j) speed += fg.vel[j] * fg.vel[j];
		speed = std::sqrt(speed);
		const double fade = prm.speed_rad > 0 ? 1.0 / (1.0 + (speed / prm.speed_rad) * (speed / prm.speed_rad)) : 1.0;
		const double prior = fg.init ? std::sqrt(prm.smooth * fade) * 0.02 : 0.0;   // metres per radian

		for (int it = 0; it < iters; ++it) {
			V fk[3];
			Forward(F, th, fg.len, fk);
			double w[3];
			for (int k = 0; k < 3; ++k) {
				const double e = Len(fk[k] - obs[k]);
				w[k] = (prm.robust_m > 0 && e > prm.robust_m) ? prm.robust_m / e : 1.0;   // Huber
			}
			// Residuals: 3 joints x 3 coordinates, then 4 prior terms. Jacobian numerically.
			double r[13], J[13][4];
			auto residuals = [&](const double t[4], double* out_r) {
				V q[3];
				Forward(F, t, fg.len, q);
				for (int k = 0; k < 3; ++k) {
					const double sw = std::sqrt(w[k]);
					const V d = (q[k] - obs[k]) * sw;
					out_r[3 * k] = d.x; out_r[3 * k + 1] = d.y; out_r[3 * k + 2] = d.z;
				}
				for (int j = 0; j < 4; ++j) out_r[9 + j] = prior * (t[j] - prev[j]);
			};
			residuals(th, r);
			for (int j = 0; j < 4; ++j) {
				double t2[4];
				std::memcpy(t2, th, sizeof(t2));
				const double h = 1e-5;
				t2[j] += h;
				double r2[13];
				residuals(t2, r2);
				for (int i = 0; i < 13; ++i) J[i][j] = (r2[i] - r[i]) / h;
			}
			double A[4][4] = {}, g[4] = {};
			for (int i = 0; i < 13; ++i)
				for (int a = 0; a < 4; ++a) {
					g[a] -= J[i][a] * r[i];
					for (int c = 0; c < 4; ++c) A[a][c] += J[i][a] * J[i][c];
				}
			for (int a = 0; a < 4; ++a) A[a][a] += 1e-7;   // keeps a straight finger's spread solvable
			double dx[4];
			if (!Solve4(A, g, dx)) break;
			for (int j = 0; j < 4; ++j) {
				th[j] += dx[j];
				if (prm.limits) th[j] = Clamp(th[j], lo[j], hi[j]);
			}
		}

		V fk[3];
		Forward(F, th, fg.len, fk);
		double fsq = 0.0;
		for (int k = 0; k < 3; ++k) {
			const double e = Len(fk[k] - obs[k]);
			fsq += e * e;
		}
		sq += fsq;
		nres += 3;
		const double frms = std::sqrt(fsq / 3.0);
		// Lengths drift toward your hand only while it fits well, from believable frames,
		// and never outside the believable range.
		if (fg.learned && plausible && frms < 0.008)
			for (int k = 0; k < 3; ++k)
				fg.len[k] = Clamp(fg.len[k] + 0.01 * (l[k] - fg.len[k]), kLenLo[k], kLenHi[k]);

		// A fit this far off isn't a hand you're making: keep this finger's tracked points
		// for this frame, and don't carry its angles into the next one.
		if (frms > kFallbackM) { fg.init = false; continue; }

		if (fg.init)
			for (int j = 0; j < 4; ++j) fg.vel[j] += 0.4 * ((th[j] - prev[j]) - fg.vel[j]);
		std::memcpy(fg.th, th, sizeof(th));
		fg.init = true;
		for (int k = 0; k < 3; ++k) {
			tmp[b + 1 + k][0] = float(fk[k].x);
			tmp[b + 1 + k][1] = float(fk[k].y);
			tmp[b + 1 + k][2] = float(fk[k].z);
		}
	}
	std::memcpy(out, tmp, sizeof(tmp));
	return nres ? std::sqrt(sq / nres) : -1.0;
}
