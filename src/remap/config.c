/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/config.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static const char *home_dir(void)
{
	const char *home = getenv("HOME");

	return home && *home ? home : ".";
}

void remap_config_path(char *buf, size_t len)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");

	if (xdg && *xdg)
		snprintf(buf, len, "%s/naga-remap/config.json", xdg);
	else
		snprintf(buf, len, "%s/.config/naga-remap/config.json",
			 home_dir());
}

void remap_pid_path(char *buf, size_t len)
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");

	if (runtime && *runtime)
		snprintf(buf, len, "%s/naga-remap.pid", runtime);
	else
		snprintf(buf, len, "/tmp/naga-remap-%ld.pid", (long)getuid());
}

static int mkdir_p(char *path)
{
	char *p;

	for (p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, 0700) < 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}
	if (mkdir(path, 0700) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

int remap_ensure_config_dir(char *err, size_t errlen)
{
	char path[REMAP_PATH_MAX];
	char *slash;

	remap_config_path(path, sizeof path);
	slash = strrchr(path, '/');
	if (!slash) {
		seterr(err, errlen, "bad config path");
		return -1;
	}
	*slash = '\0';
	if (mkdir_p(path) < 0) {
		seterr(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int remap_write_pid(char *err, size_t errlen)
{
	char path[REMAP_PATH_MAX];
	FILE *fp;

	remap_pid_path(path, sizeof path);
	fp = fopen(path, "w");
	if (!fp) {
		seterr(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	fprintf(fp, "%ld\n", (long)getpid());
	fclose(fp);
	if (chmod(path, 0600) < 0) {
		seterr(err, errlen, "%s: %s", path, strerror(errno));
		unlink(path);
		return -1;
	}
	return 0;
}

void remap_remove_pid(void)
{
	char path[REMAP_PATH_MAX];

	remap_pid_path(path, sizeof path);
	unlink(path);
}

int remap_signal_daemon(char *err, size_t errlen)
{
	char path[REMAP_PATH_MAX];
	FILE *fp;
	long pid;

	remap_pid_path(path, sizeof path);
	fp = fopen(path, "r");
	if (!fp) {
		seterr(err, errlen, "daemon is not running (no %s)", path);
		return -1;
	}
	if (fscanf(fp, "%ld", &pid) != 1 || pid <= 1) {
		fclose(fp);
		seterr(err, errlen, "%s: bad PID file", path);
		return -1;
	}
	fclose(fp);
	if (kill((pid_t)pid, SIGHUP) < 0) {
		if (errno == ESRCH)
			seterr(err, errlen, "daemon is not running");
		else
			seterr(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}
