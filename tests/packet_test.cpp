// The driver's packet parser against packets made by the headset exporter's own code
// (tests/make_packets.py writes them; CTest runs both). Also checks bad input is refused.
//   packet_test <dir with packet_*.bin>
#include "hand_link.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::vector<char> Load(const std::string& path)
{
    std::vector<char> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    FrametopFrameData frame{};
    FrametopFrameExt ext{};

    // Two hands, Frametop 0.3.0 extras: right hand (id 7) middle pinch held, left fist + pinch.
    std::vector<char> two = Load(dir + "/packet_two_hands.bin");
    CHECK(two.size() == sizeof(FrameHandsPacket));
    CHECK(ParseFrameHandsPacket(two.data(), two.size(), 42, frame, ext));
    CHECK(std::memcmp(frame.magic, "FTHAND01", 8) == 0);
    CHECK(frame.sequence == 42);
    CHECK(frame.count == 2);
    CHECK(frame.reserved == 12345);
    CHECK(frame.hands[0].right == 1 && frame.hands[1].right == 0);
    CHECK(std::fabs(frame.hands[0].confidence - 1.f) < 1e-6f);
    CHECK(std::fabs(frame.hands[0].pts[4][0] - 0.104f) < 1e-6f);
    CHECK(std::fabs(frame.hands[1].pts[20][2] + 0.4f) < 1e-6f);
    CHECK(std::memcmp(ext.magic, "FTFIT001", 8) == 0);
    CHECK(ext.hands[0].fit_flags == (FT_FIT_VALID | FT_FIT_RIGHT | FT_FIT_SETTLED));
    CHECK(ext.hands[0].gestures == (FT_GEST_VALID | FT_GEST_PINCH2 | FT_GEST_HAS_PINCH2));
    CHECK(ext.hands[1].gestures == (FT_GEST_VALID | FT_GEST_PINCH | FT_GEST_GRIP | FT_GEST_HAS_PINCH2));
    CHECK(std::fabs(ext.hands[0].pinch2_strength - 0.95f) < 1e-6f);
    CHECK(std::fabs(ext.hands[1].fit_rms - 0.0075f) < 1e-6f);

    // An older Frametop (no skeleton, no gestures): extras zero.
    std::vector<char> old = Load(dir + "/packet_old_frametop.bin");
    CHECK(ParseFrameHandsPacket(old.data(), old.size(), 2, frame, ext));
    CHECK(frame.count == 1);
    CHECK(ext.magic[0] == 0 && ext.hands[0].fit_flags == 0);
    CHECK(frame.hands[1].confidence == 0.f);   // the unused slot is cleared

    // Bad input: short, wrong magic, too many hands, NaN points.
    FrametopFrameData keep{};
    keep.sequence = 99;
    FrametopFrameData f2 = keep;
    CHECK(!ParseFrameHandsPacket(two.data(), two.size() - 1, 1, f2, ext) && f2.sequence == 99);
    std::vector<char> bad = two;
    bad[0] = 'X';
    CHECK(!ParseFrameHandsPacket(bad.data(), bad.size(), 1, f2, ext));
    bad = two;
    const uint32_t three = 3;
    std::memcpy(bad.data() + 12, &three, 4);
    CHECK(!ParseFrameHandsPacket(bad.data(), bad.size(), 1, f2, ext));
    bad = two;
    const float nan = std::nanf("");
    std::memcpy(bad.data() + 24 + 8, &nan, 4);   // hand 0, point 0, x
    CHECK(ParseFrameHandsPacket(bad.data(), bad.size(), 1, f2, ext));
    CHECK(f2.hands[0].confidence == 0.f && f2.hands[1].right == 0 && f2.hands[1].confidence == 1.f);

    // Hello datagrams are recognised, hand packets aren't hellos.
    std::vector<char> hello = Load(dir + "/packet_hello.bin");
    CHECK(IsFrameHandsHello(hello.data(), hello.size()));
    CHECK(!IsFrameHandsHello(two.data(), two.size()));

    std::printf(failures ? "%d check(s) failed\n" : "all packet checks passed\n", failures);
    return failures ? 1 : 0;
}
