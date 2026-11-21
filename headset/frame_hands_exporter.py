#!/usr/bin/env python3
"""Frame Hands exporter: sends Frametop's hand tracking from the Steam Frame to the Frame Hands
SteamVR driver on your PC.

Frametop's tracker (ft-hands) writes the tracked hands to /run/user/UID/frametop-hands/hands and
gestures (layouts: Frametop's hands/include/fh_hands.h and fh_gestures.h). This reads them as they
change and sends each frame to the PC as one small UDP packet (layout: driver/src/hand_link.h).

Finding the PC: until the driver answers, it says hello to the PC set in
~/.config/frame-hands.conf (PC=192.168.1.20), to the PC streaming to this headset (Steam's
streaming connection), and to everyone on the network (broadcast). The first answer wins; it keeps
checking, so a PC that comes back with a new address is found again.

Runs as the systemd user service frame-hands-exporter (headset/install.sh), started with
Frametop's hand tracking. By hand: python3 frame_hands_exporter.py [--verbose]
"""
import mmap
import os
import socket
import struct
import subprocess
import sys
import time

PORT = 39877                        # the driver's UDP port on the PC
RUN_DIR = os.environ.get("FRAME_HANDS_RUN_DIR") or (
    f"/run/user/{os.getuid()}/frametop-hands" if hasattr(os, "getuid") else "")   # override: tests
CONF = os.path.expanduser("~/.config/frame-hands.conf")

HELLO = b"FHHELLO1"
HERE = b"FHHERE01"
PACKET_MAGIC = b"FHPKT001"
EXT_MAGIC = b"FTFIT001"

# fh_hands.h
HANDS_MAGIC = b"FHHANDS1"
HAND_OFF, HAND_SIZE = 64, 272        # fh_hand_t array in fh_hands_t
FIT_BASE, FIT_SIZE = 64 + 2 * 272 + 64 * 32, 1024   # v2: fh_hand_fit_t after fh_hands_t
# fh_gestures.h
GEST_MAGIC = b"FHGEST01"
PINCH_OFF, GRIP_OFF, PINCH2_OFF = 64, 192, 320    # [0] left, [1] right, 64 bytes each


# ---- reading Frametop's files ---------------------------------------------------------------

def snapshot(mm):
    """A consistent copy of a seqlock-written file (seq at offset 16, odd while writing)."""
    for _ in range(20):
        before = struct.unpack_from("<Q", mm, 16)[0]
        if before & 1:
            continue
        data = mm[:]
        if struct.unpack_from("<Q", mm, 16)[0] == before:
            return data
    return None


def read_gesture(g, off):
    flags, hand_id = struct.unpack_from("<II", g, off)
    strength = struct.unpack_from("<f", g, off + 36)[0]
    return {"tracked": bool(flags & 1), "down": bool(flags & 2), "hand_id": hand_id, "strength": strength}


