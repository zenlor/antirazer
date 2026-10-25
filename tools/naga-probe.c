/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga-probe — raw protocol probe for the Naga LH 2020 (1532:008d).
 *
 * Sends one 90-byte vendor feature report (CRC and transaction id filled in)
 * and prints the answer. Intended for exploring undecoded commands; the
 * everyday tool is naga-mapper.
 *
 *   naga-probe [options] <hexbyte>...
 *     -i <iface>     HID interface (default 0, the configuration channel)
 *     -t <tid>       transaction id (default 0x1F; 0xFF resets this device)
 *     -r <id>        report id (0 = 90-byte frame, otherwise 63-byte frame)
 *     -w <ms>        how long to listen on the IN endpoint (default 2500)
 *     -s <ms>        settle delay between SET and GET (default 31)
 *     -z <size>      data size byte for -S scans (default 0x02)
 *     -L             listen on the interface's IN endpoint only
 *     -S             GET-scan classes 00..0F x commands 80..87
 *
 * Writes are refused unless NAGA_WRITE=1 is set, because the firmware trusts
 * what it is given: an accepted command with the wrong shape can damage
 * persistent state. Reads are safe. Capture first, write second.
 *
 * Build with `make tools`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libusb-1.0/libusb.h>

#define VID 0x1532
#define PID 0x008d
#define REPORT_90 90
#define REPORT_63 63

static int tid = 0x1F;

static int parse_hex(const char *s)
{
	unsigned v;

	if (sscanf(s, "%x", &v) != 1)
		return -1;
	return (int)(v & 0xFF);
}

static void dump(const char *tag, const unsigned char *b, int n)
{
	printf("%s (%d bytes):", tag, n);
	for (int i = 0; i < n; i++)
		printf(i % 16 == 0 ? "\n  %02X" : " %02X", b[i]);
	printf("\n");
}

static void ep_cb(struct libusb_transfer *t)
{
	if (t->status == LIBUSB_TRANSFER_COMPLETED && t->actual_length > 0)
		dump("EP-IN", t->buffer, (int)t->actual_length);
	else if (t->status == LIBUSB_TRANSFER_ERROR ||
		 t->status == LIBUSB_TRANSFER_TIMED_OUT)
		fprintf(stderr, "EP status %d\n", t->status);
	if (t->status != LIBUSB_TRANSFER_CANCELLED && libusb_submit_transfer(t) < 0)
		fprintf(stderr, "resubmit failed\n");
}

/* One SET + settle + GET. Returns the status byte, or a negative error. */
static int transact(struct libusb_device_handle *h, int iface, unsigned char *buf,
		    unsigned char *resp, int len, int settle_ms)
{
	unsigned char crc = 0;
	int r;

	if (len == REPORT_90) {
		buf[1] = (unsigned char)tid;
		for (int i = 2; i < 88; i++)
			crc ^= buf[i];
		buf[88] = crc;
	}

	r = libusb_control_transfer(h, 0x21, 0x09, 0x0300, iface, buf, len, 2000);
	if (r < 0)
		return -r;
	usleep((useconds_t)settle_ms * 1000);

	r = libusb_control_transfer(h, 0xA1, 0x01, 0x0300, iface, resp, len, 2000);
	if (r < 0)
		return -r;
	return resp[0];
}

