/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_CONFIG_H
#define NAGA_REMAP_CONFIG_H

#include <stddef.h>

#define REMAP_PATH_MAX 512

/* $XDG_CONFIG_HOME/naga-remap/config.json (or ~/.config/...). */
void remap_config_path(char *buf, size_t len);

/* $XDG_RUNTIME_DIR/naga-remap.pid (or /tmp/naga-remap-<uid>.pid). */
void remap_pid_path(char *buf, size_t len);

/* Create the config directory. Returns 0 or -1 with err set. */
int remap_ensure_config_dir(char *err, size_t errlen);

/* Daemon PID file helpers. */
int remap_write_pid(char *err, size_t errlen);
void remap_remove_pid(void);

/* Send SIGHUP to the running daemon. 0 on success, -1 with err set. */
int remap_signal_daemon(char *err, size_t errlen);

#endif /* NAGA_REMAP_CONFIG_H */
