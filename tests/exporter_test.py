"""The headset exporter against fake Frametop files: parsing, gesture matching, packet layout,
and finding the PC (a fake driver answering hellos on localhost)."""
import os, socket, struct, sys, threading, time
here = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [here, os.path.join(here, "..", "headset")]
import fake_frametop as ff
import frame_hands_exporter as ex

fails = 0
def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL", what)

# Frametop 0.3.0: skeleton and gestures, matched to each hand by id (not by slot).
seq, cap, hands = ex.parse(ff.hands_file(2), ff.gestures_file(3))
check(seq == 10 and cap == 123 and len(hands) == 2, "header")
r, l = hands
check(r["id"] == 7 and r["right"] and not l["right"], "sides")
check(r["fit"]["flags"] == 0b0111 and l["fit"]["flags"] == 0b1101, "fit flags")
check(r["gest"] == {"pinch": False, "pinch_strength": r["gest"]["pinch_strength"], "grip": False,
                    "grip_strength": 0.0, "pinch2": True, "pinch2_strength": r["gest"]["pinch2_strength"],
                    "has_pinch2": True}, "right gestures")
check(l["gest"]["pinch"] and l["gest"]["grip"] and not l["gest"]["pinch2"], "left gestures")
pkt = ex.build_packet(seq, 999, hands)
check(len(pkt) == 616, f"packet size {len(pkt)}")
check(pkt[:8] == b"FHPKT001" and struct.unpack_from("<IIQ", pkt, 8) == (999, 2, 10), "packet header")
check(pkt[544:552] == b"FTFIT001", "extras magic")
check(struct.unpack_from("<II", pkt, 552) == (0b0111, 8 | 2 | 16), "right extras")

# Older Frametop: points only, no extras.
seq, _, hands = ex.parse(ff.hands_file(1, hands=((5, True),)), ff.gestures_file(2))
check(len(hands) == 1 and "fit" not in hands[0], "old: no skeleton")
pkt = ex.build_packet(seq, 0, hands)
check(len(pkt) == 616 and pkt[544:616] == bytes(72), "old: extras zero")

# Discovery: a fake driver on localhost answers the hello sent to the configured PC.
drv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
drv.bind(("127.0.0.1", 0))
ex.PORT = drv.getsockname()[1]
os.environ["FRAME_HANDS_PC"] = "127.0.0.1"
ex.streaming_peers = lambda: []
def answer():
    drv.settimeout(5)
    try:
        data, addr = drv.recvfrom(64)
        if data == ex.HELLO:
            drv.sendto(ex.HERE, addr)
    except OSError:
        pass
threading.Thread(target=answer, daemon=True).start()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setblocking(False)
d = ex.Discovery(s)
found = None
for _ in range(100):
    found = d.poll()
    if found:
        break
    time.sleep(0.02)
check(found == "127.0.0.1", f"discovery found {found}")

print(f"{fails} check(s) failed" if fails else "all exporter checks passed")
sys.exit(1 if fails else 0)
