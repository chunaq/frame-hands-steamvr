#!/bin/sh
# Frame Hands, on the Steam Frame: removes the exporter. Frametop's settings stay as they are;
# the originals are in ~/.config/frametop.conf.frame-hands-backup and
# ~/.config/frametop-input.json.frame-hands-backup if you want them back.
set -u
systemctl --user disable --now frame-hands-exporter.service 2>/dev/null
rm -f "$HOME/.config/systemd/user/frame-hands-exporter.service"
systemctl --user daemon-reload
rm -rf "$HOME/.local/share/frame-hands"
echo "Frame Hands exporter removed."
for f in "$HOME/.config/frametop.conf" "$HOME/.config/frametop-input.json"; do
    [ -f "$f.frame-hands-backup" ] && echo "Frametop's settings before Frame Hands: $f.frame-hands-backup (copy it back over $f to restore)"
done
exit 0
