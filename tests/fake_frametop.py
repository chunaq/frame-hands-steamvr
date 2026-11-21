"""Fake Frametop hand and gesture files (layouts: Frametop's fh_hands.h / fh_gestures.h), for tests."""
import struct


def hands_file(version=2, hands=((7, True), (9, False))):
    size = 2656 + (2048 if version >= 2 else 0)
    h = bytearray(size)
    h[0:8] = b"FHHANDS1"
    struct.pack_into("<II", h, 8, version, size)
    struct.pack_into("<QQQ", h, 16, 10, 123, 456)     # seq, capture_ns, publish_ns
    struct.pack_into("<I", h, 40, len(hands))
    for k, (hid, right) in enumerate(hands):
        o = 64 + k * 272
        struct.pack_into("<IIf", h, o, hid, (1 if right else 0) | 2, 1.0)
        for j in range(21):
            struct.pack_into("<fff", h, o + 16 + 12 * j, (0.1 if right else -0.1) + j * 0.001, -0.2, -0.4)
        if version >= 2:
            fo = 2656 + k * 1024
            struct.pack_into("<II", h, fo, hid, 1 | (2 if right else 0) | 4 | (8 if k == 1 else 0))
            struct.pack_into("<ff", h, fo + 912, 0.011, 0.012 + k)
            struct.pack_into("<ff", h, fo + 920, 0.0075, 1.0)
    return bytes(h)


def gestures_file(version=3):
    size = 64 + (6 if version >= 3 else 4) * 64
    g = bytearray(size)
    g[0:8] = b"FHGEST01"
    struct.pack_into("<II", g, 8, version, size)
    struct.pack_into("<Q", g, 16, 4)

    def slot(off, flags, hid, strength):
        struct.pack_into("<II", g, off, flags, hid)
        struct.pack_into("<ff", g, off + 32, 0.01, strength)

    slot(64, 3, 9, 0.9)          # pinch, left slot: hand 9, held
    slot(128, 1, 7, 0.2)         # pinch, right slot: hand 7, tracked
    slot(192, 3, 9, 1.0)         # grip: hand 9 held
    slot(256, 1, 7, 0.0)
    if version >= 3:
        slot(320, 1, 9, 0.1)     # middle pinch: hand 7 held
        slot(384, 3, 7, 0.95)
    return bytes(g)
