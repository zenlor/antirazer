/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/run.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include "remap/plan.h"

#define MAX_EVENT_NODES 64
#define MAX_QUEUE 16

struct plan_entry {
	int code;
	struct remap_plan plan;
};

struct runner {
	struct libevdev *dev;
	struct libevdev_uinput *uidev;
	const struct plan_entry *entries;
	size_t entry_count;

	const struct remap_plan *queue[MAX_QUEUE];
	size_t qhead, qcount;

	const struct remap_plan *active;
	double active_start;
	size_t active_index;

	bool injected[KEY_CNT];
	bool stopped;
};

static volatile sig_atomic_t g_stop;

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int model_product(enum remap_device model)
{
	switch (model) {
	case REMAP_DEVICE_2020:
		return 0x008d;
	case REMAP_DEVICE_2014:
		return 0x0040;
	default:
		return -1;
	}
}

static struct libevdev *open_node(const char *path)
{
	struct libevdev *dev = NULL;
	int fd = open(path, O_RDONLY | O_NONBLOCK);

	if (fd < 0)
		return NULL;
	if (libevdev_new_from_fd(fd, &dev) < 0) {
		close(fd);
		return NULL;
	}
	return dev;
}

static void node_path(int index, char *buf, size_t len)
{
	snprintf(buf, len, "/dev/input/event%d", index);
}

