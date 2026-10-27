#!/bin/sh
# hp-evtest — record evtest for every input node of the HP 300, one log each.
# Ctrl+C: stop the current node and move to the next. Ctrl+\ (SIGQUIT): quit.
#
# Run it from the devshell (evtest lives there), keeping PATH for sudo:
#   sudo -E env PATH="$PATH" ./research/hp-evtest.sh
# Logs land in the current directory.
set -u

EVTEST="comma evtest"

for node in /dev/input/by-id/usb-HP_HP_Pavilion_Gaming_Mouse_300-event-*; do
	[ -e "$node" ] || continue
	base=${node##*/}
	log="evtest.$base.log"
	$EVTEST "$node" 2>&1 | tee "$log" &
done

wait
