# Autoloaded as `devShell`: everything needed to build and hack on antirazer.
# cJSON and cimgui/ImGui are vendored under vendor/, so they are not packages.
pkgs:
pkgs.mkShell {
  packages = with pkgs; [
    gcc
    gnumake
    pkg-config
    libusb1
    libevdev
    glfw
    gdb
    usbutils # lsusb / usbhid-dump while poking at the device
    evtest # live input-event dump for the HP mouse research
    wireshark-cli # tshark: live usbmon capture + tools/captures analysis
    clang-tools # clang-format
    nixpkgs-fmt
    python3 # tools/captures and wheel-summary.py
  ];

  shellHook = ''
    echo "antirazer dev shell: 'make' builds, 'make test' runs the unit tests"
  '';
}
