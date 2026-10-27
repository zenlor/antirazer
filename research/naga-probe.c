/* naga-probe: Razer Naga LH 2020 (1532:008d) config protocol probe.
 * Claims HID interface, listens on its IN endpoint, sends SET_REPORT(feature)
 * with wIndex=iface, then GETs the feature report back.
 *
 * usage: naga-probe [-i iface] [-r reportid] [-w ms] <hexbyte ...>
 *   payload zero-padded to 90 (reportid 0) or 63 bytes; CRC (xor b[2..87])
 *   auto-placed at b[88] for 90-byte payloads.
 * build: cc -I$dev/include -L$out/lib -o naga-probe naga-probe.c -lusb-1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define VID 0x1532
#define PID 0x008d

static int hex2(const char *s) { int v; return sscanf(s, "%x", &v) == 1 ? v : -1; }
static int tid = 0xFF; /* transaction id: this device generation wants 0x1F */

static void dump(const char *tag, unsigned char *b, int n) {
	printf("%s (%d bytes):", tag, n);
	for (int i = 0; i < n; i++) printf(i % 16 == 0 ? "\n  %02X" : " %02X", b[i]);
	printf("\n");
}

static void ep_cb(struct libusb_transfer *t) {
	if (t->status == LIBUSB_TRANSFER_COMPLETED && t->actual_length > 0)
		dump("EP-IN", t->buffer, t->actual_length);
	else if (t->status == LIBUSB_TRANSFER_ERROR || t->status == LIBUSB_TRANSFER_TIMED_OUT)
		fprintf(stderr, "EP status %d\n", t->status);
	/* resubmit unless cancelled at shutdown */
	if (t->status != LIBUSB_TRANSFER_CANCELLED)
		if (libusb_submit_transfer(t) < 0)
			fprintf(stderr, "resubmit failed\n");
}

/* one SET(90B, auto-TID+CRC) + sleep + GET; returns status byte, fills resp */
static int transact(struct libusb_device_handle *h, int iface, unsigned char *buf,
                    unsigned char *resp, int sleep_ms) {
	buf[1] = (unsigned char)tid;
	unsigned char crc = 0;
	for (int i = 2; i < 88; i++) crc ^= buf[i];
	buf[88] = crc;
	int r = libusb_control_transfer(h, 0x21, 0x09, 0x0300, iface, buf, 90, 2000);
	if (r < 0) return -r;
	usleep(sleep_ms * 1000);
	r = libusb_control_transfer(h, 0xA1, 0x01, 0x0300, iface, resp, 90, 2000);
	if (r < 0) return -r;
	return resp[0];
}

