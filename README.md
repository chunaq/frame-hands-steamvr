# Frame Hands

**Steam Frame hand tracking for SteamVR and VRChat, powered by [Frametop](https://github.com/Frametop/frametop).**

Frame Hands brings the hand tracking that Frametop runs on your Steam Frame into SteamVR on your
PC, as full hand skeletons: every finger and the thumb move with yours, in VRChat and other SteamVR
games that support hand tracking. No controllers needed.

- **1:1 fingers.** Every finger bone and the thumb point where yours do, inside real joint limits,
  from Frametop's fitted skeleton (your own bone lengths, its fine-tuned hand model).
- **Touches that touch.** Your avatar's fingers aren't your length, so copied angles alone leave its
  thumb short of a finger you're touching. Frame Hands bends the avatar's thumb until its tips meet
  too, for all four fingers, and lets go smoothly when you open your hand.
- **Gestures as buttons.** Thumb + index pinch = trigger, fist = grip, thumb + middle pinch = B,
  from Frametop's own detection. In VRChat, B isn't bound to anything (VRChat's own hand gestures
  open its menu); bind it in SteamVR's controller bindings if you want it to do something.
- **Hands stay hands.** The tracked hands keep both hand slots, so games don't switch to the Frame
  controllers or show a controller in your hand.
- **Steady in the room.** Hands are placed with your head pose from the moment the cameras saw them,
  so they don't swim when you turn your head.
- **Nothing to run on the PC.** The SteamVR driver receives the hand data itself and finds the
  headset on its own.

Frame Hands is an independent project, not made by or affiliated with Frametop, Valve or VRChat.

## What you need

- A **Steam Frame** with **[Frametop](https://github.com/Frametop/frametop)** installed, including
  its hand tracking (Frametop 0.3.0 or newer recommended: older versions work without the skeleton
  and gesture extras).
- A **Windows 10/11 PC** with **SteamVR**, streaming to the Frame, on the same network.

## Install

### 1. On the PC

1. Download `FrameHands-windows.zip` from the [latest release](../../releases/latest) and unzip it.
2. Close SteamVR.
3. In the unzipped folder, double-click **`install.cmd`**. Say yes when it asks to allow the hand
   data through Windows Firewall.

Nothing else to install: no Python, no runtime, no program running in the background on the PC.

It installs the driver to `%USERPROFILE%\FrameHands` and registers it with SteamVR.

### 2. On the Steam Frame

In Desktop Mode, open a terminal (or SSH in) and run:

```sh
cd ~/Downloads
curl -LO https://github.com/chunaq/frame-hands-steamvr/releases/latest/download/frame-hands-headset.tar.gz
tar xzf frame-hands-headset.tar.gz
sh frame-hands-headset/install.sh
```

Then restart the Frame once. The installer:

- installs the exporter as a service that starts with Frametop's hand tracking;
- sets Frametop so hand tracking runs in SteamVR games (`HANDS_MODE=always`, `pause_auto` off) and
  its pointer doesn't take a closed hand (`POINTER_HANDS=0`, `HANDS_NEAR=0`). It backs up the files
  first; `--no-frametop-settings` skips this.

It finds your PC by itself. If it can't (some networks block broadcasts), tell it:
`sh frame-hands-headset/install.sh --pc 192.168.1.20`.

### 3. Play

Start SteamVR on the PC as usual, put the controllers down, and hold your hands up. In VRChat,
your hands and fingers follow yours; pinch to click.

## Which games?

- **VRChat:** the full experience (it's what Frame Hands is tuned for), with button bindings
  included.
- **Other SteamVR games with hand or finger tracking** (games that use SteamVR's skeletal input,
  or OpenXR hand tracking through SteamVR): your hands and fingers show and move the same way.
- **Games made only for controllers:** you get hands with a trigger (pinch), grip (fist) and B
  (thumb + middle pinch), but no thumbstick or other buttons, so moving around usually needs
  controllers. Pick up the controllers and set `share_with_controllers` to `1` to switch between
  them (see SETTINGS.md).

## Change what the gestures do

The gestures are buttons on a controller called **Frame Hands**, so you can remap them per game in
SteamVR, like any controller:

1. In SteamVR: **Settings → Controllers → Manage Controller Bindings** (or **Show old Binding UI**).
2. Pick the game, choose **Custom**, then **Edit This Binding**, with **Frame Hands** as the
   controller.
3. Bind **Trigger** (thumb + index pinch), **Grip** (fist) or **B** (thumb + middle pinch) to
   whatever the game offers.

VRChat comes with a binding: pinch = interact/use, fist = grab. B isn't bound there, because
VRChat's own hand gestures open its menu.

## Settings

Everything is in `%USERPROFILE%\FrameHands\framehands\resources\settings\calibration.json`, and
changes apply while you play. See [docs/SETTINGS.md](docs/SETTINGS.md).

## Is it working?

- **PC:** SteamVR's log (`Steam\logs\vrserver.txt`) has a line per hand every few seconds, like
  `right hand: frame age 9.8 ms ... Frametop skeleton yes ... 1234 packets from 192.168.1.30`.
  Without hand data it says `Frame Hands: no hand data in the last 15 s` with what to check.
- **Headset:** `journalctl --user -u frame-hands-exporter -f` shows `Found the Frame Hands driver at
  <your PC>` and how many frames it sent.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `install.cmd` says SteamVR is running | Close SteamVR (and VRChat) completely, then run it again. |
| No hands in SteamVR | Is hand tracking on in Frametop? Is `frame-hands-exporter` running on the headset (above)? Run `install-driver.ps1 -FirewallOnly` as admin on the PC. On networks without broadcasts, install with `--pc <your PC's IP>`. |
| Your hand turns into a controller when you close it | Frametop's pointer took the hand: set `POINTER_HANDS=0` and `HANDS_NEAR=0` in `~/.config/frametop.conf` (the installer does) and restart the Frame. |
| Hand tracking stops when a game starts | Set `"pause_auto": false` in `~/.config/frametop-input.json` (Frametop Input Settings → Game optimization). |
| VRChat uses the Frame controllers instead of your hands | Keep `share_with_controllers` at `0` (the default). |
| Fingers lag | `skeleton_finger_filter 0`, `finger_interp 0` (see SETTINGS.md). |
| Fingers shake | `skeleton_finger_filter 1`; lower `smooth_min_cutoff`. |
| Windows slows down after hours of VR | Some RGB/peripheral apps (HyperX NGENUITY was seen doing it) leak memory in Windows' `csrss.exe` while SteamVR runs; close them before VR. |

## Uninstall

- PC: double-click `uninstall.cmd` (close SteamVR first).
- Headset: `sh ~/.local/share/frame-hands/uninstall.sh`. Frametop's previous settings are in
  `~/.config/frametop.conf.frame-hands-backup` and `~/.config/frametop-input.json.frame-hands-backup`.

## How it works

```
Steam Frame                                                  PC
Frametop's tracker (ft-hands)                                SteamVR
  └ /run/user/UID/frametop-hands/{hands,gestures}             └ Frame Hands driver (driver_framehands.dll)
      └ frame_hands_exporter.py ──── UDP 39877 (Wi-Fi) ────▶      hand skeletons + trigger/grip/B ──▶ VRChat
```

Each frame Frametop publishes (up to 30 a second per hand), the exporter sends the 21 tracked
points of each hand, Frametop's skeleton status and gestures, and the frame's age. The driver
places each hand with your head pose from when the frame was captured, smooths it, points every
finger bone at the real finger, applies contact retargeting, and gives SteamVR the skeleton.
Packet layout: [driver/src/hand_link.h](driver/src/hand_link.h).

## Build from source

Windows, with Visual Studio 2022 ("Desktop development with C++", which includes CMake) and git:

```powershell
.\scripts\build.ps1          # clones the OpenVR SDK v2.15.6 into third_party/, builds, runs the tests
.\scripts\install-driver.ps1 # installs build\output\drivers\framehands
.\scripts\package.ps1        # makes the two release downloads in dist\
```

`tools/ft_roles.py` shows which SteamVR device holds each hand (`pip install openvr`).
`tools/ft_touch_analyze.py` and `tools/ft_lag_analyze.py` analyze hand recordings (one line per
hand per frame: `time seq right confidence x0 y0 z0 ... x20 y20 z20`), and
`tests/retarget_test` runs contact retargeting on one.

## Credits and license

- Hand tracking by [Frametop](https://github.com/Frametop/frametop) (MIT) and its hand models
  (CC BY-NC 4.0), which Frame Hands doesn't include: install Frametop on the Frame.
- The SteamVR driver started from Valve's OpenVR SDK sample driver (BSD-3-Clause).

Frame Hands is MIT-licensed: see [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Because Frametop's hand models are
non-commercial, keep Frame Hands free.
