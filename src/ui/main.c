/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga-ui — clickable configuration UI for the Naga LH 2020.
 *
 * SDL3 window + SDLRenderer3, ImGui via cimgui. The UI itself lives in
 * ui_layout.c; this file only owns the window, the frame loop and shutdown.
 *
 *   naga-ui                     run it
 *   naga-ui --screenshot FILE   render a few frames, save a BMP and exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "ui.h"

static ImVec2 V(float x, float y)
{
	ImVec2 v = {x, y};
	return v;
}

/*
 * The upstream cimgui SDL3 wrapper does not link cleanly at this pin, so
 * cbridge.cpp wraps the C++ backends. These are its C entry points.
 */
extern bool naga_impl_sdl3_init_for_sdlrenderer(SDL_Window *, SDL_Renderer *);
extern bool naga_impl_sdl3_processevent(const SDL_Event *);
extern void naga_impl_sdl3_newframe(void);
extern void naga_impl_sdl3_shutdown(void);
extern bool naga_impl_sdlrenderer3_init(SDL_Renderer *);
extern void naga_impl_sdlrenderer3_newframe(void);
extern void naga_impl_sdlrenderer3_render(ImDrawData *, SDL_Renderer *);
extern void naga_impl_sdlrenderer3_shutdown(void);

#define WINDOW_TITLE "naga-ui - Razer Naga LH 2020"
#define WINDOW_W 1120
#define WINDOW_H 640

static bool create_video(SDL_Window **window, SDL_Renderer **renderer)
{
	*window =
	    SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H, SDL_WINDOW_RESIZABLE);
	if (*window)
		*renderer = SDL_CreateRenderer(*window, NULL);
	if (*window && *renderer)
		return true;

	/*
	 * Some Wayland sessions cannot give us a renderer. Retry on X11 before
	 * giving up, unless the user picked a driver explicitly.
	 */
	if (!getenv("SDL_VIDEODRIVER")) {
		SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER",
					   "x11", 1);
		SDL_DestroyRenderer(*renderer);
		SDL_DestroyWindow(*window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		*window = NULL;
		*renderer = NULL;
		if (!SDL_Init(SDL_INIT_VIDEO))
			return false;
		*window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H,
					   SDL_WINDOW_RESIZABLE);
		if (*window)
			*renderer = SDL_CreateRenderer(*window, NULL);
		if (*window && *renderer)
			return true;
	}

	SDL_DestroyRenderer(*renderer);
	SDL_DestroyWindow(*window);
	*window = NULL;
	*renderer = NULL;
	return false;
}

static bool save_screenshot(SDL_Renderer *renderer, const char *path)
{
	SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
	bool ok;

	if (!shot) {
		fprintf(stderr, "screenshot: %s\n", SDL_GetError());
		return false;
	}
	ok = SDL_SaveBMP(shot, path);
	if (!ok)
		fprintf(stderr, "screenshot: %s\n", SDL_GetError());
	SDL_DestroySurface(shot);
	return ok;
}

int main(int argc, char **argv)
{
	const char *screenshot = NULL;
	SDL_Window *window = NULL;
	SDL_Renderer *renderer = NULL;
	ui_state state;
	ImGuiStyle *style;
	bool running = true;
	int frames = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
			screenshot = argv[++i];
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("usage: %s [--screenshot FILE.bmp]\n", argv[0]);
			return 0;
		} else {
			fprintf(stderr, "usage: %s [--screenshot FILE.bmp]\n", argv[0]);
			return 1;
		}
	}

	SDL_SetAppMetadata("naga-ui", "0.1.0", "https://git.sr.ht/~zenlor/antirazer");
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	if (!create_video(&window, &renderer)) {
		fprintf(stderr, "SDL video: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	igCreateContext(NULL);
	style = igGetStyle();
	style->WindowRounding = 6.0f;
	style->ChildRounding = 6.0f;
	style->FrameRounding = 4.0f;
	style->GrabRounding = 4.0f;
	style->WindowPadding = V(12.0f, 10.0f);
	style->ItemSpacing = V(8.0f, 7.0f);

	naga_impl_sdl3_init_for_sdlrenderer(window, renderer);
	naga_impl_sdlrenderer3_init(renderer);

	ui_state_init(&state);
	ui_state_connect(&state);

	while (running) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			naga_impl_sdl3_processevent(&event);
			if (event.type == SDL_EVENT_QUIT)
				running = false;
		}

		naga_impl_sdl3_newframe();
		naga_impl_sdlrenderer3_newframe();
		igNewFrame();
		ui_draw(&state);
		igRender();
		naga_impl_sdlrenderer3_render(igGetDrawData(), renderer);
		SDL_RenderPresent(renderer);

		frames++;
		if (screenshot && frames == 3) {
			save_screenshot(renderer, screenshot);
			running = false;
		}
	}

	naga_close();
	naga_impl_sdlrenderer3_shutdown();
	naga_impl_sdl3_shutdown();
	igDestroyContext(NULL);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
