# antirazer — mapping tools, GUI, remap tool and the udev rule.
pkgs:
let
  # Only ship what the build needs: the repo's research material (and any
  # VM images next to it) must never be copied into the store.
  src = pkgs.lib.fileset.toSource {
    root = ../.;
    fileset = pkgs.lib.fileset.unions [
      ../Makefile
      ../src
      ../tests
      ../tools
      ../udev
      ../vendor
    ];
  };
in
pkgs.stdenv.mkDerivation (finalAttrs: {
  pname = "antirazer";
  version = "0.1.0";

  inherit src;

  nativeBuildInputs = [ pkgs.pkg-config ];
  buildInputs = [ pkgs.libusb1 pkgs.libevdev pkgs.sdl3 ];

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    make test
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    make install PREFIX=$out UDEVDIR=$out/lib/udev/rules.d
    runHook postInstall
  '';

  meta = {
    description = "Linux configuration and remapping tools for the Razer Naga Left-Handed Edition 2020";
    homepage = "https://git.sr.ht/~zenlor/antirazer";
    license = with pkgs.lib.licenses; [ gpl2Plus bsd2 ];
    mainProgram = "naga-mapper";
    platforms = pkgs.lib.platforms.linux;
  };
})
