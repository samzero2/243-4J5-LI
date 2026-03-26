#!/bin/bash
# Lance l'interface tactile du jeu sur TTY1
# Usage: sudo ./launch_jeu_ui.sh

set -euo pipefail

echo "Demarrage interface tactile du jeu..."
echo "Basculement vers TTY1..."
chvt 1

setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 /home/samia/243-4J5-LI/labo/Labo-01/led-control/touch_ui_jeu.py'
