/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga.c — USB transport and mapping writes for the Naga LH 2020.
 *
 * Every configuration exchange is a 90-byte vendor feature report on HID
 * interface 0:
 *
 *   SET_REPORT  bmRequestType 0x21, bRequest 0x09, wValue 0x0300, wIndex 0
 *   GET_REPORT  bmRequestType 0xA1, bRequest 0x01, wValue 0x0300, wIndex 0
 *
 * Payload layout (as used by Synapse for this device):
 *
 *   [0]    status (request 00; response 01 busy, 02 ok, 03 fail)
 *   [1]    transaction id (0x1F)
 *   [2..3] remaining packets (0, big endian)
 *   [4]    protocol type (0)
 *   [5]    data size
 *   [6]    command class
 *   [7]    command id
 *   [8..]  arguments
 *   [88]   CRC, XOR of bytes 2..87
 *   [89]   0
 */
#include "naga.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libusb-1.0/libusb.h>

#define NAGA_IFACE 0
#define NAGA_REPORT_LEN 90
#define NAGA_TID 0x1F
#define NAGA_TIMEOUT_MS 2000
#define NAGA_SETTLE_US 50000

static libusb_context *ctx;
static libusb_device_handle *dev;

const char *naga_strerror(int rc)
{
	switch (rc) {
	case NAGA_OK:
		return "ok";
	case NAGA_ERR_TRANSFER:
		return "device did not accept the request";
	case NAGA_ERR_USB_INIT:
		return "libusb could not be initialised";
	case NAGA_ERR_NO_DEVICE:
		return "Naga LH 2020 not found (check the udev rule or permissions)";
	case NAGA_ERR_CLAIM:
		return "could not claim the HID interface (is another program using "
		       "it?)";
	case NAGA_ERR_CLOSED:
		return "no device open";
	case NAGA_ERR_INVALID:
		return "invalid argument";
	default:
		return "unknown error";
	}
}

/*
 * One SET/GET pair. The request is copied so callers can pass a const
 * template; the transaction id and CRC are filled in here.
 */
static int transact(const unsigned char *req, unsigned char *resp)
{
	unsigned char buf[NAGA_REPORT_LEN];
	unsigned char crc = 0;
	int rc;

	if (!dev)
		return NAGA_ERR_CLOSED;

	memcpy(buf, req, sizeof buf);
	buf[1] = NAGA_TID;
	for (int i = 2; i < 88; i++)
		crc ^= buf[i];
	buf[88] = crc;

	rc = libusb_control_transfer(dev, 0x21, 0x09, 0x0300, NAGA_IFACE, buf,
				     sizeof buf, NAGA_TIMEOUT_MS);
	if (rc < 0)
		return NAGA_ERR_TRANSFER;
	usleep(NAGA_SETTLE_US);

	memset(resp, 0, NAGA_REPORT_LEN);
	rc = libusb_control_transfer(dev, 0xA1, 0x01, 0x0300, NAGA_IFACE, resp,
				     NAGA_REPORT_LEN, NAGA_TIMEOUT_MS);
	if (rc < 0)
		return NAGA_ERR_TRANSFER;
	if (resp[0] != 0x02) /* anything but "ok" */
		return NAGA_ERR_TRANSFER;
	return NAGA_OK;
}

int naga_open(void)
{
	if (dev)
		return NAGA_OK;
	if (libusb_init(&ctx) < 0) {
		ctx = NULL;
		return NAGA_ERR_USB_INIT;
	}
	dev = libusb_open_device_with_vid_pid(ctx, NAGA_VID, NAGA_PID);
	if (!dev) {
		libusb_exit(ctx);
		ctx = NULL;
		return NAGA_ERR_NO_DEVICE;
	}
	libusb_set_auto_detach_kernel_driver(dev, 1);
	if (libusb_claim_interface(dev, NAGA_IFACE) < 0) {
		libusb_close(dev);
		libusb_exit(ctx);
		dev = NULL;
		ctx = NULL;
		return NAGA_ERR_CLAIM;
	}
	return NAGA_OK;
}

void naga_close(void)
{
	if (!dev)
		return;
	libusb_release_interface(dev, NAGA_IFACE);
	libusb_close(dev);
	libusb_exit(ctx);
	dev = NULL;
	ctx = NULL;
}

bool naga_is_open(void) { return dev != NULL; }

int naga_commit(void)
{
	unsigned char req[NAGA_REPORT_LEN] = {0};
	unsigned char resp[NAGA_REPORT_LEN];

	req[5] = 0x01;
	req[6] = 0x00;
	req[7] = 0xB3;

	return transact(req, resp);
}

