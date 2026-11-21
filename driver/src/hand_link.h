// Frame Hands: the hand data the headset sends (headset/frame_hands_exporter.py), received by
// the driver itself over UDP. No bridge program runs on the PC.
//
// Discovery: the headset sends a small "hello" (broadcast, and to any Steam streaming peer) until
// a PC answers; the driver answers every hello, so the headset learns where to send.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

static const uint16_t kFrameHandsPort = 39877;   // UDP, on the PC

struct FrametopHandData
{
    uint32_t right;
    float confidence;
    float pts[21][3];           // metres, the headset's head frame at capture time (OpenVR HMD axes)
};

struct FrametopFrameData
{
    char magic[8];              // "FTHAND01"
    uint64_t sequence;          // even, counts frames received
    uint32_t count;             // hands in this frame (0..2)
    uint32_t reserved;          // the frame's age when the headset sent it, microseconds
    FrametopHandData hands[2];
};

// Frametop 0.3.0+ extras (hands file v2, gestures file v3). ext[k] goes with hands[k]; all zero
// (magic doesn't match) from an older headset.
enum : uint32_t {
    FT_FIT_VALID   = 1u << 0,   // Frametop fitted a skeleton to this hand (its points are the skeleton's)
    FT_FIT_RIGHT   = 1u << 1,   // fitted as a right hand
    FT_FIT_SETTLED = 1u << 2,   // its side's bone lengths have settled
    FT_FIT_FLIPPED = 1u << 3,   // fitted as the other side than the model called it
};
enum : uint32_t {
    FT_GEST_PINCH  = 1u << 0,   // Frametop: thumb-index pinch held now
    FT_GEST_PINCH2 = 1u << 1,   // Frametop: thumb-middle pinch held now
    FT_GEST_GRIP   = 1u << 2,   // Frametop: fist held now
    FT_GEST_VALID  = 1u << 3,   // the gestures above were found for this hand
    FT_GEST_HAS_PINCH2 = 1u << 4, // the headset reports middle pinches at all
};

struct FrametopExtHand
{
    uint32_t fit_flags;         // FT_FIT_*
    uint32_t gestures;          // FT_GEST_*
    float settled;              // 0..1
    float fit_rms;              // m
    float pinch_d[2];           // thumb tip to index tip, to middle tip, m (skeleton)
    float pinch_strength;       // 0 open .. 1 closed (Frametop's pinch)
    float pinch2_strength;      // the same for the middle pinch
};

struct FrametopFrameExt
{
    char magic[8];              // "FTFIT001"
    FrametopExtHand hands[2];
};

// One UDP datagram from the headset, little-endian, packed as laid out (all fields 4-byte
// aligned, no padding).
#pragma pack(push, 4)
struct FrameHandsPacket
{
    char magic[8];              // "FHPKT001"
    uint32_t age_us;            // frame age as the headset sent it
    uint32_t count;             // hands (0..2)
    uint64_t headset_seq;       // the headset's frame number
    FrametopHandData hands[2];
    FrametopFrameExt ext;
};
#pragma pack(pop)

static_assert(sizeof(FrametopHandData) == 260, "FrametopHandData layout");
static_assert(sizeof(FrametopFrameData) == 544, "FrametopFrameData layout");
static_assert(sizeof(FrametopExtHand) == 32, "FrametopExtHand layout");
static_assert(sizeof(FrametopFrameExt) == 72, "FrametopFrameExt layout");
static_assert(sizeof(FrameHandsPacket) == 616, "FrameHandsPacket layout");

// Checks and unpacks one datagram. seq: the sequence number to give the frame. Returns false
// (outputs untouched) for anything that isn't a hand packet. No OpenVR or socket dependency, so
// tests can call it (tests/packet_test.cpp).
bool ParseFrameHandsPacket(const void* data, size_t size, uint64_t seq,
                           FrametopFrameData& frame, FrametopFrameExt& ext);

// Is this datagram a headset's hello? (The receiver answers it.)
bool IsFrameHandsHello(const void* data, size_t size);

// The receiver: one per process (both hand devices read the same frames).
class FrameHandsLink
{
public:
    static FrameHandsLink& Get();

    // The newest frame (and its extras). False until one has arrived. The same frame is returned
    // again until a new one arrives (compare frame.sequence).
    bool Read(FrametopFrameData& frame, FrametopFrameExt* ext = nullptr);

    // Stop the receiver thread (driver Cleanup). Get() starts it again if needed.
    void Stop();

    // For the log: packets received, and the address they came from (empty if none).
    uint64_t Packets() const { return packets_.load(); }
    void LastSender(char* out, size_t size);

private:
    FrameHandsLink() = default;
    void Start();
    void Run();

    std::mutex mutex_;
    bool have_ = false;
    FrametopFrameData frame_{};
    FrametopFrameExt ext_{};
    char sender_[64] = {};
    std::atomic<uint64_t> packets_{ 0 };
    std::atomic<bool> running_{ false };
    std::atomic<uintptr_t> socket_{ ~uintptr_t(0) };
    std::thread thread_;
};
