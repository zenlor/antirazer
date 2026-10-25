#!/usr/bin/env bash
# Run a Python snippet in the Windows guest (for UI automation).
#
# usage: ./guest-python.sh 'python statements'
#        ./guest-python.sh script /path/to/local.py
#
# The snippet is staged in $NAGA_VM_STAGE and fetched by the guest from
# http://10.0.2.2:8000 (QEMU user networking's host alias), so run an HTTP
# server in that directory first:
#
#   NAGA_VM_STAGE=/tmp/vmstage; mkdir -p "$NAGA_VM_STAGE"
#   (cd "$NAGA_VM_STAGE" && python3 -m http.server 8000)
#
# Environment:
#   NAGA_VM_STAGE      directory served over HTTP (required)
#   NAGA_VM_PYTHON     guest interpreter, default C:\Python312\python.exe
#   NAGA_VM_WORKDIR    guest staging directory, default C:\RazerPkg
#   NAGA_VM_PASS       guest password, for guest-ssh.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
: "${NAGA_VM_STAGE:?set NAGA_VM_STAGE to the directory served on port 8000}"
python="${NAGA_VM_PYTHON:-C:\\Python312\\python.exe}"
workdir="${NAGA_VM_WORKDIR:-C:\\RazerPkg}"

if [ "${1:-}" = "script" ]; then
	src="${2:?usage: guest-python.sh script <file.py>}"
	local_script="$src"
else
	[ $# -ge 1 ] || { echo "usage: guest-python.sh '<python statements>' | script <file.py>" >&2; exit 1; }
	local_script="$(mktemp "$NAGA_VM_STAGE/winscript-XXXXXX.py")"
	{
		echo "from windows_use.agent.desktop.service import Desktop"
		echo "d = Desktop()"
		echo "$1"
	} >"$local_script"
fi

name="$(basename "$local_script")"
cp "$local_script" "$NAGA_VM_STAGE/$name"

"$here/guest-ssh.sh" "curl -s -o $(printf '%s' "$workdir")\\$name http://10.0.2.2:8000/$name && $python $(printf '%s' "$workdir")\\$name > $(printf '%s' "$workdir")\\win.out 2>&1"
"$here/guest-ssh.sh" "type $(printf '%s' "$workdir")\\win.out" 2>/dev/null || true
