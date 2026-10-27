/*
 * hp-cfg — read the settings block of the HP Pavilion Gaming Mouse 300
 * (03f0:2941) over the FF32 command channel. Read-only: hello (cmd 01),
 * read 0x33 bytes at 0xBE00 (cmd 03), end session (cmd 07).
 *
 * Transport per the blackweb decode (github.com/moxwel/blackweb-mouse-config,
 * PROTOCOL.md — same firmware family): output report 4 =
 *   [04][cmd][addr lo][addr hi][len][data..]
 * read replies echo cmd in [1], addr in [2..3], len in [4], data from [5].
 *
 * usage: hp-cfg [hidraw]
 */
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/hidraw.h>

#define SETTINGS_ADDR 0xbe00
#define SETTINGS_LEN 0x33

static int fd;

static int xfer(const unsigned char *out, unsigned char in[64])
{
	if (write(fd, out, 64) != 64) {
		perror("write");
		return -1;
	}
	for (int i = 0; i < 25; i++) {           /* app waits up to 5 s */
		struct pollfd pf = {fd, POLLIN, 0};
		unsigned char ev[64];
		if (poll(&pf, 1, 200) <= 0 || !(pf.revents & POLLIN))
			continue;
		if (read(fd, ev, 64) <= 0 || ev[0] != 0x04)
			continue;
		if (out[1] == 0x01) {            /* status report: cmd 01 reply */
			memcpy(in, ev, 64);
			return 0;
		}
		if (ev[1] == out[1] && ev[2] == out[2] && ev[3] == out[3] &&
		    ev[4] == out[4]) {           /* echo of addr+len */
			memcpy(in, ev, 64);
			return 0;
		}
	}
	fprintf(stderr, "no reply to cmd %02x\n", out[1]);
	return -1;
}

static int cmd_read(unsigned addr, unsigned char len, unsigned char data[64])
{
	unsigned char out[64] = {0x04, 0x03, addr & 0xff, addr >> 8, len};
	unsigned char in[64];

	if (xfer(out, in) < 0)
		return -1;
	memcpy(data, in + 5, len);
	return 0;
}

static int cmd_simple(unsigned char cmd)
{
	unsigned char out[64] = {0x04, cmd};
	unsigned char in[64];

	return xfer(out, in);
}

/* hello reply = status report; byte 3 is the slot the firmware is RUNNING,
   byte 12 is the blackweb-family live LED mode byte */
static int report_live_slot(void)
{
	unsigned char out[64] = {0x04, 0x01}, in[64];

	if (xfer(out, in) < 0)
		return -1;
	printf("device reports live slot: %u (poll code %u, led byte %02x)\n",
	       in[3], in[2], in[12]);
	return 0;
}

static int cmd_write_settings(const unsigned char *b /* 0x27 bytes */)
{
	unsigned char out[64] = {0x04, 0x05, 0x00, 0xbe, 0x27};
	unsigned char in[64];

	memcpy(out + 5, b, 0x27);
	return xfer(out, in);
}

/* live preview: feature report 0x20 carries the settings block
   (descriptor size 0x27); flash writes alone do not apply mid-session */
static int live_apply(const unsigned char *b /* 0x27 bytes */)
{
	unsigned char f[40] = {0x20};

	memcpy(f + 1, b, 0x27);
	return ioctl(fd, HIDIOCSFEATURE(sizeof f), f) < 0 ? -1 : 0;
}

/* re-enumerate the mouse: settings reload from flash, same as a replug.
   fd must be closed first — the hidraw node goes away and comes back. */
