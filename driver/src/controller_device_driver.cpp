//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "controller_device_driver.h"

#include <windows.h>

#include "driverlog.h"
#include "vrmath.h"
#include <algorithm>
#include <cmath>
#include <sys/stat.h>  // for _stat64 (MSVC)

// Let's create some variables for strings used in getting settings.
// This is the section where all of the settings we want are stored. A section name can be anything,
// but if you want to store driver specific settings, it's best to namespace the section with the driver identifier
// ie "<my_driver>_<section>" to avoid collisions
static const char *my_controller_main_settings_section = "driver_framehands";

// Individual right/left hand settings sections
static const char *my_controller_right_settings_section = "driver_framehands_left_controller";
static const char *my_controller_left_settings_section = "driver_framehands_right_controller";

// These are the keys we want to retrieve the values for in the settings
static const char *my_controller_settings_key_model_number = "model_number";
static const char *my_controller_settings_key_serial_number = "serial_number";

// ---------------------------------------------------------------------------
// Calibration file path (relative to the driver's resources\settings folder).
// Edit calibration.json while VRChat is running – changes apply within ~1 s.
// ---------------------------------------------------------------------------
// Our own two devices (0 = left, 1 = right), so the real-controller check skips them.
static std::atomic<uint32_t> g_my_index[2] = { vr::k_unTrackedDeviceIndexInvalid, vr::k_unTrackedDeviceIndexInvalid };

// The driver's own folder (<install>/framehands), from where this DLL was loaded
// (<install>/framehands/bin/win64/driver_framehands.dll), so settings live wherever it's installed.
static std::string DriverRoot()
{
    static std::string root = [] {
        HMODULE module = nullptr;
        char path[MAX_PATH] = {};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&DriverRoot), &module))
            GetModuleFileNameA(module, path, MAX_PATH);
        std::string p = path;
        for (int up = 0; up < 3; ++up) {   // driver_framehands.dll -> win64 -> bin -> framehands
            const size_t cut = p.find_last_of("\\/");
            if (cut == std::string::npos) return std::string();
            p.resize(cut);
        }
        return p;
    }();
    return root;
}

static std::string SettingsFile(const char* name)
{
    return DriverRoot() + "\\resources\\settings\\" + name;
}

// Tiny JSON number parser (no dependencies).
static float ParseJsonFloat(const char* json, const char* key, float fallback)
{
    const char* p = std::strstr(json, key);
    if (!p) return fallback;
    p = std::strchr(p, ':');
    if (!p) return fallback;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return static_cast<float>(std::atof(p));
}

void MyControllerDeviceDriver::LoadCalibration()
{
    // Check file mtime first to avoid re-reading when nothing changed.
    struct __stat64 st{};
    if (_stat64(SettingsFile("calibration.json").c_str(), &st) != 0) return;
    if (st.st_mtime == calib_mtime_) return;

    FILE* f = nullptr;
    if (fopen_s(&f, SettingsFile("calibration.json").c_str(), "rb") != 0 || !f) return;

    // Read whole file.
    char buf[4096]{};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    Calibration c;
    c.pitch_deg = ParseJsonFloat(buf, "\"pitch_offset_deg\"", 0.f);
    c.yaw_deg   = ParseJsonFloat(buf, "\"yaw_offset_deg\"",   0.f);
    c.roll_deg  = ParseJsonFloat(buf, "\"roll_offset_deg\"",  0.f);
    c.offset_x  = ParseJsonFloat(buf, "\"wrist_offset_x\"",  0.f);
    c.offset_y  = ParseJsonFloat(buf, "\"wrist_offset_y\"",  0.f);
    c.offset_z  = ParseJsonFloat(buf, "\"wrist_offset_z\"",  0.f);
    c.curl_mul  = ParseJsonFloat(buf, "\"curl_multiplier\"",  1.f);
    c.splay_mul = ParseJsonFloat(buf, "\"splay_multiplier\"", 1.f);

    const Calibration d; // defaults for keys that aren't in the file
    c.head_off_x      = ParseJsonFloat(buf, "\"head_offset_x\"",      d.head_off_x);
    c.head_off_y      = ParseJsonFloat(buf, "\"head_offset_y\"",      d.head_off_y);
    c.head_off_z      = ParseJsonFloat(buf, "\"head_offset_z\"",      d.head_off_z);
    c.smoothing       = ParseJsonFloat(buf, "\"smoothing\"",          d.smoothing);
    c.min_cutoff_hz   = ParseJsonFloat(buf, "\"smooth_min_cutoff\"",  d.min_cutoff_hz);
    c.beta            = ParseJsonFloat(buf, "\"smooth_beta\"",        d.beta);
    c.d_cutoff_hz     = ParseJsonFloat(buf, "\"smooth_d_cutoff\"",    d.d_cutoff_hz);
    c.prediction      = ParseJsonFloat(buf, "\"prediction\"",         d.prediction);
    c.hold_ms         = ParseJsonFloat(buf, "\"hold_ms\"",            d.hold_ms);
    c.calibrate_curls = ParseJsonFloat(buf, "\"calibrate_curls\"",    d.calibrate_curls);
    c.left_off_x      = ParseJsonFloat(buf, "\"left_offset_x\"",      d.left_off_x);
    c.left_off_y      = ParseJsonFloat(buf, "\"left_offset_y\"",      d.left_off_y);
    c.left_off_z      = ParseJsonFloat(buf, "\"left_offset_z\"",      d.left_off_z);
    c.right_off_x     = ParseJsonFloat(buf, "\"right_offset_x\"",     d.right_off_x);
    c.right_off_y     = ParseJsonFloat(buf, "\"right_offset_y\"",     d.right_off_y);
    c.right_off_z     = ParseJsonFloat(buf, "\"right_offset_z\"",     d.right_off_z);
    c.finger_mode       = ParseJsonFloat(buf, "\"finger_mode\"",       d.finger_mode);
    c.finger_min_cutoff = ParseJsonFloat(buf, "\"finger_min_cutoff\"", d.finger_min_cutoff);
    c.finger_beta       = ParseJsonFloat(buf, "\"finger_beta\"",       d.finger_beta);
    c.gestures          = ParseJsonFloat(buf, "\"gestures\"",          d.gestures);
    c.hand_off_x        = ParseJsonFloat(buf, "\"hand_offset_x\"",     d.hand_off_x);
    c.hand_off_y        = ParseJsonFloat(buf, "\"hand_offset_y\"",     d.hand_off_y);
    c.hand_off_z        = ParseJsonFloat(buf, "\"hand_offset_z\"",     d.hand_off_z);
    c.contact_assist    = ParseJsonFloat(buf, "\"contact_assist\"",    d.contact_assist);
    c.thumb_mode        = ParseJsonFloat(buf, "\"thumb_mode\"",        d.thumb_mode);
    c.finger_pt_min_cutoff = ParseJsonFloat(buf, "\"finger_point_min_cutoff\"", d.finger_pt_min_cutoff);
    c.finger_pt_beta       = ParseJsonFloat(buf, "\"finger_point_beta\"",       d.finger_pt_beta);
    if (c.finger_pt_min_cutoff < 0.05f) c.finger_pt_min_cutoff = 0.05f;
    if (c.finger_pt_beta < 0.f) c.finger_pt_beta = 0.f;
    c.capture_head_pose = ParseJsonFloat(buf, "\"capture_head_pose\"", d.capture_head_pose);
    c.network_ms        = ParseJsonFloat(buf, "\"network_ms\"",        d.network_ms);
    if (c.network_ms < 0.f) c.network_ms = 0.f;
    c.finger_aim_blend  = ParseJsonFloat(buf, "\"finger_aim_blend\"",  d.finger_aim_blend);
    if (c.finger_aim_blend < 0.f) c.finger_aim_blend = 0.f;
    if (c.finger_aim_blend > 1.f) c.finger_aim_blend = 1.f;
    c.finger_aim_limits = ParseJsonFloat(buf, "\"finger_aim_limits\"", d.finger_aim_limits);
    c.hand_fit           = ParseJsonFloat(buf, "\"hand_fit\"",           d.hand_fit);
    c.hand_fit_smooth    = ParseJsonFloat(buf, "\"hand_fit_smooth\"",    d.hand_fit_smooth);
    c.hand_fit_speed     = ParseJsonFloat(buf, "\"hand_fit_speed\"",     d.hand_fit_speed);
    c.hand_fit_robust_mm = ParseJsonFloat(buf, "\"hand_fit_robust_mm\"", d.hand_fit_robust_mm);
    c.finger_interp     = ParseJsonFloat(buf, "\"finger_interp\"",     d.finger_interp);
    c.finger_interp_ms  = ParseJsonFloat(buf, "\"finger_interp_ms\"",  d.finger_interp_ms);
    c.ft_gestures       = ParseJsonFloat(buf, "\"ft_gestures\"",       d.ft_gestures);
    c.skeleton_finger_filter = ParseJsonFloat(buf, "\"skeleton_finger_filter\"", d.skeleton_finger_filter);
    c.touch_retarget    = ParseJsonFloat(buf, "\"touch_retarget\"",    d.touch_retarget);
    c.touch_near_mm     = ParseJsonFloat(buf, "\"touch_near_mm\"",     d.touch_near_mm);
    c.touch_far_mm      = ParseJsonFloat(buf, "\"touch_far_mm\"",      d.touch_far_mm);
    c.touch_pad_mm      = ParseJsonFloat(buf, "\"touch_pad_mm\"",      d.touch_pad_mm);
    c.touch_finger_share = ParseJsonFloat(buf, "\"touch_finger_share\"", d.touch_finger_share);
    if (c.touch_far_mm < c.touch_near_mm + 1.f) c.touch_far_mm = c.touch_near_mm + 1.f;
    c.touch_finger_share = c.touch_finger_share < 0.f ? 0.f : (c.touch_finger_share > 0.5f ? 0.5f : c.touch_finger_share);
    c.shape_sides       = ParseJsonFloat(buf, "\"shape_sides\"",       d.shape_sides);
    c.share_with_controllers = ParseJsonFloat(buf, "\"share_with_controllers\"", d.share_with_controllers);
    c.controller_idle_s = ParseJsonFloat(buf, "\"controller_idle_s\"", d.controller_idle_s);
    c.disconnect_s      = ParseJsonFloat(buf, "\"disconnect_s\"",      d.disconnect_s);
    if (c.disconnect_s < c.hold_ms / 1000.f) c.disconnect_s = c.hold_ms / 1000.f;
    c.contact_assist = c.contact_assist < 0.f ? 0.f : (c.contact_assist > 1.f ? 1.f : c.contact_assist);
    if (c.finger_min_cutoff < 0.05f) c.finger_min_cutoff = 0.05f;
    if (c.finger_beta < 0.f) c.finger_beta = 0.f;

    // Keep the filter numerically sane even if the file has odd values.
    if (c.min_cutoff_hz < 0.05f) c.min_cutoff_hz = 0.05f;
    if (c.d_cutoff_hz   < 0.05f) c.d_cutoff_hz   = 0.05f;
    if (c.beta < 0.f) c.beta = 0.f;
    c.prediction = c.prediction < 0.f ? 0.f : (c.prediction > 1.f ? 1.f : c.prediction);
    if (c.hold_ms < 0.f) c.hold_ms = 0.f;

    // Start a curl-range capture when calibrate_curls changes (not on first load).
    if (!calib_trigger_init_) {
        calib_trigger_init_ = true;
        last_calib_trigger_ = c.calibrate_curls;
    } else if (c.calibrate_curls != last_calib_trigger_) {
        last_calib_trigger_ = c.calibrate_curls;
        recording_ = true;
        record_end_ = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        for (int i = 0; i < 5; ++i) { rec_min_[i] = 1e9f; rec_max_[i] = -1e9f; }
        for (int i = 0; i < 14; ++i) { rec_joint_min_[i] = 1e9f; rec_joint_max_[i] = -1e9f; }
        DriverLog("Curl calibration started (%s hand): open your hand fully, then make a tight fist, for 6 s",
                  my_controller_role_ == vr::TrackedControllerRole_RightHand ? "right" : "left");
    }

    {
        std::lock_guard<std::mutex> lk(calib_mutex_);
        calib_      = c;
        calib_mtime_= st.st_mtime;
    }
    DriverLog("Calibration reloaded: pitch=%.1f yaw=%.1f roll=%.1f "
              "off=(%.3f,%.3f,%.3f) head_off=(%.3f,%.3f,%.3f) curl=%.2f splay=%.2f "
              "smooth=%.0f mincut=%.2f beta=%.1f pred=%.2f hold=%.0fms",
              c.pitch_deg, c.yaw_deg, c.roll_deg,
              c.offset_x, c.offset_y, c.offset_z,
              c.head_off_x, c.head_off_y, c.head_off_z,
              c.curl_mul, c.splay_mul,
              c.smoothing, c.min_cutoff_hz, c.beta, c.prediction, c.hold_ms);
}

