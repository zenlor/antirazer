/*
 * hp-stage — read (no argument) or set (0..3) the DPI stage of the HP
 * Pavilion Gaming Mouse 300 (03f0:2941).
 *
 * The vendor input report 04 (interface 1) carries the live DPI state:
 * byte 3 is the stage index, bytes 20..35 the DPI table (u16 LE). This tool
 * sends the output-report-04 mirror with only byte 3 changed — the frame is
 * otherwise byte-identical to what the device itself reports. It never
 * touches the 512-byte feature reports.
 *
 * usage: hp-stage            read-only: dump the current frame
 *        hp-stage <0..3>     set the stage, then echo input for 3 s
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/hidraw.h>

static void dump(const char *tag, const unsigned char *b)
{
	printf("%s:", tag);
	for (int i = 0; i < 64; i++)
		printf(" %02x", b[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	unsigned char b[64] = {0x04};
	int fd, rc;

	/* captured frame, used if the control-pipe read is not answered */
	const unsigned char tmpl[64] = {
		0x04, 0x01, 0x01, 0x00,
		[17] = 0x01, 0x02, 0xc3,
		0x40, 0x06, 0xd0, 0x07, 0x60, 0x09,
		0xf0, 0x0a, 0x80, 0x0c, 0xac, 0x0d,
		0xa0, 0x0f, 0x88, 0x13,
	};

	fd = open("/dev/hidraw5", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("/dev/hidraw5");
		return 1;
	}

	memcpy(b, tmpl, sizeof b);
	rc = ioctl(fd, HIDIOCGINPUT(sizeof b), b);
	if (rc < 0)
		perror("HIDIOCGINPUT (using captured template)");
	if (b[0] != 0x04 || b[1] != 0x01 || b[2] != 0x01) {
		fprintf(stderr, "unexpected frame layout, aborting\n");
		dump("got", b);
		return 1;
	}
	dump("current", b);
	printf("stage %d = %u DPI\n", b[3],
	       (unsigned)(b[20 + 2 * b[3]] | b[21 + 2 * b[3]] << 8));

	if (argc < 2)
		return 0;

	int stage = atoi(argv[1]);
	if (stage < 0 || stage > 3) {
		fprintf(stderr, "stage must be 0..3\n");
		return 1;
	}

	b[3] = (unsigned char)stage;
	rc = (int)write(fd, b, sizeof b);
	printf("wrote %d bytes: stage -> %d\n", rc, stage);

	/* echo watch: the device should re-emit report 04 with the new stage */
	for (int i = 0; i < 15; i++) {
		struct pollfd pf = {fd, POLLIN, 0};
		unsigned char ev[64];
		if (poll(&pf, 1, 200) <= 0 || !(pf.revents & POLLIN))
			continue;
		ssize_t len = read(fd, ev, sizeof ev);
		if (len > 0 && ev[0] == 0x04) {
			dump("echo", ev);
			printf("stage byte = %u\n", ev[3]);
		}
	}
	return 0;
}
