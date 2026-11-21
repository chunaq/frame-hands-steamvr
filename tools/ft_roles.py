"""Read-only: which SteamVR device holds each hand, and what every controller device is.

  python ft_roles.py [seconds]      (default: one snapshot; with seconds, one line per change)

Runs as a background OpenVR app, so it takes nothing from the game.
"""
import sys, time
import openvr

ROLE = {openvr.TrackedControllerRole_LeftHand: "LEFT", openvr.TrackedControllerRole_RightHand: "RIGHT",
        openvr.TrackedControllerRole_OptOut: "optout", openvr.TrackedControllerRole_Invalid: "-"}

def prop_str(vr, i, p):
    try:
        return vr.getStringTrackedDeviceProperty(i, p)
    except Exception:
        return "?"

def prop_int(vr, i, p):
    try:
        return vr.getInt32TrackedDeviceProperty(i, p)
    except Exception:
        return -1

def snapshot(vr):
    poses = vr.getDeviceToAbsoluteTrackingPose(openvr.TrackingUniverseStanding, 0, openvr.k_unMaxTrackedDeviceCount)
    left = vr.getTrackedDeviceIndexForControllerRole(openvr.TrackedControllerRole_LeftHand)
    right = vr.getTrackedDeviceIndexForControllerRole(openvr.TrackedControllerRole_RightHand)
    rows = []
    for i in range(openvr.k_unMaxTrackedDeviceCount):
        if vr.getTrackedDeviceClass(i) == openvr.TrackedDeviceClass_Invalid:
            continue
        p = poses[i]
        holds = "LEFT HAND" if i == left else ("RIGHT HAND" if i == right else "")
        rows.append(f"  [{i}] {prop_str(vr, i, openvr.Prop_ControllerType_String):16s} "
                    f"model {prop_str(vr, i, openvr.Prop_ModelNumber_String):26s} "
                    f"hint {ROLE.get(prop_int(vr, i, openvr.Prop_ControllerRoleHint_Int32), prop_int(vr, i, openvr.Prop_ControllerRoleHint_Int32))!s:6s} "
                    f"connected {int(p.bDeviceIsConnected)} tracking {int(p.bPoseIsValid)}  {holds}")
    return f"left hand = device {left}, right hand = device {right}", rows

vr = openvr.init(openvr.VRApplication_Background)
try:
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 0
    end = time.time() + secs
    last = None
    while True:
        head, rows = snapshot(vr)
        if head != last:
            print(time.strftime("%H:%M:%S"), head)
            for r in rows:
                print(r)
            last = head
        if time.time() >= end:
            break
        time.sleep(0.25)
finally:
    openvr.shutdown()