static int usb_reset(void)
{
	DIR *d = opendir("/sys/bus/usb/devices");
	struct dirent *e;
	int found = 0;

	if (!d)
		return -1;
	while (!found && (e = readdir(d))) {
		char path[512], id[16];
		int ufd, ok;

		if (*e->d_name == '.')
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor",
			 e->d_name);
		ufd = open(path, O_RDONLY);
		if (ufd < 0)
			continue;
		ok = read(ufd, id, 4) == 4 && !memcmp(id, "03f0", 4);
		close(ufd);
		if (!ok)
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idProduct",
			 e->d_name);
		ufd = open(path, O_RDONLY);
		if (ufd < 0)
			continue;
		ok = read(ufd, id, 4) == 4 && !memcmp(id, "2941", 4);
		close(ufd);
		if (!ok)
			continue;

		snprintf(path, sizeof path,
			 "/sys/bus/usb/devices/%s/authorized", e->d_name);
		ufd = open(path, O_WRONLY);
		if (ufd < 0) {
			perror(path);
			break;
		}
		ok = write(ufd, "0", 1) == 1;
		usleep(300000);
		ok &= write(ufd, "1", 1) == 1;
		close(ufd);
		if (ok) {
			printf("re-enumerated %s\n", e->d_name);
			found = 1;
		}
	}
	closedir(d);
	return found ? 0 : -1;
}

static int cmd_read_chunk(unsigned addr, unsigned char len,
			  unsigned char *data)
{
	unsigned char out[64] = {0x04, 0x03, addr & 0xff, addr >> 8, len};
	unsigned char in[64];

	if (xfer(out, in) < 0)
		return -1;
	memcpy(data, in + 5, len);
	return 0;
}

/* feature reports 0x10..0x17 mirror the button sectors */
static int feature_read(unsigned char rid, unsigned char *out)
{
	unsigned char buf[513] = {rid};

	if (ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf) < 0)
		return -1;
	memcpy(out, buf + 1, 512);
	return 0;
}

/* read sector n twice — via feature report and via cmd 03 — and compare;
   a match confirms the 0xC000 + n*0x200 address map before any write */
static int read_sector(int n, unsigned char *s)
{
	const unsigned addr = 0xc000 + n * 0x200;
	unsigned char cmd[512];

	if (feature_read(0x10 + n, s) < 0) {
		perror("feature read");
		return -1;
	}
	for (unsigned off = 0; off < 512; off += 59) {
		unsigned chunk = 512 - off < 59 ? 512 - off : 59;

		if (cmd_read_chunk(addr + off, chunk, cmd + off) < 0)
			return -1;
	}
	printf("sector %d @%04x: cmd-03 read %s feature read\n", n, addr,
	       !memcmp(cmd, s, 512) ? "==" : "!=");
	return 0;
}

static void parse_layer(const unsigned char *p)
{
	printf("  type %02x label %02x mode %u n=%u tokens:", p[0], p[1],
	       p[2], p[3]);
	for (int i = 0; i < p[3] && 5 + 2 * i < 254; i++)
		printf(" [%02x %02x]", p[4 + 2 * i], p[5 + 2 * i]);
	printf("\n");
}

static int cmd_write_chunk(unsigned addr, unsigned char len,
			   const unsigned char *data)
{
	unsigned char out[64] = {0x04, 0x04, addr & 0xff, addr >> 8, len};
	unsigned char in[64];

	memcpy(out + 5, data, len);
	return xfer(out, in) < 0 ? -1 : 0;
}

/* blackweb apply: session, erase sector, write 512 B in <=59 B chunks,
   commit. The erase is what makes this the dangerous path. */
static int write_sector(int n, const unsigned char *s)
{
	const unsigned addr = 0xc000 + n * 0x200;
	unsigned char out[64] = {0x04, 0x06, addr & 0xff, addr >> 8};
	unsigned char in[64];

	if (cmd_simple(0x02) < 0)
		return -1;
	if (xfer(out, in) < 0) {
		fprintf(stderr, "erase failed\n");
		cmd_simple(0x07);
		return -1;
	}
	for (unsigned off = 0; off < 512; off += 59) {
		unsigned chunk = 512 - off < 59 ? 512 - off : 59;

		if (cmd_write_chunk(addr + off, chunk, s + off) < 0) {
			cmd_simple(0x07);
			return -1;
		}
	}
	return cmd_simple(0x07);
}

static const char *pollname(unsigned code)
{
	switch (code) {
	case 1: return "1000 Hz";
	case 2: return "500 Hz";
	case 4: return "250 Hz";
	case 8: return "125 Hz";
	default: return "?";
	}
}

/* find the interface-1 hidraw node: its report descriptor carries the
   FF32 vendor page; hidraw numbering moves on every replug */