def parse(h, g):
    """Frametop's hands and gestures files -> (seq, capture_ns, hands). Each hand: id, right,
    confidence, points (21 x 3, metres, head frame), and with Frametop 0.3.0+ fit and gest."""
    hversion = struct.unpack_from("<I", h, 8)[0]
    seq, capture_ns = struct.unpack_from("<QQ", h, 16)
    nhands = struct.unpack_from("<I", h, 40)[0]
    hands = []
    for i in range(min(nhands, 2)):
        o = HAND_OFF + i * HAND_SIZE
        hand_id, flags, conf = struct.unpack_from("<IIf", h, o)
        pts = struct.unpack_from("<63f", h, o + 16)
        hands.append({"id": hand_id, "right": bool(flags & 1), "confidence": conf, "points": pts})

    if hversion >= 2 and len(h) >= FIT_BASE + 2 * FIT_SIZE:
        gversion = struct.unpack_from("<I", g, 8)[0] if g else 0
        kinds = {}
        if g and gversion >= 2:
            kinds["pinch"] = [read_gesture(g, PINCH_OFF + 64 * s) for s in (0, 1)]
            kinds["grip"] = [read_gesture(g, GRIP_OFF + 64 * s) for s in (0, 1)]
        has_pinch2 = bool(g) and gversion >= 3 and len(g) >= PINCH2_OFF + 128
        if has_pinch2:
            kinds["pinch2"] = [read_gesture(g, PINCH2_OFF + 64 * s) for s in (0, 1)]
        for i, hand in enumerate(hands):
            fo = FIT_BASE + i * FIT_SIZE
            fit_id, fflags = struct.unpack_from("<II", h, fo)
            fit = {"flags": 0, "settled": 0.0, "rms": 0.0, "pinch_d": (0.0, 0.0)}
            if fit_id == hand["id"] and fflags & 1:
                fit = {"flags": fflags & 0xF, "pinch_d": struct.unpack_from("<ff", h, fo + 912)}
                fit["rms"], fit["settled"] = struct.unpack_from("<ff", h, fo + 920)
            hand["fit"] = fit
            # The gesture slots are by the tracker's side: match them to this hand by id.
            gest = {}
            for kind, slots in kinds.items():
                for st in slots:
                    if st["hand_id"] == hand["id"] and st["tracked"]:
                        gest[kind] = st["down"]
                        gest[kind + "_strength"] = st["strength"]
            if gest:
                gest["has_pinch2"] = has_pinch2
            hand["gest"] = gest
    return seq, capture_ns, hands


def build_packet(seq, age_us, hands):
    """One UDP packet for the driver (driver/src/hand_link.h, FrameHandsPacket)."""
    out = bytearray(struct.pack("<8sIIQ", PACKET_MAGIC, min(age_us, 0xFFFFFFFF), len(hands), seq))
    for i in range(2):
        if i < len(hands):
            hd = hands[i]
            out += struct.pack("<If63f", 1 if hd["right"] else 0, hd["confidence"], *hd["points"])
        else:
            out += bytes(260)
    if any("fit" in hd for hd in hands):
        out += EXT_MAGIC
        for i in range(2):
            if i < len(hands) and "fit" in hands[i]:
                fit, gest = hands[i]["fit"], hands[i].get("gest") or {}
                bits = 0
                if gest:
                    bits = 8 | (1 if gest.get("pinch") else 0) | (2 if gest.get("pinch2") else 0) \
                        | (4 if gest.get("grip") else 0) | (16 if gest.get("has_pinch2") else 0)
                out += struct.pack("<IIffffff", fit["flags"], bits, fit["settled"], fit["rms"],
                                   fit["pinch_d"][0], fit["pinch_d"][1],
                                   gest.get("pinch_strength", 0.0), gest.get("pinch2_strength", 0.0))
            else:
                out += bytes(32)
    else:
        out += bytes(72)
    return bytes(out)


class Source:
    """Frametop's two files, mapped; reopened when ft-hands restarts and makes new ones."""

    def __init__(self, run_dir):
        self.paths = (os.path.join(run_dir, "hands"), os.path.join(run_dir, "gestures"))
        self.maps = None
        self.inodes = None
        self.checked = 0.0

    def close(self):
        for f, mm in self.maps or ():
            mm.close()
            f.close()
        self.maps = None

    def open(self):
        self.close()
        try:
            maps = []
            for p in self.paths:
                f = open(p, "rb")
                maps.append((f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)))
            if maps[0][1][:8] != HANDS_MAGIC or maps[1][1][:8] != GEST_MAGIC:
                raise OSError("not Frametop's hand files")
            self.maps = maps
            self.inodes = tuple(os.stat(p).st_ino for p in self.paths)
            v = [struct.unpack_from("<I", mm, 8)[0] for _, mm in maps]
            print(f"Reading Frametop's hand tracking (hands file v{v[0]}, gestures file v{v[1]}"
                  + (", skeleton and middle pinch" if v[0] >= 2 and v[1] >= 3 else "") + ")", flush=True)
            return True
        except (OSError, ValueError):
            self.close()
            return False

    def read(self):
        now = time.monotonic()
        if self.maps is None or now - self.checked > 2.0:
            self.checked = now
            try:
                inodes = tuple(os.stat(p).st_ino for p in self.paths)
            except OSError:
                inodes = None
            if self.maps is None or inodes != self.inodes:
                if not self.open():
                    return None
        h = snapshot(self.maps[0][1])
        g = snapshot(self.maps[1][1])
        return (h, g) if h is not None and g is not None else None


