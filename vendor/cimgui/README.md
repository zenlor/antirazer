# Vendored cimgui + Dear ImGui

Vendored from https://github.com/cimgui/cimgui at commit
`2a6a93fccd10f275ee9206167dc9698f6f00fac1` (cimgui 1.93, docking branch),
with its `imgui` submodule.

Only the files needed by antirazer are kept: the cimgui C wrapper, the ImGui
core, and the GLFW + OpenGL3 backends. Generated docs, examples and tests
were removed. Upstream licenses are in `LICENSE` (cimgui) and
`LICENSE.imgui` (Dear ImGui), both MIT.

The build compiles `cimgui.cpp`, the ImGui core and the two backends; the
generated `cimgui_impl.cpp` is not used (see `src/ui/cbridge.cpp`).

The OpenGL3 backend's bundled GL loader is patched to fetch function pointers
with `glfwGetProcAddress` instead of `dlopen("libGL")`. On NixOS the GL
libraries are not on the default dlopen path in every environment, while GLFW
already has a valid context.