static int key_name_to_code(const char *name)
{
	char buf[64];
	int code;

	if (!name || !*name)
		return -1;
	code = libevdev_event_code_from_name(EV_KEY, name);
	if (code >= 0)
		return code;

	if (strncasecmp(name, "KEY_", 4) == 0)
		snprintf(buf, sizeof buf, "%s", name);
	else
		snprintf(buf, sizeof buf, "KEY_%s", name);
	for (char *p = buf; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return libevdev_event_code_from_name(EV_KEY, buf);
}

enum remap_device remap_guess_model(const char *evdev, char *err,
				    size_t errlen)
{
	struct libevdev *dev;
	bool seen_2020 = false, seen_2014 = false;

	if (evdev) {
		int product;

		dev = open_node(evdev);
		if (!dev) {
			seterr(err, errlen, "%s: %s", evdev, strerror(errno));
			return REMAP_DEVICE_AUTO;
		}
		if (libevdev_get_id_vendor(dev) != 0x1532) {
			seterr(err, errlen, "%s is not a Razer device", evdev);
			libevdev_free(dev);
			return REMAP_DEVICE_AUTO;
		}
		product = libevdev_get_id_product(dev);
		libevdev_free(dev);
		if (product == 0x008d)
			return REMAP_DEVICE_2020;
		if (product == 0x0040)
			return REMAP_DEVICE_2014;
		seterr(err, errlen, "%s is not a known Naga", evdev);
		return REMAP_DEVICE_AUTO;
	}

	for (int i = 0; i < MAX_EVENT_NODES; i++) {
		char path[64];

		node_path(i, path, sizeof path);
		dev = open_node(path);
		if (!dev)
			continue;
		if (libevdev_get_id_vendor(dev) == 0x1532) {
			int product = libevdev_get_id_product(dev);

			if (product == 0x008d)
				seen_2020 = true;
			else if (product == 0x0040)
				seen_2014 = true;
		}
		libevdev_free(dev);
	}

	if (seen_2020 && seen_2014) {
		seterr(err, errlen,
		       "both a 2020 LH and a 2014 are connected; pass --model");
		return REMAP_DEVICE_AUTO;
	}
	if (seen_2020)
		return REMAP_DEVICE_2020;
	if (seen_2014)
		return REMAP_DEVICE_2014;
	seterr(err, errlen, "no Naga found on /dev/input");
	return REMAP_DEVICE_AUTO;
}

void remap_list_devices(void)
{
	for (int i = 0; i < MAX_EVENT_NODES; i++) {
		char path[64];
		struct libevdev *dev;
		const char *name;
		int product;

		node_path(i, path, sizeof path);
		dev = open_node(path);
		if (!dev)
			continue;
		if (libevdev_get_id_vendor(dev) != 0x1532) {
			libevdev_free(dev);
			continue;
		}
		product = libevdev_get_id_product(dev);
		if (product != 0x008d && product != 0x0040) {
			libevdev_free(dev);
			continue;
		}
		name = libevdev_get_name(dev);
		printf("%s  %04x:%04x  %s\n", path, libevdev_get_id_vendor(dev),
		       product, name ? name : "?");
		libevdev_free(dev);
	}
}

/*
 * Pick the event node that carries the trigger keys. Prefer a node whose name
 * contains "Keyboard"; that is the HID interface where the side buttons (and
 * the 2020's trigger keys) appear.
 */
static struct libevdev *find_run_node(enum remap_device model,
				      const char *evdev, const int *codes,
				      size_t count, char *path_out,
				      size_t path_len, char *err, size_t errlen)
{
	struct libevdev *best = NULL;
	int best_score = -1;
	char best_path[64] = "";
	int product = model_product(model);

	if (evdev) {
		struct libevdev *dev = open_node(evdev);

		if (!dev) {
			seterr(err, errlen, "%s: %s", evdev, strerror(errno));
			return NULL;
		}
		if (libevdev_get_id_vendor(dev) != 0x1532 ||
		    libevdev_get_id_product(dev) != product) {
			seterr(err, errlen,
			       "%s is not the expected Naga (wanted %04x:%04x)",
			       evdev, 0x1532, product);
			libevdev_free(dev);
			return NULL;
		}
		snprintf(path_out, path_len, "%s", evdev);
		return dev;
	}

	for (int i = 0; i < MAX_EVENT_NODES; i++) {
		char path[64];
		struct libevdev *dev;
		const char *name;
		int score = 0;

		node_path(i, path, sizeof path);
		dev = open_node(path);
		if (!dev)
			continue;
		if (libevdev_get_id_vendor(dev) != 0x1532 ||
		    libevdev_get_id_product(dev) != product ||
		    !libevdev_has_event_type(dev, EV_KEY)) {
			libevdev_free(dev);
			continue;
		}
		for (size_t j = 0; j < count; j++)
			if (libevdev_has_event_code(dev, EV_KEY, codes[j]))
				score++;
		name = libevdev_get_name(dev);
		if (name && strstr(name, "Keyboard"))
			score += 100;

		if (score > best_score) {
			if (best)
				libevdev_free(best);
			best = dev;
			best_score = score;
			snprintf(best_path, sizeof best_path, "%s", path);
		} else {
			libevdev_free(dev);
		}
	}

	if (!best || best_score <= 0) {
		seterr(err, errlen,
		       "no %04x:%04x event node exposes the trigger keys; "
		       "use --device",
		       0x1532, product);
		if (best)
			libevdev_free(best);
		return NULL;
	}
	snprintf(path_out, path_len, "%s", best_path);
	return best;
}

static int build_entries(const struct remap_map *map,
			 enum remap_device model, struct arena *arena,
			 struct plan_entry **out, size_t *out_count, char *err,
			 size_t errlen)
{
	struct plan_entry *entries;
	size_t count = 0;

	entries = arena_alloc(arena,
			      (map->count ? map->count : 1) * sizeof *entries,
			      _Alignof(struct plan_entry));
	if (!entries) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	memset(entries, 0,
	       (map->count ? map->count : 1) * sizeof *entries);

	for (size_t i = 0; i < map->count; i++) {
		const struct remap_binding *b = &map->bindings[i];
		int code;

		if (model == REMAP_DEVICE_2020 && !b->is_sequence)
			continue;

		if (model == REMAP_DEVICE_2020)
			code = remap_usage_to_keycode(b->trigger_usage);
		else
			code = key_name_to_code(b->trigger);
		if (code < 0) {
			seterr(err, errlen, "%s: trigger \"%s\" has no Linux "
					    "key code",
			       b->id, b->trigger);
			goto fail;
		}
		for (size_t j = 0; j < count; j++) {
			if (entries[j].code == code) {
				seterr(err, errlen, "duplicate trigger code %d",
				       code);
				goto fail;
			}
		}
		if (remap_plan_build(b, &entries[count].plan, err, errlen) < 0)
			goto fail;
		entries[count].code = code;
		count++;
	}

	*out = entries;
	*out_count = count;
	return 0;
fail:
	for (size_t j = 0; j < count; j++)
		remap_plan_free(&entries[j].plan);
	return -1;
}

static void copy_capabilities(struct libevdev *src, struct libevdev *dst)
{
	static const struct {
		unsigned int type;
		unsigned int max;
	} types[] = {
		{EV_KEY, KEY_CNT}, {EV_REL, REL_CNT}, {EV_ABS, ABS_CNT},
		{EV_MSC, MSC_CNT}, {EV_LED, LED_CNT}, {EV_SND, SND_CNT},
		{EV_SW, SW_CNT},   {EV_FF, FF_CNT},
	};

	for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
		if (!libevdev_has_event_type(src, types[i].type))
			continue;
		libevdev_enable_event_type(dst, types[i].type);
		for (unsigned int code = 0; code < types[i].max; code++)
			if (libevdev_has_event_code(src, types[i].type, code))
				libevdev_enable_event_code(dst, types[i].type,
							   code, NULL);
	}
}

