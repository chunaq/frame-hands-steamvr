//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include "openvr_driver.h"

// 0-1 values (1 fully curled)
struct MyFingerCurls
{
	float thumb;
	float index;
	float middle;
	float ring;
	float pinky;
};

//-1-1 values (1 fully to the left)
struct MyFingerSplays
{
	float thumb;
	float index;
	float middle;
	float ring;
	float pinky;
};

// Per-joint angles measured from the tracked landmarks (radians unless noted).
// Fingers are index, middle, ring, pinky.
struct MyHandJoints
{
	float mcp_flex[4]; // knuckle bend
	float mcp_abd[4];  // knuckle sideways spread (+ = toward thumb side)
	float pip[4];      // middle joint bend
	float dip[4];      // fingertip joint bend
	float thumb_mcp01; // thumb MCP bend, 0..1
	float thumb_ip01;  // thumb tip joint bend, 0..1
};

enum HandSkeletonBone : vr::BoneIndex_t
{
	eBone_Root = 0,
	eBone_Wrist,
	eBone_Thumb0,
	eBone_Thumb1,
	eBone_Thumb2,
	eBone_Thumb3,
	eBone_IndexFinger0,
	eBone_IndexFinger1,
	eBone_IndexFinger2,
	eBone_IndexFinger3,
	eBone_IndexFinger4,
	eBone_MiddleFinger0,
	eBone_MiddleFinger1,
	eBone_MiddleFinger2,
	eBone_MiddleFinger3,
	eBone_MiddleFinger4,
	eBone_RingFinger0,
	eBone_RingFinger1,
	eBone_RingFinger2,
	eBone_RingFinger3,
	eBone_RingFinger4,
	eBone_PinkyFinger0,
	eBone_PinkyFinger1,
	eBone_PinkyFinger2,
	eBone_PinkyFinger3,
	eBone_PinkyFinger4,
	eBone_Aux_Thumb,
	eBone_Aux_IndexFinger,
	eBone_Aux_MiddleFinger,
	eBone_Aux_RingFinger,
	eBone_Aux_PinkyFinger,
	eBone_Count
};

class MyHandSimulation
{
public:
	
        static vr::HmdQuaternion_t ComputePalmOrientation(const float landmarks[21][3]);
        void ComputeSkeletonTransformsFromLandmarks(vr::ETrackedControllerRole role, const float landmarks[21][3], vr::VRBoneTransform_t* out_transforms);
void ComputeSkeletonTransforms( vr::ETrackedControllerRole role, const MyFingerCurls &curls, const MyFingerSplays &splays, vr::VRBoneTransform_t *out_transforms );
        // Frametop: same proven bone pipeline as ComputeSkeletonTransforms, but every
        // finger joint gets its own measured angle instead of one curl per finger.
        void ComputeSkeletonTransformsFromJoints( vr::ETrackedControllerRole role, const MyHandJoints &joints, vr::VRBoneTransform_t *out_transforms );
        // Frametop (thumb_mode 2): turn the three thumb bones of an already built skeleton
        // so each points the way your real thumb segment does, relative to your palm. Bone
        // lengths and every other bone stay as they are. landmarks: the 21 tracked points
        // (any frame, any origin: only their directions relative to the palm are used).
        static void AimThumbAtLandmarks( const float landmarks[21][3], vr::VRBoneTransform_t *bones );
        // Frametop (finger_mode 2): the same for the four fingers' proximal, middle and tip
        // bones. blend 1 = exactly the real finger's directions, 0 = as built (per-joint angles).
        // limits: keep each segment within real joint ranges (bend, over-extension, sideways).
        // Frametop: a skeleton part way from `from` to `to` (t 0..1): rotations slerped,
        // positions lerped, per bone. Used to move the fingers smoothly between camera frames.
        static void BlendBones( const vr::VRBoneTransform_t *from, const vr::VRBoneTransform_t *to, float t,
                                vr::VRBoneTransform_t *out, int count = 31 );
        // Frametop: contact retargeting. weight[f] (0..1) = how close your real thumb tip is to
        // finger f's tip (index..pinky); the skeleton's thumb (and finger_share of the finger)
        // is bent so its tips meet too, blended in by that weight. pad_m = gap left between them.
        static void RetargetTouch( vr::VRBoneTransform_t *bones, const float weight[4], float pad_m, float finger_share );
        static void TouchGaps( const vr::VRBoneTransform_t *bones, float out[4] );
        static void AimFingersAtLandmarks( vr::ETrackedControllerRole role, const float landmarks[21][3],
                                           vr::VRBoneTransform_t *bones, float blend, bool limits );
};