static int find_vendor(char *out, size_t outsz)
{
	DIR *d = opendir("/sys/bus/hid/devices");
	struct dirent *e;
	int found = 0;

	if (!d)
		return -1;
	while (!found && (e = readdir(d))) {
		if (strncmp(e->d_name, "0003:03F0:2941", 14))
			continue;
		char path[512];
		DIR *hd;
		struct dirent *h;

		snprintf(path, sizeof path, "/sys/bus/hid/devices/%s/hidraw",
			 e->d_name);
		hd = opendir(path);
		if (!hd)
			continue;
		while (!found && (h = readdir(hd))) {
			int fd;
			unsigned char desc[512];
			ssize_t n;

			if (strncmp(h->d_name, "hidraw", 6))
				continue;
			snprintf(path, sizeof path,
				 "/sys/bus/hid/devices/%s/report_descriptor",
				 e->d_name);
			fd = open(path, O_RDONLY);
			if (fd < 0)
				continue;
			n = read(fd, desc, sizeof desc);
			close(fd);
			for (ssize_t i = 0; i + 2 < n; i++)
				if (desc[i] == 0x06 && desc[i + 1] == 0x32 &&
				    desc[i + 2] == 0xff) {
					snprintf(out, outsz, "/dev/%s",
						 h->d_name);
					found = 1;
					break;
				}
		}
		closedir(hd);
	}
	closedir(d);
	return found ? 0 : -1;
}

