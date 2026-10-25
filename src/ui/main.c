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
#include "ui_keys.h"

static ImVec2 V(float x, float y)
{
	ImVec2 v = {x, y};
	return v;
}

static ImVec4 V4(float r, float g, float b, float a)
{
	ImVec4 v = {r, g, b, a};
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
	char err[256] = "";
	const char *driver;

	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
	*window =
	    SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H, SDL_WINDOW_RESIZABLE);
	if (*window)
		*renderer = SDL_CreateRenderer(*window, NULL);
	if (*window && *renderer) {
		/* Not all drivers support it; the frame limiter is the fallback. */
		SDL_SetRenderVSync(*renderer, 1);
		return true;
	}
	snprintf(err, sizeof err, "%s", SDL_GetError());

	/* Some sessions expose only the software renderer. */
	if (*window && !*renderer) {
		SDL_ClearError();
		*renderer = SDL_CreateRenderer(*window, "software");
		if (*renderer) {
			SDL_SetRenderVSync(*renderer, 1);
			return true;
		}
	}

	/*
	 * Some Wayland sessions cannot give us a renderer. Retry on X11
	 * (usually XWayland) before giving up, unless the user picked a driver
	 * explicitly.
	 */
	if (!getenv("SDL_VIDEODRIVER")) {
		SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER",
					   "x11", 1);
		if (*renderer)
			SDL_DestroyRenderer(*renderer);
		if (*window)
			SDL_DestroyWindow(*window);
		*window = NULL;
		*renderer = NULL;
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			snprintf(err, sizeof err, "%s", SDL_GetError());
			goto fail;
		}
		*window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H,
					   SDL_WINDOW_RESIZABLE);
		if (*window)
			*renderer = SDL_CreateRenderer(*window, NULL);
		if (*window && *renderer) {
			SDL_SetRenderVSync(*renderer, 1);
			return true;
		}
		snprintf(err, sizeof err, "%s", SDL_GetError());
	}

fail:
	driver = SDL_GetCurrentVideoDriver();
	if (*renderer)
		SDL_DestroyRenderer(*renderer);
	if (*window)
		SDL_DestroyWindow(*window);
	*window = NULL;
	*renderer = NULL;
	fprintf(stderr, "SDL video (%s): %s\n", driver ? driver : "?", err);
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

/*
 * Cap event-driven redraws at ~60 Hz. With a working vsync this returns
 * immediately; it exists for drivers that cannot vsync.
 */
static void frame_limit(Uint64 *last)
{
	const Uint64 target_ms = 16;
	Uint64 now = SDL_GetTicks();
	Uint64 elapsed = now - *last;

	if (elapsed < target_ms)
		SDL_Delay((Uint32)(target_ms - elapsed));
	*last = SDL_GetTicks();
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
	Uint64 last_frame = 0;

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
	/* Popup lists are long; the default translucent background is hard to read. */
	style->Colors[ImGuiCol_PopupBg] = V4(0.105f, 0.113f, 0.13f, 1.0f);

	naga_impl_sdl3_init_for_sdlrenderer(window, renderer);
	naga_impl_sdlrenderer3_init(renderer);

	ui_keys_rebuild();
	ui_state_init(&state);
	ui_state_connect(&state);

	while (running) {
		SDL_Event event;
		bool had_event =
		    SDL_WaitEventTimeout(&event, screenshot ? 0 : 500);

		if (had_event) {
			do {
				if (event.type == SDL_EVENT_KEYMAP_CHANGED) {
					ui_keys_rebuild();
				} else if (state.recording_key) {
					unsigned usage = 0;
					unsigned mod = 0;
					enum ui_key_capture captured =
					    ui_keys_capture(&event, &usage, &mod);

					if (captured == UI_KEY_CAPTURE_KEY) {
						ui_state_set_key(&state, usage,
								 mod);
						ui_state_set_status(
						    &state, false, "recorded %s",
						    state.key);
						continue; /* not into the UI too */
					}
					if (captured == UI_KEY_CAPTURE_CANCEL) {
						ui_state_cancel_key_record(&state);
						continue;
					}
					if (captured == UI_KEY_CAPTURE_MOUSE) {
						ui_state_set_mouse(&state,
								   (int)usage);
						ui_state_set_status(
						    &state, false,
						    "recorded mouse %s",
						    naga_mouse_name((int)usage));
						continue;
					}
				}

				naga_impl_sdl3_processevent(&event);
				if (event.type == SDL_EVENT_QUIT)
					running = false;
			} while (SDL_PollEvent(&event));
		}
		if (!running)
			break;

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
		frame_limit(&last_frame);
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
