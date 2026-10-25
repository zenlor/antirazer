/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga-mapper — command-line key mapping for the Naga LH 2020.
 *
 *   naga-mapper list
 *   naga-mapper get <ctr-hex>
 *   naga-mapper set <ctr-hex> <key> [mods] [layer-hex]
 *   naga-mapper turbo <ctr-hex> <key> <delay-ms> [mods]
 *   naga-mapper commit
 *   naga-mapper dpi <x> <y>
 *
 * <ctr-hex> identifies the control: thumb buttons 40..4B (labelled
 * 1..9, 0, -, =), main buttons 01..03 (left/right/middle), wheel 04/05,
 * DPI keys 0B/0C. See `naga-mapper list`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "naga.h"

static void usage(FILE *out, const char *argv0)
{
	fprintf(
	    out,
	    "usage: %s <command> [args]\n"
	    "\n"
	    "  list                              read and print every control\n"
	    "  get <ctr-hex>                     read one mapping record\n"
	    "  set <ctr-hex> <key> [mods] [layer]   bind a key (layer: 00/01)\n"
	    "  set <ctr-hex> mouse:<button> [layer]  left/right/middle/back/forward\n"
	    "  set <ctr-hex> hypershift [layer]      make it the layer switch\n"
	    "  turbo <ctr-hex> <key> <ms> [mods]     autofire while held\n"
	    "  commit                            re-apply the stored table\n"
	    "  dpi <x> <y>                       set DPI (counts per inch)\n"
	    "\n"
	    "  <ctr-hex>  01..03 main buttons, 04/05 wheel, 0B/0C DPI,\n"
	    "             40..4B thumb buttons 1..9, 0, -, =\n"
	    "  <key>      a..z 0..9 f1..f24 enter esc space tab pgup kp1 ...\n"
	    "             or a raw HID usage in hex (1a, 0x1a)\n"
	    "  <mods>     ctrl, shift, alt, super joined with '+' (default none)\n"
	    "\n"
	    "example: %s set 40 ctrl+shift+t\n",
	    argv0, argv0);
}

static int parse_ctr(const char *s, unsigned *out)
{
	int used = 0;
	unsigned v;

	if (!s || sscanf(s, "%x%n", &v, &used) != 1 || used != (int)strlen(s))
		return -1;
	*out = v;
	return 0;
}

static void print_rec(const naga_ctrl *ctrl, unsigned ctr, const naga_rec *rec)
{
	char binding[64];

	printf("%-12s ctr %02X  %-20s", ctrl ? ctrl->name : "?", ctr,
	       naga_binding_name(rec, binding, sizeof binding));
	if (rec->valid)
		printf("  (layer %02X, type %02X, mod %02X, usage %02X%s%u)\n",
		       rec->layer, rec->type, rec->mod, rec->usage,
		       rec->rate ? ", rate " : "", rec->rate);
	else
		printf("  (read failed)\n");
}

