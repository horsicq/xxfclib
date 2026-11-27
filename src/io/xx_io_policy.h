/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Internal filesystem policy boundaries. */
#ifndef XX_IO_POLICY_H
#define XX_IO_POLICY_H
#include "xxfclib/io/xx_io.h"
bool xx_io_policy_mutation_allowed(void);
bool xx_io_policy_file_open_allowed(const char *mode);
xx_io_device *xx_io_memory_temp_open(void);
bool xx_io_memory_temp_is_device(const xx_io_device *device);
/* The calling thread's innermost extraction scope (xx_io_extract_begin): its
 * root, or NULL outside one; and a failed filesystem change, kept as an errno
 * value when it is the first since the scope's last clear. */
const char *xx_io_extract_root(void);
void xx_io_extract_fail(int error);
/* A file opened for writing (counted by the innermost extraction scope). */
void xx_io_extract_opened(void);
/* Puts back the failure kept before (xx_io_extract_errno): one that a later
 * call recorded was no failure of the member. */
void xx_io_extract_restore(int error);
/* A new stage file for `destination` at `candidate`, a name in the same
 * folder, made by exclusive creation. A candidate that names the
 * destination itself (also as a file system that folds case or drops
 * trailing dots and spaces reads it) or that is taken is skipped without a
 * recorded failure: NULL with *retry true, and the caller tries its next
 * name. NULL with *retry false is a failure of the folder, recorded. */
xx_io_device *xx_io_stage_open(const char *destination, const char *candidate, bool *retry);
#endif
