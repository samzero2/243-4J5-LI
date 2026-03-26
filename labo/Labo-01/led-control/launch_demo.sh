#!/bin/bash
# Lance la demo tactile complete sur TTY1
# Usage: sudo ./launch_demo.sh

set -euo pipefail

echo "Demarrage de la demo tactile (touch_ui_demo.py)..."
echo "Basculement vers TTY1..."
chvt 1

setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 /home/samia/243-4J5-LI/labo/Labo-01/led-control/touch_ui_demo.py'
