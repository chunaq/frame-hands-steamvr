#!/bin/sh
# Frame Hands, on the Steam Frame: installs the exporter that sends Frametop's hand tracking to
# the Frame Hands SteamVR driver on your PC. Needs Frametop (https://github.com/Frametop/frametop) with its
# hand tracking installed. Run it on the Frame (Desktop Mode's terminal, or over SSH), as you:
#
#   ./install.sh                 install, and set up Frametop for hand tracking in SteamVR games
#   ./install.sh --pc 192.168.1.20   also tell it your PC's address (normally it finds the PC itself)
#   ./install.sh --no-frametop-settings   don't change Frametop's settings (see below)
#
# Frametop settings it changes (each file backed up first as <file>.frame-hands-backup):
#   ~/.config/frametop.conf       HANDS_MODE=always   hand tracking runs whenever SteamVR runs, in games too
#                                 POINTER_HANDS=0, HANDS_NEAR=0   Frametop's pointer doesn't take a hand
#                                     when you close it (else VRChat shows a controller instead of your hand)
#   ~/.config/frametop-input.json "pause_auto": false   VR games don't pause Frametop's hand tracking
# No sudo, no system files: everything is in your home folder.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
dest="$HOME/.local/share/frame-hands"
unit_dir="$HOME/.config/systemd/user"
conf="$HOME/.config/frametop.conf"
input_json="$HOME/.config/frametop-input.json"
pc=""
frametop_settings=1

while [ $# -gt 0 ]; do
    case "$1" in
        --pc) pc="${2:-}"; shift 2 ;;
        --no-frametop-settings) frametop_settings=0; shift ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 2 ;;
    esac
done

if ! systemctl --user cat frametop-hands.service >/dev/null 2>&1; then
    echo "Frametop's hand tracking (frametop-hands.service) isn't installed."
    echo "Install Frametop first, with hand tracking, then run this again."
    exit 1
fi
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }

mkdir -p "$dest" "$unit_dir"
install -m 0644 "$here/frame_hands_exporter.py" "$dest/frame_hands_exporter.py"
install -m 0755 "$here/uninstall.sh" "$dest/uninstall.sh"

cat > "$unit_dir/frame-hands-exporter.service" <<EOF
[Unit]
Description=Frame Hands: send Frametop's hand tracking to the SteamVR driver on your PC
After=frametop-hands.service
PartOf=frametop-hands.service

[Service]
ExecStart=/usr/bin/python3 -u $dest/frame_hands_exporter.py
Restart=on-failure
RestartSec=3

[Install]
WantedBy=frametop-hands.service
EOF

if [ -n "$pc" ]; then
    printf 'PC=%s   # your PC (the Frame Hands driver); leave empty to find it automatically\n' "$pc" > "$HOME/.config/frame-hands.conf"
    echo "PC address set: $pc (~/.config/frame-hands.conf)"
fi

set_conf() {   # set_conf KEY VALUE: replace KEY=... in frametop.conf, or add it
    if grep -q "^$1=" "$conf" 2>/dev/null; then
        old=$(grep "^$1=" "$conf" | head -n 1 | sed "s/^$1=\([^ #]*\).*/\1/")
        [ "$old" = "$2" ] && return 0
        sed -i "s/^$1=[^ #]*/$1=$2/" "$conf"
        echo "  $1: $old -> $2"
    else
        printf '%s=%s   # set by Frame Hands\n' "$1" "$2" >> "$conf"
        echo "  $1=$2 (added)"
    fi
}

if [ "$frametop_settings" = 1 ]; then
    echo "Frametop settings:"
    [ -f "$conf" ] && [ ! -f "$conf.frame-hands-backup" ] && cp -p "$conf" "$conf.frame-hands-backup"
    touch "$conf"
    set_conf HANDS_MODE always
    set_conf POINTER_HANDS 0
    set_conf HANDS_NEAR 0
    if [ -f "$input_json" ]; then
        [ ! -f "$input_json.frame-hands-backup" ] && cp -p "$input_json" "$input_json.frame-hands-backup"
        python3 - "$input_json" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
if d.get("pause_auto", True) is not False:
    d["pause_auto"] = False
    json.dump(d, open(p, "w"), indent=1)
    print("  pause_auto: -> false")
EOF
    else
        printf '{\n "pause_auto": false\n}\n' > "$input_json"
        echo "  pause_auto: false (new $input_json)"
    fi
fi

systemctl --user daemon-reload
systemctl --user enable frame-hands-exporter.service >/dev/null
if systemctl --user is-active --quiet frametop-hands.service; then
    systemctl --user restart frame-hands-exporter.service
fi

echo ""
echo "Frame Hands exporter installed. It starts with Frametop's hand tracking."
[ "$frametop_settings" = 1 ] && echo "Restart the Frame once so Frametop picks up its new settings."
echo "Check it:   journalctl --user -u frame-hands-exporter -f"
echo "Uninstall:  $dest/uninstall.sh"
