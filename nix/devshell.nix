# Autoloaded as `devShell`: everything needed to build and hack on antirazer.
pkgs:
let
  cimgui = import ./cimgui.nix pkgs;
in
pkgs.mkShell {
  packages = with pkgs; [
    gcc
    gnumake
    pkg-config
    libusb1
    sdl3
    gdb
    usbutils # lsusb / usbhid-dump while poking at the device
    clang-tools # clang-format
    nixpkgs-fmt
  ];

  env.CIMGUI = "${cimgui}";

  shellHook = ''
    echo "antirazer dev shell: 'make' builds, 'make test' runs the unit tests"
  '';
}
