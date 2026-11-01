/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * hp.h — HP Pavilion Gaming Mouse 300 (03f0:2941) configuration transport.
 *
 * The mouse exposes its config channel on the FF32 vendor interface as
 * 64-byte reports with report id 4:
 *
 *   [04][cmd][addr lo][addr hi][len][data .. <=59]
 *
 * Settings live at 0xBE00 (read 0x33 bytes, write 0x27 incl. checksum) and
 * the eight button sectors at 0xC000 + n*0x200. See
 * research/notes/hp-pavilion-300.md.
 */
#ifndef ANTIRAZER_HP_H
#define ANTIRAZER_HP_H

#include <stdbool.h>
#include <stddef.h>

#define HP_VID 0x03f0
#define HP_PID 0x2941

#define HP_SETTINGS_ADDR 0xbe00
#define HP_SETTINGS_READ_LEN 0x33
#define HP_SETTINGS_WRITE_LEN 0x27
#define HP_SECTOR_BASE 0xc000
#define HP_SECTOR_COUNT 8
#define HP_SECTOR_SIZE 512

enum {
	HP_OK = 0,
	HP_ERR_IO = -1,		/* transfer failed or the device said no */
	HP_ERR_NO_DEVICE = -2,	/* FF32 interface not found / no permission */
	HP_ERR_INVALID = -3,	/* bad argument */
	HP_ERR_CLOSED = -4,	/* no device open */
};

const char *hp_strerror(int rc);

int hp_open(void);
void hp_close(void);
bool hp_is_open(void);

/* cmd 01: returns the 64-byte status report. */
int hp_hello(unsigned char status[64]);

/* Read the settings window; only the first HP_SETTINGS_WRITE_LEN bytes are
 * the checksummed block. */
int hp_read_settings(unsigned char out[HP_SETTINGS_READ_LEN]);

/* Write HP_SETTINGS_WRITE_LEN bytes (payload + u16le checksum). */
int hp_write_settings(const unsigned char *block, size_t len);

int hp_read_sector(unsigned n, unsigned char out[HP_SECTOR_SIZE]);
int hp_write_sector(unsigned n, const unsigned char *sec);

/* u16le checksum 0x8032 + sum, over 0x25 (settings) or 0x1FE (sector)
 * bytes; written at the end of the block. Returns the value. */
unsigned hp_checksum(unsigned char block[HP_SETTINGS_WRITE_LEN]);
unsigned hp_checksum_sector(unsigned char sec[HP_SECTOR_SIZE]);

#endif /* ANTIRAZER_HP_H */
