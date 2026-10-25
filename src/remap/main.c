/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/map.h"
#include "remap/plan.h"
#include "remap/run.h"
#include "remap/config.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"usage: %s <command> [options]\n"
		"\n"
		"Remap a Naga from a JSON map and run sequences host-side.\n"
		"\n"
		"  validate FILE              parse and check a map\n"
		"  apply FILE [--dry-run]     2020 only: write single actions and\n"
		"                             sequence triggers to the mouse\n"
		"  dump [--output FILE]       2020: write the onboard map as JSON\n"
		"  run FILE [--device PATH] [--model 2020|2014]\n"
		"                             grab the mouse and run host-side bindings\n"
		"  daemon [--config FILE] [--device PATH] [--model 2020|2014]\n"
		"                             stay resident; SIGHUP reloads the config\n"
		"  devices                    list Naga event nodes\n"
		"\n"
		"Modifiers: ctrl shift alt super rctrl rshift ralt rsuper, joined by '+'.\n"
		"Actions:   key, mouse:left|right|middle|back|forward, hypershift,\n"
		"           turbo:<key>@<ms>, default, macro:<name>.\n"
		"Macros:    \"macros\": {\"name\": [{\"key\":\"a\"},{\"delay\":50},\n"
		"           {\"text\":\"hi\"},{\"mouse\":\"left\"}]}\n"
		"Wheel:     bindings.normal.wheel-up / wheel-down (host-side).\n"
		"\n"
		"See docs/remap.md for the full format.\n",
		prog);
}

static int fail(const char *err)
{
	fprintf(stderr, "naga-remap: %s\n", err);
	return 1;
}

static int parse_model(const char *s, enum remap_device *model)
{
	if (!strcmp(s, "auto"))
		*model = REMAP_DEVICE_AUTO;
	else if (!strcmp(s, "2020") || !strcmp(s, "naga-2020") ||
		 !strcmp(s, "naga-lh-2020"))
		*model = REMAP_DEVICE_2020;
	else if (!strcmp(s, "2014") || !strcmp(s, "naga-2014"))
		*model = REMAP_DEVICE_2014;
	else
		return -1;
	return 0;
}

static const char *model_name(enum remap_device model)
{
	switch (model) {
	case REMAP_DEVICE_2020:
		return "2020 LH";
	case REMAP_DEVICE_2014:
		return "2014";
	default:
		return "auto";
	}
}

static int prepare_map(struct remap_map *map, enum remap_device override,
		       const char *evdev, char *err, size_t errlen)
{
	enum remap_device model = override != REMAP_DEVICE_AUTO ?
				      override :
				      map->device;

	if (model == REMAP_DEVICE_AUTO)
		model = remap_guess_model(evdev, err, errlen);
	if (model == REMAP_DEVICE_AUTO) {
		if (!err[0])
			snprintf(err, errlen,
				 "set \"device\" in the map or pass --model");
		return -1;
	}
	map->device = model;
	if (remap_map_validate(map, err, errlen) < 0)
		return -1;
	if (remap_map_assign_triggers(map, err, errlen) < 0)
		return -1;
	return 0;
}

static void print_summary(const struct remap_map *map)
{
	size_t sequences = 0;

	printf("%s: %zu binding(s)", model_name(map->device), map->count);
	for (size_t i = 0; i < map->count; i++)
		if (map->bindings[i].is_sequence)
			sequences++;
	if (sequences)
		printf(", %zu sequence(s)", sequences);
	printf("\n");
	for (size_t i = 0; i < map->count; i++) {
		const struct remap_binding *b = &map->bindings[i];

		if (!b->is_sequence)
			continue;
		printf("  %s%s -> trigger %s\n", b->id,
		       b->layer ? "@hypershift" : "", b->trigger);
	}
}

static int cmd_validate(const char *path, enum remap_device override)
{
	struct remap_map map;
	char err[256] = "";

	if (remap_map_load(path, &map, err, sizeof err) < 0)
		return fail(err);
	if (map.device == REMAP_DEVICE_AUTO && override == REMAP_DEVICE_AUTO)
		map.device = REMAP_DEVICE_2020;
	if (prepare_map(&map, override, NULL, err, sizeof err) < 0) {
		remap_map_free(&map);
		return fail(err);
	}
	printf("ok: ");
	print_summary(&map);
	remap_map_free(&map);
	return 0;
}

static int cmd_apply(const char *path, bool dry_run,
		     enum remap_device override)
{
	struct remap_map map;
	char err[256] = "";

