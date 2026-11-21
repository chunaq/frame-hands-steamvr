//============ Copyright (c) Valve Corporation, All rights reserved. ============
// Inspired by Moshi Turner's code from Monado https://gitlab.freedesktop.org/monado/monado/-/blob/main/src/xrt/auxiliary/util/u_hand_simulation.c
#include "hand_simulation.h"
#include "vrmath.h"
#include <cstring>

struct HandSimSplayableJoint
{
	vr::HmdVector2_t swing = { 0.f, 0.f };
	float twist = 0.f;
};

struct HandSimJoint
{
	float rotation = 0.f;
};

struct HandSimThumb
{
	HandSimSplayableJoint metacarpal;
	HandSimSplayableJoint proximal;
	HandSimJoint distal;
};

struct HandSimFinger
{
	HandSimSplayableJoint metacarpal;
	HandSimSplayableJoint proximal;
	HandSimJoint intermediate;
	HandSimJoint distal;
};

struct HandSimHand
{
	vr::ETrackedControllerRole role; // Whether we're left or right handed

	HandSimThumb thumb;

	// index, middle, ring, pinky (in that order)
	HandSimFinger fingers[4];
};

// rough finger lengths
static const float finger_joint_lengths[5][5] = {
	{ 0.05f, 0.05f, 0.035f, 0.025f, 0.f },	  // thumb
	{ 0.03f, 0.073f, 0.045f, 0.025f, 0.02f }, // index
	{ 0.01f, 0.091f, 0.049f, 0.03f, 0.02f },  // middle
	{ 0.02f, 0.073f, 0.045f, 0.03f, 0.03f },  // ring
	{ 0.03f, 0.067f, 0.03f, 0.025f, 0.02f },  // pinky
};

//-----------------------------------------------------------------------------
// Purpose: Sets up a default open hand pose which can then be manipulated with curl and splay values.
// Much of this is very approximate.
//-----------------------------------------------------------------------------
static void InitHand(HandSimHand& out_hand)
{
	// Default curls for each of the fingers
	for (auto& finger : out_hand.fingers)
	{
		finger.metacarpal.swing.v[1] = 0.f;
		finger.metacarpal.twist = 0.f;

		finger.proximal.swing.v[1] = DEG_TO_RAD(10);
		finger.intermediate.rotation = DEG_TO_RAD(5.f);

		finger.intermediate.rotation = DEG_TO_RAD(5.f);
		finger.distal.rotation = DEG_TO_RAD(5.f);
	}

	// Curl, splay and "twist" the thumb into a default position
	out_hand.thumb.metacarpal.swing.v[0] = DEG_TO_RAD(10);
	out_hand.thumb.metacarpal.swing.v[1] = DEG_TO_RAD(40);
	out_hand.thumb.metacarpal.twist = DEG_TO_RAD(70);

	out_hand.thumb.proximal.swing.v[0] = 0.f;
	out_hand.thumb.proximal.swing.v[1] = 0.f;
	out_hand.thumb.proximal.twist = 0.f;

	out_hand.thumb.distal.rotation = 0.f;

	// Metacarpal splays for each of the fingers. Do this to add some spread to the fingers' metacarpal (the joints that connect to the wrist).
	out_hand.fingers[0].metacarpal.swing.v[1] = DEG_TO_RAD(13.f);
	out_hand.fingers[1].metacarpal.swing.v[1] = DEG_TO_RAD(-0.f);
	out_hand.fingers[2].metacarpal.swing.v[1] = DEG_TO_RAD(-15.f);
	out_hand.fingers[3].metacarpal.swing.v[1] = DEG_TO_RAD(-27.f);

	// Proximal splays for each of the fingers. Do this to add some spread to the finger's proximal joints. These joints are also the ones that splay with the values provided.
	out_hand.fingers[0].proximal.swing.v[1] = DEG_TO_RAD(3.f);
	out_hand.fingers[1].proximal.swing.v[1] = DEG_TO_RAD(0.f);
	out_hand.fingers[2].proximal.swing.v[1] = DEG_TO_RAD(-1.f);
	out_hand.fingers[3].proximal.swing.v[1] = DEG_TO_RAD(-2.f);
}

//-----------------------------------------------------------------------------
// Purpose: All fingers (expect thumb) have basically the same properties in terms of curl and splay.
// You can splay only the proximal, and curl the proximal, intermediate and distal joints.
// This applies the curl and splay to these joints.
//-----------------------------------------------------------------------------
static void ApplyGenericFingerTransform(const float curl, const float splay, HandSimFinger& out_finger)
{
	out_finger.metacarpal.swing.v[0] += DEG_TO_RAD(curl * 5.f); // the metacarpal only curls a little

	out_finger.proximal.swing.v[0] += DEG_TO_RAD(curl * 90.f);
	out_finger.proximal.swing.v[1] += DEG_TO_RAD(splay * 15.f);

	out_finger.intermediate.rotation += DEG_TO_RAD(curl * 80.f);
	out_finger.distal.rotation += DEG_TO_RAD(curl * 80.f);
}


//-----------------------------------------------------------------------------
// Purpose: Takes an orientation and length of a joint and converts it to a vr::VRBoneTransform_t
// We have to do convert the quaternion as we use vr::HmdQuaternion_t for our representation, but OpenVR wants vr::HmdQuaternionf_t
//-----------------------------------------------------------------------------
static void ComputeBoneTransform(const vr::ETrackedControllerRole role, const vr::HmdQuaternion_t& orientation, const vr::HmdVector3_t& position, vr::VRBoneTransform_t& out_transform)
{
	// Note that we use vr::HmdQuaternion_t but the Skeletal Input API needs vr::HmdQuaternionf_t, so we'll use this helper to convert
	HmdQuaternion_ConvertQuaternion(orientation, out_transform.orientation);

	// Fit the HmdVector3_t into the HmdVector4_t that the Skeletal Input API accepts
	HmdVector3_CovertVector(position, out_transform.position);
	out_transform.position.v[3] = 1.f;

	//"up" axis is flipped between hands, so inverse the joint length for the right hand, as we base all our computations of the left skeleton pose.
	if (role == vr::TrackedControllerRole_RightHand)
	{
		out_transform.position.v[0] *= -1.f;
	}
}