static int make_uinput(struct libevdev *src, const struct plan_entry *entries,
		       size_t count, struct libevdev_uinput **out, char *err,
		       size_t errlen)
{
	static const int buttons[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE,
				      BTN_EXTRA};
	struct libevdev *copy = libevdev_new();
	char name[160];
	const char *src_name = libevdev_get_name(src);
	int rc;

	if (!copy) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	snprintf(name, sizeof name, "naga-remap (%s)",
		 src_name ? src_name : "Naga");
	libevdev_set_name(copy, name);
	libevdev_set_id_bustype(copy, libevdev_get_id_bustype(src));
	libevdev_set_id_vendor(copy, libevdev_get_id_vendor(src));
	libevdev_set_id_product(copy, libevdev_get_id_product(src));
	libevdev_set_id_version(copy, libevdev_get_id_version(src));

	copy_capabilities(src, copy);
	if (!libevdev_has_event_type(copy, EV_KEY))
		libevdev_enable_event_type(copy, EV_KEY);
	for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
		libevdev_enable_event_code(copy, EV_KEY, buttons[i], NULL);
	for (size_t i = 0; i < count; i++)
		for (size_t j = 0; j < entries[i].plan.count; j++)
			if (entries[i].plan.events[j].type == EV_KEY)
				libevdev_enable_event_code(
				    copy, EV_KEY, entries[i].plan.events[j].code,
				    NULL);

	rc = libevdev_uinput_create_from_device(
	    copy, LIBEVDEV_UINPUT_OPEN_MANAGED, out);
	libevdev_free(copy);
	if (rc < 0) {
		seterr(err, errlen, "cannot create uinput device: %s",
		       strerror(-rc));
		return -1;
	}
	return 0;
}

static int write_injected(struct runner *r, unsigned int type,
			  unsigned int code, int value)
{
	int rc = libevdev_uinput_write_event(r->uidev, type, code, value);

	if (rc < 0) {
		fprintf(stderr, "naga-remap: uinput write: %s\n",
			strerror(-rc));
		r->stopped = true;
		return -1;
	}
	if (type == EV_KEY && code < KEY_CNT)
		r->injected[code] = value != 0;
	return 0;
}

static void emit_syn(struct runner *r)
{
	(void)write_injected(r, EV_SYN, SYN_REPORT, 0);
}

static const struct remap_plan *trigger_plan(struct runner *r, int code)
{
	for (size_t i = 0; i < r->entry_count; i++)
		if (r->entries[i].code == code)
			return &r->entries[i].plan;
	return NULL;
}

static void enqueue(struct runner *r, const struct remap_plan *plan)
{
	if (r->qcount == MAX_QUEUE) {
		fprintf(stderr, "naga-remap: sequence queue full, dropping\n");
		return;
	}
	r->queue[(r->qhead + r->qcount) % MAX_QUEUE] = plan;
	r->qcount++;
}

static void start_next(struct runner *r)
{
	if (r->active || !r->qcount)
		return;
	r->active = r->queue[r->qhead];
	r->qhead = (r->qhead + 1) % MAX_QUEUE;
	r->qcount--;
	r->active_start = now_ms();
	r->active_index = 0;
}

static void run_due(struct runner *r, double now)
{
	for (;;) {
		bool wrote = false;

		start_next(r);
		if (!r->active)
			break;
		while (r->active_index < r->active->count &&
		       r->active_start +
			       r->active->events[r->active_index].at_ms <=
			   now) {
			const struct remap_event *ev =
			    &r->active->events[r->active_index++];

			if (write_injected(r, ev->type, ev->code, ev->value) < 0)
				return;
			wrote = true;
		}
		if (wrote)
			emit_syn(r);
		if (r->active_index >= r->active->count &&
		    now - r->active_start >= r->active->duration_ms) {
			r->active = NULL;
			continue;
		}
		break;
	}
}

static double next_deadline(const struct runner *r, double now)
{
	double next = now + 200.0;

	if (r->active) {
		if (r->active_index < r->active->count) {
			double at = r->active_start +
				    r->active->events[r->active_index].at_ms;

			if (at < next)
				next = at;
		} else {
			double end = r->active_start + r->active->duration_ms;

			if (end < next)
				next = end;
		}
	} else if (r->qcount) {
		next = now;
	}
	return next;
}

