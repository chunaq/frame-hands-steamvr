//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "hand_simulation.h"
#include "hand_fit.h"
#include "hand_link.h"

#include "openvr_driver.h"


enum MyComponent
{

	MyComponent_indexFinger,
	MyComponent_middleFinger,
	MyComponent_ringFinger,
	MyComponent_pinkyFinger,

	MyComponent_skeleton,

	// Gesture inputs: pinch = trigger, fist = grip, thumb-to-middle pinch = B (menu).
	MyComponent_trigger_value,
	MyComponent_trigger_click,
	MyComponent_grip_value,
	MyComponent_grip_click,
	MyComponent_b_click,

	MyComponent_MAX
};

//-----------------------------------------------------------------------------
// Purpose: Represents a single tracked device in the system.
// What this device actually is (controller, hmd) depends on the
// properties you set within the device (see implementation of Activate)
//-----------------------------------------------------------------------------
class MyControllerDeviceDriver : public vr::ITrackedDeviceServerDriver
{
public:
	MyControllerDeviceDriver( vr::ETrackedControllerRole role );

	vr::EVRInitError Activate( uint32_t unObjectId ) override;

	void EnterStandby() override;

	void *GetComponent( const char *pchComponentNameAndVersion ) override;

	void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) override;

	vr::DriverPose_t GetPose() override;

	void Deactivate() override;

	// ----- Functions we declare ourselves below -----

	const std::string &MyGetSerialNumber();