//-----------------------------------------------------------------------------
// Purpose: This is just a little helper function to make our calls when trying to compute each bone a little simpler by specifying just a float for joint length, instead of the whole vector
//-----------------------------------------------------------------------------
static void ComputeBoneTransform(const vr::ETrackedControllerRole role, const vr::HmdQuaternion_t& orientation, const float joint_length, vr::VRBoneTransform_t& out_transform)
{
	ComputeBoneTransform(role, orientation, { joint_length, 0.f, 0.f }, out_transform);
}

//-----------------------------------------------------------------------------
// Purpose: Takes an orientation and length of a joint and converts it to a vr::VRBoneTransform_t, but with the orientation applied to the offset
// We have to do convert the quaternion as we use vr::HmdQuaternion_t for our representation, but OpenVR wants vr::HmdQuaternionf_t
//-----------------------------------------------------------------------------
static void ComputeBoneTransformMetacarpal(const vr::ETrackedControllerRole role, const vr::HmdQuaternion_t& orientation, const float joint_length, vr::VRBoneTransform_t& out_transform)
{
	const vr::HmdVector3_t offset = { joint_length, 0.f, 0.f };

	/*
	The Skeletal Input API is designed to be used with common industry tools, such as Maya, to make it easier to move content from 3D editors into VR.
	The way that FBX handles conversion to a different coordinate system is to transform the root bone (wrist), then counter-transform the root's children to account for the root's change, but then
	leave the local coordinate systems of the remaining bones as-is This means that the metacarpals will be rotated 90 degrees from the wrist if trying to build a skeleton programmatically. So, we
	apply this extra rotation to the metacarpals to account for this.
	*/
	vr::HmdQuaternion_t magic = { 0.5f, 0.5f, -0.5f, 0.5f };

	vr::HmdQuaternion_t bone_orientation = magic * orientation;

	// Rotate the offset vector by the orientation
	vr::HmdVector3_t bone_position = offset * bone_orientation;

	//"up" axis is flipped between hands, so we need to inverse the x and y axis for the right hand, as all our calculations are based on the left hand currently.
	if (role == vr::TrackedControllerRole_RightHand)
	{
		std::swap(bone_orientation.w, bone_orientation.x);
		std::swap(bone_orientation.y, bone_orientation.z);

		bone_orientation.x *= -1.f;
		bone_orientation.z *= -1.f;
	}

	// pass off to put the position and orientation we've calculated into the skeleton
	ComputeBoneTransform(role, bone_orientation, bone_position, out_transform);
}

