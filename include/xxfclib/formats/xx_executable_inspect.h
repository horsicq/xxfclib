/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_EXECUTABLE_INSPECT_H
#define XXFCLIB_EXECUTABLE_INSPECT_H
#include "xxfclib/formats/xx_format.h"

/* A bounded read-only view owned by an executable inspection state. Its
 * parent device is borrowed. Inspection offsets are relative to this view,
 * hence relative to the native reader's base_address. */
typedef struct xx_executable_input {
    xx_io_device *device;
    int64_t size;
    xx_pd_struct *pd;
    bool failed;
    bool parsing;
    size_t string_bytes;
    size_t read_work;
} xx_executable_input;
#endif
