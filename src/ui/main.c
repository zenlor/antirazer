/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga-ui — clickable configuration UI for the Naga LH 2020.
 *
 * GLFW window + OpenGL3, ImGui via cimgui. The UI itself lives in
 * ui_layout.c; this file only owns the window, callbacks, frame loop and
 * shutdown.
 *
 *   naga-ui                     run it
 *   naga-ui --screenshot FILE   render a few frames, save a BMP and exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <GLFW/glfw3.h>
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

/* C entry points in cbridge.cpp (the C++ ImGui backends). */
extern bool naga_impl_glfw_init_for_opengl3(GLFWwindow *);
extern void naga_impl_glfw_key_callback(GLFWwindow *, int, int, int, int);
extern void naga_impl_glfw_mouse_button_callback(GLFWwindow *, int, int, int);
extern void naga_impl_glfw_newframe(void);
extern void naga_impl_glfw_shutdown(void);
extern bool naga_impl_opengl3_init(const char *);
extern void naga_impl_opengl3_newframe(void);
extern void naga_impl_opengl3_render(ImDrawData *);
extern void naga_impl_opengl3_shutdown(void);

#define WINDOW_TITLE "naga-ui - Razer Naga LH 2020"
#define WINDOW_W 1120
#define WINDOW_H 640

/* Minimal GL entry points; loaded with glfwGetProcAddress, no GL headers. */
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

typedef void (*gl_viewport_fn)(int, int, int, int);
typedef void (*gl_clear_color_fn)(float, float, float, float);
typedef void (*gl_clear_fn)(unsigned);
typedef void (*gl_read_pixels_fn)(int, int, int, int, unsigned, unsigned,
				  void *);
typedef void (*gl_finish_fn)(void);

static gl_viewport_fn gl_viewport;
static gl_clear_color_fn gl_clear_color;
static gl_clear_fn gl_clear;
static gl_read_pixels_fn gl_read_pixels;
static gl_finish_fn gl_finish;

static ui_state *g_state;

static void error_callback(int error, const char *description)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static bool load_gl(void)
{
	gl_viewport = (gl_viewport_fn)glfwGetProcAddress("glViewport");
	gl_clear_color =
	    (gl_clear_color_fn)glfwGetProcAddress("glClearColor");
	gl_clear = (gl_clear_fn)glfwGetProcAddress("glClear");
	gl_read_pixels =
	    (gl_read_pixels_fn)glfwGetProcAddress("glReadPixels");
	gl_finish = (gl_finish_fn)glfwGetProcAddress("glFinish");

	if (!gl_viewport || !gl_clear_color || !gl_clear || !gl_read_pixels ||
	    !gl_finish) {
		fprintf(stderr, "GLFW: missing OpenGL entry points\n");
		return false;
	}
	return true;
}

static bool create_window(GLFWwindow **out)
{
	const char *desc;
	int code;

	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) {
		code = glfwGetError(&desc);
		fprintf(stderr, "glfwInit (%d): %s\n", code, desc ? desc : "?");
		return false;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, "org.zenlor.antirazer");
	*out = glfwCreateWindow(WINDOW_W, WINDOW_H, WINDOW_TITLE, NULL, NULL);
	if (!*out) {
		code = glfwGetError(&desc);
		fprintf(stderr, "glfwCreateWindow (%d): %s\n", code,
			desc ? desc : "?");
		glfwTerminate();
		return false;
	}
	glfwMakeContextCurrent(*out);
	glfwSwapInterval(1);
	return true;
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
			 int mods)
{
	if (g_state && g_state->recording_key) {
		unsigned usage = 0;
		unsigned mod = 0;
		enum ui_key_capture captured =
		    ui_keys_capture_key(key, action, mods, &usage, &mod);

		if (captured == UI_KEY_CAPTURE_KEY) {
			ui_state_set_key(g_state, usage, mod);
			ui_state_set_status(g_state, false, "recorded %s",
					    g_state->key);
			return;
		}
		if (captured == UI_KEY_CAPTURE_CANCEL) {
			ui_state_cancel_key_record(g_state);
			return;
		}
	}
	naga_impl_glfw_key_callback(window, key, scancode, action, mods);
}

static void mouse_button_callback(GLFWwindow *window, int button, int action,
				  int mods)
{
	naga_impl_glfw_mouse_button_callback(window, button, action, mods);
	if (g_state && g_state->recording_key && action == GLFW_PRESS) {
		unsigned usage = 0;

		if (ui_keys_capture_mouse(button, &usage) ==
		    UI_KEY_CAPTURE_MOUSE) {
			ui_state_set_mouse(g_state, (int)usage);
			ui_state_set_status(g_state, false,
					    "recorded mouse %s",
					    naga_mouse_name((int)usage));
		}
	}
}

