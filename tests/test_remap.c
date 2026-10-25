/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/* Unit tests for the naga-remap map parser and event planner. */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

#include "remap/map.h"
#include "remap/plan.h"

static int failures;

#define CHECK(cond)                                                      \
	do {                                                             \
		if (!(cond)) {                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
				#cond);                                  \
			failures++;                                      \
		}                                                        \
	} while (0)

static const char *map_2020 =
    "{\n"
    "  \"device\": \"2020\",\n"
    "  \"bindings\": {\n"
    "    \"normal\": {\n"
    "      \"1\": \"x\",\n"
    "      \"2\": \"ctrl+shift+t\",\n"
    "      \"3\": \"mouse:middle\",\n"
    "      \"4\": \"hypershift\",\n"
    "      \"5\": \"turbo:f5@3000\",\n"
    "      \"6\": \"macro:hello\"\n"
    "    },\n"
    "    \"hypershift\": { \"9\": \"f13\" }\n"
    "  },\n"
    "  \"macros\": {\n"
    "    \"hello\": [\n"
    "      {\"key\": \"a\"},\n"
    "      {\"delay\": 50},\n"
    "      {\"text\": \"Hi\", \"hold_ms\": 10, \"delay_ms\": 20},\n"
    "      {\"mouse\": \"left\"}\n"
    "    ]\n"
    "  },\n"
    "  \"triggers\": { \"normal\": { \"6\": \"f20\" } }\n"
    "}\n";

static const char *map_2014 =
    "{\n"
    "  \"device\": \"2014\",\n"
    "  \"bindings\": {\n"
    "    \"normal\": {\n"
    "      \"1\": \"x\",\n"
    "      \"2\": \"macro:copy_paste\"\n"
    "    }\n"
    "  },\n"
    "  \"macros\": {\n"
    "    \"copy_paste\": [{\"key\": \"ctrl+c\"}, {\"delay\": 10}]\n"
    "  }\n"
    "}\n";

static void load_ok(const char *json, struct remap_map *map)
{
	char err[256] = "";

	if (remap_map_load_string(json, map, err, sizeof err) < 0) {
		fprintf(stderr, "load failed: %s\n", err);
		failures++;
	}
}

static void prepare_ok(struct remap_map *map, enum remap_device model)
{
	char err[256] = "";

	map->device = model;
	if (remap_map_validate(map, err, sizeof err) < 0) {
		fprintf(stderr, "validate failed: %s\n", err);
		failures++;
		return;
	}
	if (remap_map_assign_triggers(map, err, sizeof err) < 0) {
		fprintf(stderr, "trigger assignment failed: %s\n", err);
		failures++;
	}
}

static struct remap_binding *find_binding(struct remap_map *map,
					  const char *id, int layer)
{
	for (size_t i = 0; i < map->count; i++)
		if (!strcmp(map->bindings[i].id, id) &&
		    map->bindings[i].layer == layer)
			return &map->bindings[i];
	return NULL;
}

static void test_2020_parse_and_triggers(void)
{
	struct remap_map map;
	struct remap_binding *b;

	load_ok(map_2020, &map);
	prepare_ok(&map, REMAP_DEVICE_2020);
	if (failures)
		goto out;

	CHECK(map.count == 7);
	CHECK(map.macro_count == 1);
	b = find_binding(&map, "1", 0);
	CHECK(b && b->type == NAGA_ASSIGN_KEY && b->usage == 0x1B); /* x */
	b = find_binding(&map, "2", 0);
	CHECK(b && b->mod == 0x03 && b->usage == 0x17); /* ctrl+shift+t */
	b = find_binding(&map, "3", 0);
	CHECK(b && b->type == NAGA_ASSIGN_MOUSE && b->mouse == NAGA_MOUSE_MIDDLE);
	b = find_binding(&map, "4", 0);
	CHECK(b && b->type == NAGA_ASSIGN_HYPERSHIFT);
	b = find_binding(&map, "5", 0);
	CHECK(b && b->type == NAGA_ASSIGN_TURBO && b->turbo_ms == 3000);
	b = find_binding(&map, "6", 0);
	CHECK(b && b->is_sequence && b->macro &&
	      !strcmp(b->macro, "hello"));
	CHECK(b && b->seq && b->seq->count == 4);
	CHECK(b && b->trigger_usage == 0x6F); /* f20 */
	b = find_binding(&map, "9", 1);
	CHECK(b && b->type == NAGA_ASSIGN_KEY && b->usage == 0x68); /* f13 */

out:
	remap_map_free(&map);
}