// Curl range is saved per hand next to calibration.json.
std::string MyControllerDeviceDriver::CurlRangePath() const
{
    return SettingsFile(my_controller_role_ == vr::TrackedControllerRole_RightHand
                        ? "curl_range_right.json" : "curl_range_left.json");
}

static const char* kFingerNames[5] = { "thumb", "index", "middle", "ring", "pinky" };

void MyControllerDeviceDriver::LoadCurlRange()
{
    FILE* f = nullptr;
    if (fopen_s(&f, CurlRangePath().c_str(), "rb") != 0 || !f) return; // none saved yet: use 0..1
    char buf[2048]{};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    for (int i = 0; i < 5; ++i) {
        char kmin[32], kmax[32];
        snprintf(kmin, sizeof(kmin), "\"%s_open\"", kFingerNames[i]);
        snprintf(kmax, sizeof(kmax), "\"%s_fist\"", kFingerNames[i]);
        const float lo = ParseJsonFloat(buf, kmin, 0.f);
        const float hi = ParseJsonFloat(buf, kmax, 1.f);
        // Ignore a finger whose captured range is too small to be real.
        if (hi - lo >= 0.1f) { curl_min_[i] = lo; curl_max_[i] = hi; }
        else { curl_min_[i] = 0.f; curl_max_[i] = 1.f; }
    }
    // Per-joint open/fist angles (radians) for 1:1 finger mode.
    for (int i = 0; i < 14; ++i) {
        char kmin[32], kmax[32];
        snprintf(kmin, sizeof(kmin), "\"joint%d_open\"", i);
        snprintf(kmax, sizeof(kmax), "\"joint%d_fist\"", i);
        const float lo = ParseJsonFloat(buf, kmin, 0.f);
        const float hi = ParseJsonFloat(buf, kmax, 0.f);
        if (hi - lo >= 0.17f) { joint_open_[i] = lo; joint_fist_[i] = hi; } // need >= ~10 deg of range
        else { joint_open_[i] = 0.f; joint_fist_[i] = 0.f; }
    }

    DriverLog("Curl range loaded: thumb %.2f-%.2f index %.2f-%.2f middle %.2f-%.2f ring %.2f-%.2f pinky %.2f-%.2f",
              curl_min_[0], curl_max_[0], curl_min_[1], curl_max_[1], curl_min_[2], curl_max_[2],
              curl_min_[3], curl_max_[3], curl_min_[4], curl_max_[4]);
}

void MyControllerDeviceDriver::SaveCurlRange()
{
    FILE* f = nullptr;
    if (fopen_s(&f, CurlRangePath().c_str(), "wb") != 0 || !f) {
        DriverLog("Curl calibration: could not write %s", CurlRangePath().c_str());
        return;
    }
    fprintf(f, "{\n  \"_help\": \"Raw curl when your hand is open / in a fist. Delete this file to reset.\",\n");
    for (int i = 0; i < 5; ++i) {
        fprintf(f, "  \"%s_open\": %.3f,\n  \"%s_fist\": %.3f,\n",
                kFingerNames[i], rec_min_[i], kFingerNames[i], rec_max_[i]);
    }
    // joint0-11: index/middle/ring/pinky x MCP/PIP/DIP, joint12-13: thumb MCP/IP (radians)
    for (int i = 0; i < 14; ++i) {
        fprintf(f, "  \"joint%d_open\": %.4f,\n  \"joint%d_fist\": %.4f%s\n",
                i, rec_joint_min_[i], i, rec_joint_max_[i], i < 13 ? "," : "");
    }
    fprintf(f, "}\n");
    fclose(f);
    DriverLog("Curl calibration saved to %s", CurlRangePath().c_str());
}

void MyControllerDeviceDriver::ResetFilters()
{
    fitter_.Reset();   // a re-found hand starts its fit fresh (lengths are relearned too)
    for (auto& joint : filt_)
        for (auto& axis : joint)
            axis = OneEuro{};
    for (auto& joint : rel_filt_)
        for (auto& axis : joint)
            axis = OneEuro{};
    for (auto& j : joint_filt_) j = OneEuro{};
    for (auto& a : abd_filt_) a = OneEuro{};
    reject_count_ = 0;
}

void MyControllerDeviceDriver::ReleaseGestures()
{
    pinch_down_ = grip_down_ = menu_down_ = menu_candidate_ = false;
    vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_trigger_value], 0.f, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_trigger_click], false, 0);
    vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_grip_value], 0.f, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_grip_click], false, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_b_click], false, 0);
}


