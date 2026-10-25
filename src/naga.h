/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga.h — Razer Naga Left-Handed Edition 2020 (1532:008d) configuration API.
 *
 * Shared by naga-mapper (CLI) and naga-ui. The device speaks a 90-byte
 * feature-report protocol on HID interface 0; see README.md for the wire
 * format and the decoded command map.
 */
#ifndef ANTIRAZER_NAGA_H
#define ANTIRAZER_NAGA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NAGA_VID 0x1532
#define NAGA_PID 0x008d

/* Return codes. */
enum {
	NAGA_OK = 0,
	NAGA_ERR_TRANSFER = -1,	 /* USB transfer failed or the device said no */
	NAGA_ERR_USB_INIT = -2,	 /* libusb could not be initialised */
	NAGA_ERR_NO_DEVICE = -3, /* no Naga LH 2020 found (or no permission) */
	NAGA_ERR_CLAIM = -4,	 /* HID interface 0 could not be claimed */
	NAGA_ERR_CLOSED = -5,	 /* no device open */
	NAGA_ERR_INVALID = -6,	 /* bad argument */
};

const char *naga_strerror(int rc);

/* ---- mapping records --------------------------------------------------- */

enum naga_layer {
	NAGA_LAYER_NORMAL = 0x00,
	NAGA_LAYER_HYPERSHIFT = 0x01,
};

/* Assignment types the firmware stores in a mapping record. */
enum naga_assign {
	NAGA_ASSIGN_DEFAULT = 0x00, /* device default */
	NAGA_ASSIGN_KEY = 0x02,	    /* keyboard key */
	NAGA_ASSIGN_TURBO = 0x0D,   /* autofire */
};

typedef struct {
	bool valid;
	uint8_t ctr;   /* control instance */
	uint8_t layer; /* 0x00 normal, 0x01 hypershift */
	uint8_t type;  /* enum naga_assign */
	uint8_t mod;   /* HID modifier bitmap */
	uint8_t usage; /* HID keyboard usage */
	uint16_t rate; /* turbo: repeats per second (1000000 / delay_ms) */
} naga_rec;

/* ---- controls ---------------------------------------------------------- */

enum naga_group {
	NAGA_GROUP_MAIN,  /* left / right / middle */
	NAGA_GROUP_WHEEL, /* wheel up / down */
	NAGA_GROUP_DPI,	  /* DPI up / down */
	NAGA_GROUP_THUMB, /* the 12-button thumb grid */
};

typedef struct {
	const char *name; /* short human name: "left", "7", "dpi-up", ... */
	uint8_t ctr;	  /* control instance in the mapping record */
	enum naga_group group;
} naga_ctrl;

extern const naga_ctrl naga_ctrls[];
extern const size_t naga_ctrls_count;

const naga_ctrl *naga_ctrl_by_ctr(unsigned ctr);
const naga_ctrl *naga_ctrl_by_name(const char *name);

/* ---- device ------------------------------------------------------------ */

int naga_open(void);
void naga_close(void);
bool naga_is_open(void);

/*
 * Read the mapping record for <ctr>. The record is filled in even when the
 * read fails, with .valid = false.
 */
int naga_get_rec(unsigned ctr, naga_rec *out);

/* Write + commit a keyboard assignment in <layer>. */
int naga_set_key(unsigned ctr, unsigned usage, unsigned mod, unsigned layer);

/* Write + commit a turbo (autofire) assignment. delay_ms is the repeat delay. */
int naga_set_turbo(unsigned ctr, unsigned usage, unsigned mod, unsigned delay_ms);

/* Write + commit a DPI value in counts per inch. */
int naga_set_dpi(unsigned x, unsigned y);

/* Re-apply the stored mapping table (needed for writes to take effect). */
int naga_commit(void);

/* ---- keys -------------------------------------------------------------- */

/* HID usage -> "x" / "f5" / "enter" / NULL. Not reentrant. */
const char *naga_keyname(int usage);

/* "x" / "f5" / "enter" / "1a" / "0x1a" -> HID usage, or -1. */
int naga_parse_usage(const char *s);

/* "ctrl+shift" / "3" (HID bitmap) -> modifier byte, or -1. */
int naga_parse_mod(const char *s);

/* Render a record as "ctrl+shift+t" / "turbo f5 @3000ms" / "default". */
const char *naga_binding_name(const naga_rec *rec, char *buf, size_t len);

#endif /* ANTIRAZER_NAGA_H */
