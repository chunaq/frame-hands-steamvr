"""Writes packets made by the headset exporter's own code, for the driver's packet_test."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [here, os.path.join(here, "..", "headset")]
import fake_frametop as ff
import frame_hands_exporter as ex

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
seq, _, hands = ex.parse(ff.hands_file(2), ff.gestures_file(3))
open(os.path.join(out, "packet_two_hands.bin"), "wb").write(ex.build_packet(seq, 12345, hands))
seq, _, hands = ex.parse(ff.hands_file(1, hands=((5, True),)), ff.gestures_file(2))
open(os.path.join(out, "packet_old_frametop.bin"), "wb").write(ex.build_packet(seq, 0, hands))
open(os.path.join(out, "packet_hello.bin"), "wb").write(ex.HELLO)
print("packets written to", out)