static void test_2014_defaults(void)
{
	struct remap_map map;
	struct remap_binding *b;

	load_ok(map_2014, &map);
	prepare_ok(&map, REMAP_DEVICE_2014);
	if (failures)
		goto out;

	CHECK(map.count == 2);
	b = find_binding(&map, "1", 0);
	CHECK(b && !strcmp(b->trigger, "KEY_KP1"));
	b = find_binding(&map, "2", 0);
	CHECK(b && b->is_sequence && b->seq && b->seq->count == 2);
	CHECK(b && !strcmp(b->trigger, "KEY_KP2"));

out:
	remap_map_free(&map);
}

static void test_sequence_plan(void)
{
	struct remap_map map;
	struct remap_binding *b;
	struct remap_plan plan;
	char err[256] = "";

	load_ok(map_2020, &map);
	prepare_ok(&map, REMAP_DEVICE_2020);
	b = find_binding(&map, "6", 0);
	if (!b) {
		failures++;
		goto out;
	}
	if (remap_plan_build(b, &plan, err, sizeof err) < 0) {
		fprintf(stderr, "plan failed: %s\n", err);
		failures++;
		goto out;
	}

	CHECK(plan.count == 10);
	CHECK(plan.duration_ms == 150);
	if (plan.count >= 10) {
		CHECK(plan.events[0].code == KEY_A);
		CHECK(plan.events[0].value == 1);
		CHECK(plan.events[1].code == KEY_A);
		CHECK(plan.events[1].value == 0);
		CHECK(plan.events[2].code == KEY_LEFTSHIFT);
		CHECK(plan.events[2].value == 1);
		CHECK(plan.events[3].code == KEY_H);
		CHECK(plan.events[4].code == KEY_H);
		CHECK(plan.events[4].value == 0);
		CHECK(plan.events[5].code == KEY_LEFTSHIFT);
		CHECK(plan.events[6].code == KEY_I);
		CHECK(plan.events[8].code == BTN_LEFT);
		CHECK(plan.events[9].code == BTN_LEFT);
		CHECK(plan.events[9].value == 0);
	}
	remap_plan_free(&plan);
out:
	remap_map_free(&map);
}

static void test_single_plan(void)
{
	struct remap_map map;
	struct remap_binding *b;
	struct remap_plan plan;
	char err[256] = "";

	load_ok(map_2020, &map);
	prepare_ok(&map, REMAP_DEVICE_2020);
	b = find_binding(&map, "1", 0);
	if (!b || remap_plan_build(b, &plan, err, sizeof err) < 0) {
		failures++;
		goto out;
	}
	CHECK(plan.count == 2);
	CHECK(plan.events[0].code == KEY_X);
	CHECK(plan.events[0].value == 1);
	CHECK(plan.events[1].value == 0);
	remap_plan_free(&plan);
out:
	remap_map_free(&map);
}

static void test_default_trigger(void)
{
	struct remap_map map;
	struct remap_binding *b;
	char err[256] = "";

	load_ok("{\"device\":\"2020\",\"bindings\":{\"normal\":"
		"{\"1\":\"macro:m\"}},\"macros\":{\"m\":[{\"key\":\"a\"}]}}",
		&map);
	map.device = REMAP_DEVICE_2020;
	if (remap_map_validate(&map, err, sizeof err) < 0 ||
	    remap_map_assign_triggers(&map, err, sizeof err) < 0) {
		fprintf(stderr, "default trigger failed: %s\n", err);
		failures++;
		goto out;
	}
	b = find_binding(&map, "1", 0);
	CHECK(b && b->trigger_usage == 0x68 && !strcmp(b->trigger, "f13"));
out:
	remap_map_free(&map);
}

