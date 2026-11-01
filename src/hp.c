/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * hp.c — transport for the HP Pavilion Gaming Mouse 300 (03f0:2941).
 *
 * Ported from research/hp-cfg.c. The config interface is the hidraw node
 * whose report descriptor carries usage page 0xFF32; hidraw numbering moves
 * on every replug, so it is discovered through /sys each time.
 */
#include "hp.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/hidraw.h>

static int fd = -1;

const char *hp_strerror(int rc)
{
	switch (rc) {
	case HP_OK:
		return "ok";
	case HP_ERR_IO:
		return "device did not answer the request";
	case HP_ERR_NO_DEVICE:
		return "HP Pavilion 300 not found (FF32 interface missing or no permission)";
	case HP_ERR_INVALID:
		return "invalid argument";
	case HP_ERR_CLOSED:
		return "no device open";
	default:
		return "unknown error";
	}
}

/* Find the interface-1 hidraw node by its FF32 usage page. */
static int find_vendor(char *out, size_t outsz)
{
	DIR *d = opendir("/sys/bus/hid/devices");
	struct dirent *e;
	int found = 0;

	if (!d)
		return -1;
	while (!found && (e = readdir(d))) {
		char path[512];
		DIR *hd;
		struct dirent *h;

		if (strncasecmp(e->d_name, "0003:03f0:2941", 14))
			continue;
		snprintf(path, sizeof path, "/sys/bus/hid/devices/%s/hidraw",
			 e->d_name);
		hd = opendir(path);
		if (!hd)
			continue;
		while (!found && (h = readdir(hd))) {
			unsigned char desc[512];
			ssize_t n;
			int dfd;

			if (strncmp(h->d_name, "hidraw", 6))
				continue;
			snprintf(path, sizeof path,
				 "/sys/bus/hid/devices/%s/report_descriptor",
				 e->d_name);
			dfd = open(path, O_RDONLY);
			if (dfd < 0)
				continue;
			n = read(dfd, desc, sizeof desc);
			close(dfd);
			for (ssize_t i = 0; i + 2 < n; i++) {
				if (desc[i] == 0x06 && desc[i + 1] == 0x32 &&
				    desc[i + 2] == 0xff) {
					snprintf(out, outsz, "/dev/%s",
						 h->d_name);
					found = 1;
					break;
				}
			}
		}
		closedir(hd);
	}
	closedir(d);
	return found ? 0 : -1;
}

static int xfer(const unsigned char *out, unsigned char in[64])
{
	if (fd < 0)
		return HP_ERR_CLOSED;
	if (write(fd, out, 64) != 64)
		return HP_ERR_IO;
	for (int i = 0; i < 25; i++) { /* up to 5 s, like the vendor app */
		struct pollfd pf = {fd, POLLIN, 0};
		unsigned char ev[64];

		if (poll(&pf, 1, 200) <= 0 || !(pf.revents & POLLIN))
			continue;
		if (read(fd, ev, 64) <= 0 || ev[0] != 0x04)
			continue;
		if (out[1] == 0x01) { /* hello reply is the status report */
			memcpy(in, ev, 64);
			return HP_OK;
		}
		if (ev[1] == out[1] && ev[2] == out[2] && ev[3] == out[3] &&
		    ev[4] == out[4]) {
			memcpy(in, ev, 64);
			return HP_OK;
		}
	}
	return HP_ERR_IO;
}

static int cmd_read(unsigned addr, unsigned char len, unsigned char *data)
{
	unsigned char out[64] = {0x04, 0x03, addr & 0xff, addr >> 8, len};
	unsigned char in[64];
	int rc = xfer(out, in);

	if (rc != HP_OK)
		return rc;
	memcpy(data, in + 5, len);
	return HP_OK;
}

static int cmd_simple(unsigned char cmd)
{
	unsigned char out[64] = {0x04, cmd};
	unsigned char in[64];

	return xfer(out, in);
}