MyControllerDeviceDriver::MyControllerDeviceDriver( vr::ETrackedControllerRole role )
{
	// we're not activated yet
	is_active_ = false;

	// The constructor takes a role argument, that gives us information about if our controller is a left or right hand.
	// Let's store it for later use. We'll need it.
	my_controller_role_ = role;

	// We have our model number and serial number stored in SteamVR settings. We need to get them and do so here.
	// Other IVRSettings methods (to get int32, floats, bools) return the data, instead of modifying, but strings are
	// different.
	char model_number[ 1024 ];
	vr::VRSettings()->GetString( my_controller_main_settings_section, my_controller_settings_key_model_number, model_number, sizeof( model_number ) );
	my_controller_model_number_ = model_number;

	// Get our serial number depending on our "handedness"
	char serial_number[ 1024 ];
	vr::VRSettings()->GetString( my_controller_role_ == vr::TrackedControllerRole_LeftHand ? my_controller_left_settings_section : my_controller_right_settings_section,
		my_controller_settings_key_serial_number, serial_number, sizeof( serial_number ) );
	my_controller_serial_number_ = serial_number;

	// Here's an example of how to use our logging wrapper around IVRDriverLog
	// In SteamVR logs (SteamVR Hamburger Menu > Developer Settings > Web console) drivers have a prefix of
	// "<driver_name>:". You can search this in the top search bar to find the info that you've logged.
	DriverLog( "My Controller Model Number: %s", my_controller_model_number_.c_str() );
	DriverLog( "My Controller Serial Number: %s", my_controller_serial_number_.c_str() );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver after our
//  IServerTrackedDeviceProvider calls IVRServerDriverHost::TrackedDeviceAdded.
//-----------------------------------------------------------------------------
vr::EVRInitError MyControllerDeviceDriver::Activate( uint32_t unObjectId )
{
	// Let's keep track of our device index. It'll be useful later.
	my_controller_index_ = unObjectId;
	g_my_index[ my_controller_role_ == vr::TrackedControllerRole_RightHand ? 1 : 0 ] = unObjectId;

	// Properties are stored in containers, usually one container per device index. We need to get this container to set
	// The properties we want, so we call this to retrieve a handle to it.
	vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer( my_controller_index_ );

	// Let's begin setting up the properties now we've got our container.
	// A list of properties available is contained in vr::ETrackedDeviceProperty.

	// First, let's set the model number.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ModelNumber_String, my_controller_model_number_.c_str() );

	// Let's tell SteamVR our role which we received from the constructor earlier.
	// OptOut until this hand is actually tracked (see connected_ in the header).
	// Claim this hand from the start. If the hand devices start without a hand role, the
	// headset stream's Frame controller entries (present even with the controllers off) get
	// the hands first, and SteamVR hands them to VRChat as simulated Oculus Touch controllers:
	// no finger tracking, a solid controller hand. share_with_controllers 1 (calibration.json)
	// brings back the hand-off to real controllers, which gives the role up while no hand is seen.
	vr::VRProperties()->SetInt32Property( container, vr::Prop_ControllerRoleHint_Int32, my_controller_role_ );
	// When a Frame controller wakes up (or the headset reports one as on), it asks for the same
	// hand. SteamVR picks the device with the higher hand-selection priority, so make the
	// tracked hand win by a wide margin: the hand never flips to a controller on its own.
	vr::VRProperties()->SetInt32Property( container, vr::Prop_ControllerHandSelectionPriority_Int32, 1000000 );


	// Now let's set up our inputs

	// This tells the UI what to show the user for bindings for this controller,
	// As well as what default bindings should be for legacy apps.
	// Note, we can use the wildcard {<driver_name>} to match the root folder location
	// of our driver.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_InputProfilePath_String, "{framehands}/input/framehands_hand_profile.json" );
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ControllerType_String, "framehands_hand" );

	// Without a render model SteamVR draws its generic fallback (a gamepad) on each hand.
	// Point it at a near-invisible model in resources/rendermodels so only the skeletal hand shows.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_RenderModelName_String, "{framehands}framehands_invisible_hand" );


	// Let's set up some inputs for our curls. Not strictly needed, but cool to have.
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/finger/index", &input_handles_[ MyComponent_indexFinger ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/finger/middle", &input_handles_[ MyComponent_middleFinger ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/finger/ring", &input_handles_[ MyComponent_ringFinger ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/finger/pinky", &input_handles_[ MyComponent_pinkyFinger ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );

	// Gesture inputs (pinch = trigger, fist = grip, thumb-to-middle pinch = B).
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/trigger/value", &input_handles_[ MyComponent_trigger_value ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );
	vr::VRDriverInput()->CreateBooleanComponent( container, "/input/trigger/click", &input_handles_[ MyComponent_trigger_click ] );
	vr::VRDriverInput()->CreateScalarComponent( container, "/input/grip/value", &input_handles_[ MyComponent_grip_value ], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided );
	vr::VRDriverInput()->CreateBooleanComponent( container, "/input/grip/click", &input_handles_[ MyComponent_grip_click ] );
	vr::VRDriverInput()->CreateBooleanComponent( container, "/input/b/click", &input_handles_[ MyComponent_b_click ] );

	// Now our skeletons. This is slightly harder.
	vr::EVRInputError err = vr::VRDriverInput()->CreateSkeletonComponent( container,								  // container
		my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "/input/skeleton/left" : "/input/skeleton/right", // The path to the skeleton for input. This is for binding to applications.
		my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "/skeleton/hand/left" : "/skeleton/hand/right",	  // Where on the body this skeleton is.
		"/pose/raw",							// Where the origin for the skeleton is going to be. These pose locations are defined in the render model file.
		vr::VRSkeletalTracking_Full,			// Inform the runtime about the capabilities of the device.
		nullptr,								// Used for calculating curl and splay values. If this is null then defaults are used.
		0,										// How many bones there are in the gripLimitTransforms above.
		&input_handles_[ MyComponent_skeleton ] // Bind the component to a handle.
	);

	// SteamVR wants skeletal data immediately.
	MyInputThread();
	MyInputThread();

	// initialise our hand tracking simulation class
	my_hand_simulation_ = std::make_unique< MyHandSimulation >();

	// Per-hand curl range from a previous calibration, if any.
	LoadCurlRange();

	// create a thread for updating our skeleton
	is_active_ = true;
	my_input_thread_ = std::thread( &MyControllerDeviceDriver::MyInputThread, this );

	// We've activated everything successfully!
	// Let's tell SteamVR that by saying we don't have any errors.
	return vr::VRInitError_None;
}

//-----------------------------------------------------------------------------
// Purpose: If you're an HMD, this is where you would return an implementation
// of vr::IVRDisplayComponent, vr::IVRVirtualDisplay or vr::IVRDirectModeComponent.
//
// But this a simple example to demo for a controller, so we'll just return nullptr here.
//-----------------------------------------------------------------------------
void *MyControllerDeviceDriver::GetComponent( const char *pchComponentNameAndVersion )
{
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when a debug request has been made from an application to the driver.
// What is in the response and request is up to the application and driver to figure out themselves.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize )
{
	if ( unResponseBufferSize >= 1 )
		pchResponseBuffer[ 0 ] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should enter standby mode.
// The device should be put into whatever low power mode it has
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::EnterStandby()
{
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should deactivate.
// This is typically at the end of a session
// The device should free any resources it has allocated here.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::Deactivate()
{
	// unassign our controller index (we don't want to be calling vrserver any more after Deactivate() has been called
	my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;

	if ( is_active_.exchange( false ) )
	{
		my_input_thread_.join();
	}
}


//-----------------------------------------------------------------------------
// Purpose: This is never called by vrserver, but is useful for giving data to
// vr::VRServerDriverHost::TrackedDevicePoseUpdated.
//-----------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Left/right assignment shared by both hand drivers (same process).
// The camera model's left/right label sometimes flips, which made one hand
// jump onto the other. Instead, each hand is followed by where it was a
// moment ago; the label is only a fallback when there's no recent history.
// ---------------------------------------------------------------------------
namespace {
struct SideTrack {
    float wrist[3] = { 0.f, 0.f, 0.f };
    std::chrono::steady_clock::time_point t{};
    bool valid = false;
};
std::mutex g_side_mutex;
SideTrack  g_side[2]; // 0 = left, 1 = right

float Dist(const float a[3], const float b[3])
{
    const float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Which hand this is, from its 3D shape alone (+1 right, -1 left, 0 can't tell).
// A left hand is a mirror image of a right one, and the tracker's points are real 3D
// (two cameras), so the shape itself says which it is: the palm normal
// n = (index MCP - pinky MCP) x (middle MCP - wrist) points out of the palm of a right
// hand and out of the back of a left one, and the thumb and fingertips always lie on the
// palm side of the knuckles, more so the more you pinch or curl (exactly when the camera
// model's own left/right label tends to flip). Works in any head frame.
float ShapeScore(const FrametopHandData& h)
{
    const float* p[21];
    for (int i = 0; i < 21; ++i) p[i] = h.pts[i];
    float a[3], b[3], n[3];
    for (int k = 0; k < 3; ++k) { a[k] = p[5][k] - p[17][k]; b[k] = p[9][k] - p[0][k]; }
    n[0] = a[1]*b[2] - a[2]*b[1]; n[1] = a[2]*b[0] - a[0]*b[2]; n[2] = a[0]*b[1] - a[1]*b[0];
    const float nl = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (nl < 1e-8f) return 0.f;
    float knuckles[3], c[3] = { 0.f, 0.f, 0.f };
    for (int k = 0; k < 3; ++k) knuckles[k] = 0.25f * (p[5][k] + p[9][k] + p[13][k] + p[17][k]);
    const int tips[7] = { 2, 3, 4, 8, 12, 16, 20 };
    for (int t : tips) for (int k = 0; k < 3; ++k) c[k] += p[t][k] - knuckles[k];
    const float cl = std::sqrt(c[0]*c[0] + c[1]*c[1] + c[2]*c[2]);
    if (cl < 1e-6f) return 0.f;
    return (n[0]*c[0] + n[1]*c[1] + n[2]*c[2]) / (nl * cl);   // -1..1
}
// Which sign of ShapeScore means "right" is learned from your own hands rather than
// assumed: a hand well out to one side of your head (> 12 cm) is almost always that
// hand, so each such sample with a clear shape counts for one sign or the other. The
// shape is only used once 30+ samples agree at least 85%. Until then: labels as before.
std::atomic<int>   g_shape_agree{ 0 }, g_shape_disagree{ 0 };
std::atomic<bool>  g_shape_logged{ false };
std::atomic<float> g_shape_threshold{ 0.35f };   // calibration.json "shape_sides" (0 = off)
void LearnShapeSign(const FrametopHandData& h)
{
    const float x = h.pts[0][0];   // head frame, + = your right
    if (std::fabs(x) < 0.12f) return;
    const float s = ShapeScore(h);
    if (std::fabs(s) < 0.35f) return;
    ((x > 0.f) == (s > 0.f) ? g_shape_agree : g_shape_disagree)++;
    if (g_shape_agree + g_shape_disagree > 2000) { g_shape_agree = g_shape_agree / 2; g_shape_disagree = g_shape_disagree / 2; }
}
int ShapeSign()
{
    const int a = g_shape_agree, d = g_shape_disagree, total = a + d;
    int sign = 0;
    if (total >= 30) sign = a >= 0.85f * total ? 1 : (d >= 0.85f * total ? -1 : 0);
    if (sign && !g_shape_logged.exchange(true))
        DriverLog("Hand shape now decides left/right when it's clear (%d of %d samples agree, sign %+d)",
                  sign > 0 ? a : d, total, sign);
    return sign;
}
int ShapeSide(const FrametopHandData& h, float threshold)
{
    if (threshold <= 0.f) return 0;
    const int sign = ShapeSign();
    if (!sign) return 0;
    const float s = ShapeScore(h) * float(sign);
    return s > threshold ? 1 : (s < -threshold ? -1 : 0);
}
// The side to trust for a hand: its shape when that's clear, else the tracker's label.
bool IsRightHand(const FrametopHandData& h, float threshold)
{
    const int s = ShapeSide(h, threshold);
    return s != 0 ? s > 0 : h.right != 0;
}
std::atomic<unsigned> g_shape_overrides{ 0 };
}

// True while a real controller for this hand (not one of ours) is tracked and has moved
// within controller_idle_s: picking up a Frame controller hands it the role back.
bool MyControllerDeviceDriver::RealControllerInUse()
{
    Calibration c;
    { std::lock_guard<std::mutex> lk(calib_mutex_); c = calib_; }
    if (c.controller_idle_s <= 0.f) return false;

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - last_ctrl_check_).count() < 0.1f) return ctrl_in_use_;
    last_ctrl_check_ = now;

    vr::TrackedDevicePose_t my_poses[vr::k_unMaxTrackedDeviceCount]{};
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, my_poses, vr::k_unMaxTrackedDeviceCount);

    bool in_use = false;
    for (uint32_t i = 1; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (i == g_my_index[0].load() || i == g_my_index[1].load()) continue;
        const vr::TrackedDevicePose_t& p = my_poses[i];
        OtherDevice& o = others_[i];
        if (!p.bDeviceIsConnected || !p.bPoseIsValid) { o.init = false; continue; }

        vr::PropertyContainerHandle_t pc = vr::VRProperties()->TrackedDeviceToPropertyContainer(i);
        vr::ETrackedPropertyError err = vr::TrackedProp_Success;
        if (vr::VRProperties()->GetInt32Property(pc, vr::Prop_DeviceClass_Int32, &err) != vr::TrackedDeviceClass_Controller) continue;
        const int32_t role = vr::VRProperties()->GetInt32Property(pc, vr::Prop_ControllerRoleHint_Int32, &err);
        if (role != my_controller_role_) continue;

        const float pos[3] = { p.mDeviceToAbsoluteTracking.m[0][3], p.mDeviceToAbsoluteTracking.m[1][3], p.mDeviceToAbsoluteTracking.m[2][3] };
        // Moving = a hand is holding it: > 1 cm since it last moved, or SteamVR's own velocity above 0.35 m/s.
        const float speed = std::sqrt(p.vVelocity.v[0]*p.vVelocity.v[0] + p.vVelocity.v[1]*p.vVelocity.v[1] + p.vVelocity.v[2]*p.vVelocity.v[2]);
        if (!o.init) { o.init = true; std::memcpy(o.pos, pos, sizeof(pos)); o.moved = now; }
        else if (Dist(pos, o.pos) > 0.01f || speed > 0.35f) { std::memcpy(o.pos, pos, sizeof(pos)); o.moved = now; }
        if (std::chrono::duration<float>(now - o.moved).count() < c.controller_idle_s) in_use = true;
    }
    ctrl_in_use_ = in_use;
    return in_use;
}

int MyControllerDeviceDriver::SelectMyHand(const FrametopFrameData& frame, float hold_s) const
{
    const int my_side = my_controller_role_ == vr::TrackedControllerRole_RightHand ? 1 : 0;

    // Candidates: hands with usable confidence.
    int idx[2]; int n = 0;
    for (uint32_t i = 0; i < frame.count && i < 2; ++i)
        if (frame.hands[i].confidence >= 0.15f) idx[n++] = (int)i;
    if (n == 0) return -1;

    const float shape_thr = g_shape_threshold;
    for (int k = 0; k < n; ++k) LearnShapeSign(frame.hands[idx[k]]);
    // A hand whose shape clearly says it's the OTHER hand is never ours: taking it would
    // draw that hand mirrored (upside down) on this side.
    auto shape_says_other = [&](int i) {
        const int s = ShapeSide(frame.hands[i], shape_thr);
        return s != 0 && (s > 0) != (my_side == 1);
    };

    vr::TrackedDevicePose_t hmd_pose{};
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, &hmd_pose, 1);

    // Without a head pose we can't compare positions: fall back to labels (or shape).
    if (!hmd_pose.bPoseIsValid) {
        for (int k = 0; k < n; ++k)
            if (IsRightHand(frame.hands[idx[k]], shape_thr) == (my_side == 1)) return idx[k];
        return -1;
    }

    const vr::HmdVector3_t hp = HmdVector3_From34Matrix(hmd_pose.mDeviceToAbsoluteTracking);
    const vr::HmdQuaternion_t hq = HmdQuaternion_FromMatrix(hmd_pose.mDeviceToAbsoluteTracking);

    float w[2][3];     // world-space wrist of each candidate
    float head_x[2];   // head-frame x (+ = your right)
    for (int k = 0; k < n; ++k) {
        const float* p = frame.hands[idx[k]].pts[0];
        const vr::HmdVector3_t ww = hp + (vr::HmdVector3_t{ p[0], p[1], p[2] } * hq);
        w[k][0] = ww.v[0]; w[k][1] = ww.v[1]; w[k][2] = ww.v[2];
        head_x[k] = p[0];
    }

    // The detector sometimes reports ONE hand twice when hands are close together.
    // If both "hands" have nearly the same wrist and fingertips, keep only the better one.
    if (n == 2) {
        const float* a = frame.hands[idx[0]].pts[0];
        const float* b = frame.hands[idx[1]].pts[0];
        float tip_d = 0.f;
        const int tips[5] = { 4, 8, 12, 16, 20 };
        for (int t : tips) {
            const float* pa = frame.hands[idx[0]].pts[t];
            const float* pb = frame.hands[idx[1]].pts[t];
            tip_d += Dist(pa, pb) / 5.f;
        }
        // Two real wrists are never within 6 cm; the tracker's duplicates during a pinch
        // often are, with different finger guesses (and a flipped label).
        if ((Dist(a, b) < 0.04f && tip_d < 0.04f) || Dist(a, b) < 0.06f) {
            if (frame.hands[idx[1]].confidence > frame.hands[idx[0]].confidence) {
                idx[0] = idx[1]; w[0][0] = w[1][0]; w[0][1] = w[1][1]; w[0][2] = w[1][2]; head_x[0] = head_x[1];
            }
            n = 1;
        }
    }

    SideTrack side[2];
    { std::lock_guard<std::mutex> lk(g_side_mutex); side[0] = g_side[0]; side[1] = g_side[1]; }
    const auto now = std::chrono::steady_clock::now();
    const float recent_s = hold_s > 0.5f ? hold_s : 0.5f;
    bool recent[2];
    for (int s = 0; s < 2; ++s)
        recent[s] = side[s].valid &&
            std::chrono::duration<float>(now - side[s].t).count() < recent_s;

    if (n == 2) {
        bool first_is_left;
        if (recent[0] && recent[1]) {
            // Pick the pairing with the least total movement.
            const float a = Dist(w[0], side[0].wrist) + Dist(w[1], side[1].wrist);
            const float b = Dist(w[0], side[1].wrist) + Dist(w[1], side[0].wrist);
            first_is_left = a <= b;
        } else if (recent[0] || recent[1]) {
            const int s = recent[0] ? 0 : 1;
            const bool first_closer = Dist(w[0], side[s].wrist) <= Dist(w[1], side[s].wrist);
            first_is_left = (s == 0) ? first_closer : !first_closer;
        } else {
            // No history: the hand further left in view is the left hand.
            first_is_left = head_x[0] <= head_x[1];
        }
        const bool want_first = (my_side == 0) == first_is_left;
        const int k = want_first ? 0 : 1;
        if (shape_says_other(idx[k])) {
            // Both hands have fresh history: position is right, so this is a mirrored frame
            // (the tracker sometimes mirrors a hand mid-pinch). Hold, take nothing.
            if (recent[0] && recent[1]) { ++g_shape_overrides; return -1; }
            // Position paired them the wrong way round if the other one is clearly ours.
            if (!shape_says_other(idx[1 - k]) && ShapeSide(frame.hands[idx[1 - k]], shape_thr) != 0) {
                ++g_shape_overrides;
                return idx[1 - k];
            }
            ++g_shape_overrides;
            return -1;
        }
        // Swap guard: never take a hand sitting where the OTHER hand just was.
        if (recent[1 - my_side] && recent[my_side] &&
            Dist(w[k], side[1 - my_side].wrist) < 0.06f &&
            Dist(w[k], side[my_side].wrist) > 0.12f)
            return -1;
        return idx[k];
    }

    // One hand visible.
    // Position first: a hand within 15 cm of where one side's hand was in the last 2 s is
    // that side's hand, whatever its label or shape says. If its shape then says the other
    // hand, the tracker mirrored it (it does, mid-pinch): drop the frame so this side holds
    // its last good pose, and so the OTHER side can't take it and draw it upside down.
    {
        int owner = -1;
        float best = 0.15f;
        for (int s = 0; s < 2; ++s) {
            if (!side[s].valid || std::chrono::duration<float>(now - side[s].t).count() > 2.0f) continue;
            const float d = Dist(w[0], side[s].wrist);
            if (d < best) { best = d; owner = s; }
        }
        if (owner >= 0) {
            if (owner != my_side) return -1;
            if (shape_says_other(idx[0])) { ++g_shape_overrides; return -1; }
            return idx[0];
        }
    }

    const int label_side = IsRightHand(frame.hands[idx[0]], shape_thr) ? 1 : 0;
    if (shape_says_other(idx[0])) { ++g_shape_overrides; return -1; }
    int chosen = label_side;
    if (recent[0] && recent[1]) {
        chosen = Dist(w[0], side[0].wrist) <= Dist(w[0], side[1].wrist) ? 0 : 1;
    } else if (recent[0] || recent[1]) {
        const int s = recent[0] ? 0 : 1;
        const float d = Dist(w[0], side[s].wrist);
        if (d < 0.15f) chosen = s;          // clearly the hand we were just tracking
        else if (d > 0.30f) chosen = 1 - s; // clearly a different hand
        // in between: trust the label
    }
    if (chosen != my_side) return -1;
    if (recent[1 - my_side] && recent[my_side] &&
        Dist(w[0], side[1 - my_side].wrist) < 0.06f &&
        Dist(w[0], side[my_side].wrist) > 0.12f)
        return -1;
    return idx[0];
}


vr::DriverPose_t MyControllerDeviceDriver::GetPose()
{
    // The input thread computes the pose; this just hands out the latest one.
    std::lock_guard<std::mutex> lk(pose_mutex_);
    vr::DriverPose_t pose = last_pose_;
    pose.qWorldFromDriverRotation.w = 1.f;
    pose.qDriverFromHeadRotation.w = 1.f;
    if (pose.qRotation.w == 0.0 && pose.qRotation.x == 0.0 &&
        pose.qRotation.y == 0.0 && pose.qRotation.z == 0.0)
        pose.qRotation.w = 1.0;
    return pose;
}


void MyControllerDeviceDriver::MyInputThread()
{
    using clock = std::chrono::steady_clock;

    // Throttle calibration re-reads to once per ~500 ms.
    auto last_calib_check = clock::now();

    while (is_active_) {
        const auto now = clock::now();

        // ---- Hot-reload calibration.json once every 500 ms ----
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_calib_check).count() >= 500)
        {
            LoadCalibration();
            last_calib_check = now;
        }

        Calibration c;
        { std::lock_guard<std::mutex> lk(calib_mutex_); c = calib_; }
        g_shape_threshold = c.shape_sides;

        const bool want_right =
            my_controller_role_ == vr::TrackedControllerRole_RightHand;

        // ---- Find this hand in the newest frame ----
        const float hold_s = c.hold_ms / 1000.f;

        // ---- Find this hand in the newest frame (by position, not just label) ----
        FrametopFrameData frame{};
        FrametopFrameExt ext{};
        const FrametopHandData* hand = nullptr;
        if (FrameHandsLink::Get().Read(frame, &ext)) {
            if (frame.sequence != sel_seq_) {
                sel_seq_ = frame.sequence;
                sel_index_ = SelectMyHand(frame, hold_s);
            }
            if (sel_index_ >= 0) {
                hand = &frame.hands[sel_index_];
                cur_ext_ = ext.hands[sel_index_];
            } else {
                cur_ext_ = FrametopExtHand{};
            }
        }
        const bool ft_skeleton = (cur_ext_.fit_flags & FT_FIT_VALID) != 0;
        const float since_seen = have_hand_
            ? std::chrono::duration<float>(now - last_seen_time_).count() : 1e9f;

        bool updated = false;
        float frame_dt = 1.f / 60.f;

        // ---- New camera frame for this hand: transform, filter, rebuild ----
        if (hand && frame.sequence != last_seq_) {
            last_seq_ = frame.sequence;

            // The landmarks are in the head frame of the moment the cameras took them, so use
            // the head pose from then: hands stay put in the room while you turn your head.
            // frame.reserved = the frame's age in microseconds as the headset sent it (0 from
            // an older exporter, which falls back to the head pose now).
            float head_time = 0.f;
            if (c.capture_head_pose > 0.5f && frame.reserved > 0) {
                head_time = -(frame.reserved * 1e-6f + c.network_ms * 1e-3f);
                if (head_time < -0.15f) head_time = -0.15f;   // stale or odd: don't look further back
            }
            char sender[64] = {};
            FrameHandsLink::Get().LastSender(sender, sizeof sender);
            if (++age_log_count_ % 300 == 1)
                DriverLog("%s hand: frame age %.1f ms at send, head pose taken %.1f ms back, thumb_mode %.0f, "
                          "shape samples %d agree / %d disagree, shape overrides %u, hand fit %.1f mm, "
                          "finger lengths (mm) index %.0f/%.0f/%.0f middle %.0f/%.0f/%.0f, "
                          "Frametop skeleton %s (settled %.2f, fit %.1f mm%s), Frametop gestures %s, "
                          "%llu packets from %s",
                          want_right ? "right" : "left", frame.reserved / 1000.f, -head_time * 1000.f, c.thumb_mode,
                          g_shape_agree.load(), g_shape_disagree.load(), g_shape_overrides.load(),
                          last_fit_rms_ * 1000.0,
                          fitter_.Lengths(0)[0] * 1000, fitter_.Lengths(0)[1] * 1000, fitter_.Lengths(0)[2] * 1000,
                          fitter_.Lengths(1)[0] * 1000, fitter_.Lengths(1)[1] * 1000, fitter_.Lengths(1)[2] * 1000,
                          ft_skeleton ? "yes" : "no", cur_ext_.settled, cur_ext_.fit_rms * 1000.f,
                          (cur_ext_.fit_flags & FT_FIT_FLIPPED) ? ", flipped" : "",
                          (cur_ext_.gestures & FT_GEST_VALID) ? ((cur_ext_.gestures & FT_GEST_HAS_PINCH2) ? "yes (with middle pinch)" : "yes") : "no",
                          (unsigned long long)FrameHandsLink::Get().Packets(), sender);
            vr::TrackedDevicePose_t hmd_pose{};
            vr::VRServerDriverHost()->GetRawTrackedDevicePoses(head_time, &hmd_pose, 1);

            if (hmd_pose.bPoseIsValid) {
                const vr::HmdVector3_t hmd_position =
                    HmdVector3_From34Matrix(hmd_pose.mDeviceToAbsoluteTracking);
                const vr::HmdQuaternion_t hmd_orientation =
                    HmdQuaternion_FromMatrix(hmd_pose.mDeviceToAbsoluteTracking);

                // Head-frame landmarks (+ head-relative offset) -> world space.
                // Filtering in world space means turning your head doesn't drag the hand.
                const float ox = c.head_off_x + (want_right ? c.right_off_x : c.left_off_x);
                const float oy = c.head_off_y + (want_right ? c.right_off_y : c.left_off_y);
                const float oz = c.head_off_z + (want_right ? c.right_off_z : c.left_off_z);
                float raw[21][3];
                for (int i = 0; i < 21; ++i) {
                    const vr::HmdVector3_t p_head = {
                        hand->pts[i][0] + ox,
                        hand->pts[i][1] + oy,
                        hand->pts[i][2] + oz };
                    const vr::HmdVector3_t w = hmd_position + (p_head * hmd_orientation);
                    raw[i][0] = w.v[0]; raw[i][1] = w.v[1]; raw[i][2] = w.v[2];
                }

                // Restart cleanly after a dropout so the hand doesn't slide in from its old spot.
                const bool reacquired = !have_hand_ || since_seen > hold_s;
                if (reacquired) ResetFilters();

                // Reject a single-frame wrist jump > 25 cm (bad detection), up to 3 frames in a row.
                bool reject = false;
                if (!reacquired) {
                    const float dx = raw[0][0] - world_pts_[0][0];
                    const float dy = raw[0][1] - world_pts_[0][1];
                    const float dz = raw[0][2] - world_pts_[0][2];
                    if (dx*dx + dy*dy + dz*dz > 0.25f * 0.25f && reject_count_ < 3) {
                        reject = true;
                        ++reject_count_;
                    }
                }

                if (!reject) {
                    reject_count_ = 0;

                    float dt = reacquired ? (1.f / 60.f)
                        : std::chrono::duration<float>(now - last_frame_time_).count();
                    dt = dt < (1.f / 240.f) ? (1.f / 240.f) : (dt > 0.1f ? 0.1f : dt);
                    last_frame_time_ = now;
                    frame_dt = dt;

                    // Wrist and palm (position/rotation of the hand): wrist smoothing settings.
                    for (int i = 0; i < 21; ++i)
                        for (int k = 0; k < 3; ++k)
                            world_pts_[i][k] = (c.smoothing > 0.5f)
                                ? filt_[i][k].Update(raw[i][k], dt, c.min_cutoff_hz, c.beta, c.d_cutoff_hz)
                                : raw[i][k];

                    // Fingers: shape relative to the wrist, with their own lighter smoothing,
                    // so finger bends stay quick and free.
                    // Frametop 0.3.0's points are its fitted skeleton, already filtered on the
                    // headset: skeleton_finger_filter 0 skips this second smoothing.
                    const bool finger_filter = c.smoothing > 0.5f &&
                        !(ft_skeleton && c.skeleton_finger_filter < 0.5f);
                    for (int i = 0; i < 21; ++i)
                        for (int k = 0; k < 3; ++k) {
                            const float rel = raw[i][k] - raw[0][k];
                            if (!finger_filter) rel_filt_[i][k].init = false;   // restarts cleanly if turned back on
                            rel_pts_[i][k] = finger_filter
                                ? rel_filt_[i][k].Update(rel, dt, c.finger_pt_min_cutoff, c.finger_pt_beta, c.d_cutoff_hz)
                                : rel;
                        }

                    have_hand_ = true;
                    last_seen_time_ = now;
                    updated = true;

                    // Share where this hand is so the other hand's driver can tell them apart.
                    {
                        std::lock_guard<std::mutex> lk(g_side_mutex);
                        SideTrack& t = g_side[want_right ? 1 : 0];
                        t.wrist[0] = world_pts_[0][0];
                        t.wrist[1] = world_pts_[0][1];
                        t.wrist[2] = world_pts_[0][2];
                        t.t = now;
                        t.valid = true;
                    }
                }
            }
        }

        // ---- Curls / splays / bones from the filtered landmarks ----
        if (updated) {
            const float (*P)[3] = rel_pts_; // hand shape: fingers use their own smoothing
            // Hand-model fitting: everything below (curls, joint angles, aimed fingers,
            // gestures) then works from a hand a real hand could make.
            if (c.hand_fit > 0.5f) {
                HandFitter::Params fp;
                fp.smooth = c.hand_fit_smooth;
                fp.speed_rad = c.hand_fit_speed;
                fp.robust_m = c.hand_fit_robust_mm / 1000.0;
                fp.limits = true;
                last_fit_rms_ = fitter_.Fit(rel_pts_, want_right, fp, fit_pts_);
                if (last_fit_rms_ >= 0.0) P = fit_pts_;
            }

            // Returns 0 (straight) .. 1 (bent by max_angle) for the angle at vertex b.
            auto joint_curl = [&](int a, int b, int cc, float max_angle) -> float {
                const float ax = P[a][0] - P[b][0], ay = P[a][1] - P[b][1], az = P[a][2] - P[b][2];
                const float cx = P[cc][0] - P[b][0], cy = P[cc][1] - P[b][1], cz = P[cc][2] - P[b][2];
                const float la = std::sqrt(ax*ax + ay*ay + az*az);
                const float lc = std::sqrt(cx*cx + cy*cy + cz*cz);
                if (la < 0.00001f || lc < 0.00001f) return 0.f;
                float cosine = (ax*cx + ay*cy + az*cz) / (la*lc);
                cosine = cosine < -1.f ? -1.f : (cosine > 1.f ? 1.f : cosine);
                const float bend = 180.f - std::acos(cosine) * 57.2957795f;
                float v = bend / max_angle;
                return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            };

            // Same joints and anatomical limits as before.
            float raw_curl[5];
            raw_curl[0] = 0.55f * joint_curl(1, 2, 3, 55.f)   + 0.45f * joint_curl(2, 3, 4, 80.f);
            raw_curl[1] = 0.55f * joint_curl(5, 6, 7, 90.f)   + 0.45f * joint_curl(6, 7, 8, 110.f);
            raw_curl[2] = 0.55f * joint_curl(9, 10, 11, 90.f) + 0.45f * joint_curl(10, 11, 12, 110.f);
            raw_curl[3] = 0.55f * joint_curl(13, 14, 15, 90.f)+ 0.45f * joint_curl(14, 15, 16, 110.f);
            raw_curl[4] = 0.55f * joint_curl(17, 18, 19, 90.f)+ 0.45f * joint_curl(18, 19, 20, 110.f);

            // ---- Per-joint angles in the palm frame (for 1:1 fingers) ----
            auto sub = [&](int a, int b, float o[3]) { for (int k = 0; k < 3; ++k) o[k] = P[a][k] - P[b][k]; };
            auto dot = [](const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
            auto norm = [&](float v[3]) { const float l = std::sqrt(dot(v, v)); if (l < 1e-6f) return false; for (int k = 0; k < 3; ++k) v[k] /= l; return true; };
            auto ang = [&](const float a[3], const float b[3]) { float d = dot(a, b); d = d < -1.f ? -1.f : (d > 1.f ? 1.f : d); return std::acos(d); };

            // Palm axes: x = pinky->index knuckles, y = wrist->middle knuckle, z = palm normal.
            float ax_x[3], ax_y[3], ax_z[3];
            sub(5, 17, ax_x); sub(9, 0, ax_y);
            bool palm_ok = norm(ax_x) && norm(ax_y);
            if (palm_ok) {
                const float d = dot(ax_x, ax_y);
                for (int k = 0; k < 3; ++k) ax_y[k] -= d * ax_x[k];
                palm_ok = norm(ax_y);
                ax_z[0] = ax_x[1]*ax_y[2] - ax_x[2]*ax_y[1];
                ax_z[1] = ax_x[2]*ax_y[0] - ax_x[0]*ax_y[2];
                ax_z[2] = ax_x[0]*ax_y[1] - ax_x[1]*ax_y[0];
            }

            // raw_joint[0..11] = finger f*3 + (MCP flex, PIP, DIP); [12],[13] = thumb MCP, IP.
            float raw_joint[14] = {};
            float raw_abd[4] = {};
            const int chain[4][4] = { {5,6,7,8}, {9,10,11,12}, {13,14,15,16}, {17,18,19,20} };
            for (int f = 0; f < 4 && palm_ok; ++f) {
                float meta[3], s1[3], s2[3], s3[3];
                sub(chain[f][0], 0, meta); sub(chain[f][1], chain[f][0], s1);
                sub(chain[f][2], chain[f][1], s2); sub(chain[f][3], chain[f][2], s3);
                if (!norm(meta) || !norm(s1) || !norm(s2) || !norm(s3)) continue;

                // Sideways spread: in-palm-plane angle of the first segment vs the metacarpal,
                // positive toward the index/thumb side (same sign for both hands).
                float m_in[3], s_in[3];
                const float mz = dot(meta, ax_z), sz = dot(s1, ax_z);
                for (int k = 0; k < 3; ++k) { m_in[k] = meta[k] - mz * ax_z[k]; s_in[k] = s1[k] - sz * ax_z[k]; }
                float abd = 0.f;
                if (norm(m_in) && norm(s_in)) {
                    abd = ang(m_in, s_in);
                    if (dot(s_in, ax_x) < dot(m_in, ax_x)) abd = -abd;
                }
                const float total = ang(meta, s1);
                const float flex2 = total*total - abd*abd;
                const float flex = flex2 > 0.f ? std::sqrt(flex2) : 0.f;

                raw_joint[f*3 + 0] = flex;
                raw_joint[f*3 + 1] = ang(s1, s2);
                raw_joint[f*3 + 2] = ang(s2, s3);
                raw_abd[f] = abd;
            }
            {
                float t1[3], t2[3], t3[3];
                sub(2, 1, t1); sub(3, 2, t2); sub(4, 3, t3);
                if (norm(t1) && norm(t2) && norm(t3)) {
                    raw_joint[12] = ang(t1, t2);
                    raw_joint[13] = ang(t2, t3);
                }
            }

            // Curl-range capture in progress?
            if (recording_) {
                for (int i = 0; i < 14; ++i) {
                    if (raw_joint[i] < rec_joint_min_[i]) rec_joint_min_[i] = raw_joint[i];
                    if (raw_joint[i] > rec_joint_max_[i]) rec_joint_max_[i] = raw_joint[i];
                }
                for (int i = 0; i < 5; ++i) {
                    if (raw_curl[i] < rec_min_[i]) rec_min_[i] = raw_curl[i];
                    if (raw_curl[i] > rec_max_[i]) rec_max_[i] = raw_curl[i];
                }
                if (now >= record_end_) {
                    recording_ = false;
                    SaveCurlRange();
                    LoadCurlRange();
                }
            }

            auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

            // Map your own open..fist range onto the avatar's 0..1, then apply curl_multiplier.
            float curl[5];
            for (int i = 0; i < 5; ++i) {
                const float span = curl_max_[i] - curl_min_[i];
                const float mapped = span > 0.0001f ? (raw_curl[i] - curl_min_[i]) / span : raw_curl[i];
                curl[i] = clamp01(clamp01(mapped) * c.curl_mul);
            }

            MyFingerCurls curls{};
            curls.thumb  = curl[0];
            curls.index  = curl[1];
            curls.middle = curl[2];
            curls.ring   = curl[3];
            curls.pinky  = curl[4];

            // Splay: each finger's real sideways spread, measured in the palm's own frame
            // (the old version used world directions, so it changed as you turned around).
            // 15 deg of spread = 1.0, matching how the skeleton applies splay.
            auto splay_of = [&](int f) -> float {
                float v = raw_abd[f] / (15.f * 3.14159265f / 180.f);
                v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
                return v * c.splay_mul;
            };

            MyFingerSplays splays{};
            splays.thumb  = 0.f;
            splays.index  = splay_of(0);
            splays.middle = splay_of(1);
            splays.ring   = splay_of(2);
            splays.pinky  = splay_of(3);

            // ---- Contact amounts (used by contact assist and gestures) ----
            float pinch_amt = 0.f, r_pinch = 1.f, r_mid = 1.f;
            const float grip_amt = clamp01((curl[2] + curl[3] + curl[4]) / 3.f);
            {
                float d49[3], d412[3], d09[3];
                sub(4, 8, d49); sub(4, 12, d412); sub(9, 0, d09);
                const float palm = std::sqrt(dot(d09, d09));
                if (palm > 0.02f) {
                    r_pinch = std::sqrt(dot(d49, d49)) / palm;
                    r_mid   = std::sqrt(dot(d412, d412)) / palm;
                    pinch_amt = clamp01((0.45f - r_pinch) / (0.45f - 0.18f));
                }
            }

            vr::VRBoneTransform_t bones[31]{};
            if (my_hand_simulation_ && c.finger_mode > 0.5f) {
                // ---- 1:1 fingers: each joint from its own measured angle ----
                const float jdt = frame_dt;
                const float D2R = 3.14159265f / 180.f;
                // Anatomical limits: MCP 0-90, PIP 0-110, DIP 0-90 deg; thumb MCP 0-55, IP 0-80.
                const float limit[3] = { 90.f * D2R, 110.f * D2R, 90.f * D2R };

                float jf[14];
                for (int i = 0; i < 14; ++i) {
                    float a = raw_joint[i];
                    // Your calibrated open..fist range maps onto the full anatomical range.
                    if (joint_fist_[i] > joint_open_[i]) {
                        const float full = (i < 12) ? limit[i % 3] : (i == 12 ? 55.f * D2R : 80.f * D2R);
                        a = (a - joint_open_[i]) / (joint_fist_[i] - joint_open_[i]) * full;
                    }
                    jf[i] = joint_filt_[i].Update(a, jdt, c.finger_min_cutoff, c.finger_beta, 1.f);
                }

                MyHandJoints J{};
                for (int f = 0; f < 4; ++f) {
                    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
                    J.mcp_flex[f] = clampf(jf[f*3 + 0] * c.curl_mul, -10.f * D2R, limit[0]);
                    const float pip = clampf(jf[f*3 + 1] * c.curl_mul, 0.f, limit[1]);
                    // Fingertip joint is noisy to measure; blend with its natural link to the middle joint (~2/3).
                    const float dip = clampf(jf[f*3 + 2] * c.curl_mul, 0.f, limit[2]);
                    J.pip[f] = pip;
                    J.dip[f] = clampf(0.5f * dip + 0.5f * (pip * 2.f / 3.f), 0.f, limit[2]);
                    J.mcp_abd[f] = clampf(abd_filt_[f].Update(raw_abd[f], jdt, c.finger_min_cutoff, c.finger_beta, 1.f) * c.splay_mul,
                                          -25.f * D2R, 25.f * D2R);

                    // Contact assist: true joint angles don't make the avatar's fingers touch
                    // (its thumb can't swing across the palm like yours). While you pinch or make
                    // a fist, blend toward the old curl pose, which does touch.
                    const float cu = curl[f + 1];
                    const float w = c.contact_assist * (f == 0 ? (pinch_amt > grip_amt ? pinch_amt : grip_amt) : grip_amt);
                    if (w > 0.f) {
                        J.mcp_flex[f] += w * (cu * 90.f * D2R - J.mcp_flex[f]);
                        J.pip[f]      += w * ((5.f + cu * 80.f) * D2R - J.pip[f]);
                        J.dip[f]      += w * ((5.f + cu * 80.f) * D2R - J.dip[f]);
                    }
                }
                if (c.thumb_mode > 0.5f) {
                    J.thumb_mcp01 = clamp01(jf[12] / (55.f * D2R) * c.curl_mul);
                    J.thumb_ip01  = clamp01(jf[13] / (80.f * D2R) * c.curl_mul);
                } else {
                    // Old thumb: both joints follow the thumb curl, so it wraps over the fingers like before.
                    J.thumb_mcp01 = curl[0];
                    J.thumb_ip01  = curl[0];
                }

                my_hand_simulation_->ComputeSkeletonTransformsFromJoints(my_controller_role_, J, bones);
            } else if (my_hand_simulation_) {
                my_hand_simulation_->ComputeSkeletonTransforms(
                    my_controller_role_, curls, splays, bones);
            }
            // thumb_mode 2: every thumb bone points where your real thumb does (all axes).
            if (my_hand_simulation_ && c.thumb_mode > 1.5f)
                MyHandSimulation::AimThumbAtLandmarks(P, bones);
            // finger_mode 2: the fingers too (finger_aim_blend 1 = fully aimed, lower = mixed
            // with the per-joint pose, which is steadier and keeps the joint limits).
            if (my_hand_simulation_ && c.finger_mode > 1.5f)
                MyHandSimulation::AimFingersAtLandmarks(my_controller_role_, P, bones, c.finger_aim_blend,
                                                        c.finger_aim_limits > 0.5f);

            // Contact retargeting: your real thumb touching a finger -> the skeleton's tips meet
            // too (the avatar's fingers aren't your length, so copied angles alone miss).
            if (my_hand_simulation_ && c.touch_retarget > 0.5f && c.thumb_mode > 1.5f) {
                static const int tips[4] = { 8, 12, 16, 20 };
                float w[4];
                for (int f = 0; f < 4; ++f) {
                    float d[3]; sub(4, tips[f], d);
                    const float mm = std::sqrt(dot(d, d)) * 1000.f;
                    float t = (c.touch_far_mm - mm) / (c.touch_far_mm - c.touch_near_mm);
                    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                    w[f] = t * t * (3.f - 2.f * t);   // smoothstep: no kink where it starts
                }
                MyHandSimulation::RetargetTouch(bones, w, c.touch_pad_mm / 1000.f, c.touch_finger_share);
            }

            // ---- Gestures ----
            if (c.gestures > 0.5f) {
                if (r_pinch < 1.f) {
                    const float grip_v = grip_amt;

                    // Frametop 0.3.0 detects pinches, middle pinches and fists itself (with the
                    // v20a model, trained on those touches): use its answer when it sends one.
                    const bool ft_gest = c.ft_gestures > 0.5f && (cur_ext_.gestures & FT_GEST_VALID);
                    float pinch_v = pinch_amt;
                    if (ft_gest) {
                        grip_down_  = (cur_ext_.gestures & FT_GEST_GRIP) != 0;
                        pinch_down_ = (cur_ext_.gestures & FT_GEST_PINCH) != 0;
                        // The analog value follows Frametop's pinch strength, kept consistent with the click.
                        pinch_v = cur_ext_.pinch_strength > pinch_v ? cur_ext_.pinch_strength : pinch_v;
                        pinch_v = pinch_down_ ? 1.f : (pinch_v > 0.7f ? 0.7f : pinch_v);
                        if (grip_down_) pinch_v = 0.f;
                    } else {
                        // Grip (fist): on above 0.75, off below 0.5.
                        if (!grip_down_ && grip_v > 0.75f) grip_down_ = true;
                        else if (grip_down_ && grip_v < 0.5f) grip_down_ = false;

                        // Pinch (thumb tip to index tip). Ignored while making a fist.
                        if (grip_down_) pinch_v = 0.f;
                        if (!pinch_down_ && pinch_v > 0.8f) pinch_down_ = true;
                        else if (pinch_down_ && pinch_v < 0.5f) pinch_down_ = false;
                    }

                    if (ft_gest && (cur_ext_.gestures & FT_GEST_HAS_PINCH2)) {
                        // Frametop's middle pinch (it already keeps it apart from an index pinch).
                        menu_down_ = (cur_ext_.gestures & FT_GEST_PINCH2) != 0 && !grip_down_;
                        menu_candidate_ = false;
                    } else {
                        // Menu (thumb tip to middle tip, index straight), held 150 ms to avoid accidents.
                        const bool menu_pose = r_mid < 0.2f && r_pinch > 0.35f && curl[1] < 0.4f && !grip_down_;
                        if (menu_pose && !menu_candidate_) { menu_candidate_ = true; menu_start_ = now; }
                        if (!menu_pose) { menu_candidate_ = false; menu_down_ = false; }
                        if (menu_candidate_ && std::chrono::duration<float>(now - menu_start_).count() > 0.15f)
                            menu_down_ = true;
                    }

                    vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_trigger_value], pinch_v, 0);
                    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_trigger_click], pinch_down_, 0);
                    vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_grip_value], grip_v, 0);
                    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_grip_click], grip_down_, 0);
                    vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_b_click], menu_down_, 0);
                }
            } else if (pinch_down_ || grip_down_ || menu_down_) {
                ReleaseGestures();
            }

            vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_indexFinger],  curls.index,  0);
            vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_middleFinger], curls.middle, 0);
            vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_ringFinger],   curls.ring,   0);
            vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_pinkyFinger],  curls.pinky,  0);

            // ---- Pose: filtered wrist, palm orientation from filtered landmarks ----
            vr::DriverPose_t pose = {};
            pose.qWorldFromDriverRotation.w = 1.f;
            pose.qDriverFromHeadRotation.w = 1.f;

            // Landmarks are already in world space, so this equals the old
            // hmd_orientation * ComputePalmOrientation(head-frame points).
            pose.qRotation = MyHandSimulation::ComputePalmOrientation(world_pts_);

            const vr::HmdQuaternion_t calibRot = HmdQuaternion_FromEulerAngles(
                static_cast<double>(c.roll_deg  * (float)M_PI / 180.f),
                static_cast<double>(c.pitch_deg * (float)M_PI / 180.f),
                static_cast<double>(c.yaw_deg   * (float)M_PI / 180.f));
            pose.qRotation = pose.qRotation * calibRot;

            // Legacy world-space offsets still apply, unchanged.
            pose.vecPosition[0] = world_pts_[0][0] + c.offset_x;
            pose.vecPosition[1] = world_pts_[0][1] + c.offset_y;
            pose.vecPosition[2] = world_pts_[0][2] + c.offset_z;

            // Hand-local offset: turns with your hand, matching how VRChat places the avatar hand.
            if (palm_ok) {
                // Palm-side normal: x cross y points out of the palm for a right hand, out of the back for a left.
                const float zs = want_right ? 1.f : -1.f;
                for (int k = 0; k < 3; ++k)
                    pose.vecPosition[k] += ax_x[k] * c.hand_off_x + ax_y[k] * c.hand_off_y + ax_z[k] * zs * c.hand_off_z;
            }

            // Filtered wrist velocity lets SteamVR predict ahead and hide some latency.
            for (int k = 0; k < 3; ++k) {
                float v = (c.smoothing > 0.5f ? filt_[0][k].dx : 0.f) * c.prediction;
                v = v < -3.f ? -3.f : (v > 3.f ? 3.f : v);
                pose.vecVelocity[k] = v;
            }

            pose.poseIsValid = true;
            pose.deviceIsConnected = true;
            pose.result = vr::TrackingResult_Running_OK;

            std::lock_guard<std::mutex> lk(pose_mutex_);
            last_pose_ = pose;
            std::memcpy(last_bones_, bones, sizeof(bones));
            have_bones_ = true;
            new_bones_ = true;
        }
        // ---- No hand this frame ----
        else if (!hand) {
            if (!(have_hand_ && since_seen <= hold_s) && (pinch_down_ || grip_down_ || menu_down_))
                ReleaseGestures();
            std::lock_guard<std::mutex> lk(pose_mutex_);
            if (have_hand_ && since_seen <= hold_s) {
                // Brief dropout: hold the last pose still instead of snapping away.
                last_pose_.vecVelocity[0] = last_pose_.vecVelocity[1] = last_pose_.vecVelocity[2] = 0.0;
            } else {
                // Lost for longer than hold_ms: stay connected but report not tracking.
                have_hand_ = false;
                last_pose_.poseIsValid = false;
                last_pose_.deviceIsConnected = true;
                last_pose_.result = vr::TrackingResult_Running_OutOfRange;
                last_pose_.vecVelocity[0] = last_pose_.vecVelocity[1] = last_pose_.vecVelocity[2] = 0.0;
            }
        }

        // ---- Connect while this hand is tracked and its real controller is idle ----
        const float seen_ago = std::chrono::duration<float>(clock::now() - last_seen_time_).count();
        // Default: keep the hand role and stay connected (like the first working version); a
        // lost hand just reports "not tracking". share_with_controllers 1: hand off as before.
        const bool want_connected = c.share_with_controllers > 0.5f
            ? (seen_ago <= c.disconnect_s && !RealControllerInUse())
            : true;
        if (want_connected != connected_) {
            // Claim the hand before connecting; give it back when disconnecting.
            vr::VRProperties()->SetInt32Property(
                vr::VRProperties()->TrackedDeviceToPropertyContainer(my_controller_index_),
                vr::Prop_ControllerRoleHint_Int32,
                want_connected ? int32_t(my_controller_role_) : int32_t(vr::TrackedControllerRole_OptOut));
            if (!want_connected && (pinch_down_ || grip_down_ || menu_down_)) ReleaseGestures();
            DriverLog("%s hand %s%s", want_right ? "right" : "left", want_connected ? "connected" : "disconnected",
                      (!want_connected && ctrl_in_use_) ? " (real controller in use)" : "");
            connected_ = want_connected;
            reported_off_ = false;
        }
        if (!connected_) {
            // Disconnected: the pose goes out once, it says the same thing every frame after.
            if (!reported_off_) {
                vr::DriverPose_t off = GetPose();
                off.poseIsValid = false;
                off.deviceIsConnected = false;
                off.result = vr::TrackingResult_Uninitialized;
                vr::VRServerDriverHost()->TrackedDevicePoseUpdated(my_controller_index_, off, sizeof(vr::DriverPose_t));
                reported_off_ = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // ---- Publish ----
        vr::VRBoneTransform_t bones_copy[31];
        bool send_bones = false;
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lk(pose_mutex_);
            if (have_bones_) { std::memcpy(bones_copy, last_bones_, sizeof(bones_copy)); send_bones = true; }
            fresh = new_bones_;
            new_bones_ = false;
        }
        // Fingers between camera frames: frames come ~25 times a second, the headset draws at
        // 90, so without this the fingers jump to each new pose. A new pose is reached over the
        // time frames have recently taken to arrive (finger_interp_ms, 0 = measured), starting
        // from what was last shown, so the fingers move steadily with no overshoot. Costs about
        // half a frame (~20 ms) of extra finger delay; finger_interp 0 turns it off.
        if (send_bones && c.finger_interp > 0.5f) {
            const auto tnow = clock::now();
            if (fresh) {
                if (interp_have_) {
                    const float since = std::chrono::duration<float>(tnow - interp_start_).count();
                    if (since > 0.f && since < 0.25f)
                        frame_interval_s_ += 0.2f * (since - frame_interval_s_);   // recent frame spacing
                    std::memcpy(interp_from_, interp_out_, sizeof(interp_from_));
                } else {
                    std::memcpy(interp_from_, bones_copy, sizeof(interp_from_));
                }
                interp_start_ = tnow;
                interp_have_ = true;
                float dur = c.finger_interp_ms > 0.f ? c.finger_interp_ms / 1000.f : frame_interval_s_;
                interp_dur_s_ = dur < 0.012f ? 0.012f : (dur > 0.06f ? 0.06f : dur);
            }
            const float t = std::chrono::duration<float>(tnow - interp_start_).count() / interp_dur_s_;
            MyHandSimulation::BlendBones(interp_from_, bones_copy, t, interp_out_);
            std::memcpy(bones_copy, interp_out_, sizeof(bones_copy));
        } else if (send_bones) {
            std::memcpy(interp_out_, bones_copy, sizeof(interp_out_));
            interp_have_ = false;
        }
        if (send_bones) {
            vr::VRDriverInput()->UpdateSkeletonComponent(
                input_handles_[MyComponent_skeleton],
                vr::VRSkeletalMotionRange_WithController, bones_copy, 31);
            vr::VRDriverInput()->UpdateSkeletonComponent(
                input_handles_[MyComponent_skeleton],
                vr::VRSkeletalMotionRange_WithoutController, bones_copy, 31);
        }

        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            my_controller_index_, GetPose(), sizeof(vr::DriverPose_t));

        // Camera data is ~60 Hz; 2 ms polling picks up each new frame quickly.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}



//-----------------------------------------------------------------------------
// Purpose: Our IServerTrackedDeviceProvider needs our serial number to add us to vrserver.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
const std::string &MyControllerDeviceDriver::MyGetSerialNumber()
{
	return my_controller_serial_number_;
}