int main(int argc, char **argv)
{
	int iface = 0, reportid = 0, wait_ms = 2500, settle_ms = 31, scan = 0;
	int listen = 0, first = 1, rsize = 0x02, len, nargs = 0;
	unsigned char buf[REPORT_90] = {0};
	unsigned char resp[REPORT_90] = {0};
	libusb_context *ctx;
	libusb_device_handle *h;

	for (; first < argc; first++) {
		if (!strcmp(argv[first], "-i"))
			iface = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-t"))
			tid = (int)strtol(argv[++first], NULL, 16);
		else if (!strcmp(argv[first], "-r"))
			reportid = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-w"))
			wait_ms = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-s"))
			settle_ms = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-z"))
			rsize = (int)strtol(argv[++first], NULL, 16);
		else if (!strcmp(argv[first], "-L"))
			listen = 1;
		else if (!strcmp(argv[first], "-S"))
			scan = 1;
		else if (!strcmp(argv[first], "-h") || !strcmp(argv[first], "--help")) {
			printf("usage: %s [-i iface] [-t tid] [-r reportid] [-w ms] "
			       "[-s ms] [-z size] [-L] [-S] <hexbyte>...\n",
			       argv[0]);
			return 0;
		} else
			break;
	}

	len = reportid == 0 ? REPORT_90 : REPORT_63;
	for (int i = first; i < argc && nargs < len; i++, nargs++) {
		int v = parse_hex(argv[i]);

		if (v < 0) {
			fprintf(stderr, "bad hex byte: %s\n", argv[i]);
			return 1;
		}
		buf[nargs] = (unsigned char)v;
	}
	if (!listen && !scan && nargs == 0) {
		fprintf(stderr, "nothing to send; see --help\n");
		return 1;
	}
	if (!listen && !scan && !getenv("NAGA_WRITE")) {
		fprintf(stderr, "refusing to write: set NAGA_WRITE=1 once you know the "
				"payload is safe (reads are safe, -S works)\n");
		return 1;
	}
	if (len == REPORT_90) {
		buf[1] = (unsigned char)tid;
		unsigned char crc = 0;
		for (int i = 2; i < 88; i++)
			crc ^= buf[i];
		buf[88] = crc;
	}
	if (!scan && !listen)
		dump("TX", buf, len);

	libusb_init(&ctx);
	h = libusb_open_device_with_vid_pid(ctx, VID, PID);
	if (!h) {
		fprintf(stderr, "device not found (udev rule or permissions?)\n");
		libusb_exit(ctx);
		return 1;
	}
	libusb_set_auto_detach_kernel_driver(h, 1);
	if (libusb_claim_interface(h, iface) < 0) {
		fprintf(stderr, "claim interface %d failed (busy?)\n", iface);
		libusb_close(h);
		libusb_exit(ctx);
		return 1;
	}

	if (listen) {
		/* Passive: no control traffic, just dump the IN endpoint. */
		unsigned char inbuf[512];
		int ep = 0x80 | (iface + 1);
		struct libusb_transfer *lt = libusb_alloc_transfer(0);

		libusb_fill_interrupt_transfer(lt, h, ep, inbuf, sizeof inbuf, ep_cb,
					       NULL, 0);
		if (libusb_submit_transfer(lt) < 0) {
			fprintf(stderr, "submit IN transfer failed\n");
			goto out;
		}
		for (int i = 0; i < wait_ms / 50; i++) {
			struct timeval tv = {0, 50000};

			libusb_handle_events_timeout(ctx, &tv);
		}
		libusb_cancel_transfer(lt);
		libusb_handle_events(ctx);
		libusb_free_transfer(lt);
		goto out;
	}

	if (scan) {
		/* GET-style scan: class 0x00..0x0F x command 0x80..0x87. */
		for (int cls = 0x00; cls <= 0x0F; cls++) {
			for (int cmd = 0x80; cmd <= 0x87; cmd++) {
				int st;

				memset(buf, 0, sizeof buf);
				buf[5] = (unsigned char)rsize;
				buf[6] = (unsigned char)cls;
				buf[7] = (unsigned char)cmd;
				st = transact(h, iface, buf, resp, len, settle_ms);
				if (st < 0) {
					printf("class %02X cmd %02X -> transport "
					       "error %d\n",
					       cls, cmd, st);
					goto out;
				}
				printf("class %02X cmd %02X -> status %02X size %02X |",
				       cls, cmd, st, resp[5]);
				if (st == 0x02) {
					for (int i = 8; i < 32; i++)
						printf(" %02X", resp[i]);
				}
				printf("\n");
				fflush(stdout);
			}
		}
		goto out;
	}

	{
		/* SET, pump events during the settle delay, then GET a few times. */
		unsigned char inbuf[512];
		int ep = 0x80 | (iface + 1);
		struct libusb_transfer *t = libusb_alloc_transfer(0);
		int r;

		libusb_fill_interrupt_transfer(t, h, ep, inbuf, sizeof inbuf, ep_cb,
					       NULL, 0);
		libusb_submit_transfer(t);

		r = libusb_control_transfer(h, 0x21, 0x09, 0x0300, iface, buf, len,
					    2000);
		if (r < 0)
			fprintf(stderr, "SET_REPORT: %s\n", libusb_error_name(r));
		else
			printf("SET_REPORT ok (%d bytes)\n", r);

		for (int i = 0; i < 10; i++) {
			struct timeval tv = {0, (settle_ms > 0 ? settle_ms : wait_ms) *
						    1000 / 10};

			libusb_handle_events_timeout(ctx, &tv);
		}

		for (int attempt = 0; attempt < 3 && r >= 0; attempt++) {
			unsigned char rb[REPORT_90] = {0};
			struct timeval tv = {0, 200000};

			r = libusb_control_transfer(h, 0xA1, 0x01, 0x0300, iface, rb,
						    len, 2000);
			if (r < 0) {
				fprintf(stderr, "GET_REPORT: %s\n",
					libusb_error_name(r));
				break;
			}
			dump("GET", rb, r);
			libusb_handle_events_timeout(ctx, &tv);
		}

		libusb_cancel_transfer(t);
		libusb_handle_events(ctx);
		libusb_free_transfer(t);
	}

out:
	libusb_release_interface(h, iface);
	libusb_close(h);
	libusb_exit(ctx);
	return 0;
}
