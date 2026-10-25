/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * bench-remap — measure the latency naga-remap adds.
 *
 * Creates a virtual Naga 2014 source device (uinput), starts
 * `naga-remap run` against it, and measures:
 *
 *   cold  fork/exec -> daemon has grabbed the source and created its output
 *         uinput device (config/map load, plan build, grab, uinput setup)
 *   hot   writing the trigger key -> the mapped key appearing on the output
 *         uinput device, for N iterations
 *
 * The source and output are both uinput devices, so the hot number includes
 * the kernel round trip, poll wakeup, the daemon's queue/scheduler and the
 * output write. It is an upper bound for what a physical mouse sees.
 *
 * Needs /dev/uinput and an event node the daemon can read, so run it as a user
 * in the input group (or root), outside a restricted sandbox.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#define MAX_EVENT_NODES 64
#define DEFAULT_ITERATIONS 100
#define READY_TIMEOUT_MS 5000
#define EVENT_TIMEOUT_MS 1000

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"usage: %s [options]\n"
		"\n"
		"  --iterations N   hot-path samples (default %d)\n"
		"  --daemon PATH    naga-remap binary (default ./naga-remap)\n"
		"  --verbose        keep the daemon's stdout/stderr\n"
		"  -h, --help       this text\n"
		"\n"
		"The benchmark needs /dev/uinput and access to /dev/input.\n",
		prog, DEFAULT_ITERATIONS);
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(unsigned ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (long)(ms % 1000) * 1000000L,
	};

	nanosleep(&ts, NULL);
}

static int compare_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

/*
 * A Naga-shaped virtual keyboard node. The IDs are what naga-remap checks;
 * the name is unique so the benchmark can find the daemon's output device.
 */
static int create_source(const char *name, struct libevdev **dev_out,
			 struct libevdev_uinput **ui_out, char *path,
			 size_t path_len)
{
	struct libevdev *dev = libevdev_new();
	struct libevdev_uinput *ui;
	const char *node;
	int rc;

	if (!dev) {
		fprintf(stderr, "bench-remap: out of memory\n");
		return -1;
	}
	libevdev_set_name(dev, name);
	libevdev_set_id_bustype(dev, BUS_USB);
	libevdev_set_id_vendor(dev, 0x1532);
	libevdev_set_id_product(dev, 0x0040);
	libevdev_set_id_version(dev, 0x0200);
	libevdev_enable_event_type(dev, EV_KEY);
	libevdev_enable_event_code(dev, EV_KEY, KEY_KP1, NULL);

	rc = libevdev_uinput_create_from_device(
	    dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &ui);
	if (rc < 0) {
		fprintf(stderr,
			"bench-remap: cannot create uinput source: %s\n"
			"            (is /dev/uinput available and writable?)\n",
			strerror(-rc));
		libevdev_free(dev);
		return -1;
	}
	node = libevdev_uinput_get_devnode(ui);
	if (!node) {
		fprintf(stderr, "bench-remap: uinput source has no devnode\n");
		libevdev_uinput_destroy(ui);
		libevdev_free(dev);
		return -1;
	}
	snprintf(path, path_len, "%s", node);
	*dev_out = dev;
	*ui_out = ui;
	return 0;
}

static int write_event(struct libevdev_uinput *ui, unsigned type,
		       unsigned code, int value)
{
	int rc = libevdev_uinput_write_event(ui, type, code, value);

	if (rc < 0) {
		fprintf(stderr, "bench-remap: uinput write: %s\n",
			strerror(-rc));
		return -1;
	}
	return 0;
}

static int write_key(struct libevdev_uinput *ui, unsigned code, int value)
{
	if (write_event(ui, EV_KEY, code, value) < 0)
		return -1;
	return write_event(ui, EV_SYN, SYN_REPORT, 0);
}

static void make_map(char *path, size_t path_len)
{
	static const char json[] =
	    "{\n"
	    "  \"device\": \"2014\",\n"
	    "  \"bindings\": {\n"
	    "    \"normal\": { \"1\": \"x\" }\n"
	    "  }\n"
	    "}\n";
	int fd;

	snprintf(path, path_len, "/tmp/naga-bench-map-XXXXXX");
	fd = mkstemp(path);
	if (fd < 0) {
		fprintf(stderr, "bench-remap: mkstemp: %s\n", strerror(errno));
		exit(1);
	}
	if (write(fd, json, sizeof json - 1) != (ssize_t)(sizeof json - 1)) {
		fprintf(stderr, "bench-remap: cannot write map: %s\n",
			strerror(errno));
		close(fd);
		unlink(path);
		exit(1);
	}
	close(fd);
}