static int cmd_write_chunk(unsigned addr, unsigned char len,
			   const unsigned char *data)
{
	unsigned char out[64] = {0x04, 0x04, addr & 0xff, addr >> 8, len};
	unsigned char in[64];

	memcpy(out + 5, data, len);
	return xfer(out, in);
}

int hp_open(void)
{
	char path[272];

	if (fd >= 0)
		return HP_OK;
	if (find_vendor(path, sizeof path) < 0)
		return HP_ERR_NO_DEVICE;
	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fd = -1;
		return HP_ERR_NO_DEVICE;
	}
	return HP_OK;
}

void hp_close(void)
{
	if (fd >= 0)
		close(fd);
	fd = -1;
}

bool hp_is_open(void) { return fd >= 0; }

int hp_hello(unsigned char status[64])
{
	unsigned char out[64] = {0x04, 0x01};

	if (!status)
		return HP_ERR_INVALID;
	return xfer(out, status);
}

int hp_read_settings(unsigned char out[HP_SETTINGS_READ_LEN])
{
	if (!out)
		return HP_ERR_INVALID;
	return cmd_read(HP_SETTINGS_ADDR, HP_SETTINGS_READ_LEN, out);
}

int hp_write_settings(const unsigned char *block, size_t len)
{
	unsigned char out[64] = {0x04, 0x05,
				 HP_SETTINGS_ADDR & 0xff,
				 HP_SETTINGS_ADDR >> 8};
	unsigned char in[64];

	if (!block || !len || len > sizeof out - 5)
		return HP_ERR_INVALID;
	out[4] = (unsigned char)len;
	memcpy(out + 5, block, len);
	return xfer(out, in);
}

int hp_read_sector(unsigned n, unsigned char out[HP_SECTOR_SIZE])
{
	unsigned char buf[1 + HP_SECTOR_SIZE] = {0};

	if (n >= HP_SECTOR_COUNT || !out)
		return HP_ERR_INVALID;
	if (fd < 0)
		return HP_ERR_CLOSED;
	buf[0] = (unsigned char)(0x10 + n); /* feature reports 0x10..0x17 */
	if (ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf) < 0)
		return HP_ERR_IO;
	memcpy(out, buf + 1, HP_SECTOR_SIZE);
	return HP_OK;
}

int hp_write_sector(unsigned n, const unsigned char *sec)
{
	const unsigned addr = HP_SECTOR_BASE + n * HP_SECTOR_SIZE;
	unsigned char out[64] = {0x04, 0x06, addr & 0xff, addr >> 8};
	unsigned char in[64];

	if (n >= HP_SECTOR_COUNT || !sec)
		return HP_ERR_INVALID;
	if (cmd_simple(0x02) != HP_OK)
		return HP_ERR_IO;
	if (xfer(out, in) != HP_OK) {
		cmd_simple(0x07);
		return HP_ERR_IO;
	}
	for (unsigned off = 0; off < HP_SECTOR_SIZE; off += 59) {
		unsigned chunk = HP_SECTOR_SIZE - off < 59 ?
				     HP_SECTOR_SIZE - off : 59;

		if (cmd_write_chunk(addr + off, (unsigned char)chunk,
				    sec + off) != HP_OK) {
			cmd_simple(0x07);
			return HP_ERR_IO;
		}
	}
	return cmd_simple(0x07);
}

unsigned hp_checksum(unsigned char block[HP_SETTINGS_WRITE_LEN])
{
	unsigned s = 0x8032;

	for (int i = 0; i < 0x25; i++)
		s += block[i];
	s &= 0xffff;
	block[0x25] = (unsigned char)(s & 0xff);
	block[0x26] = (unsigned char)(s >> 8);
	return s;
}

unsigned hp_checksum_sector(unsigned char sec[HP_SECTOR_SIZE])
{
	unsigned s = 0x8032;

	for (int i = 0; i < 0x1fe; i++)
		s += sec[i];
	s &= 0xffff;
	sec[0x1fe] = (unsigned char)(s & 0xff);
	sec[0x1ff] = (unsigned char)(s >> 8);
	return s;
}
