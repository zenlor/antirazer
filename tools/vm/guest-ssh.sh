#!/usr/bin/env bash
# Run a command in the Windows guest over SSH.
#
# usage: NAGA_VM_PASS=... ./guest-ssh.sh "command"
#
#   NAGA_VM_PASS   guest password (required; or set up an SSH key instead)
#   NAGA_VM_USER   default naga
#   NAGA_VM_SSH_PORT   default 2222 (as forwarded by run-windows-vm.sh)
#
# The guest needs an SSH server; see docs/vm-capture.md for the automation
# caveats (interactive desktop, scheduled tasks).
set -euo pipefail

: "${NAGA_VM_PASS:?set NAGA_VM_PASS to the guest password (or use ssh keys)}"
USER_NAME="${NAGA_VM_USER:-naga}"
PORT="${NAGA_VM_SSH_PORT:-2222}"

exec sshpass -p "$NAGA_VM_PASS" ssh \
	-p "$PORT" \
	-o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null \
	-o ConnectTimeout=25 \
	"$USER_NAME@127.0.0.1" "$@"