static struct libevdev *open_input(const char *path)
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

/*
 * udev applies the input group/ACL a moment after uinput creates the node.
 * Opening it immediately gives EACCES; wait until the benchmark's user can
 * actually read it before starting the daemon.
 */
static int wait_readable(const char *path, int timeout_ms)
{
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;

	for (;;) {
		uint64_t now;

		if (access(path, R_OK) == 0)
			return 0;
		if (errno != EACCES && errno != ENOENT) {
			fprintf(stderr, "bench-remap: access %s: %s\n", path,
				strerror(errno));
			return -1;
		}
		now = now_ns();
		if (now >= deadline) {
			fprintf(stderr,
				"bench-remap: %s is not readable by this user "
				"(%d ms); add the user to the input group or "
				"run as root\n",
				path, timeout_ms);
			return -1;
		}
		sleep_ms(5);
	}
}


/*
 * The daemon creates its output device only after grabbing the source. Find
 * it by its name (`naga-remap (<source name>)`).
 */
static struct libevdev *find_output(const char *want, char *path_out,
				    size_t path_len)
{
	for (int i = 0; i < MAX_EVENT_NODES; i++) {
		char path[64];
		struct libevdev *dev;
		const char *name;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		dev = open_input(path);
		if (!dev)
			continue;
		name = libevdev_get_name(dev);
		if (name && !strcmp(name, want)) {
			snprintf(path_out, path_len, "%s", path);
			return dev;
		}
		libevdev_free(dev);
	}
	return NULL;
}

static struct libevdev *wait_output(pid_t pid, const char *want,
				    int timeout_ms, char *path, size_t path_len)
{
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;

	for (;;) {
		struct libevdev *dev = find_output(want, path, path_len);
		int status;

		if (dev)
			return dev;
		if (waitpid(pid, &status, WNOHANG) == pid) {
			fprintf(stderr,
				"bench-remap: daemon exited before creating "
				"its output device (status %d)\n",
				status);
			return NULL;
		}
		if (now_ns() >= deadline) {
			fprintf(stderr,
				"bench-remap: timed out waiting for the "
				"daemon's output device\n");
			return NULL;
		}
		sleep_ms(5);
	}
}

/*
 * Read from the output device until code/value arrives. Returns the time of
 * the read in *when_ns. Other events and SYN are ignored.
 */
static int wait_key(struct libevdev *dev, unsigned code, int value,
		    int timeout_ms, uint64_t *when_ns)
{
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;

	for (;;) {
		struct input_event ev;
		uint64_t now = now_ns();
		int timeout = now >= deadline ? 0 :
			      (int)((deadline - now) / 1000000ULL);
		struct pollfd pfd = {
			.fd = libevdev_get_fd(dev),
			.events = POLLIN,
		};
		int rc;

		rc = poll(&pfd, 1, timeout);
		if (rc == 0) {
			fprintf(stderr, "bench-remap: timed out waiting for "
					"key %u value %d\n",
				code, value);
			return -1;
		}
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "bench-remap: poll: %s\n",
				strerror(errno));
			return -1;
		}
		for (;;) {
			rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL,
						 &ev);
			if (rc == -EAGAIN)
				break;
			if (rc == -EINTR)
				continue;
			if (rc < 0) {
				fprintf(stderr, "bench-remap: read: %s\n",
					strerror(-rc));
				return -1;
			}
			if (ev.type == EV_KEY && ev.code == code &&
			    ev.value == value) {
				if (when_ns)
					*when_ns = now_ns();
				return 0;
			}
		}
	}
}

static void drain(struct libevdev *dev)
{
	struct input_event ev;

	for (;;) {
		int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

		if (rc == -EAGAIN || rc < 0)
			return;
	}
}

