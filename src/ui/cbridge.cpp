/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * cbridge.cpp — extern "C" shims for the ImGui SDL3 + SDLRenderer3 backends.
 *
 * cimgui's generated SDL3 wrapper does not link cleanly at the pinned
 * upstream commit, so the C UI calls these wrappers around the upstream C++
 * backends instead.
 */
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"

extern "C" {

bool naga_impl_sdl3_init_for_sdlrenderer(SDL_Window *window, SDL_Renderer *renderer)
{
	return ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
}

bool naga_impl_sdl3_processevent(const SDL_Event *event)
{
	return ImGui_ImplSDL3_ProcessEvent(event);
}

void naga_impl_sdl3_newframe(void) { ImGui_ImplSDL3_NewFrame(); }

void naga_impl_sdl3_shutdown(void) { ImGui_ImplSDL3_Shutdown(); }

bool naga_impl_sdlrenderer3_init(SDL_Renderer *renderer)
{
	return ImGui_ImplSDLRenderer3_Init(renderer);
}

void naga_impl_sdlrenderer3_newframe(void) { ImGui_ImplSDLRenderer3_NewFrame(); }

void naga_impl_sdlrenderer3_render(ImDrawData *draw_data, SDL_Renderer *renderer)
{
	ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer);
}

void naga_impl_sdlrenderer3_shutdown(void) { ImGui_ImplSDLRenderer3_Shutdown(); }

} // extern "C"
