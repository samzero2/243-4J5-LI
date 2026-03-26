#!/bin/bash
# Lance l'interface tactile des modes unifies sur TTY1
# Usage: sudo ./launch_unified_modes_ui.sh

set -euo pipefail

echo "Demarrage UI modes unifies..."
chvt 1
setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 /home/samia/243-4J5-LI/labo/Labo-01/led-control/touch_ui_unified_modes.py'