	if (remap_map_load(path, &map, err, sizeof err) < 0)
		return fail(err);
	if (map.device == REMAP_DEVICE_2014 || override == REMAP_DEVICE_2014) {
		remap_map_free(&map);
		return fail("the 2014 has no onboard mapping; use `run`");
	}
	if (override == REMAP_DEVICE_2020 || map.device == REMAP_DEVICE_AUTO)
		map.device = REMAP_DEVICE_2020;
	if (prepare_map(&map, REMAP_DEVICE_2020, NULL, err, sizeof err) < 0) {
		remap_map_free(&map);
		return fail(err);
	}
	if (remap_apply(&map, dry_run, err, sizeof err) < 0) {
		remap_map_free(&map);
		return fail(err);
	}
	remap_map_free(&map);
	return 0;
}

static int cmd_dump(const char *output)
{
	char err[256] = "";
	FILE *out = stdout;
	cJSON *root;
	cJSON *bindings;
	cJSON *normal;
	cJSON *hyper;
	char *text;
	int rc;

	rc = naga_open();
	if (rc != NAGA_OK) {
		snprintf(err, sizeof err, "open: %s", naga_strerror(rc));
		return fail(err);
	}

	root = cJSON_CreateObject();
	bindings = cJSON_CreateObject();
	normal = cJSON_CreateObject();
	hyper = cJSON_CreateObject();
	if (!root || !bindings || !normal || !hyper) {
		cJSON_Delete(root);
		cJSON_Delete(bindings);
		cJSON_Delete(normal);
		cJSON_Delete(hyper);
		naga_close();
		return fail("out of memory");
	}
	cJSON_AddItemToObject(root, "device", cJSON_CreateString("2020"));
	cJSON_AddItemToObject(bindings, "normal", normal);
	cJSON_AddItemToObject(bindings, "hypershift", hyper);
	cJSON_AddItemToObject(root, "bindings", bindings);

	for (size_t i = 0; i < naga_ctrls_count; i++) {
		const naga_ctrl *ctrl = &naga_ctrls[i];

		for (int layer = 0; layer <= 1; layer++) {
			naga_rec rec;
			struct remap_binding b = {0};
			char value[96];
			cJSON *target = layer ? hyper : normal;

			if (naga_get_rec_layer(ctrl->ctr, (unsigned)layer,
					       &rec) != NAGA_OK ||
			    !rec.valid)
				continue;
			if (rec.type != NAGA_ASSIGN_KEY &&
			    rec.type != NAGA_ASSIGN_MOUSE &&
			    rec.type != NAGA_ASSIGN_HYPERSHIFT &&
			    rec.type != NAGA_ASSIGN_TURBO &&
			    rec.type != NAGA_ASSIGN_DEFAULT)
				continue;
			b.type = rec.type;
			b.usage = rec.usage;
			b.mod = rec.mod;
			b.mouse = rec.mouse;
			if (rec.type == NAGA_ASSIGN_TURBO && rec.rate)
				b.turbo_ms = 1000000u / rec.rate;
			if (rec.type == NAGA_ASSIGN_TURBO && !b.turbo_ms)
				continue;
			remap_binding_string(&b, value, sizeof value);
			cJSON_AddItemToObject(target, ctrl->name,
					      cJSON_CreateString(value));
		}
	}

	if (output) {
		out = fopen(output, "w");
		if (!out) {
			cJSON_Delete(root);
			naga_close();
			snprintf(err, sizeof err, "%s: cannot open", output);
			return fail(err);
		}
	}
	text = cJSON_Print(root);
	if (!text) {
		if (out != stdout)
			fclose(out);
		cJSON_Delete(root);
		naga_close();
		return fail("cannot write JSON");
	}
	fputs(text, out);
	fputc('\n', out);
	cJSON_free(text);
	if (out != stdout)
		fclose(out);
	cJSON_Delete(root);
	naga_close();
	return 0;
}

static int cmd_run(const char *path, const char *evdev,
		   enum remap_device override)
{
	struct remap_map map;
	char err[256] = "";
	size_t host_bindings = 0;
	int rc;

	if (remap_map_load(path, &map, err, sizeof err) < 0)
		return fail(err);
	if (prepare_map(&map, override, evdev, err, sizeof err) < 0) {
		remap_map_free(&map);
		return fail(err);
	}
	for (size_t i = 0; i < map.count; i++)
		if (map.bindings[i].host)
			host_bindings++;