# ---- finding the PC ----------------------------------------------------------------------------

def configured_pc():
    pc = os.environ.get("FRAME_HANDS_PC", "")
    try:
        for line in open(CONF):
            k, _, v = line.partition("=")
            if k.strip() == "PC" and v.split("#")[0].strip():
                pc = v.split("#")[0].strip()
    except OSError:
        pass
    return pc


def streaming_peers():
    """Addresses this headset has Steam streaming connections with (the PC it streams from)."""
    try:
        out = subprocess.run(["ss", "-Htun", "state", "established"], capture_output=True,
                             text=True, timeout=2).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    peers = []
    for line in out.splitlines():
        cols = line.split()
        if len(cols) < 5:
            continue
        local, peer = cols[-2], cols[-1]
        lport = local.rsplit(":", 1)[-1]
        host, _, pport = peer.rpartition(":")
        host = host.strip("[]")
        ports = {lport, pport}
        if any(p.isdigit() and 27031 <= int(p) <= 27037 for p in ports) and host not in peers \
                and not host.startswith(("127.", "::1")):
            peers.append(host)
    return peers


class Discovery:
    def __init__(self, sock, verbose=False):
        self.sock = sock
        self.verbose = verbose
        self.pc = None
        self.last_answer = 0.0
        self.next_hello = 0.0

    def poll(self):
        now = time.monotonic()
        try:
            while True:
                data, addr = self.sock.recvfrom(64)
                if data[:8] == HERE:
                    if addr[0] != self.pc:
                        print(f"Found the Frame Hands driver at {addr[0]}", flush=True)
                    self.pc, self.last_answer = addr[0], now
        except (BlockingIOError, InterruptedError):
            pass
        except OSError:
            pass
        if now >= self.next_hello:
            found = self.pc and now - self.last_answer < 15
            self.next_hello = now + (5.0 if found else 1.0)
            targets = [self.pc] if found else []
            if not found:
                targets += [t for t in [configured_pc()] + streaming_peers() if t]
                targets.append("255.255.255.255")
            for t in dict.fromkeys(targets):
                try:
                    self.sock.sendto(HELLO, (t, PORT))
                except OSError as e:
                    if self.verbose:
                        print(f"hello to {t} failed: {e}", flush=True)
        return self.pc


# ---- main loop -----------------------------------------------------------------------------------

def main():
    verbose = "--verbose" in sys.argv
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setblocking(False)
    disc = Discovery(sock, verbose)
    src = Source(RUN_DIR)
    print("Frame Hands exporter: waiting for Frametop's hand tracking and the PC driver", flush=True)
    last_seq, sent, last_report, waiting_said = None, 0, time.monotonic(), False
    while True:
        pc = disc.poll()
        snap = src.read()
        if snap is None:
            if not waiting_said:
                print("Hand tracking isn't running (start it in Frametop, or "
                      "systemctl --user start frametop-hands)", flush=True)
                waiting_said = True
            time.sleep(0.5)
            continue
        waiting_said = False
        h, g = snap
        seq = struct.unpack_from("<Q", h, 16)[0]
        if seq == last_seq:
            time.sleep(0.002)
            continue
        last_seq = seq
        hseq, capture_ns, hands = parse(h, g)
        if pc:
            age_us = max(0, (time.monotonic_ns() - capture_ns) // 1000) if capture_ns else 0
            try:
                sock.sendto(build_packet(hseq, age_us, hands), (pc, PORT))
                sent += 1
            except OSError as e:
                if verbose:
                    print(f"send failed: {e}", flush=True)
        now = time.monotonic()
        if now - last_report >= (2 if verbose else 60):
            print(f"Sent {sent} frames to {pc or '(no PC yet)'}; tracking {len(hands)} hand(s)", flush=True)
            last_report = now
        time.sleep(0.001)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