//-----------------------------------------------------------------------------
// Purpose: Get the OpenVR bone index from a finger (index=0, middle=1...) and the bone position in the finger. Used in ComputeSkeletalTransforms
//-----------------------------------------------------------------------------
static int CalculateBoneTransformPositionFromFinger(int finger, int bone_in_finger)
{
	const int bone_transform_finger_start_offset = eBone_IndexFinger0;

	const int result = bone_transform_finger_start_offset + finger * 5 + bone_in_finger;

	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Given the curls and splays, convert this to a vr::VRBoneTransform_t array
//-----------------------------------------------------------------------------
static void ComputeSkeletalTransforms(const HandSimHand& hand, vr::VRBoneTransform_t* out_transforms)
{
	// Do the thumb separately as it's special and not like the other fingers
	ComputeBoneTransformMetacarpal(
		hand.role, HmdQuaternion_FromSwingTwist(hand.thumb.metacarpal.swing, hand.thumb.metacarpal.twist), finger_joint_lengths[0][0], out_transforms[eBone_Thumb0]);
	ComputeBoneTransform(hand.role, HmdQuaternion_FromSwingTwist(hand.thumb.proximal.swing, hand.thumb.metacarpal.twist), finger_joint_lengths[0][1], out_transforms[eBone_Thumb1]);
	ComputeBoneTransform(hand.role, HmdQuaternion_FromEulerAngles(hand.thumb.distal.rotation, 0.f, 0.f), finger_joint_lengths[0][2], out_transforms[eBone_Thumb2]);
	ComputeBoneTransform(hand.role, HmdQuaternion_Identity, finger_joint_lengths[0][3], out_transforms[eBone_Thumb3]);

	// index, middle, ring, pinky
	// We can do these all together as they all require the same calculations
	for (int finger = 0; finger < 4; finger++)
	{
		ComputeBoneTransformMetacarpal(hand.role, HmdQuaternion_FromSwingTwist(hand.fingers[finger].metacarpal.swing, hand.fingers[finger].metacarpal.twist),
			finger_joint_lengths[finger + 1][0], out_transforms[CalculateBoneTransformPositionFromFinger(finger, 0)]);

		ComputeBoneTransform(hand.role, HmdQuaternion_FromSwingTwist(hand.fingers[finger].proximal.swing, hand.fingers[finger].proximal.twist), finger_joint_lengths[finger + 1][1],
			out_transforms[CalculateBoneTransformPositionFromFinger(finger, 1)]);

		ComputeBoneTransform(hand.role, HmdQuaternion_FromEulerAngles(hand.fingers[finger].intermediate.rotation, 0.f, 0.f), finger_joint_lengths[finger + 1][2],
			out_transforms[CalculateBoneTransformPositionFromFinger(finger, 2)]);

		ComputeBoneTransform(hand.role, HmdQuaternion_FromEulerAngles(hand.fingers[finger].distal.rotation, 0.f, 0.f), finger_joint_lengths[finger + 1][3],
			out_transforms[CalculateBoneTransformPositionFromFinger(finger, 3)]);

		ComputeBoneTransform(hand.role, HmdQuaternion_Identity, finger_joint_lengths[finger + 1][4], out_transforms[CalculateBoneTransformPositionFromFinger(finger, 4)]);
	}
}

void MyHandSimulation::ComputeSkeletonTransforms(vr::ETrackedControllerRole role, const MyFingerCurls& curls, const MyFingerSplays& splays, vr::VRBoneTransform_t* out_transforms)
{
	// This is where we store our internal representation of curls and splays for the hand.
	HandSimHand hand{};

	// Set the handed-ness of the current skeleton (left or right hand)
	hand.role = role;

	// root bone. This is just 0s. It's aligned to /pose/raw.
	out_transforms[0] = { { 0.000000f, 0.000000f, 0.000000f, 1.000000f }, { 1.000000f, -0.000000f, -0.000000f, 0.000000f } };

	// wrist bone. This was taken from the index controller pose, and is used to place the wrist
	// at the correct location relative to where /pose/raw is.
	out_transforms[1] = { { 0.000000f, 0.000000f, 0.000000f, 1.000000f }, { 1.000000f, 0.000000f, 0.000000f, 0.000000f } }; // Raw pose is already at the tracked wrist.

	//"up" axis is flipped between hands so invert
	if (role == vr::TrackedControllerRole_RightHand)
	{
		out_transforms[1].position.v[0] *= -1.f;

		out_transforms[1].orientation.y *= -1.f;
		out_transforms[1].orientation.z *= -1.f;
	}

	// Initialize a default hand pose
	InitHand(hand);

	// We need to apply the curls and splays separately to the thumb as it's special
	hand.thumb.metacarpal.swing.v[0] += DEG_TO_RAD(curls.thumb * 5.f);
	hand.thumb.metacarpal.swing.v[1] += DEG_TO_RAD(splays.thumb * 5.f);
	hand.thumb.metacarpal.twist = 0.f;

	hand.thumb.proximal.swing.v[0] += DEG_TO_RAD(curls.thumb * 90.f);
	hand.thumb.proximal.swing.v[1] += DEG_TO_RAD(splays.thumb * 20.f);
	hand.thumb.proximal.twist = 0.f;

	hand.thumb.distal.rotation += DEG_TO_RAD(curls.thumb * 90.f);

	// But we can batch up the fingers with a generic apply function.
	ApplyGenericFingerTransform(curls.index, splays.index, hand.fingers[0]);
	ApplyGenericFingerTransform(curls.middle, splays.middle, hand.fingers[1]);
	ApplyGenericFingerTransform(curls.ring, splays.ring, hand.fingers[2]);
	ApplyGenericFingerTransform(curls.pinky, splays.pinky, hand.fingers[3]);

	// Now compute
	ComputeSkeletalTransforms(hand, out_transforms);
}
// -----------------------------------------------------------------------------
// Frametop: landmark-driven OpenVR skeletal transforms.
// Landmark positions are head-frame metres. The tracked device pose is placed
// at the wrist and rotated to the palm frame; these bones are wrist-relative.
// -----------------------------------------------------------------------------

static vr::HmdVector3_t FT_VectorSub(
    const vr::HmdVector3_t& a, const vr::HmdVector3_t& b)
{
    return { a.v[0]-b.v[0], a.v[1]-b.v[1], a.v[2]-b.v[2] };
}

static vr::HmdVector3_t FT_VectorScale(
    const vr::HmdVector3_t& a, float scale)
{
    return { a.v[0]*scale, a.v[1]*scale, a.v[2]*scale };
}

static float FT_VectorDot(
    const vr::HmdVector3_t& a, const vr::HmdVector3_t& b)
{
    return a.v[0]*b.v[0] + a.v[1]*b.v[1] + a.v[2]*b.v[2];
}

static vr::HmdVector3_t FT_VectorCross(
    const vr::HmdVector3_t& a, const vr::HmdVector3_t& b)
{
    return {
        a.v[1]*b.v[2]-a.v[2]*b.v[1],
        a.v[2]*b.v[0]-a.v[0]*b.v[2],
        a.v[0]*b.v[1]-a.v[1]*b.v[0]
    };
}

static bool FT_VectorNormalize(vr::HmdVector3_t& v)
{
    const float n = std::sqrt(FT_VectorDot(v,v));
    if (n < 0.00001f) return false;
    v = FT_VectorScale(v, 1.f/n);
    return true;
}

static vr::HmdQuaternion_t FT_QuaternionNormalize(
    const vr::HmdQuaternion_t& q)
{
    const double n = std::sqrt(
        q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (n < 0.000001) return HmdQuaternion_Identity;
    return { q.w/n, q.x/n, q.y/n, q.z/n };
}

static vr::HmdQuaternion_t FT_QuaternionFromBasis(
    const vr::HmdVector3_t& x,
    const vr::HmdVector3_t& y,
    const vr::HmdVector3_t& z)
{
    vr::HmdMatrix34_t m{};
    for (int r=0; r<3; ++r) {
        m.m[r][0] = x.v[r];
        m.m[r][1] = y.v[r];
        m.m[r][2] = z.v[r];
    }
    return FT_QuaternionNormalize(HmdQuaternion_FromMatrix(m));
}

static vr::HmdVector3_t FT_Landmark(
    const float landmarks[21][3], int index)
{
    return {
        landmarks[index][0],
        landmarks[index][1],
        landmarks[index][2]
    };
}

vr::HmdQuaternion_t MyHandSimulation::ComputePalmOrientation(
    const float landmarks[21][3])
{
    // Local X: across the palm, pinky toward index.
    vr::HmdVector3_t x = FT_VectorSub(
        FT_Landmark(landmarks, 5), FT_Landmark(landmarks, 17));

    // Local Y: wrist toward middle finger.
    vr::HmdVector3_t y = FT_VectorSub(
        FT_Landmark(landmarks, 9), FT_Landmark(landmarks, 0));

    if (!FT_VectorNormalize(x) || !FT_VectorNormalize(y))
        return HmdQuaternion_Identity;

    y = FT_VectorSub(y, FT_VectorScale(x, FT_VectorDot(x, y)));
    if (!FT_VectorNormalize(y))
        return HmdQuaternion_Identity;

    // Local Z: palm normal.
    vr::HmdVector3_t z = FT_VectorCross(x, y);
    if (!FT_VectorNormalize(z))
        return HmdQuaternion_Identity;

    y = FT_VectorCross(z, x);
    if (!FT_VectorNormalize(y))
        return HmdQuaternion_Identity;

    return FT_QuaternionFromBasis(x, y, z);
}

static vr::HmdQuaternion_t FT_SegmentOrientation(
    const vr::HmdVector3_t& direction)
{
    vr::HmdVector3_t x = direction;
    if (!FT_VectorNormalize(x)) return HmdQuaternion_Identity;

    // Palm-local +Y stabilizes the roll of each finger segment.
    vr::HmdVector3_t y = { 0.f, 1.f, 0.f };
    y = FT_VectorSub(y, FT_VectorScale(x, FT_VectorDot(y,x)));

    if (!FT_VectorNormalize(y)) {
        y = { 0.f, 0.f, 1.f };
        y = FT_VectorSub(y, FT_VectorScale(x, FT_VectorDot(y,x)));
        if (!FT_VectorNormalize(y)) return HmdQuaternion_Identity;
    }

    vr::HmdVector3_t z = FT_VectorCross(x,y);
    if (!FT_VectorNormalize(z)) return HmdQuaternion_Identity;

    y = FT_VectorCross(z,x);
    if (!FT_VectorNormalize(y)) return HmdQuaternion_Identity;

    return FT_QuaternionFromBasis(x,y,z);
}

static vr::HmdQuaternion_t FT_WriteLandmarkBone(
    const vr::HmdVector3_t& start,
    const vr::HmdVector3_t& end,
    const vr::HmdQuaternion_t& parentGlobal,
    vr::VRBoneTransform_t& output)
{
    const vr::HmdVector3_t delta = FT_VectorSub(end,start);
    const vr::HmdQuaternion_t parentInverse = -parentGlobal;

    if (FT_VectorDot(delta,delta) < 0.00000001f) {
        output.position = { 0.f,0.f,0.f,1.f };
        output.orientation = { 1.f,0.f,0.f,0.f };
        return parentGlobal;
    }

    const vr::HmdQuaternion_t childGlobal =
        FT_SegmentOrientation(delta);

    const vr::HmdVector3_t localPosition = delta * parentInverse;
    const vr::HmdQuaternion_t localOrientation =
        FT_QuaternionNormalize(parentInverse * childGlobal);

    HmdVector3_CovertVector(localPosition,output.position);
    output.position.v[3] = 1.f;
    HmdQuaternion_ConvertQuaternion(localOrientation,output.orientation);

    return childGlobal;
}

void MyHandSimulation::ComputeSkeletonTransformsFromLandmarks(
    vr::ETrackedControllerRole role,
    const float landmarks[21][3],
    vr::VRBoneTransform_t* out_transforms)
{
    (void)role;

    for (int i=0; i<eBone_Count; ++i) {
        out_transforms[i].position = { 0.f,0.f,0.f,1.f };
        out_transforms[i].orientation = { 1.f,0.f,0.f,0.f };
    }

    const vr::HmdVector3_t wrist = FT_Landmark(landmarks,0);
    const vr::HmdQuaternion_t palm = ComputePalmOrientation(landmarks);
    const vr::HmdQuaternion_t palmInverse = -palm;

    // Re-express the tracked points in wrist-relative palm coordinates.
    vr::HmdVector3_t p[21]{};
    for (int i=0; i<21; ++i) {
        p[i] = FT_VectorSub(FT_Landmark(landmarks,i),wrist) * palmInverse;
    }

    // Root and wrist coincide with /pose/raw, whose pose is at landmark 0.
    out_transforms[eBone_Root].position = { 0.f,0.f,0.f,1.f };
    out_transforms[eBone_Root].orientation = { 1.f,0.f,0.f,0.f };
    out_transforms[eBone_Wrist].position = { 0.f,0.f,0.f,1.f };
    out_transforms[eBone_Wrist].orientation = { 1.f,0.f,0.f,0.f };

    // Thumb: wrist -> CMC -> MCP -> IP -> tip.
    const int thumbPath[5] = { 0,1,2,3,4 };
    vr::HmdQuaternion_t parent = HmdQuaternion_Identity;

    for (int j=0; j<4; ++j) {
        parent = FT_WriteLandmarkBone(
            p[thumbPath[j]],p[thumbPath[j+1]],parent,
            out_transforms[eBone_Thumb0+j]);
    }

    // Other fingers: wrist -> MCP -> PIP -> DIP -> tip.
    const int paths[4][5] = {
        { 0,5,6,7,8 },
        { 0,9,10,11,12 },
        { 0,13,14,15,16 },
        { 0,17,18,19,20 }
    };

    const int starts[4] = {
        eBone_IndexFinger0,
        eBone_MiddleFinger0,
        eBone_RingFinger0,
        eBone_PinkyFinger0
    };

    for (int f=0; f<4; ++f) {
        parent = HmdQuaternion_Identity;

        for (int j=0; j<4; ++j) {
            parent = FT_WriteLandmarkBone(
                p[paths[f][j]],p[paths[f][j+1]],parent,
                out_transforms[starts[f]+j]);
        }

        // Valve's fifth bone is the terminal fingertip bone.
        out_transforms[starts[f]+4].position =
            { 0.f,0.f,0.f,1.f };
        out_transforms[starts[f]+4].orientation =
            { 1.f,0.f,0.f,0.f };
    }

    // Auxiliary bones remain identity until their reference offsets are mapped.
}

// -----------------------------------------------------------------------------
// Frametop: per-joint skeleton. Uses the same InitHand/ComputeSkeletalTransforms
// as the curl path above (which renders correctly in VRChat), so bone axes and
// left/right mirroring are unchanged; only the joint angles come from tracking.
// -----------------------------------------------------------------------------
void MyHandSimulation::ComputeSkeletonTransformsFromJoints(
    vr::ETrackedControllerRole role, const MyHandJoints& j, vr::VRBoneTransform_t* out_transforms)
{
	HandSimHand hand{};
	hand.role = role;

	// Root and wrist: identical to ComputeSkeletonTransforms.
	out_transforms[0] = { { 0.000000f, 0.000000f, 0.000000f, 1.000000f }, { 1.000000f, -0.000000f, -0.000000f, 0.000000f } };
	out_transforms[1] = { { 0.000000f, 0.000000f, 0.000000f, 1.000000f }, { 1.000000f, 0.000000f, 0.000000f, 0.000000f } };
	if (role == vr::TrackedControllerRole_RightHand)
	{
		out_transforms[1].position.v[0] *= -1.f;
		out_transforms[1].orientation.y *= -1.f;
		out_transforms[1].orientation.z *= -1.f;
	}

	InitHand(hand);

	// Thumb: same joints and ranges as the curl path, driven per joint.
	const float thumb_avg = 0.5f * (j.thumb_mcp01 + j.thumb_ip01);
	hand.thumb.metacarpal.swing.v[0] += DEG_TO_RAD(thumb_avg * 5.f);
	hand.thumb.metacarpal.twist = 0.f;
	hand.thumb.proximal.swing.v[0] += DEG_TO_RAD(j.thumb_mcp01 * 90.f);
	hand.thumb.proximal.twist = 0.f;
	hand.thumb.distal.rotation += DEG_TO_RAD(j.thumb_ip01 * 90.f);

	for (int f = 0; f < 4; ++f)
	{
		HandSimFinger& finger = hand.fingers[f];
		// Metacarpal follows the knuckle a little, as in ApplyGenericFingerTransform (5 of 90 deg).
		finger.metacarpal.swing.v[0] += j.mcp_flex[f] * (5.f / 90.f);
		finger.proximal.swing.v[0]  += j.mcp_flex[f];
		finger.proximal.swing.v[1]  += j.mcp_abd[f];
		finger.intermediate.rotation = j.pip[f];
		finger.distal.rotation       = j.dip[f];
	}

	ComputeSkeletalTransforms(hand, out_transforms);
}

// -----------------------------------------------------------------------------
// Frametop: thumb_mode 2, the thumb aimed at your real thumb.
// The curl pipeline above can only bend the thumb along one fixed path. Here each
// thumb bone is turned, the shortest way, onto the direction its real segment points.
// Both directions are compared relative to a palm frame built the same way from the
// skeleton's knuckles and from the tracked knuckles, so no bone-axis convention (or
// left/right mirroring) has to be known: SteamVR just gets parent-relative rotations.
// -----------------------------------------------------------------------------
namespace {
struct AQ { double w, x, y, z; };
struct AV { double x, y, z; };

AQ AMul(const AQ& a, const AQ& b)
{
	return { a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
	         a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
	         a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
	         a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w };
}
AQ AConj(const AQ& q) { return { q.w, -q.x, -q.y, -q.z }; }
AQ ANorm(const AQ& q)
{
	const double n = std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
	return n < 1e-12 ? AQ{ 1, 0, 0, 0 } : AQ{ q.w/n, q.x/n, q.y/n, q.z/n };
}
AV ARot(const AQ& q, const AV& v)   // q v q*
{
	const AQ r = AMul(AMul(q, AQ{ 0, v.x, v.y, v.z }), AConj(q));
	return { r.x, r.y, r.z };
}
AV AAdd(const AV& a, const AV& b) { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
AV ASub(const AV& a, const AV& b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
AV AScale(const AV& a, double s) { return { a.x*s, a.y*s, a.z*s }; }
double ADot(const AV& a, const AV& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
AV ACross(const AV& a, const AV& b) { return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; }
bool ANormalize(AV& v)
{
	const double n = std::sqrt(ADot(v, v));
	if (n < 1e-6) return false;
	v = AScale(v, 1.0 / n);
	return true;
}
// Shortest rotation taking unit vector a onto unit vector b.
AQ AFromTo(const AV& a, const AV& b)
{
	const double d = ADot(a, b);
	if (d < -0.999999) {   // opposite: turn 180 degrees about any perpendicular axis
		AV axis = ACross({ 1, 0, 0 }, a);
		if (!ANormalize(axis)) { axis = ACross({ 0, 1, 0 }, a); ANormalize(axis); }
		return { 0, axis.x, axis.y, axis.z };
	}
	const AV c = ACross(a, b);
	return ANorm({ 1.0 + d, c.x, c.y, c.z });
}
// Orthonormal palm frame from pinky->index and wrist->middle directions.
bool APalmFrame(AV x, AV y, AV out[3])
{
	if (!ANormalize(x) || !ANormalize(y)) return false;
	AV z = ACross(x, y);
	if (!ANormalize(z)) return false;
	y = ACross(z, x);
	out[0] = x; out[1] = y; out[2] = z;
	return true;
}

int AParent(int b)
{
	if (b <= 0) return -1;
	if (b == eBone_Wrist) return eBone_Root;
	if (b == eBone_Thumb0 || b == eBone_IndexFinger0 || b == eBone_MiddleFinger0 ||
	    b == eBone_RingFinger0 || b == eBone_PinkyFinger0) return eBone_Wrist;
	return b - 1;
}
}

// Spherical interpolation from a to b (both unit), t in 0..1.
static AQ ASlerp(AQ a, AQ b, double t)
{
	double d = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;
	if (d < 0) { b = { -b.w, -b.x, -b.y, -b.z }; d = -d; }
	if (d > 0.9995)
		return ANorm({ a.w + t*(b.w-a.w), a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), a.z + t*(b.z-a.z) });
	const double th = std::acos(d), s = std::sin(th);
	const double wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
	return { wa*a.w + wb*b.w, wa*a.x + wb*b.x, wa*a.y + wb*b.y, wa*a.z + wb*b.z };
}

// Joint limits for an aimed finger segment, relative to the segment before it:
// flexion (toward the palm, +) and extension (-), and sideways tilt.
struct ASegLimit { double flex_min, flex_max, side; };   // degrees
static const ASegLimit kFingerLimits[3] = {
	{ -30.0,  95.0, 25.0 },   // knuckle (MCP): metacarpal -> proximal
	{ -10.0, 115.0,  8.0 },   // middle joint (PIP)
	{ -15.0,  90.0,  8.0 },   // tip joint (DIP)
};

// Clamps direction w (unit) against the previous segment p (unit). axis = the finger's bend
// axis (unit, perpendicular to p): bending forward turns p about +axis toward the palm. It
// is set once at the knuckle and carried down the finger (see AimChains), so "forward"
// keeps meaning the curl direction even past 90 degrees, and follows the finger's spread.
static AV ALimitSegment(const AV& p, const AV& w, const AV& axis, const ASegLimit& lim)
{
	const double D2R = 3.14159265358979 / 180.0;
	AV e2 = ACross(axis, p);                         // perpendicular to p, the way it curls
	if (!ANormalize(e2)) return w;
	const AV e3 = axis;                              // sideways
	const double a = ADot(w, p), b = ADot(w, e2), c = ADot(w, e3);
	double flex = std::atan2(b, a);
	double side = std::asin(c < -1 ? -1 : (c > 1 ? 1 : c));
	const double fmin = lim.flex_min * D2R, fmax = lim.flex_max * D2R, smax = lim.side * D2R;
#ifdef AIM_DEBUG
	std::printf("  seg flex %6.1f side %6.1f (limits %.0f..%.0f, +-%.0f)\n", flex / D2R, side / D2R, lim.flex_min, lim.flex_max, lim.side);
#endif
	if (flex >= fmin && flex <= fmax && side >= -smax && side <= smax) return w;
	flex = flex < fmin ? fmin : (flex > fmax ? fmax : flex);
	side = side < -smax ? -smax : (side > smax ? smax : side);
	const double cs = std::cos(side);
	return AAdd(AAdd(AScale(p, cs * std::cos(flex)), AScale(e2, cs * std::sin(flex))), AScale(e3, std::sin(side)));
}

// Aims bones [first..last] of one chain: bone first+k runs from landmark lm0+k to lm0+k+1.
// blend 1 = point exactly where the real segment points, 0 = leave the bone as built.
// palm_sign != 0 enforces kFingerLimits along the chain (fingers; the chain's landmark
// before lm0 is the wrist, 0). Returns false (bones untouched) if the palm frame fails.
static bool AimChains(const float L[21][3], vr::VRBoneTransform_t* bones,
                      const int (*chains)[3], int nchains, double blend, double palm_sign = 0.0)
{
	const int N = eBone_PinkyFinger4 + 1;
	AQ lq[N], gq[N];
	AV lp[N], gp[N];
	for (int b = 0; b < N; ++b) {
		const vr::HmdQuaternionf_t& o = bones[b].orientation;
		lq[b] = ANorm({ o.w, o.x, o.y, o.z });
		lp[b] = { bones[b].position.v[0], bones[b].position.v[1], bones[b].position.v[2] };
	}
	auto fk = [&](int from) {
		for (int b = from; b < N; ++b) {
			const int p = AParent(b);
			if (p < 0) { gq[b] = lq[b]; gp[b] = lp[b]; continue; }
			gq[b] = ANorm(AMul(gq[p], lq[b]));
			gp[b] = AAdd(gp[p], ARot(gq[p], lp[b]));
		}
	};
	fk(0);

	auto lm = [&](int i) { return AV{ L[i][0], L[i][1], L[i][2] }; };
	AV skel[3], track[3];
	if (!APalmFrame(ASub(gp[eBone_IndexFinger1], gp[eBone_PinkyFinger1]),
	                ASub(gp[eBone_MiddleFinger1], gp[eBone_Wrist]), skel)) return false;
	if (!APalmFrame(ASub(lm(5), lm(17)), ASub(lm(9), lm(0)), track)) return false;
	auto toSkeleton = [&](const AV& v) {
		return AAdd(AAdd(AScale(skel[0], ADot(v, track[0])), AScale(skel[1], ADot(v, track[1]))),
		            AScale(skel[2], ADot(v, track[2])));
	};

	bool changed[N] = {};
	for (int c = 0; c < nchains; ++c) {
		const int first = chains[c][0], last = chains[c][1], lm0 = chains[c][2];
		// For limits: the previous segment's (limited) direction, in the tracked palm frame,
		// starting with the metacarpal (wrist -> knuckle).
		AV prev = ASub(lm(lm0), lm(0));
		bool limits = palm_sign != 0.0 && ANormalize(prev);
		// Bend axis at the knuckle: curling turns the metacarpal direction toward the palm.
		AV axis{};
		if (limits) {
			axis = ACross(prev, AScale(track[2], palm_sign));
			limits = ANormalize(axis);
		}
		for (int b = first; b <= last; ++b) {
			const int k = lm0 + (b - first);
			AV seg = ASub(lm(k + 1), lm(k));
			if (limits && ANormalize(seg)) {
				ASegLimit lim = kFingerLimits[(b - first) < 3 ? (b - first) : 2];
				if (lm0 == 17 && b == first) lim.side = 30.0;   // the pinky spreads further
				seg = ALimitSegment(prev, seg, axis, lim);
				// Carry the axis down the finger, kept perpendicular to the new segment.
				AV next = ASub(axis, AScale(seg, ADot(axis, seg)));
				if (ANormalize(next)) axis = next;
				prev = seg;
			}
			AV want = toSkeleton(seg);
			AV have = ASub(gp[b + 1], gp[b]);
			if (!ANormalize(want) || !ANormalize(have)) break;   // degenerate segment: rest of chain as built
			const AQ aimed = ANorm(AMul(AFromTo(have, want), gq[b]));
			gq[b] = blend >= 1.0 ? aimed : ASlerp(gq[b], aimed, blend);
			lq[b] = ANorm(AMul(AConj(gq[AParent(b)]), gq[b]));
			changed[b] = true;
			fk(b);   // the rest of the chain follows
		}
	}
	for (int b = 0; b < N; ++b) {
		if (!changed[b]) continue;
		bones[b].orientation.w = float(lq[b].w);
		bones[b].orientation.x = float(lq[b].x);
		bones[b].orientation.y = float(lq[b].y);
		bones[b].orientation.z = float(lq[b].z);
	}
	return true;
}

// finger_mode 2: the proximal, middle and tip bones of each finger aimed at the real
// finger (MCP 5 -> PIP 6 -> DIP 7 -> tip 8, and so on). The metacarpals stay as built, so
// the palm (and with it the frame everything is measured in) doesn't move.
void MyHandSimulation::AimFingersAtLandmarks(vr::ETrackedControllerRole role, const float L[21][3],
                                             vr::VRBoneTransform_t* bones, float blend, bool limits)
{
	if (blend <= 0.f) return;
	const double palm_sign = !limits ? 0.0 : (role == vr::TrackedControllerRole_RightHand ? 1.0 : -1.0);
	static const int chains[4][3] = {
		{ eBone_IndexFinger1,  eBone_IndexFinger3,  5 },
		{ eBone_MiddleFinger1, eBone_MiddleFinger3, 9 },
		{ eBone_RingFinger1,   eBone_RingFinger3,   13 },
		{ eBone_PinkyFinger1,  eBone_PinkyFinger3,  17 },
	};
	AimChains(L, bones, chains, 4, blend > 1.f ? 1.0 : double(blend), palm_sign);
}

void MyHandSimulation::BlendBones(const vr::VRBoneTransform_t* from, const vr::VRBoneTransform_t* to, float t,
                                  vr::VRBoneTransform_t* out, int count)
{
	const double u = t < 0.f ? 0.0 : (t > 1.f ? 1.0 : double(t));
	for (int b = 0; b < count; ++b) {
		// A bone that's the same in both (the aux bones, left all zero by the skeleton) is
		// passed through as it is: normalising a zero quaternion would turn it into identity.
		if (std::memcmp(&from[b], &to[b], sizeof(from[b])) == 0) { out[b] = to[b]; continue; }
		const vr::HmdQuaternionf_t& a = from[b].orientation;
		const vr::HmdQuaternionf_t& c = to[b].orientation;
		const AQ q = ASlerp(ANorm({ a.w, a.x, a.y, a.z }), ANorm({ c.w, c.x, c.y, c.z }), u);
		out[b].orientation.w = float(q.w);
		out[b].orientation.x = float(q.x);
		out[b].orientation.y = float(q.y);
		out[b].orientation.z = float(q.z);
		for (int k = 0; k < 4; ++k)
			out[b].position.v[k] = float(from[b].position.v[k] + u * (to[b].position.v[k] - from[b].position.v[k]));
	}
}

void MyHandSimulation::AimThumbAtLandmarks(const float L[21][3], vr::VRBoneTransform_t* bones)
{
	static const int chain[1][3] = { { eBone_Thumb0, eBone_Thumb2, 1 } };
	AimChains(L, bones, chain, 1, 1.0);
}

// -----------------------------------------------------------------------------
// Frametop: contact retargeting. VRChat gets bone rotations, not your finger lengths, so with
// the real angles copied exactly the avatar's thumb stops short of (or passes) a finger your
// real thumb is touching. When the real tips are close, this bends the skeleton's thumb (and a
// little of the finger) with CCD IK until the skeleton's tips meet too, then blends that in by
// how close the real tips are. Works in model space from the bones themselves, so no bone-axis
// convention or left/right mirroring has to be known.
// -----------------------------------------------------------------------------
namespace {
const int kTouchTip[4]      = { eBone_IndexFinger4, eBone_MiddleFinger4, eBone_RingFinger4, eBone_PinkyFinger4 };
const int kTouchChain[4][3] = { { eBone_IndexFinger1, eBone_IndexFinger2, eBone_IndexFinger3 },
                                { eBone_MiddleFinger1, eBone_MiddleFinger2, eBone_MiddleFinger3 },
                                { eBone_RingFinger1, eBone_RingFinger2, eBone_RingFinger3 },
                                { eBone_PinkyFinger1, eBone_PinkyFinger2, eBone_PinkyFinger3 } };
const int kThumbChain[3]    = { eBone_Thumb0, eBone_Thumb1, eBone_Thumb2 };

struct ASkel {
	static const int N = eBone_PinkyFinger4 + 1;
	AQ lq[N], gq[N];
	AV lp[N], gp[N];
	void Load(const vr::VRBoneTransform_t* bones)
	{
		for (int b = 0; b < N; ++b) {
			const vr::HmdQuaternionf_t& o = bones[b].orientation;
			lq[b] = ANorm({ o.w, o.x, o.y, o.z });
			lp[b] = { bones[b].position.v[0], bones[b].position.v[1], bones[b].position.v[2] };
		}
		Fk(0);
	}
	void Fk(int from)
	{
		for (int b = from; b < N; ++b) {
			const int p = AParent(b);
			if (p < 0) { gq[b] = lq[b]; gp[b] = lp[b]; continue; }
			gq[b] = ANorm(AMul(gq[p], lq[b]));
			gp[b] = AAdd(gp[p], ARot(gq[p], lp[b]));
		}
	}
	// Turn joint j (model space, about its own position) by r, keeping its parent.
	void Turn(int j, const AQ& r)
	{
		gq[j] = ANorm(AMul(r, gq[j]));
		lq[j] = ANorm(AMul(AConj(gq[AParent(j)]), gq[j]));
		Fk(j);
	}
};

// One CCD pass set: moves end bone `eff`'s position toward `target` by turning the chain's
// joints (last first). gain scales each joint's turn (the chain's base moves less), and each
// turn is at most max_deg, so it settles instead of snapping.
void ACcd(ASkel& s, const int* chain, const double* gain, int n, int eff, const AV& target, int iters, double max_deg)
{
	const double maxr = max_deg * 3.14159265358979 / 180.0;
	for (int it = 0; it < iters; ++it) {
		for (int k = n - 1; k >= 0; --k) {
			const int j = chain[k];
			AV a = ASub(s.gp[eff], s.gp[j]), b = ASub(target, s.gp[j]);
			if (!ANormalize(a) || !ANormalize(b)) continue;
			AQ r = AFromTo(a, b);
			// scale the turn by gain and cap it
			double ang = 2.0 * std::acos(r.w > 1 ? 1 : (r.w < -1 ? -1 : r.w)) * gain[k];
			if (ang > maxr) ang = maxr;
			AV axis{ r.x, r.y, r.z };
			if (!ANormalize(axis) || ang < 1e-6) continue;
			const double h = 0.5 * ang, sh = std::sin(h);
			s.Turn(j, { std::cos(h), axis.x * sh, axis.y * sh, axis.z * sh });
		}
	}
}

// IK so the thumb tip meets finger f's tip (pad_m apart). finger_share: how much of the way the
// finger comes to meet the thumb (0 = only the thumb moves).
void ATouchSolve(ASkel& s, int f, double pad_m, double finger_share)
{
	static const double thumb_gain[3]  = { 0.5, 0.9, 1.0 };
	static const double finger_gain[3] = { 0.6, 0.8, 1.0 };
	const int tip = kTouchTip[f];
	// The ring and little fingers curl in further to meet the thumb (as real ones do: the
	// thumb alone can't reach across the palm).
	static const double reach[4] = { 1.0, 1.0, 2.0, 2.4 };
	finger_share = finger_share * reach[f] > 0.6 ? 0.6 : finger_share * reach[f];
	if (finger_share > 0.0) {
		const AV meet = AAdd(s.gp[tip], AScale(ASub(s.gp[eBone_Thumb3], s.gp[tip]), finger_share));
		ACcd(s, kTouchChain[f], finger_gain, 3, tip, meet, 6, 12.0);
	}
	for (int round = 0; round < 5; ++round) {
		AV dir = ASub(s.gp[eBone_Thumb3], s.gp[tip]);
		if (!ANormalize(dir)) dir = { 0, 0, 0 };
		const AV target = AAdd(s.gp[tip], AScale(dir, pad_m));
		ACcd(s, kThumbChain, thumb_gain, 3, eBone_Thumb3, target, 10, 15.0);
	}
}
}

void MyHandSimulation::RetargetTouch(vr::VRBoneTransform_t* bones, const float weight[4], float pad_m, float finger_share)
{
	// The two closest fingers (the thumb sliding from one to the next blends between them).
	int a = -1, b = -1;
	for (int f = 0; f < 4; ++f) {
		if (weight[f] <= 0.f) continue;
		if (a < 0 || weight[f] > weight[a]) { b = a; a = f; }
		else if (b < 0 || weight[f] > weight[b]) b = f;
	}
	if (a < 0) return;
	ASkel base; base.Load(bones);
	ASkel sa = base; ATouchSolve(sa, a, pad_m, finger_share);
	ASkel sb = base; if (b >= 0) ATouchSolve(sb, b, pad_m, finger_share);
	const double wa = weight[a] > 1.f ? 1.0 : weight[a];
	const double wb = b >= 0 ? (weight[b] > 1.f ? 1.0 : weight[b]) : 0.0;
	// Share of the second solution: half when the two are tied (the thumb sliding from one
	// finger to the next), none once the closest is clearly closer (weights 0.3 apart, about
	// 6 mm), so a touch goes to the finger it's on, not between two neighbours.
	double mix = 0.5 * (1.0 - (wa - wb) / 0.3);
	mix = mix < 0.0 ? 0.0 : mix;
	const double total = wa;                                    // how far from the real pose
	auto apply = [&](int bone) {
		AQ q = sa.lq[bone];
		if (b >= 0) q = ASlerp(q, sb.lq[bone], mix);
		q = ASlerp(base.lq[bone], q, total);
		bones[bone].orientation.w = float(q.w);
		bones[bone].orientation.x = float(q.x);
		bones[bone].orientation.y = float(q.y);
		bones[bone].orientation.z = float(q.z);
	};
	for (int k = 0; k < 3; ++k) apply(kThumbChain[k]);
	if (finger_share > 0.f) {
		for (int k = 0; k < 3; ++k) apply(kTouchChain[a][k]);
		if (b >= 0) for (int k = 0; k < 3; ++k) apply(kTouchChain[b][k]);
	}
}

// Thumb tip to each fingertip distance of a built skeleton (metres), for tests and logging.
void MyHandSimulation::TouchGaps(const vr::VRBoneTransform_t* bones, float out[4])
{
	ASkel s; s.Load(bones);
	for (int f = 0; f < 4; ++f) {
		const AV d = ASub(s.gp[eBone_Thumb3], s.gp[kTouchTip[f]]);
		out[f] = float(std::sqrt(ADot(d, d)));
	}
}
