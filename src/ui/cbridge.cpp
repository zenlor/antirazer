/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * cbridge.cpp — extern "C" shims for the ImGui GLFW + OpenGL3 backends.
 *
 * cimgui's generated wrappers are not used for the backends; the C UI calls
 * these wrappers around the upstream C++ backends instead.
 */
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

extern "C" {

bool naga_impl_glfw_init_for_opengl3(GLFWwindow *window)
{
	return ImGui_ImplGlfw_InitForOpenGL(window, true);
}

void naga_impl_glfw_key_callback(GLFWwindow *window, int key, int scancode,
				 int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void naga_impl_glfw_mouse_button_callback(GLFWwindow *window, int button,
					  int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void naga_impl_glfw_newframe(void) { ImGui_ImplGlfw_NewFrame(); }

void naga_impl_glfw_shutdown(void) { ImGui_ImplGlfw_Shutdown(); }

bool naga_impl_opengl3_init(const char *glsl_version)
{
	return ImGui_ImplOpenGL3_Init(glsl_version);
}

void naga_impl_opengl3_newframe(void) { ImGui_ImplOpenGL3_NewFrame(); }

void naga_impl_opengl3_render(ImDrawData *draw_data)
{
	ImGui_ImplOpenGL3_RenderDrawData(draw_data);
}

void naga_impl_opengl3_shutdown(void) { ImGui_ImplOpenGL3_Shutdown(); }

} /* extern "C" */
