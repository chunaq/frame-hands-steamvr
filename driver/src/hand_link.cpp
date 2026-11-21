#include "hand_link.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

static const char kPacketMagic[8] = { 'F', 'H', 'P', 'K', 'T', '0', '0', '1' };
static const char kHelloMagic[8]  = { 'F', 'H', 'H', 'E', 'L', 'L', 'O', '1' };
static const char kHereMagic[8]   = { 'F', 'H', 'H', 'E', 'R', 'E', '0', '1' };

bool ParseFrameHandsPacket(const void* data, size_t size, uint64_t seq,
                           FrametopFrameData& frame, FrametopFrameExt& ext)
{
    if (size < sizeof(FrameHandsPacket)) return false;
    FrameHandsPacket p;
    std::memcpy(&p, data, sizeof p);
    if (std::memcmp(p.magic, kPacketMagic, 8) != 0 || p.count > 2) return false;

    std::memcpy(frame.magic, "FTHAND01", 8);
    frame.sequence = seq;
    frame.count = p.count;
    frame.reserved = p.age_us > 1000000u ? 1000000u : p.age_us;
    for (int i = 0; i < 2; ++i) {
        frame.hands[i] = p.hands[i];
        // A NaN from a broken frame would spread through every filter: drop the hand instead.
        bool ok = true;
        for (int j = 0; j < 21 && ok; ++j)
            for (int k = 0; k < 3 && ok; ++k) {
                const float v = p.hands[i].pts[j][k];
                ok = v == v && v > -10.f && v < 10.f;
            }
        if (!ok || i >= int(p.count)) std::memset(&frame.hands[i], 0, sizeof frame.hands[i]);
    }
    ext = p.ext;
    if (std::memcmp(ext.magic, "FTFIT001", 8) != 0) std::memset(&ext, 0, sizeof ext);
    return true;
}

bool IsFrameHandsHello(const void* data, size_t size)
{
    return size >= 8 && std::memcmp(data, kHelloMagic, 8) == 0;
}

FrameHandsLink& FrameHandsLink::Get()
{
    static FrameHandsLink link;
    if (!link.running_.load()) link.Start();
    return link;
}

bool FrameHandsLink::Read(FrametopFrameData& frame, FrametopFrameExt* ext)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!have_) return false;
    frame = frame_;
    if (ext) *ext = ext_;
    return true;
}

void FrameHandsLink::LastSender(char* out, size_t size)
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::strncpy(out, sender_, size - 1);
    out[size - 1] = 0;
}

#ifdef _WIN32
void FrameHandsLink::Start()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_.load()) return;
    if (thread_.joinable()) thread_.join();
    running_ = true;
    thread_ = std::thread(&FrameHandsLink::Run, this);
}

void FrameHandsLink::Stop()
{
    running_ = false;
    const uintptr_t s = socket_.exchange(~uintptr_t(0));
    if (s != ~uintptr_t(0)) closesocket(SOCKET(s));   // wakes recvfrom
    if (thread_.joinable()) thread_.join();
}

void FrameHandsLink::Run()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { running_ = false; return; }
    while (running_.load()) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) { Sleep(1000); continue; }
        // Bigger receive buffer: frames arrive at up to 60 a second, never let a stall drop them.
        int buf = 1 << 18;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof buf);
        DWORD timeout_ms = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof timeout_ms);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kFrameHandsPort);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            closesocket(s);
            Sleep(2000);   // port busy (another SteamVR?): try again
            continue;
        }
        socket_ = uintptr_t(s);

        uint64_t seq = 0;
        char data[2048];
        while (running_.load()) {
            sockaddr_in from{};
            int from_len = sizeof from;
            const int n = recvfrom(s, data, sizeof data, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n <= 0) {
                if (WSAGetLastError() == WSAETIMEDOUT) continue;
                break;   // closed (Stop) or failed: reopen unless stopping
            }
            if (IsFrameHandsHello(data, size_t(n))) {
                sendto(s, kHereMagic, 8, 0, reinterpret_cast<sockaddr*>(&from), from_len);
                continue;
            }
            FrametopFrameData frame{};
            FrametopFrameExt ext{};
            if (!ParseFrameHandsPacket(data, size_t(n), (seq + 1) * 2, frame, ext)) continue;
            ++seq;
            ++packets_;
            std::lock_guard<std::mutex> lk(mutex_);
            frame_ = frame;
            ext_ = ext;
            have_ = true;
            inet_ntop(AF_INET, &from.sin_addr, sender_, sizeof sender_);
        }
        const uintptr_t mine = socket_.exchange(~uintptr_t(0));
        if (mine != ~uintptr_t(0)) closesocket(SOCKET(mine));
    }
    WSACleanup();
}
#else
// Tests on other platforms only use the parser.
void FrameHandsLink::Start() {}
void FrameHandsLink::Stop() {}
void FrameHandsLink::Run() {}
#endif