static int run_bench(const char *daemon, unsigned iterations, bool verbose)
{
	struct libevdev *source_dev = NULL;
	struct libevdev_uinput *source_ui = NULL;
	struct libevdev *output = NULL;
	char source_name[64];
	char source_path[64];
	char output_path[64];
	char output_name[160];
	char map_path[64];
	uint64_t *samples = NULL;
	uint64_t cold_start, cold_end, first_ns = 0;
	pid_t pid = -1;
	int status;
	int rc = 1;

	snprintf(source_name, sizeof source_name, "naga-bench-%ld",
		 (long)getpid());
	snprintf(output_name, sizeof output_name, "naga-remap (%s)",
		 source_name);

	if (create_source(source_name, &source_dev, &source_ui, source_path,
			  sizeof source_path) < 0)
		return 1;
	printf("source: %s (%s)\n", source_path, source_name);
	if (wait_readable(source_path, READY_TIMEOUT_MS) < 0) {
		rc = 1;
		goto out;
	}
	printf("output: waiting for %s\n", output_name);

	make_map(map_path, sizeof map_path);
	cold_start = now_ns();
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "bench-remap: fork: %s\n", strerror(errno));
		goto out;
	}
	if (pid == 0) {
		char *args[] = {(char *)daemon,   "run",       map_path,
				"--device", source_path, "--model", "2014",
				NULL};

		if (!verbose) {
			int nullfd = open("/dev/null", O_WRONLY);

			if (nullfd >= 0) {
				dup2(nullfd, STDOUT_FILENO);
				dup2(nullfd, STDERR_FILENO);
				if (nullfd > STDERR_FILENO)
					close(nullfd);
			}
		}
		if (strchr(daemon, '/'))
			execv(daemon, args);
		else
			execvp(daemon, args);
		_exit(127);
	}

	output = wait_output(pid, output_name, READY_TIMEOUT_MS, output_path,
			     sizeof output_path);
	if (!output)
		goto out;
	cold_end = now_ns();
	printf("output: %s\n", output_path);
	printf("cold: %.3f ms (fork/exec -> grabbed and uinput ready)\n",
	       (double)(cold_end - cold_start) / 1e6);

	samples = calloc(iterations, sizeof *samples);
	if (!samples) {
		fprintf(stderr, "bench-remap: out of memory\n");
		goto out;
	}

	/* Let udev/libinput settle and drain any startup events. */
	for (int i = 0; i < 5; i++) {
		if (write_key(source_ui, KEY_KP1, 1) < 0 ||
		    wait_key(output, KEY_X, 1, EVENT_TIMEOUT_MS, NULL) < 0 ||
		    write_key(source_ui, KEY_KP1, 0) < 0 ||
		    wait_key(output, KEY_X, 0, EVENT_TIMEOUT_MS, NULL) < 0)
			goto out;
	}
	drain(output);

	for (unsigned i = 0; i < iterations; i++) {
		uint64_t start, end;

		drain(output);
		start = now_ns();
		if (write_key(source_ui, KEY_KP1, 1) < 0)
			goto out;
		if (wait_key(output, KEY_X, 1, EVENT_TIMEOUT_MS, &end) < 0)
			goto out;
		samples[i] = end - start;
		if (i == 0)
			first_ns = samples[i];
		if (write_key(source_ui, KEY_KP1, 0) < 0)
			goto out;
		/* Consume the release so the next sample starts clean. */
		if (wait_key(output, KEY_X, 0, EVENT_TIMEOUT_MS, NULL) < 0)
			goto out;
	}

	qsort(samples, iterations, sizeof *samples, compare_u64);
	printf("hot: %u samples, min %.3f ms, p50 %.3f ms, p95 %.3f ms, "
	       "max %.3f ms\n",
	       iterations, (double)samples[0] / 1e6,
	       (double)samples[iterations / 2] / 1e6,
	       (double)samples[(iterations * 95) / 100] / 1e6,
	       (double)samples[iterations - 1] / 1e6);
	if (first_ns)
		printf("first sample: %.3f ms\n", (double)first_ns / 1e6);
	rc = 0;

out:
	if (pid > 0) {
		kill(pid, SIGTERM);
		waitpid(pid, &status, 0);
	}
	if (output)
		libevdev_free(output);
	unlink(map_path);
	free(samples);
	if (source_ui)
		libevdev_uinput_destroy(source_ui);
	if (source_dev)
		libevdev_free(source_dev);
	return rc;
}

int main(int argc, char **argv)
{
	const char *daemon = "./naga-remap";
	unsigned iterations = DEFAULT_ITERATIONS;
	bool verbose = false;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(stdout, argv[0]);
			return 0;
		}
		if (!strcmp(argv[i], "--verbose")) {
			verbose = true;
			continue;
		}
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc) {
			char *end;
			long n = strtol(argv[++i], &end, 10);

			if (*end || n < 1 || n > 100000) {
				fprintf(stderr,
					"bench-remap: invalid iteration count\n");
				return 2;
			}
			iterations = (unsigned)n;
			continue;
		}
		if (!strcmp(argv[i], "--daemon") && i + 1 < argc) {
			daemon = argv[++i];
			continue;
		}
		fprintf(stderr, "bench-remap: unknown option %s\n", argv[i]);
		usage(stderr, argv[0]);
		return 2;
	}

	return run_bench(daemon, iterations, verbose);
}
