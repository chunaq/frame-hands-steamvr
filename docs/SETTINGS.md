# Settings

All settings are in `calibration.json` in the driver's folder:
`%LOCALAPPDATA%\FrameHands\framehands\resources\settings\calibration.json`.

Edit it while you play: changes apply within a second, no restart needed. Updating Frame Hands
keeps your file (the new version's defaults are saved next to it as `calibration.default.json`).

## Fingers

| Setting | Default | What it does |
| --- | --- | --- |
| `finger_mode` | `2` | `2` = every finger bone points where your real finger points (all directions). `1` = each joint follows your finger's bend angles. `0` = one curl per finger. |
| `thumb_mode` | `2` | `2` = the thumb follows your real thumb in every direction. `1` = bend only. `0` = it wraps over the fingers. |
| `finger_aim_limits` | `1` | Keep aimed fingers within real joint ranges. |
| `finger_aim_blend` | `1.0` | With `finger_mode 2`: `1` = fully aimed, lower = mixed with the per-joint pose (steadier). |
| `finger_interp` | `1` | Fingers glide between camera frames (about 20 ms extra delay) instead of jumping. `0` = off. |
| `skeleton_finger_filter` | `0` | `1` = also smooth the fingers on top of Frametop's skeleton (steadier, a little more lag). |
| `finger_point_min_cutoff`, `finger_point_beta` | `4`, `25` | That finger smoothing: higher = freer and faster, lower = steadier. |

## Touching your fingers (contact retargeting)

Your avatar's fingers aren't your length, so with your real angles copied exactly its thumb misses
a finger your real thumb touches. When your real thumb tip is close to a fingertip, the avatar's
thumb is bent (and the finger comes part of the way) until its tips meet too.

| Setting | Default | What it does |
| --- | --- | --- |
| `touch_retarget` | `1` | `0` = off (pure copied angles). |
| `touch_near_mm` | `20` | Real tips this close count as touching (fingertip points sit 1-2 cm apart when the pads touch). Raise it if tips don't quite meet. |
| `touch_far_mm` | `45` | It starts blending in from this far. Lower it if the thumb snaps over too early. |
| `touch_pad_mm` | `8` | Gap left between the avatar's tips. Raise it if they sink into each other. |
| `touch_finger_share` | `0.25` | How far the finger comes to meet the thumb (`0` = only the thumb moves; ring and pinky come about twice as far). |

## Gestures (controller buttons)

| Setting | Default | What it does |
| --- | --- | --- |
| `gestures` | `1` | Thumb + index pinch = trigger, fist = grip, thumb + middle pinch = B. `0` = off. |
| `ft_gestures` | `1` | Use Frametop's own pinch, middle pinch and fist detection (Frametop 0.3.0+, trained on those touches). `0` = the driver's own checks. |

## Hand position and smoothing

| Setting | Default | What it does |
| --- | --- | --- |
| `smoothing` | `1` | `0` = no smoothing of the hand's position. |
| `smooth_min_cutoff`, `smooth_beta` | `3.0`, `15` | Lower `min_cutoff` = steadier when still; higher `beta` = less lag on fast moves. |
| `prediction` | `0.65` | How far ahead SteamVR predicts the hand (0-1). Lower it if hands overshoot. |
| `hold_ms` | `300` | A lost hand stays this long before it disappears. |
| `capture_head_pose` | `1` | Place hands with your head pose from when the cameras saw them (no swimming when you turn your head). |
| `network_ms` | `3` | Wi-Fi delay added to the frame's age for that. |
| `head_offset_x/y/z` | `-0.015`, `0`, `-0.03` | Both hands, metres relative to your head: x negative = left, y positive = up, z negative = forward. |
| `left_offset_*`, `right_offset_*` | `0` | The same, per hand. |
| `hand_offset_x/y/z` | `0.045`, `0`, `0` | In the hand's own frame: x = toward the thumb side, y = toward the fingertips, z = out of the palm. |
| `pitch_offset_deg`, `yaw_offset_deg`, `roll_offset_deg` | `-90`, `0`, `-90` | The hand's rotation fix-up (don't change unless hands point the wrong way). |

## Left/right and controllers

| Setting | Default | What it does |
| --- | --- | --- |
| `shape_sides` | `0.35` | How sure the hand's 3D shape must be (0-1) before it decides left/right over the tracker's label. `0` = off. |
| `share_with_controllers` | `0` | `0` = the tracked hands keep both hand slots (games never switch to the Frame controllers). `1` = a Frame controller gets its slot back while no hand is seen. |
| `controller_idle_s`, `disconnect_s` | `0`, `30` | With `share_with_controllers 1`: a controller that moved this recently keeps its hand; a hand lost this long gives its slot back. |

## Others

`curl_multiplier`, `splay_multiplier`, `contact_assist`, `finger_min_cutoff`, `finger_beta` and
the `hand_fit*` settings belong to the older finger modes; `calibrate_curls` (change the number,
then open your hands fully and make tight fists for 6 seconds) records your curl range for
`finger_mode 0`.