static void cleanup(struct runner *r)
{
	if (r->uidev) {
		bool wrote = false;

		for (unsigned int code = 0; code < KEY_CNT; code++) {
			if (!r->injected[code])
				continue;
			(void)libevdev_uinput_write_event(r->uidev, EV_KEY, code,
							  0);
			r->injected[code] = false;
			wrote = true;
		}
		if (wrote)
			(void)libevdev_uinput_write_event(r->uidev, EV_SYN,
							  SYN_REPORT, 0);
		libevdev_uinput_destroy(r->uidev);
		r->uidev = NULL;
	}
	if (r->dev) {
		libevdev_grab(r->dev, LIBEVDEV_UNGRAB);
		libevdev_free(r->dev);
		r->dev = NULL;
	}
}

static int run_loop(struct runner *r, char *err, size_t errlen)
{
	while (!g_stop && !r->stopped) {
		double now = now_ms();
		double deadline;
		int timeout;
		struct pollfd pfd;

		run_due(r, now);
		deadline = next_deadline(r, now);
		timeout = (int)(deadline - now_ms());
		if (timeout < 0)
			timeout = 0;
		if (timeout > 200)
			timeout = 200;

		pfd.fd = libevdev_get_fd(r->dev);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, timeout) < 0) {
			if (errno == EINTR)
				continue;
			seterr(err, errlen, "poll: %s", strerror(errno));
			return -1;
		}
		if (!(pfd.revents & POLLIN))
			continue;

		for (;;) {
			struct input_event ev;
			int rc = libevdev_next_event(
			    r->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

			if (rc == -EAGAIN)
				break;
			if (rc == -EINTR)
				continue;
			if (rc < 0) {
				seterr(err, errlen, "read: %s", strerror(-rc));
				return -1;
			}
			if (ev.type == EV_SYN && ev.code == SYN_DROPPED)
				continue;

			if (ev.type == EV_KEY) {
				const struct remap_plan *plan =
				    trigger_plan(r, ev.code);

				if (plan) {
					if (ev.value == 1)
						enqueue(r, plan);
					continue; /* swallow trigger down/up */
				}
			}
			if (libevdev_uinput_write_event(r->uidev, ev.type,
							ev.code, ev.value) < 0) {
				seterr(err, errlen, "uinput write failed");
				return -1;
			}
		}
	}
	return 0;
}

int remap_run(const struct remap_map *map, enum remap_device model,
	      const char *evdev, char *err, size_t errlen)
{
	struct plan_entry *entries = NULL;
	size_t entry_count = 0;
	struct runner r;
	struct arena arena;
	char path[64] = "";
	int codes[256];
	int rc;

	if (model == REMAP_DEVICE_AUTO) {
		seterr(err, errlen, "device model is not resolved");
		return -1;
	}
	arena_init(&arena, 4096);
	if (build_entries(map, model, &arena, &entries, &entry_count, err,
			  errlen) < 0) {
		arena_free(&arena);
		return -1;
	}
	if (entry_count == 0) {
		arena_free(&arena);
		return 0;
	}
	if (entry_count > sizeof codes / sizeof codes[0]) {
		seterr(err, errlen, "too many triggers");
		goto fail_entries;
	}
	for (size_t i = 0; i < entry_count; i++)
		codes[i] = entries[i].code;

	memset(&r, 0, sizeof r);
	r.entries = entries;
	r.entry_count = entry_count;
	r.dev = find_run_node(model, evdev, codes, entry_count, path,
			      sizeof path, err, errlen);
	if (!r.dev)
		goto fail_entries;
	if (libevdev_grab(r.dev, LIBEVDEV_GRAB) < 0) {
		seterr(err, errlen, "cannot grab %s: %s", path,
		       strerror(errno));
		goto fail_dev;
	}
	if (make_uinput(r.dev, entries, entry_count, &r.uidev, err,
			errlen) < 0)
		goto fail_dev;

	printf("naga-remap: %s on %s, %zu trigger(s)\n",
	       libevdev_get_name(r.dev), path, entry_count);
	printf("  Ctrl-C to stop\n");
	fflush(stdout);

	g_stop = 0;
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	rc = run_loop(&r, err, errlen);
	cleanup(&r);
	for (size_t i = 0; i < entry_count; i++)
		remap_plan_free(&entries[i].plan);
	arena_free(&arena);
	return rc;

fail_dev:
	libevdev_grab(r.dev, LIBEVDEV_UNGRAB);
	libevdev_free(r.dev);
fail_entries:
	for (size_t i = 0; i < entry_count; i++)
		remap_plan_free(&entries[i].plan);
	arena_free(&arena);
	return -1;
}
