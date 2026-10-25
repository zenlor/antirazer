#!/usr/bin/env bash
# Windows VM for capturing Synapse ground truth (see docs/vm-capture.md).
#
# usage: ./run-windows-vm.sh [/path/to/windows.iso]
#
# The first run needs an ISO and boots the installer; later runs boot the
# installed system. Everything (disk, firmware variables, TPM state, monitor
# socket) lives in $NAGA_VM_DIR, so the repository stays clean:
#
#   NAGA_VM_DIR   default ~/.local/share/antirazer/vm
#   NAGA_VM_SPICE_PORT   SPICE display, default 5900
#   NAGA_VM_SSH_PORT     forwarded SSH, default 2222
#   NAGA_VM_RDP_PORT     forwarded RDP, default 3389
#
# The mouse is shared from the SPICE client's USB menu (usbredir), not passed
# through at the host level: this is usually the only mouse on the desk.
set -euo pipefail

ISO="${1:-}"
VM_DIR="${NAGA_VM_DIR:-$HOME/.local/share/antirazer/vm}"
SPICE_PORT="${NAGA_VM_SPICE_PORT:-5900}"
SSH_PORT="${NAGA_VM_SSH_PORT:-2222}"
RDP_PORT="${NAGA_VM_RDP_PORT:-3389}"

mkdir -p "$VM_DIR/tpm"

case "${ISO,,}" in
	*win7* | *win.7* | *winxp*) LEGACY=1 ;;
	*) LEGACY=0 ;;
esac
if [ "$LEGACY" = 1 ]; then
	DISK="$VM_DIR/win7.qcow2"
	MACHINE="pc,kernel_irqchip=split"
else
	DISK="$VM_DIR/win11.qcow2"
	MACHINE="q35,kernel_irqchip=split"
fi

[ -f "$DISK" ] || {
	[ -n "$ISO" ] && [ -f "$ISO" ] || {
		echo "need a Windows ISO (or an existing $DISK)" >&2
		exit 1
	}
}

if [ "$LEGACY" = 0 ] && { [ ! -f "$VM_DIR/OVMF_CODE.fd" ] || [ ! -f "$VM_DIR/OVMF_VARS.fd" ]; }; then
	ovmf="$(nix build --no-link --print-out-paths 'nixpkgs/nixos-26.05#OVMF.fd')"
	cp "$ovmf/FV/OVMF_CODE.fd" "$ovmf/FV/OVMF_VARS.fd" "$VM_DIR/"
	chmod u+w "$VM_DIR"/OVMF_*.fd
fi

[ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" 64G

ARGS=(
	-enable-kvm -machine "$MACHINE" -cpu host -smp 4 -m 8G
	-device ich9-ahci,id=ahci
	-drive "file=$DISK,if=none,format=qcow2,id=d0"
	-device ide-hd,drive=d0,bus=ahci.0
	-device qemu-xhci,id=xhci
	# SPICE usbredir: tick the mouse in the client's USB menu
	-chardev spicevmc,name=usbredir,id=ur1 -device usb-redir,chardev=ur1
	-chardev spicevmc,name=usbredir,id=ur2 -device usb-redir,chardev=ur2
	-chardev spicevmc,name=usbredir,id=ur3 -device usb-redir,chardev=ur3
	-chardev spicevmc,name=usbredir,id=ur4 -device usb-redir,chardev=ur4
	-device e1000,netdev=n0
	-netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22,hostfwd=tcp:127.0.0.1:$RDP_PORT-:3389"
	-device virtio-vga
	-spice "port=$SPICE_PORT,addr=127.0.0.1,disable-ticketing=on"
	-monitor "unix:$VM_DIR/monitor.sock,server,nowait"
	-serial none -daemonize
)

if [ "$LEGACY" = 1 ]; then
	# SeaBIOS, no TPM: the native boot path for old Windows
	ARGS+=(-usb -device usb-kbd -device usb-tablet)
	[ -n "$ISO" ] && ARGS+=(-cdrom "$ISO" -boot d)
else
	ARGS+=(
		-drive "if=pflash,format=raw,readonly=on,file=$VM_DIR/OVMF_CODE.fd"
		-drive "if=pflash,format=raw,file=$VM_DIR/OVMF_VARS.fd"
		-chardev "socket,id=chrtpm,path=$VM_DIR/tpm/swtpm-sock"
		-tpmdev emulator,id=tpm0,chardev=chrtpm
		-device tpm-tis,tpmdev=tpm0
		-device virtio-keyboard -device virtio-tablet
	)
	pgrep -f "swtpm socket.*$VM_DIR/tpm/swtpm-sock" >/dev/null ||
		swtpm socket --tpmstate "dir=$VM_DIR/tpm" \
			--ctrl "type=unixio,path=$VM_DIR/tpm/swtpm-sock" \
			--tpm2 --daemon --log "file=$VM_DIR/tpm/swtpm.log"
	[ -n "$ISO" ] && ARGS+=(-cdrom "$ISO" -boot d)
fi

exec qemu-system-x86_64 "${ARGS[@]}"