/* BMP writer used only by --screenshot; avoids pulling in an image library. */
static void put16(FILE *f, unsigned v)
{
	fputc((int)(v & 0xFF), f);
	fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, unsigned v)
{
	put16(f, v & 0xFFFF);
	put16(f, (v >> 16) & 0xFFFF);
}

static bool save_screenshot(const char *path, int width, int height)
{
	size_t stride = (size_t)width * 3;
	size_t pad = (4 - (stride % 4)) % 4;
	size_t image_size = (stride + pad) * (size_t)height;
	size_t bytes = (size_t)width * (size_t)height * 4;
	unsigned char *rgba;
	FILE *f;

	if (width <= 0 || height <= 0 || image_size > 0xFFFFFFFFu)
		return false;
	rgba = malloc(bytes);
	if (!rgba)
		return false;
	gl_finish();
	gl_read_pixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	f = fopen(path, "wb");
	if (!f) {
		free(rgba);
		return false;
	}
	fputc('B', f);
	fputc('M', f);
	put32(f, (unsigned)(54 + image_size));
	put16(f, 0);
	put16(f, 0);
	put32(f, 54);
	put32(f, 40);
	put32(f, (unsigned)width);
	put32(f, (unsigned)height);
	put16(f, 1);
	put16(f, 24);
	put32(f, 0);
	put32(f, (unsigned)image_size);
	put32(f, 2835);
	put32(f, 2835);
	put32(f, 0);
	put32(f, 0);

	/* GL rows start at the bottom and BMP rows are bottom-up too. */
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const unsigned char *p =
			    rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;

			fputc(p[2], f);
			fputc(p[1], f);
			fputc(p[0], f);
		}
		for (size_t i = 0; i < pad; i++)
			fputc(0, f);
	}
	fclose(f);
	free(rgba);
	return true;
}

static void frame_limit(double *last)
{
	const double target = 1.0 / 60.0;
	double now = glfwGetTime();
	double wait = target - (now - *last);

	if (wait > 0.0) {
		struct timespec ts;

		ts.tv_sec = (time_t)wait;
		ts.tv_nsec = (long)((wait - (double)ts.tv_sec) * 1e9);
		nanosleep(&ts, NULL);
	}
	*last = glfwGetTime();
}

int main(int argc, char **argv)
{
	const char *screenshot = NULL;
	GLFWwindow *window = NULL;
	ui_state state;
	ImGuiStyle *style;
	bool running = true;
	int frames = 0;
	double last_frame = 0.0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
			screenshot = argv[++i];
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("usage: %s [--screenshot FILE.bmp]\n", argv[0]);
			return 0;
		} else {
			fprintf(stderr, "usage: %s [--screenshot FILE.bmp]\n",
				argv[0]);
			return 1;
		}
	}

	if (!create_window(&window))
		return 1;
	if (!load_gl()) {
		glfwDestroyWindow(window);
		glfwTerminate();
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

	if (!naga_impl_glfw_init_for_opengl3(window)) {
		fprintf(stderr, "ImGui GLFW backend initialisation failed\n");
		igDestroyContext(NULL);
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}
	if (!naga_impl_opengl3_init("#version 150")) {
		fprintf(stderr, "ImGui OpenGL3 backend initialisation failed\n");
		naga_impl_glfw_shutdown();
		igDestroyContext(NULL);
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	/* Install our callbacks after ImGui's; we chain key/mouse to it. */
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);

	ui_keys_rebuild();
	ui_state_init(&state);
	ui_state_connect(&state);
	g_state = &state;

	while (running && !glfwWindowShouldClose(window)) {
		int fbw = 0, fbh = 0;

		if (screenshot)
			glfwPollEvents();
		else
			glfwWaitEventsTimeout(0.5);
		if (glfwWindowShouldClose(window))
			break;

		naga_impl_glfw_newframe();
		naga_impl_opengl3_newframe();
		igNewFrame();
		ui_draw(&state);
		igRender();

		glfwGetFramebufferSize(window, &fbw, &fbh);
		if (fbw <= 0 || fbh <= 0)
			continue;
		gl_viewport(0, 0, fbw, fbh);
		gl_clear_color(0.08f, 0.09f, 0.10f, 1.0f);
		gl_clear(GL_COLOR_BUFFER_BIT);
		naga_impl_opengl3_render(igGetDrawData());

		if (screenshot && frames == 2 &&
		    !save_screenshot(screenshot, fbw, fbh)) {
			fprintf(stderr, "screenshot: cannot write %s\n",
				screenshot);
		}
		glfwSwapBuffers(window);

		frames++;
		if (screenshot && frames >= 3)
			running = false;
		frame_limit(&last_frame);
	}

	naga_close();
	naga_impl_opengl3_shutdown();
	naga_impl_glfw_shutdown();
	igDestroyContext(NULL);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
