# Third-party notices

## Included in this repository

### OpenVR SDK driver sample (Valve Corporation) — BSD-3-Clause

The SteamVR driver started from the `handskeletonsimulation` sample driver in Valve's OpenVR SDK
(https://github.com/ValveSoftware/openvr, v2.15.6). These files keep Valve's copyright line and
are modified for Frame Hands: `driver/src/controller_device_driver.*`, `driver/src/device_provider.*`,
`driver/src/hand_simulation.*`, `driver/src/hmd_driver_factory.cpp`, and the driver's resource
files (manifest, input profile, status icons). The build also uses the SDK itself and the samples'
`driverlog` and `vrmath` helpers, fetched from Valve's repository at build time (not stored here).

```
Copyright (c) 2015, Valve Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Used, not included

### Frametop — MIT

Frame Hands needs [Frametop](https://github.com/Frametop/frametop) installed on the Steam Frame:
Frametop's tracker (ft-hands) does the hand tracking. Frame Hands contains no Frametop code. Its
exporter (`headset/frame_hands_exporter.py`) reads the files Frametop's tracker publishes for
other programs (`/run/user/UID/frametop-hands/hands` and `gestures`), following the layouts in
Frametop's `hands/include/fh_hands.h` and `fh_gestures.h`. Frametop is MIT-licensed, copyright
(c) 2026 DeeJanuz. Frame Hands is an independent project, not made by or affiliated with the
Frametop project.

### Frametop's hand models — CC BY-NC 4.0

Frametop's fine-tuned hand models (for example v20a, from the Hugging Face repository
`DeeJanuz/frametop-hands-models`) are licensed CC BY-NC 4.0 (non-commercial). Frame Hands does
not include or distribute them: Frametop downloads them on the headset. Because the hand tracking
relies on them, Frame Hands is free and should not be sold or bundled with them commercially.

## Trademarks

Steam, Steam Frame and SteamVR are trademarks of Valve Corporation. VRChat is a trademark of
VRChat Inc. Frame Hands is not made by, endorsed by or affiliated with Valve or VRChat; the names
are only used to say what it works with.
