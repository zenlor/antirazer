# cimgui — C bindings for Dear ImGui (docking branch), built as a static
# library with only the SDL3 + SDLRenderer3 backends enabled.
#
# cimgui_impl.cpp is deliberately not built: at this pin its generated SDL3
# wrapper clashes with the backend's C linkage, so src/ui/cbridge.cpp wraps
# the upstream C++ backends directly.
pkgs: pkgs.stdenv.mkDerivation (finalAttrs: {
  pname = "cimgui";
  version = "1.93-docking-2a6a93f";

  src = pkgs.fetchFromGitHub {
    owner = "cimgui";
    repo = "cimgui";
    rev = "2a6a93fccd10f275ee9206167dc9698f6f00fac1";
    fetchSubmodules = true;
    hash = "sha256-qogtGtvQhg2Lf3sU7h5T5g9WOY+CSMZQZ5yXKGH13c8=";
  };

  nativeBuildInputs = [ pkgs.pkg-config ];
  buildInputs = [ pkgs.sdl3 ];

  buildPhase = ''
    runHook preBuild
    flags="$(pkg-config --cflags sdl3) -I. -Iimgui -Iimgui/backends -DCIMGUI_USE_SDL3"
    for f in cimgui.cpp imgui/imgui.cpp imgui/imgui_draw.cpp \
             imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_demo.cpp \
             imgui/backends/imgui_impl_sdl3.cpp \
             imgui/backends/imgui_impl_sdlrenderer3.cpp; do
      $CC -O2 -fPIC -c "$f" $flags
    done
    ar rcs libcimgui.a *.o
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/include $out/lib
    cp cimgui.h cimconfig.h $out/include/
    cp -r imgui $out/include/imgui
    rm -rf $out/include/imgui/docs $out/include/imgui/examples $out/include/imgui/test
    find $out/include/imgui -name '*.cpp' -delete
    cp libcimgui.a $out/lib/
    runHook postInstall
  '';

  meta = {
    description = "C bindings for Dear ImGui";
    homepage = "https://github.com/cimgui/cimgui";
    license = pkgs.lib.licenses.mit;
    platforms = pkgs.lib.platforms.unix;
  };
})
