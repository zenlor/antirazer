#!/usr/bin/env bash
# Type ASCII text into the guest through the QEMU monitor (sendkey).
#
# usage: ./type.sh "text"
#        DBG=1 ./type.sh "text"    print the monitor commands instead
#
# Needs socat. Set NAGA_VM_DIR if the VM does not use the default state
# directory (see run-windows-vm.sh).
set -euo pipefail

text="${1:?usage: type.sh \"text to type\"}"
sock="${NAGA_VM_DIR:-$HOME/.local/share/antirazer/vm}/monitor.sock"

keys=""
for ((i = 0; i < ${#text}; i++)); do
	c="${text:i:1}"
	k=""
	case "$c" in
		[a-z]) k="$c" ;;
		[A-Z]) k="shift-${c,}" ;;
		[0-9]) k="$c" ;;
		' ') k=spc ;;
		.) k=dot ;;
		,) k=comma ;;
		-) k=minus ;;
		_) k=shift-minus ;;
		=) k=equal ;;
		/) k=slash ;;
		\\) k=backslash ;;
		';') k=semicolon ;;
		:) k=shift-semicolon ;;
		"'") k=apostrophe ;;
		'"') k=shift-apostrophe ;;
		'\n') k=ret ;;
		%) k=shift-5 ;;
		'>') k=shift-dot ;;
		'<') k=shift-comma ;;
		'&') k=shift-7 ;;
		'|') k=shift-backslash ;;
		'(') k=shift-9 ;;
		')') k=shift-0 ;;
		@) k=shift-2 ;;
		'#') k=shift-3 ;;
		'!') k=shift-1 ;;
		'?') k=shift-slash ;;
		'*') k=shift-8 ;;
		'$') k=shift-4 ;;
		'^') k=shift-6 ;;
		+) k=shift-equal ;;
		'[') k=bracket_left ;;
		']') k=bracket_right ;;
		'{') k=shift-bracket_left ;;
		'}') k=shift-bracket_right ;;
		'~') k=shift-grave ;;
	esac
	[ -n "$k" ] && keys+="sendkey $k"$'\n'
done
keys+="sleep 0.3"$'\n'

if [ "${DBG:-0}" = 1 ]; then
	printf '%s' "$keys"
	exit 0
fi

[ -S "$sock" ] || { echo "no QEMU monitor at $sock (is the VM running?)" >&2; exit 1; }
command -v socat >/dev/null || { echo "socat not found (nix shell nixpkgs#socat)" >&2; exit 1; }
printf '%s' "$keys" | socat - "unix-connect:$sock" >/dev/null