int main(int argc, char **argv) {
	int iface = 3, reportid = 0, wait_ms = 2500, sleep_ms = 31, scan = 0, rsize = 0x02, listen = 0, first = 1;
	for (; first < argc; first++) {
		if (!strcmp(argv[first], "-i")) iface = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-r")) reportid = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-w")) wait_ms = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-s")) sleep_ms = atoi(argv[++first]);
		else if (!strcmp(argv[first], "-t")) tid = (int)strtol(argv[++first], 0, 16);
		else if (!strcmp(argv[first], "-S")) scan = 1;
		else if (!strcmp(argv[first], "-L")) listen = 1;
		else if (!strcmp(argv[first], "-z")) rsize = (int)strtol(argv[++first], 0, 16);
		else break;
	}
	int len = reportid == 0 ? 90 : 63;
	unsigned char buf[90] = {0};
	int narg = 0;
	for (int i = first; i < argc && narg < len; i++, narg++) {
		int v = hex2(argv[i]);
		if (v < 0) { fprintf(stderr, "bad hex: %s\n", argv[i]); return 1; }
		buf[narg] = v;
	}
	if (len == 90) {
		buf[1] = (unsigned char)tid;
		unsigned char crc = 0;
		for (int i = 2; i < 88; i++) crc ^= buf[i];
		buf[88] = crc;
	}
	dump("TX", buf, len);

	libusb_context *ctx;
	libusb_init(&ctx);
	struct libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID, PID);
	if (!h) { fprintf(stderr, "device not found\n"); return 1; }
	libusb_set_auto_detach_kernel_driver(h, 1);
	if (libusb_claim_interface(h, iface) < 0) { fprintf(stderr, "claim iface %d failed\n", iface); return 1; }

	if (listen) {
		/* listen-only: no control traffic, just dump EP IN for wait_ms */
		unsigned char inbuf[512];
		int ep = 0x80 | (iface + 1);
		struct libusb_transfer *lt = libusb_alloc_transfer(0);
		libusb_fill_interrupt_transfer(lt, h, ep, inbuf, sizeof inbuf, ep_cb, NULL, 0);
		libusb_submit_transfer(lt);
		for (int i = 0; i < wait_ms / 50; i++) {
			struct timeval tv = {0, 50000};
			libusb_handle_events_timeout(ctx, &tv);
		}
		libusb_cancel_transfer(lt);
		libusb_handle_events(ctx);
		libusb_free_transfer(lt);
		libusb_release_interface(h, iface);
		libusb_close(h);
		libusb_exit(ctx);
		return 0;
	}

	if (getenv("NAGA_WRITE")) { /* explicit SET+GET; gated because it MUTATES device state */
		int r = libusb_control_transfer(h, 0x21, 0x09, 0x0300 | reportid, iface, buf, len, 2000);
		if (r < 0) { fprintf(stderr, "SET_REPORT: %s\n", libusb_error_name(r)); return 1; }
		printf("SET_REPORT ok (%d bytes)\n", r);
		usleep(sleep_ms * 1000);
		unsigned char rb[90] = {0};
		r = libusb_control_transfer(h, 0xA1, 0x01, 0x0300 | reportid, iface, rb, len, 2000);
		if (r < 0) fprintf(stderr, "GET_REPORT: %s\n", libusb_error_name(r));
		else dump("GET", rb, r);
		libusb_release_interface(h, iface);
		libusb_close(h);
		libusb_exit(ctx);
		return 0;
	}

	if (scan) {
		/* GET-scan: class 0x00-0x0F x cmd 0x80-0x87. GET-style only. */
		unsigned char req[90] = {0}, resp[90];
		for (int cls = 0x00; cls <= 0x0F; cls++) {
			for (int cmd = 0x80; cmd <= 0x87; cmd++) {
				memset(req, 0, 90);
				req[5] = rsize; req[6] = cls; req[7] = cmd;
				int st = transact(h, iface, req, resp, sleep_ms);
				if (st < 0) { printf("class %02X cmd %02X -> TRANSPORT ERR %d\n", cls, cmd, st); goto out; }
				printf("class %02X cmd %02X -> st %02X dsz %02X |", cls, cmd, st, resp[5]);
				if (st == 0x02)
					for (int i = 8; i < 8 + 24 && i < 88; i++) printf(" %02X", resp[i]);
				printf("\n");
				fflush(stdout);
			}
		}
out:
		libusb_release_interface(h, iface);
		libusb_close(h);
		libusb_exit(ctx);
		return 0;
	}

	unsigned char inbuf[512];
	int ep = 0x80 | (iface + 1);
	struct libusb_transfer *t = libusb_alloc_transfer(0);
	libusb_fill_interrupt_transfer(t, h, ep, inbuf, sizeof inbuf, ep_cb, NULL, 0);
	if (libusb_submit_transfer(t) < 0) { fprintf(stderr, "submit EP IN failed\n"); return 1; }

	int r = libusb_control_transfer(h, 0x21, 0x09, 0x0300 | reportid, iface, buf, len, 2000);
	if (r < 0) fprintf(stderr, "SET_REPORT: %s\n", libusb_error_name(r));
	else printf("SET_REPORT ok (%d bytes)\n", r);

	/* pump events during the inter-SET/GET wait (prints EP data if any) */
	int slices = sleep_ms > 0 ? 10 : 1;
	for (int i = 0; i < slices; i++) {
		struct timeval tv = {0, (sleep_ms > 0 ? sleep_ms : wait_ms) * 1000 / slices};
		libusb_handle_events_timeout(ctx, &tv);
	}

	for (int attempt = 0; attempt < 3 && r >= 0; attempt++) {
		unsigned char rb[90] = {0};
		r = libusb_control_transfer(h, 0xA1, 0x01, 0x0300 | reportid, iface, rb, len, 2000);
		if (r < 0) { fprintf(stderr, "GET_REPORT: %s\n", libusb_error_name(r)); break; }
		dump("GET", rb, r);
		struct timeval tv = {0, 200000};
		libusb_handle_events_timeout(ctx, &tv);
	}

	libusb_cancel_transfer(t);
	libusb_handle_events(ctx);
	libusb_free_transfer(t);
	libusb_release_interface(h, iface);
	libusb_close(h);
	libusb_exit(ctx);
	return 0;
}