int main(int argc, char **argv)
{
	const char *cmd;
	int rc = 1;

	if (argc < 2) {
		usage(stderr, argv[0]);
		return 1;
	}
	cmd = argv[1];
	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout, argv[0]);
		return 0;
	}

	if (naga_open() != NAGA_OK) {
		int err = naga_open();
		fprintf(stderr, "%s: %s\n", argv[0], naga_strerror(err));
		return 1;
	}

	if (!strcmp(cmd, "list")) {
		for (size_t i = 0; i < naga_ctrls_count; i++) {
			naga_rec rec;

			if (naga_get_rec(naga_ctrls[i].ctr, &rec)) {
				print_rec(&naga_ctrls[i], naga_ctrls[i].ctr, &rec);
				continue;
			}
			print_rec(&naga_ctrls[i], naga_ctrls[i].ctr, &rec);
		}
		rc = 0;
	} else if (!strcmp(cmd, "get") && argc > 2) {
		unsigned ctr;
		naga_rec rec;
		int err;

		if (parse_ctr(argv[2], &ctr)) {
			fprintf(stderr, "bad ctr: %s\n", argv[2]);
			goto out;
		}
		err = naga_get_rec(ctr, &rec);
		print_rec(naga_ctrl_by_ctr(ctr), ctr, &rec);
		if (err) {
			fprintf(stderr, "read failed: %s\n", naga_strerror(err));
			goto out;
		}
		rc = 0;
	} else if (!strcmp(cmd, "set") && argc > 3) {
		unsigned ctr;
		long layer;
		int err = 0;

		if (parse_ctr(argv[2], &ctr)) {
			fprintf(stderr, "bad ctr: %s\n", argv[2]);
			goto out;
		}

		if (!strcmp(argv[3], "hypershift")) {
			layer = argc > 4 ? strtol(argv[4], NULL, 16) : 0;
			if (layer != NAGA_LAYER_NORMAL &&
			    layer != NAGA_LAYER_HYPERSHIFT) {
				fprintf(stderr, "bad layer: %s (00 or 01)\n", argv[4]);
				goto out;
			}
			err = naga_set_hypershift(ctr, (unsigned)layer);
			if (!err)
				printf("ctr %02X -> hypershift (%s), committed\n", ctr,
				       layer ? "hypershift" : "normal");
		} else if (!strncmp(argv[3], "mouse:", 6)) {
			int button = naga_parse_mouse(argv[3]);

			layer = argc > 4 ? strtol(argv[4], NULL, 16) : 0;
			if (button < 0) {
				fprintf(stderr,
					"bad mouse button: %s (left, right, "
					"middle, back, forward)\n",
					argv[3]);
				goto out;
			}
			if (layer != NAGA_LAYER_NORMAL &&
			    layer != NAGA_LAYER_HYPERSHIFT) {
				fprintf(stderr, "bad layer: %s (00 or 01)\n", argv[4]);
				goto out;
			}
			err = naga_set_mouse(ctr, (unsigned)button, (unsigned)layer);
			if (!err)
				printf("ctr %02X -> mouse %s (%s), committed\n", ctr,
				       naga_mouse_name(button),
				       layer ? "hypershift" : "normal");
		} else {
			int usage = naga_parse_usage(argv[3]);
			int mod = argc > 4 ? naga_parse_mod(argv[4]) : 0;

			layer = argc > 5 ? strtol(argv[5], NULL, 16) : 0;
			if (usage < 0) {
				fprintf(stderr,
					"bad key: %s (try a name or a hex usage)\n",
					argv[3]);
				goto out;
			}
			if (mod < 0) {
				fprintf(stderr, "bad modifiers: %s\n", argv[4]);
				goto out;
			}
			if (layer != NAGA_LAYER_NORMAL &&
			    layer != NAGA_LAYER_HYPERSHIFT) {
				fprintf(stderr, "bad layer: %s (00 or 01)\n", argv[5]);
				goto out;
			}
			err = naga_set_key(ctr, (unsigned)usage, (unsigned)mod,
					   (unsigned)layer);
			if (!err)
				printf("ctr %02X -> %s%s%s (%s), committed\n", ctr,
				       mod & 0x01 ? "ctrl+" : "",
				       mod & 0x02 ? "shift+" : "", argv[3],
				       layer ? "hypershift" : "normal");
		}
		if (err) {
			fprintf(stderr, "write failed: %s\n", naga_strerror(err));
			goto out;
		}
		rc = 0;
	} else if (!strcmp(cmd, "turbo") && argc > 4) {
		unsigned ctr;
		int usage = naga_parse_usage(argv[3]);
		int mod = argc > 5 ? naga_parse_mod(argv[5]) : 0;
		long ms = strtol(argv[4], NULL, 10);
		int err;

		if (parse_ctr(argv[2], &ctr)) {
			fprintf(stderr, "bad ctr: %s\n", argv[2]);
			goto out;
		}
		if (usage < 0 || mod < 0 || ms < 1 || ms > 1000000) {
			fprintf(stderr, "bad key, delay or modifiers\n");
			goto out;
		}
		err = naga_set_turbo(ctr, (unsigned)usage, (unsigned)mod, (unsigned)ms);
		if (err) {
			fprintf(stderr, "write failed: %s\n", naga_strerror(err));
			goto out;
		}
		printf("ctr %02X -> turbo %s every %ldms, committed\n", ctr, argv[3],
		       ms);
		rc = 0;
	} else if (!strcmp(cmd, "commit")) {
		int err = naga_commit();

		if (err) {
			fprintf(stderr, "commit failed: %s\n", naga_strerror(err));
			goto out;
		}
		printf("committed\n");
		rc = 0;
	} else if (!strcmp(cmd, "dpi") && argc > 3) {
		unsigned x, y;
		int err;
		int used = 0;

		if (sscanf(argv[2], "%u%n", &x, &used) != 1 ||
		    used != (int)strlen(argv[2]) ||
		    sscanf(argv[3], "%u%n", &y, &used) != 1 ||
		    used != (int)strlen(argv[3]) || !x || !y) {
			fprintf(stderr, "bad dpi: %s %s\n", argv[2], argv[3]);
			goto out;
		}
		err = naga_set_dpi(x, y);
		if (err) {
			fprintf(stderr, "dpi failed: %s\n", naga_strerror(err));
			goto out;
		}
		printf("dpi %u/%u, committed\n", x, y);
		rc = 0;
	} else {
		fprintf(stderr, "bad command or arguments\n\n");
		usage(stderr, argv[0]);
	}

out:
	naga_close();
	return rc;
}