int naga_get_rec(unsigned ctr, naga_rec *out)
{
	unsigned char req[NAGA_REPORT_LEN] = {0};
	unsigned char resp[NAGA_REPORT_LEN];
	int rc;

	if (!out)
		return NAGA_ERR_INVALID;
	memset(out, 0, sizeof *out);

	req[5] = 0x0A; /* data size */
	req[6] = 0x02; /* class: button mapping */
	req[7] = 0x8C; /* read record */
	req[8] = 0x01;
	req[9] = (unsigned char)ctr;
	req[10] = 0x00;
	req[11] = 0x02; /* input class: side buttons */

	rc = transact(req, resp);
	if (rc)
		return rc;

	out->valid = true;
	out->ctr = resp[9];
	out->layer = resp[10];
	/* Record: 01 <ctr> <layer> <type> <length> <data...> */
	out->type = resp[11];
	out->mouse = resp[13];
	out->param = resp[13];
	out->mod = resp[13];
	out->usage = resp[14];
	out->rate = (uint16_t)resp[15] << 8 | resp[16];
	return NAGA_OK;
}

/*
 * Write one mapping record and commit it:
 *
 *   01 <ctr> <layer> <type> <length> <data...>
 */
static int write_record(unsigned ctr, unsigned layer, unsigned type,
			const unsigned char *data, unsigned len)
{
	unsigned char req[NAGA_REPORT_LEN] = {0};
	unsigned char resp[NAGA_REPORT_LEN];
	int rc;

	if (ctr > 0xFF || layer > 0xFF || type > 0xFF || len > 7)
		return NAGA_ERR_INVALID;

	req[5] = 0x0A; /* data size */
	req[6] = 0x02; /* class: button mapping */
	req[7] = 0x0C; /* write record */
	req[8] = 0x01; /* live profile */
	req[9] = (unsigned char)ctr;
	req[10] = (unsigned char)layer;
	req[11] = (unsigned char)type;
	req[12] = (unsigned char)len;
	memcpy(&req[13], data, len);

	rc = transact(req, resp);
	if (rc)
		return rc;
	return naga_commit();
}

int naga_set_key(unsigned ctr, unsigned usage, unsigned mod, unsigned layer)
{
	const unsigned char data[2] = {
	    (unsigned char)mod,
	    (unsigned char)usage,
	};

	if (usage > 0xFF || mod > 0xFF)
		return NAGA_ERR_INVALID;
	return write_record(ctr, layer, NAGA_ASSIGN_KEY, data, sizeof data);
}

int naga_set_mouse(unsigned ctr, unsigned button, unsigned layer)
{
	const unsigned char data[1] = {(unsigned char)button};

	if (button == 0 || button > 0xFF)
		return NAGA_ERR_INVALID;
	return write_record(ctr, layer, NAGA_ASSIGN_MOUSE, data, sizeof data);
}

int naga_set_hypershift(unsigned ctr, unsigned layer)
{
	const unsigned char data[1] = {0x01};

	return write_record(ctr, layer, NAGA_ASSIGN_HYPERSHIFT, data, sizeof data);
}

int naga_set_turbo(unsigned ctr, unsigned usage, unsigned mod, unsigned delay_ms)
{
	unsigned char data[4];
	unsigned rate;

	if (usage > 0xFF || mod > 0xFF || delay_ms == 0)
		return NAGA_ERR_INVALID;

	rate = 1000000u / delay_ms;
	if (rate > 0xFFFF)
		rate = 0xFFFF;

	data[0] = (unsigned char)mod;
	data[1] = (unsigned char)usage;
	data[2] = (unsigned char)(rate >> 8);
	data[3] = (unsigned char)rate;

	return write_record(ctr, NAGA_LAYER_NORMAL, NAGA_ASSIGN_TURBO, data,
			    sizeof data);
}

int naga_set_dpi(unsigned x, unsigned y)
{
	unsigned char req[NAGA_REPORT_LEN] = {0};
	unsigned char resp[NAGA_REPORT_LEN];
	int rc;

	if (x > 0xFFFF || y > 0xFFFF)
		return NAGA_ERR_INVALID;

	req[5] = 0x07;
	req[6] = 0x04; /* class: DPI */
	req[7] = 0x05; /* set DPI */
	req[8] = 0x00;
	req[9] = (unsigned char)(x & 0xff);
	req[10] = (unsigned char)(x >> 8);
	req[11] = (unsigned char)(y & 0xff);
	req[12] = (unsigned char)(y >> 8);

	rc = transact(req, resp);
	if (rc)
		return rc;
	return naga_commit();
}