static void expect_load_fail(const char *json)
{
	struct remap_map map;
	char err[256] = "";

	if (remap_map_load_string(json, &map, err, sizeof err) == 0) {
		fprintf(stderr, "unexpectedly parsed: %s\n", json);
		failures++;
		remap_map_free(&map);
	}
}

static void expect_validate_fail(const char *json, enum remap_device model)
{
	struct remap_map map;
	char err[256] = "";

	load_ok(json, &map);
	map.device = model;
	if (remap_map_validate(&map, err, sizeof err) == 0) {
		fprintf(stderr, "unexpectedly validated: %s\n", json);
		failures++;
	}
	remap_map_free(&map);
}

static void test_invalid(void)
{
	expect_load_fail("{\"bindings\":{\"normal\":{\"1\":\"bogus\"}}}");
	expect_load_fail("{\"bindings\":{\"normal\":{\"1\":\"macro:m\"}},"
			 "\"macros\":{\"m\":[]}}");
	expect_load_fail("{\"bindings\":{\"normal\":{\"1\":{\"sequence\":"
			 "[{\"delay\":1}]}}}}");
	expect_load_fail("{\"bindings\":{\"normal\":{\"1\":\"x\"},"
			 "\"back\":{\"1\":\"y\"}}}");
	expect_validate_fail("{\"device\":\"2020\",\"bindings\":{\"normal\":"
			     "{\"99\":\"x\"}}}",
			     REMAP_DEVICE_2020);
	expect_validate_fail("{\"device\":\"2020\",\"bindings\":{\"normal\":"
			     "{\"1\":\"macro:missing\"}}}",
			     REMAP_DEVICE_2020);
	expect_validate_fail("{\"device\":\"2014\",\"bindings\":{\"normal\":"
			     "{\"1\":\"x\"},\"hypershift\":{\"1\":\"y\"}}}",
			     REMAP_DEVICE_2014);
	expect_validate_fail("{\"device\":\"2020\",\"bindings\":{\"normal\":"
			     "{\"1\":\"x\"},\"hypershift\":"
			     "{\"2\":\"turbo:f5@100\"}}}",
			     REMAP_DEVICE_2020);
	expect_validate_fail("{\"device\":\"2020\",\"bindings\":{\"normal\":"
			     "{\"1\":\"x\"}},\"triggers\":{\"normal\":"
			     "{\"1\":\"f13\"}}}",
			     REMAP_DEVICE_2020);
	expect_validate_fail("{\"device\":\"2020\",\"bindings\":{\"normal\":"
			     "{\"1\":\"x\"}},\"triggers\":{\"normal\":"
			     "{\"9\":\"f13\"}}}",
			     REMAP_DEVICE_2020);
}

static void test_key_mapping(void)
{
	CHECK(remap_usage_to_keycode(0x04) == KEY_A);
	CHECK(remap_usage_to_keycode(0x1C) == KEY_Y);
	CHECK(remap_usage_to_keycode(0x1D) == KEY_Z);
	CHECK(remap_usage_to_keycode(0x1E) == KEY_1);
	CHECK(remap_usage_to_keycode(0x28) == KEY_ENTER);
	CHECK(remap_usage_to_keycode(0x68) == KEY_F13);
	CHECK(remap_usage_to_keycode(0xFFFF) == -1);
	CHECK(remap_mouse_to_keycode(NAGA_MOUSE_MIDDLE) == BTN_MIDDLE);
}

int main(void)
{
	test_2020_parse_and_triggers();
	test_2014_defaults();
	test_sequence_plan();
	test_single_plan();
	test_default_trigger();
	test_invalid();
	test_key_mapping();

	if (failures) {
		fprintf(stderr, "%d remap check(s) failed\n", failures);
		return 1;
	}
	printf("all remap tests passed\n");
	return 0;
}
