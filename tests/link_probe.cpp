// Live check of the driver's receiver without SteamVR: runs FrameHandsLink (the driver's own
// UDP code) for N seconds and prints what arrives from the headset.
//   link_probe [seconds]
#include "hand_link.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv)
{
    const int secs = argc > 1 ? std::atoi(argv[1]) : 20;
    FrameHandsLink& link = FrameHandsLink::Get();
    std::printf("Listening on UDP %u for %d s...\n", unsigned(kFrameHandsPort), secs);
    uint64_t last_seq = 0;
    for (int i = 0; i < secs; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        FrametopFrameData f{};
        FrametopFrameExt e{};
        char from[64];
        link.LastSender(from, sizeof from);
        if (link.Read(f, &e) && f.sequence != last_seq) {
            last_seq = f.sequence;
            std::printf("%2d s: %llu packets from %s, %u hand(s), age %.1f ms, wrist %.3f %.3f %.3f, skeleton %s, gestures %s\n",
                        i + 1, (unsigned long long)link.Packets(), from, f.count, f.reserved / 1000.0,
                        f.hands[0].pts[0][0], f.hands[0].pts[0][1], f.hands[0].pts[0][2],
                        (e.hands[0].fit_flags & FT_FIT_VALID) ? "yes" : "no",
                        (e.hands[0].gestures & FT_GEST_VALID) ? "yes" : "no");
        } else {
            std::printf("%2d s: %llu packets\n", i + 1, (unsigned long long)link.Packets());
        }
    }
    link.Stop();
    return link.Packets() > 0 ? 0 : 1;
}
