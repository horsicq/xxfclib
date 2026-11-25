/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_STRUCTS_INTERNAL_H
#define XX_PROCESS_STRUCTS_INTERNAL_H
#include "xxfclib/process/xx_process_structs.h"
#include "xxfclib/io/xx_io.h"
#include <stdarg.h>

typedef struct xx_ps_context {
    xx_process_structs_snapshot *snapshot;
    xx_process_structs_options limits;
    size_t structure_capacity;
    xx_process_structs_status status;
} xx_ps_context;

xx_process_structure *xx_ps_add(xx_ps_context *, const char *name, const char *layout, uint64_t address, bool has_address, uint64_t size, uint64_t thread_id);
bool xx_ps_uint(xx_ps_context *, xx_process_structure *, const wchar_t *name, const wchar_t *type, uint64_t offset, size_t size,
                xx_data_struct_record_property_t properties, uint64_t value, bool available);
bool xx_ps_bytes(xx_ps_context *, xx_process_structure *, const wchar_t *name, const wchar_t *type, uint64_t offset, size_t size,
                 xx_data_struct_record_property_t properties, const void *bytes, bool available);
bool xx_ps_decode(xx_ps_context *, xx_process_structure *, const xx_data_struct_field_desc *fields, size_t count, const void *bytes, size_t length, bool big_endian);
void xx_ps_warn(xx_ps_context *, const char *format, ...);
/* Never dereferences a target pointer locally. Bounded native read only. */
bool xx_ps_read(xx_io_device *, uint64_t address, void *bytes, size_t size);
xx_process_structs_status xx_ps_collect(xx_ps_context *, uint64_t pid, uint64_t expected_start_time);
#endif
