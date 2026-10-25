# Applies the Naga LH 2020 onboard-button patch to OpenRazer from nixpkgs.
#
# Usage:
#   nixpkgs.overlays = [ antirazer.overlays.default ];
#   hardware.openrazer.enable = true;
#
# The kernel module and the daemon are separate derivations with different
# source roots, so each gets the part of the change that touches it. The
# overlay patches `pkgs.linuxPackages`; if you boot another kernel package set
# (e.g. `pkgs.linuxPackages_latest`), extend that set with the driver patch or
# point `hardware.openrazer.packages.kernel` at a patched build.
final: prev:
let
  driverPatch = ./patches/naga-lh-2020-driver.patch;
  daemonPatch = ./patches/naga-lh-2020-daemon.patch;

  withPatch = patch: old: {
    patches = (old.patches or [ ]) ++ [ patch ];
  };
in {
  python3Packages = prev.python3Packages.overrideScope (self: super: {
    openrazer-daemon = super.openrazer-daemon.overrideAttrs (withPatch daemonPatch);
  });

  # Kept consistent for anyone using the application wrapper directly.
  openrazer-daemon = prev.python3Packages.toPythonApplication
    (prev.python3Packages.openrazer-daemon.overrideAttrs (withPatch daemonPatch));

  linuxPackages = prev.linuxPackages.extend (self: super: {
    openrazer = super.openrazer.overrideAttrs (withPatch driverPatch);
  });
}
