/*
 * hp-watch — live diff of the HP Pavilion Gaming Mouse 300 (03f0:2941)
 * vendor feature records, plus a tap on the input endpoints.
 *
 * Read-only: feature GET_REPORTs on the interface-1 vendor channel and
 * passive read()s. Never writes. Click buttons on the mouse and watch
 * which bytes move. Ctrl-C to stop.
 *
 * usage: hp-watch [hidraw-vendor [hidraw-mouse]]
 *        default: hp-watch /dev/hidraw5 /dev/hidraw4
 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/hidraw.h>

#define NREC 8
static const unsigned char rids[NREC] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

static unsigned char snap[NREC][64];
static int fdv;
static volatile sig_atomic_t stop;

static void onint(int sig) { (void)sig; stop = 1; }

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int readrec(unsigned char rid, unsigned char out[64])
{
	unsigned char buf[1 + 512] = {rid};
	int rc = ioctl(fdv, HIDIOCGFEATURE(sizeof buf), buf);
	if (rc < 0)
		return -1;
	memcpy(out, buf + 1, 64);
	return 0;
}

static void hexrec(const char *tag, unsigned char rid, const unsigned char *b)
{
	printf("[%8.2fs] %s r%02x:", now(), tag, rid);
	for (int i = 0; i < 64; i++)
		printf(" %02x", b[i]);
	printf("\n");
}

static void drain(int fd, const char *name)
{
	unsigned char ev[64];
	ssize_t len;

	while ((len = read(fd, ev, sizeof ev)) > 0) {
		printf("[%8.2fs] input %s:", now(), name);
		for (ssize_t k = 0; k < len; k++)
			printf(" %02x", ev[k]);
		printf("\n");
	}
}

int main(int argc, char **argv)
{
	const char *vpath = argc > 1 ? argv[1] : "/dev/hidraw5";
	const char *mpath = argc > 2 ? argv[2] : "/dev/hidraw4";
	unsigned char cur[64];
	struct pollfd pf[2];

	fdv = open(vpath, O_RDWR | O_NONBLOCK);
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (fdv < 0) {
		perror(vpath);
		return 1;
	}
	pf[0].fd = fdv;
	pf[1].fd = open(mpath, O_RDONLY | O_NONBLOCK);
	pf[1].events = POLLIN;
	if (pf[1].fd < 0)
		perror(mpath);
	pf[0].events = POLLIN;
	signal(SIGINT, onint);

	for (int r = 0; r < NREC; r++)
		if (readrec(rids[r], snap[r]) == 0)
			hexrec("init", rids[r], snap[r]);
		else
			printf("r%02x: read failed\n", rids[r]);
	printf("watching — click buttons on the mouse; Ctrl-C to stop\n");

	while (!stop) {
		poll(pf, 2, 200);
		if (pf[0].revents & POLLIN)
			drain(pf[0].fd, vpath);
		if (pf[1].revents & POLLIN)
			drain(pf[1].fd, mpath);
		for (int r = 0; r < NREC; r++) {
			if (readrec(rids[r], cur) != 0 ||
			    !memcmp(cur, snap[r], 64))
				continue;
			hexrec("diff", rids[r], cur);
			for (int k = 0; k < 64; k++)
				if (cur[k] != snap[r][k])
					printf("         byte %02d: %02x -> %02x\n",
					       k, snap[r][k], cur[k]);
			memcpy(snap[r], cur, 64);
		}
	}
	puts("bye");
	return 0;
}
