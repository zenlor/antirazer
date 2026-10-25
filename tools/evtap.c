/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * evtap — dump EV_KEY / EV_REL / EV_MSC events from one or more evdev nodes.
 *
 * Useful to see what the firmware emits natively for the thumb buttons, i.e.
 * which HID usages the stored mapping replays.
 *
 *   sudo ./evtap /dev/input/event10 /dev/input/event22 [seconds]
 *
 * Build with `make tools`.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

#define MAX_NODES 8

int main(int argc, char **argv)
{
	struct pollfd pfd[MAX_NODES];
	const char *names[MAX_NODES];
	int n = 0, seconds = 25;
	time_t end;

	if (argc < 2) {
		fprintf(stderr,
			"usage: %s <event-node>... [seconds]\n"
			"  e.g. sudo %s /dev/input/event10 /dev/input/event22 30\n",
			argv[0], argv[0]);
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		int fd;

		if (i == argc - 1 && atoi(argv[i]) > 0) {
			seconds = atoi(argv[i]);
			break;
		}
		if (n == MAX_NODES) {
			fprintf(stderr, "too many nodes (max %d)\n", MAX_NODES);
			break;
		}
		fd = open(argv[i], O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			perror(argv[i]);
			continue;
		}
		pfd[n].fd = fd;
		pfd[n].events = POLLIN;
		names[n] = argv[i];
		n++;
	}
	if (!n) {
		fprintf(stderr, "no usable event nodes\n");
		return 1;
	}

	printf("tapping %d node(s) for %d s\n", n, seconds);
	fflush(stdout);

	end = time(NULL) + seconds;
	while (time(NULL) < end) {
		if (poll(pfd, (nfds_t)n, 500) <= 0)
			continue;
		for (int i = 0; i < n; i++) {
			struct input_event ev;

			if (!(pfd[i].revents & POLLIN))
				continue;
			while (read(pfd[i].fd, &ev, sizeof ev) == sizeof ev) {
				if (ev.type != EV_KEY && ev.type != EV_REL &&
				    ev.type != EV_MSC)
					continue;
				printf("%s type %d code 0x%03x value %d\n", names[i],
				       ev.type, ev.code, ev.value);
				fflush(stdout);
			}
		}
	}
	return 0;
}