private:
	void MyInputThread();
	void LoadCalibration();

	// ---- Calibration (hot-reloaded from calibration.json) ----
	struct Calibration {
		float pitch_deg  = 0.f;
		float yaw_deg    = 0.f;
		float roll_deg   = 0.f;
		float offset_x   = 0.f;
		float offset_y   = 0.f;
		float offset_z   = 0.f;
		float curl_mul   = 1.f;
		float splay_mul  = 1.f;

		// Head-relative offset (metres): +x = your right, +y = up, -z = forward.
		// Unlike wrist_offset_*, this stays correct when you turn around.
		float head_off_x = 0.f;
		float head_off_y = 0.f;
		float head_off_z = 0.f;

		// One Euro filter (smooths when still, follows instantly when fast).
		float smoothing      = 1.f;   // 0 = off, 1 = on
		float min_cutoff_hz  = 1.2f;  // lower = smoother at rest, more lag
		float beta           = 12.f;  // higher = less lag on fast moves
		float d_cutoff_hz    = 1.0f;

		float prediction     = 0.5f;  // 0..1, scales velocity sent to SteamVR
		float hold_ms        = 300.f; // keep the hand in place this long after tracking drops
		float calibrate_curls = 0.f;  // change this number to start a 6 s curl-range capture

		// Per-hand head-relative offsets (added to head_off_*), metres.
		float left_off_x = 0.f, left_off_y = 0.f, left_off_z = 0.f;
		float right_off_x = 0.f, right_off_y = 0.f, right_off_z = 0.f;

		float finger_mode       = 1.f;   // 1 = per-joint (1:1) fingers, 0 = old one-curl-per-finger
		float finger_min_cutoff = 1.5f;  // extra smoothing on joint angles
		float finger_beta       = 0.6f;
		float gestures          = 1.f;   // 1 = pinch/fist/menu gestures on, 0 = off

		// Offset in the HAND's own frame (metres), so it turns with your hand:
		// x = toward index/thumb side, y = toward fingertips, z = out of the palm.
		float hand_off_x = 0.f, hand_off_y = 0.f, hand_off_z = 0.f;
		float contact_assist = 1.f; // 0..1, pull fingers to touching poses when you pinch / make a fist
		float thumb_mode     = 0.f;

		// Finger smoothing, separate from the wrist. Fingers are filtered relative to the
		// wrist, so moving your whole hand doesn't add finger lag. Higher = freer / faster.
		float finger_pt_min_cutoff = 3.0f;
		float finger_pt_beta       = 20.f; // 0 = old reliable thumb (wraps over fingers), 1 = measured thumb angles

		// Head pose from when the cameras took the frame (1) instead of now (0). The bridge
		// passes the frame's age as the headset sent it; network_ms adds the Wi-Fi hop.
		float capture_head_pose = 1.f;
		float network_ms        = 3.f;

		// finger_mode 2: how far the fingers follow the real fingers' directions (0..1).
		float finger_aim_blend  = 1.f;
		float finger_aim_limits = 1.f;   // 1 = aimed fingers stay within real joint ranges
		// Hand-model fitting (hand_fit.h): fingers solved as real joint chains every frame.
		float hand_fit           = 1.f;   // 1 = on, 0 = use the tracked points as they are
		float hand_fit_smooth    = 3.f;   // pull toward the last pose while still
		float hand_fit_speed     = 0.04f; // per-frame angle change where that pull has halved
		float hand_fit_robust_mm = 15.f;  // points further than this from the fit count for less
		float finger_interp     = 1.f;   // 1 = move fingers smoothly between camera frames
		float finger_interp_ms  = 0.f;   // how long each move takes; 0 = the measured frame spacing
		// Frametop 0.3.0+ on the headset (its fitted skeleton and gestures, via receiver_bridge.py):
		float ft_gestures       = 1.f;   // 1 = pinch, middle pinch (B) and fist from Frametop's own detector when it sends them
		float skeleton_finger_filter = 1.f; // 1 = our finger smoothing on top of Frametop's skeleton, 0 = skip ours when the skeleton is there
		// Contact retargeting: real thumb-finger touches become touches on the avatar's hand.
		float touch_retarget    = 1.f;   // 1 = on, 0 = off (pure copied angles)
		float touch_near_mm     = 20.f;  // real thumb tip to fingertip this close = touching (tip landmarks sit ~1-2 cm apart pad to pad)
		float touch_far_mm      = 45.f;  // ... from this far it starts blending in
		float touch_pad_mm      = 8.f;   // gap left between the skeleton's tips when touching
		float touch_finger_share = 0.25f; // how much of the way the finger comes to meet the thumb (0 = only the thumb moves)

		// Left/right from the hand's 3D shape once it has learned which way round (0 = off).
		float shape_sides       = 0.35f;
		// Share the hand roles with real controllers (Frame controllers over the stream):
		// 0 = the tracked hands keep both hand roles always (default), 1 = hand off as below.
		float share_with_controllers = 0.f;
		float controller_idle_s = 20.f;  // a real controller that moved this recently keeps its hand (0 = hands always win)
		float disconnect_s      = 1.5f;  // hand lost this long: give the hand role back
	};

	// ---- One Euro filter (Casiez et al. 2012) ----
	struct OneEuro {
		bool  init = false;
		float x = 0.f, dx = 0.f;
		static float Alpha(float cutoff, float dt) {
			const float tau = 1.f / (2.f * 3.14159265f * cutoff);
			return 1.f / (1.f + tau / dt);
		}
		float Update(float v, float dt, float min_cutoff, float beta, float d_cutoff) {
			if (!init) { init = true; x = v; dx = 0.f; return v; }
			const float d = (v - x) / dt;
			dx += Alpha(d_cutoff, dt) * (d - dx);
			const float cutoff = min_cutoff + beta * (dx < 0.f ? -dx : dx);
			x += Alpha(cutoff, dt) * (v - x);
			return x;
		}
	};

	void ResetFilters();
	void LoadCurlRange();
	void SaveCurlRange();
	std::string CurlRangePath() const;

	// Filtered landmark state (world space, metres).
	OneEuro filt_[21][3];
	OneEuro rel_filt_[21][3];      // finger points relative to the wrist
	float   rel_pts_[21][3] = {};  // hand shape (wrist at origin), used for fingers

	// Per-joint angle filters and calibration (14 bends: 4 fingers x MCP/PIP/DIP, thumb MCP/IP).
	OneEuro joint_filt_[14];
	OneEuro abd_filt_[4];
	float joint_open_[14] = {};   // angle with your hand fully open
	float joint_fist_[14] = {};   // angle in a tight fist (0 = not calibrated)
	float rec_joint_min_[14] = {};
	float rec_joint_max_[14] = {};

	// Gesture state (with hysteresis so clicks don't flicker).
	bool pinch_down_ = false;
	bool grip_down_ = false;
	bool menu_down_ = false;
	std::chrono::steady_clock::time_point menu_start_{};
	bool menu_candidate_ = false;
	void ReleaseGestures();
	float   world_pts_[21][3] = {};
	bool    have_hand_ = false;
	uint64_t last_seq_ = 0;
	uint64_t sel_seq_ = 0;   // frame sequence the hand assignment below was made for
	int      sel_index_ = -1; // which of the frame's hands is ours (-1 = none)
	FrametopExtHand cur_ext_{};   // Frametop 0.3.0 extras for our hand in the newest frame (zero = none)
	int      SelectMyHand(const FrametopFrameData& frame, float hold_s) const;
	int     reject_count_ = 0;
	std::chrono::steady_clock::time_point last_frame_time_{};
	std::chrono::steady_clock::time_point last_seen_time_{};

	// Latest pose and bones, shared with GetPose().
	vr::DriverPose_t last_pose_{};
	vr::VRBoneTransform_t last_bones_[31]{};
	bool    have_bones_ = false;
	std::mutex pose_mutex_;

	// Per-finger curl range (thumb, index, middle, ring, pinky).
	float curl_min_[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	float curl_max_[5] = { 1.f, 1.f, 1.f, 1.f, 1.f };
	float last_calib_trigger_ = 0.f;
	bool  calib_trigger_init_ = false;
	bool  recording_ = false;
	std::chrono::steady_clock::time_point record_end_{};
	float rec_min_[5] = {};
	float rec_max_[5] = {};
	Calibration calib_;
	std::mutex  calib_mutex_;
	long long   calib_mtime_ = 0;  // last-modified time (seconds since epoch)

	std::thread my_input_thread_;

	std::unique_ptr< MyHandSimulation > my_hand_simulation_;

	std::atomic< bool > is_active_ = false;

	std::atomic< int > frame_ = 0;
	std::atomic< float > last_curl_ = 0.f;
	std::atomic< float > last_splay_ = 0.f;

	vr::TrackedDeviceIndex_t my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;

	vr::ETrackedControllerRole my_controller_role_ = vr::TrackedControllerRole_Invalid;
	unsigned age_log_count_ = 0;

	// Hand-model fitting.
	HandFitter fitter_;
	float fit_pts_[21][3] = {};
	double last_fit_rms_ = -1.0;

	// Fingers between camera frames (see Publish in MyInputThread).
	bool  new_bones_ = false;                 // last_bones_ changed since the last publish
	bool  interp_have_ = false;
	vr::VRBoneTransform_t interp_from_[31]{};  // pose shown when the newest frame arrived
	vr::VRBoneTransform_t interp_out_[31]{};   // pose shown now
	std::chrono::steady_clock::time_point interp_start_{};
	float interp_dur_s_ = 0.04f;
	float frame_interval_s_ = 0.04f;           // recent time between new frames

	// ---- Share the hand role with the real controllers ----
	// The device starts disconnected with an OptOut role hint, claims its hand only while
	// that hand is tracked and its real controller hasn't moved recently, and gives the
	// role back when it disconnects (SteamVR keeps a role reserved for a device that still
	// hints it, so the real controller would never get it otherwise).
	bool  RealControllerInUse();
	bool  connected_ = true;       // what SteamVR was last told (the role is claimed in Activate)
	bool  reported_off_ = false;   // the disconnected pose went out once
	std::chrono::steady_clock::time_point last_ctrl_check_{};
	bool  ctrl_in_use_ = false;
	struct OtherDevice { bool init = false; float pos[3] = {}; std::chrono::steady_clock::time_point moved{}; };
	OtherDevice others_[vr::k_unMaxTrackedDeviceCount];

	std::string my_controller_model_number_;
	std::string my_controller_serial_number_;

	std::array< vr::VRInputComponentHandle_t, MyComponent_MAX > input_handles_;
};