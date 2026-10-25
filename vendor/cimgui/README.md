# Vendored cimgui + Dear ImGui

Vendored from https://github.com/cimgui/cimgui at commit
`2a6a93fccd10f275ee9206167dc9698f6f00fac1` (cimgui 1.93, docking branch),
with its `imgui` submodule.

Only the files needed by antirazer are kept: the cimgui C wrapper, the ImGui
core, and the SDL3/SDLRenderer3 backends. Generated docs, examples and tests
were removed. Upstream licenses are in `LICENSE` (cimgui) and
`LICENSE.imgui` (Dear ImGui), both MIT.

The build compiles `cimgui.cpp`, the ImGui core and the two backends; the
generated `cimgui_impl.cpp` is not used (see `src/ui/cbridge.cpp`).
