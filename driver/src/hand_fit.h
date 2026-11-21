// Frametop hand-model fitting: turns 21 noisy tracked points into a hand that a real hand
// could make. Each finger is a chain of three segments from its tracked knuckle (MCP), with
// four angles (spread at the knuckle; bend at the knuckle, middle and tip joints) inside real
// joint limits, and per-finger segment lengths learned from your own hand. Every frame the
// angles that best match the tracked points are solved for (a few Gauss-Newton steps), with:
//   - a pull toward the previous frame's angles (steady fingers without the lag of a filter
//     on the points themselves),
//   - a robust match (a point far off its finger - a bad detection - counts for less),
//   - the joint limits applied to every step.
// The output is the same 21 points with each finger's middle joint, tip joint and tip
// replaced by the fitted ones; the wrist, knuckles and thumb pass through unchanged.
// No OpenVR dependency, so it can be tested on its own (frame_driver/test/fit_test.cpp).
#pragma once

class HandFitter
{
public:
	struct Params {
		// Pull toward the previous frame's angles while the finger is still (0 = none). It
		// fades as the finger moves (speed_rad: per-frame angle change where it has halved),
		// like a One Euro filter: steady at rest, no lag on a quick curl.
		double smooth = 3.0;
		double speed_rad = 0.04;
		double robust_m = 0.015;   // points further than this from the fit count for less
		bool   limits = true;      // keep angles within real joint ranges
	};

	void Reset();

	// in: the 21 tracked points (metres, any frame and origin). right: which hand (decides
	// which side the palm faces). out may be the same array as in. Returns the fit's RMS
	// distance to the tracked finger points in metres, or -1 if the palm couldn't be built
	// (out = in then).
	double Fit(const float in[21][3], bool right, const Params& p, float out[21][3]);

	// Learned segment lengths (metres) of finger f (0 index .. 3 pinky), for logging/tests.
	const double* Lengths(int f) const { return fingers_[f].len; }
	bool Learned(int f) const { return fingers_[f].learned; }

private:
	struct Finger {
		bool   init = false;
		double th[4] = { 0, 0, 0, 0 };   // spread, bend MCP, bend PIP, bend DIP (radians)
		double vel[4] = { 0, 0, 0, 0 };  // recent per-frame change of each angle (averaged, signed)
		double len[3] = { 0, 0, 0 };     // MCP->PIP, PIP->DIP, DIP->tip
		// Lengths are learned once from the median of the first 31 frames whose segment
		// lengths are all believable, kept when the hand is lost (Reset keeps them), and then
		// only drift slowly while the fit is good.
		double samples[3][31] = {};
		int    nsamp = 0;
		bool   learned = false;
	};
	// Safety net: a fit further off than this (RMS over a finger's three joints) isn't used,
	// and that finger keeps its tracked points for the frame. The main guard is the finger-size
	// check in Fit; this only catches what gets past it.
	static constexpr double kFallbackM = 0.060;
	Finger fingers_[4];
};
