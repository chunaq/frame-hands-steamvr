// Contact retargeting on recorded hands: builds each frame's skeleton the way the driver does
// (thumb_mode 2, finger_mode 2), then measures, before and after RetargetTouch:
//   - touching frames (your real thumb tip within near_mm of a fingertip): how far apart the
//     skeleton's thumb tip and that fingertip are (should drop to about the pad);
//   - open frames (every real gap over far_mm): how much the thumb changed (should be 0);
//   - thumb steadiness while touching (frame-to-frame change of the thumb tip, mm).
//   retarget_test recording.txt [near_mm far_mm pad_mm finger_share]
#include "hand_simulation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static double Med(std::vector<double> v)
{
	if (v.empty()) return NAN;
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}
static double P90(std::vector<double> v)
{
	if (v.empty()) return NAN;
	std::sort(v.begin(), v.end());
	return v[size_t(v.size() * 0.9)];
}

int main(int argc, char** argv)
{
	if (argc < 2) return 2;
	const float near_mm = argc > 2 ? std::atof(argv[2]) : 20.f;
	const float far_mm = argc > 3 ? std::atof(argv[3]) : 45.f;
	const float pad_mm = argc > 4 ? std::atof(argv[4]) : 8.f;
	const float share = argc > 5 ? std::atof(argv[5]) : 0.25f;
	FILE* fp = std::fopen(argv[1], "r");
	if (!fp) return 2;
	struct Fr { int right; float L[21][3]; };
	std::vector<Fr> fr;
	char line[8192];
	while (std::fgets(line, sizeof line, fp)) {
		char* p = line;
		std::strtod(p, &p); std::strtod(p, &p);
		Fr f{};
		f.right = int(std::strtol(p, &p, 10));
		std::strtod(p, &p);
		for (int j = 0; j < 21; ++j) for (int c = 0; c < 3; ++c) f.L[j][c] = float(std::strtod(p, &p));
		fr.push_back(f);
	}
	std::fclose(fp);

	MyHandSimulation sim;
	MyHandJoints base{};
	for (int k = 0; k < 4; ++k) { base.mcp_flex[k] = 0.4f; base.pip[k] = 0.5f; base.dip[k] = 0.3f; }
	base.thumb_mcp01 = base.thumb_ip01 = 0.4f;
	static const int tips[4] = { 8, 12, 16, 20 };
	const char* names[4] = { "index", "middle", "ring", "pinky" };

	for (int side = 0; side < 2; ++side) {
		const auto role = side ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand;
		std::vector<double> before[4], after[4], open_change, steady_b, steady_a;
		int n = 0, touching = 0;
		float prev_b[3] = {}, prev_a[3] = {};
		bool have_prev = false;
		for (const Fr& f : fr) {
			if (f.right != side) { continue; }
			++n;
			vr::VRBoneTransform_t b[31], a[31];
			std::memset(b, 0, sizeof b);
			sim.ComputeSkeletonTransformsFromJoints(role, base, b);
			MyHandSimulation::AimThumbAtLandmarks(f.L, b);
			MyHandSimulation::AimFingersAtLandmarks(role, f.L, b, 1.f, true);
			std::memcpy(a, b, sizeof a);
			float w[4], real[4];
			for (int k = 0; k < 4; ++k) {
				float d2 = 0;
				for (int c = 0; c < 3; ++c) { const float d = f.L[4][c] - f.L[tips[k]][c]; d2 += d * d; }
				real[k] = std::sqrt(d2) * 1000.f;
				float t = (far_mm - real[k]) / (far_mm - near_mm);
				t = t < 0 ? 0 : (t > 1 ? 1 : t);
				w[k] = t * t * (3 - 2 * t);
			}
			MyHandSimulation::RetargetTouch(a, w, pad_mm / 1000.f, share);
			float gb[4], ga[4];
			MyHandSimulation::TouchGaps(b, gb);
			MyHandSimulation::TouchGaps(a, ga);
			const int best = int(std::min_element(real, real + 4) - real);
			if (real[best] < near_mm) {
				++touching;
				before[best].push_back(gb[best] * 1000.0);
				after[best].push_back(ga[best] * 1000.0);
			}
			if (*std::min_element(real, real + 4) > far_mm) {
				double m = 0;
				for (int bone = 2; bone <= 25; ++bone) {
					const auto& q1 = b[bone].orientation; const auto& q2 = a[bone].orientation;
					const double d = std::fabs(double(q1.w)*q2.w + double(q1.x)*q2.x + double(q1.y)*q2.y + double(q1.z)*q2.z);
					m = std::max(m, 2.0 * std::acos(std::min(1.0, d)) * 57.29578);
				}
				open_change.push_back(m);
			}
			// thumb tip position relative to the wrist: steadiness while near a touch
			// (the thumb tip relative to the index tip changes as you move, so compare frames)
			if (real[best] < far_mm) {
				float tb[4], ta[4];
				MyHandSimulation::TouchGaps(b, tb); MyHandSimulation::TouchGaps(a, ta);
				if (have_prev) {
					steady_b.push_back(std::fabs(tb[best] - prev_b[0]) * 1000.0);
					steady_a.push_back(std::fabs(ta[best] - prev_a[0]) * 1000.0);
				}
				prev_b[0] = tb[best]; prev_a[0] = ta[best];
				have_prev = true;
			} else have_prev = false;
		}
		std::printf("%s hand: %d frames, %d touching\n", side ? "RIGHT" : "LEFT ", n, touching);
		for (int k = 0; k < 4; ++k)
			if (!before[k].empty())
				std::printf("  %-6s touches %4zu: skeleton tip gap median %.1f -> %.1f mm (90%% %.1f -> %.1f)\n", names[k],
				            before[k].size(), Med(before[k]), Med(after[k]), P90(before[k]), P90(after[k]));
		std::printf("  open hand: largest bone change median %.2f deg, max %.2f deg (%zu frames)\n",
		            Med(open_change), open_change.empty() ? 0.0 : *std::max_element(open_change.begin(), open_change.end()),
		            open_change.size());
		std::printf("  near a touch: gap change per frame median %.1f -> %.1f mm (90%% %.1f -> %.1f)\n",
		            Med(steady_b), Med(steady_a), P90(steady_b), P90(steady_a));
	}
	return 0;
}