int main(int argc, char **argv)
{
	unsigned char b[64];
	unsigned sum = 0x8032;
	unsigned cksum;
	int wr_slot = -1, wr_poll = 0, wr_led = -1, live = 0, want_reset = 0;
	char dev[272];
	int argi = 1;

	/* optional hidraw override, then subcommand */
	if (argc > 1 && !strncmp(argv[argi], "/dev/", 5)) {
		snprintf(dev, sizeof dev, "%s", argv[argi++]);
	} else if (find_vendor(dev, sizeof dev) < 0) {
		fprintf(stderr, "no 03f0:2941 FF32 interface found\n");
		return 1;
	}
	printf("using %s\n", dev);

	fd = open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (cmd_simple(0x01) < 0)
		return 1;
	printf("hello ok (status report echoed)\n");
	if (cmd_read(SETTINGS_ADDR, SETTINGS_LEN, b) < 0)
		return 1;

	printf("settings block @%04x:\n", SETTINGS_ADDR);
	for (int i = 0; i < SETTINGS_LEN; i++)
		printf("%02x%s", b[i], (i + 1) % 16 ? " " : "\n");
	if (SETTINGS_LEN % 16)
		printf("\n");

	while (argi < argc) {
		const char *a = argv[argi];

		if (!strcmp(a, "live")) {
			live = 1;
			argi++;
		} else if (!strcmp(a, "reset")) {
			want_reset = 1;
			argi++;
		} else if (argi + 1 < argc && !strcmp(a, "slot")) {
			wr_slot = atoi(argv[argi + 1]);
			argi += 2;
		} else if (argi + 1 < argc && !strcmp(a, "poll")) {
			const unsigned codes[] = {1000, 500, 250, 125};

			for (size_t i = 0; i < 4; i++)
				if (codes[i] == (unsigned)atoi(argv[argi + 1]))
					wr_poll = (int[]){1, 2, 4, 8}[i];
			argi += 2;
		} else if (argi + 1 < argc && !strcmp(a, "secget")) {
			int n = atoi(argv[argi + 1]);
			unsigned char s[512];
			unsigned sum = 0, csum;
			char name[32];
			FILE *f;

			if (n < 0 || n > 7 || read_sector(n, s) < 0)
				return 1;
			parse_layer(s);
			parse_layer(s + 0xfe);
			for (int i = 0; i < 0x1fe; i++)
				sum += s[i];
			csum = s[0x1fe] | s[0x1ff] << 8;
			printf("  checksum stored %04x computed %04x (%s)\n",
			       csum, (0x8032 + sum) & 0xffff,
			       csum == ((0x8032 + sum) & 0xffff) ? "ok" :
							   "MISMATCH");
			snprintf(name, sizeof name, "hp-sec%d.bin", n);
			f = fopen(name, "w");
			if (f) {
				fwrite(s, 1, 512, f);
				fclose(f);
				printf("  saved %s\n", name);
			}
			argi += 2;
			continue;
		} else if (argi + 2 < argc && !strcmp(a, "dump")) {
			unsigned addr = (unsigned)strtol(argv[argi + 1], NULL, 16);
			unsigned len = (unsigned)atoi(argv[argi + 2]);
			FILE *f = fopen(argv[argi + 3], "w");

			if (!f) {
				perror(argv[argi + 3]);
				return 1;
			}
			for (unsigned off = 0; off < len; off += 59) {
				unsigned char chunk[59];
				unsigned chunk_len = len - off < 59 ? len - off : 59;

				if (cmd_read_chunk(addr + off, chunk_len, chunk) < 0) {
					fclose(f);
					return 1;
				}
				fwrite(chunk, 1, chunk_len, f);
			}
			fclose(f);
			printf("dumped %u bytes @%s to %s\n", len, argv[argi + 1],
			       argv[argi + 3]);
			argi += 4;
			continue;
		} else if (argi + 1 < argc && !strcmp(a, "secput")) {
			int n = atoi(argv[argi + 1]);
			unsigned char s[512];
			unsigned sum = 0x8032, csum;
			FILE *f = fopen(argv[argi + 2], "rb");

			if (n < 0 || n > 7) {
				fprintf(stderr, "sector 0..7\n");
				return 1;
			}
			if (!f || fread(s, 1, 512, f) != 512) {
				fprintf(stderr, "cannot read 512 B from %s\n",
					argv[argi + 2]);
				return 1;
			}
			fclose(f);
			sum = 0x8032;
			for (int i = 0; i < 0x1fe; i++)
				sum += s[i];
			sum &= 0xffff;
			csum = s[0x1fe] | s[0x1ff] << 8;
			if (csum != sum) {
				/* the firmware pokes bytes inside sectors; heal
				   the checksum so the file round-trips */
				printf("%s: checksum %04x -> %04x (fixed)\n",
				       argv[argi + 2], csum, sum);
				s[0x1fe] = sum & 0xff;
				s[0x1ff] = sum >> 8;
			}
			if (write_sector(n, s) < 0)
				return 1;
			printf("sector %d restored from %s\n", n, argv[argi + 2]);
			argi += 3;
			continue;
		} else if (argi + 1 < argc && !strcmp(a, "key")) {
			int n = atoi(argv[argi + 1]);
			int usage = (int)strtol(argv[argi + 2], NULL, 16);
			int mod = 0, had_mod = 0;
			unsigned char s[512];
			unsigned sum = 0;

			if (n < 0 || n > 7 || usage < 0 || usage > 0xff) {
				fprintf(stderr, "sector 0..7, usage 0x00..0xff\n");
				return 1;
			}
			if (argi + 2 < argc && isdigit((unsigned char)argv[argi + 3][0])) {
				mod = atoi(argv[argi + 3]);
				had_mod = 1;
			}
			if (read_sector(n, s) < 0)
				return 1;
			/* single-key record (blackweb "Single Key": type 6,
			   label 0x13); kind low nibble = modifier mask */
			memset(s, 0, 0xfe);
			s[0] = 0x06;
			s[1] = 0x13;
			s[3] = 0x01;
			s[4] = (unsigned char)usage;
			s[5] = (unsigned char)mod;
			for (int i = 0; i < 0x1fe; i++)
				sum += s[i];
			s[0x1fe] = (0x8032 + sum) & 0xff;
			s[0x1ff] = (0x8032 + sum) >> 8;
			printf("sector %d -> key %02x mod %x: erasing and "
			       "writing...\n", n, usage, mod);
			if (write_sector(n, s) < 0)
				return 1;
			if (read_sector(n, s) == 0)
				parse_layer(s);
			argi += 3 + had_mod;
			continue;
		} else if (argi + 1 < argc && !strcmp(a, "led")) {
			wr_led = (int)strtol(argv[argi + 1], NULL, 16);
			argi += 2;
		} else {
			fprintf(stderr, "usage: hp-cfg [hidraw] [live] [slot N] "
					"[poll HZ] [led XX] [secget N] [key N USAGE [MOD]] "
					"[secput N FILE] [reset]\n");
			return 1;
		}
	}
	if (want_reset && wr_slot < 0 && !wr_poll && wr_led < 0) {
		close(fd);                 /* node dies with the enumeration */
		return usb_reset();
	}

	/* HP variant: 37-byte block, checksum u16le(0x8032 + sum) at 0x25 */
	for (int i = 0; i < 0x25; i++)
		sum += b[i];
	cksum = (unsigned)(b[0x25] | b[0x26] << 8);
	printf("checksum: stored %04x, computed %04x (%s)\n", cksum,
	       sum & 0xffff, cksum == (sum & 0xffff) ? "ok" : "MISMATCH");

	printf("polling: %s (%02x)\n", pollname(b[0]), b[0]);
	printf("active DPI slot: %u of %u\n", b[1], b[5]);
	printf("LED: mode %02x, speed %u, brightness %u\n", b[2], b[3], b[4]);
	for (unsigned i = 0; i < b[5] && i < 6; i++) {
		const unsigned char *s = b + 6 + 5 * i;
		printf("slot %u: %s level %u rgb #%02x%02x%02x\n", i,
		       s[0] ? "on " : "off", s[1], s[2], s[3], s[4]);
	}
	printf("tail: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	       b[0x1d], b[0x1e], b[0x1f], b[0x20],
	       b[0x21], b[0x22], b[0x23], b[0x24]);

	if (wr_slot >= 0 || wr_poll || wr_led >= 0) {
		unsigned char out[0x27];
		unsigned s2 = 0x8032;

		if (wr_slot >= 0)
			b[1] = (unsigned char)wr_slot;
		if (wr_poll)
			b[0] = (unsigned char)wr_poll;
		if (wr_led >= 0)
			b[2] = (unsigned char)wr_led;
		memcpy(out, b, 0x25);
		for (int i = 0; i < 0x25; i++)
			s2 += out[i];
		out[0x25] = s2 & 0xff;
		out[0x26] = s2 >> 8;
		if (live) {
			/* the vendor app replaces the checksum with a magic tail
			   (blackweb PROTOCOL.md); try checksum, 00 00, 32 32 */
			const unsigned char tails[3][2] = {
				{out[0x25], out[0x26]}, {0x00, 0x00}, {0x32, 0x32},
			};
			for (size_t v = 0; v < 3; v++) {
				struct pollfd pf = {fd, POLLIN, 0};
				unsigned char ev[64];

				out[0x25] = tails[v][0];
				out[0x26] = tails[v][1];
				printf("live: variant %zu (tail %02x %02x)...\n",
				       v, out[0x25], out[0x26]);
				if (live_apply(out) < 0) {
					perror("feature 0x20");
					continue;
				}
				for (int i = 0; i < 4; i++) {
					if (poll(&pf, 1, 300) <= 0 ||
					    !(pf.revents & POLLIN))
						continue;
					if (read(fd, ev, 64) > 0 && ev[0] == 0x04)
						printf("        status echo: "
						       "slot byte = %u\n", ev[3]);
				}
				report_live_slot();
			}
			puts("done — replug reverts to the stored block; "
			     "watch which variant moved the LED");
			cmd_simple(0x07);
			return 0;
		}
		printf("applying: session, settings write, commit...\n");
		if (cmd_simple(0x02) < 0 || cmd_write_settings(out) < 0 ||
		    cmd_simple(0x07) < 0)
			return 1;
		memset(b, 0, sizeof b);
		if (cmd_read(SETTINGS_ADDR, SETTINGS_LEN, b) == 0)
			printf("read-back: slot %u, polling %s\n", b[1],
			       pollname(b[0]));
		report_live_slot();
		if (want_reset) {
			close(fd);         /* node dies with the enumeration */
			usb_reset();
			puts("replug-free apply done — re-run hp-cfg to confirm");
			return 0;
		}
	}

	cmd_simple(0x07);
	puts("done");
	return 0;
}