	if (map.device == REMAP_DEVICE_2020) {
		if (remap_apply(&map, false, err, sizeof err) < 0) {
			remap_map_free(&map);
			return fail(err);
		}
		if (!host_bindings) {
			printf("no host-side bindings; onboard bindings are applied\n");
			remap_map_free(&map);
			return 0;
		}
	}
	print_summary(&map);
	rc = remap_run(&map, map.device, evdev, err, sizeof err);
	if (rc < 0) {
		remap_map_free(&map);
		return fail(err);
	}
	if (rc == 1)
		printf("SIGHUP received; use `daemon` to reload in place\n");
	remap_map_free(&map);
	return 0;
}

static int cmd_daemon(const char *config, const char *evdev,
		      enum remap_device override)
{
	char err[256] = "";
	char default_path[REMAP_PATH_MAX];
	bool have_pid = false;
	int status = 0;

	if (!config) {
		remap_config_path(default_path, sizeof default_path);
		config = default_path;
	}
	if (remap_ensure_config_dir(err, sizeof err) < 0)
		return fail(err);
	if (remap_write_pid(err, sizeof err) < 0)
		return fail(err);
	have_pid = true;
	printf("naga-remap: daemon watching %s\n", config);
	fflush(stdout);

	for (;;) {
		struct remap_map map;
		char load_err[256] = "";
		int rc;

		if (remap_map_load(config, &map, load_err,
				   sizeof load_err) < 0) {
			fprintf(stderr, "naga-remap: %s\n", load_err);
			status = 1;
			break;
		}
		if (prepare_map(&map, override, evdev, load_err,
				sizeof load_err) < 0) {
			fprintf(stderr, "naga-remap: %s\n", load_err);
			remap_map_free(&map);
			status = 1;
			break;
		}
		if (map.device == REMAP_DEVICE_2020 &&
		    remap_apply(&map, false, load_err, sizeof load_err) < 0) {
			fprintf(stderr, "naga-remap: %s\n", load_err);
			remap_map_free(&map);
			status = 1;
			break;
		}
		print_summary(&map);
		fflush(stdout);
		rc = remap_run(&map, map.device, evdev, load_err,
			       sizeof load_err);
		remap_map_free(&map);
		if (rc == 1) {
			printf("naga-remap: reloading %s\n", config);
			fflush(stdout);
			continue;
		}
		if (rc < 0) {
			fprintf(stderr, "naga-remap: %s\n", load_err);
			status = 1;
		}
		break;
	}

	if (have_pid)
		remap_remove_pid();
	return status;
}

int main(int argc, char **argv)
{
	const char *command;
	const char *path = NULL;
	const char *evdev = NULL;
	const char *output = NULL;
	const char *config = NULL;
	bool dry_run = false;
	enum remap_device model = REMAP_DEVICE_AUTO;

	if (argc < 2) {
		usage(stderr, argv[0]);
		return 1;
	}
	command = argv[1];

	if (!strcmp(command, "help") || !strcmp(command, "-h") ||
	    !strcmp(command, "--help")) {
		usage(stdout, argv[0]);
		return 0;
	}
	if (!strcmp(command, "devices")) {
		remap_list_devices();
		return 0;
	}

	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--dry-run")) {
			dry_run = true;
		} else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
			evdev = argv[++i];
		} else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
			if (parse_model(argv[++i], &model) < 0) {
				fprintf(stderr, "naga-remap: unknown model\n");
				return 1;
			}
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
			output = argv[++i];
		} else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			config = argv[++i];
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "naga-remap: unknown option %s\n",
				argv[i]);
			return 1;
		} else if (!path) {
			path = argv[i];
		} else {
			fprintf(stderr, "naga-remap: unexpected argument %s\n",
				argv[i]);
			return 1;
		}
	}

	if (!strcmp(command, "validate")) {
		if (!path)
			return fail("validate needs a map file");
		return cmd_validate(path, model);
	}
	if (!strcmp(command, "apply")) {
		if (!path)
			return fail("apply needs a map file");
		return cmd_apply(path, dry_run, model);
	}
	if (!strcmp(command, "dump"))
		return cmd_dump(output);
	if (!strcmp(command, "run")) {
		if (!path)
			return fail("run needs a map file");
		return cmd_run(path, evdev, model);
	}
	if (!strcmp(command, "daemon"))
		return cmd_daemon(config ? config : path, evdev, model);

	usage(stderr, argv[0]);
	return 1;
}
