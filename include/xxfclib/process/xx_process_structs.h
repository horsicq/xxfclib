/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_STRUCTS_H
#define XX_PROCESS_STRUCTS_H
#include "xxfclib/formats/xx_format.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_process_structs_status {
    XX_PROCESS_STRUCTS_OK = 0,
    XX_PROCESS_STRUCTS_INVALID_ARGUMENT,
    XX_PROCESS_STRUCTS_OUT_OF_MEMORY,
    XX_PROCESS_STRUCTS_UNAVAILABLE,
    XX_PROCESS_STRUCTS_IO_ERROR,
    XX_PROCESS_STRUCTS_CHANGED,
    XX_PROCESS_STRUCTS_UNSUPPORTED
} xx_process_structs_status;

/* Field descriptions reuse the file-format metadata. All strings referenced
 * by desc are static. Raw/value/comment belong to the snapshot. Addresses are
 * unsigned and explicitly optional; query results have no target address. */
typedef struct xx_process_struct_field {
    xx_data_struct_field_desc desc;
    uint64_t address;
    bool has_address, available;
    char raw[160], value[256], comment[192];
} xx_process_struct_field;

typedef struct xx_process_structure {
    char name[96], layout[96];
    uint64_t address, size, thread_id;
    bool has_address;
    xx_process_struct_field *fields;
    size_t field_count;
} xx_process_structure;

typedef struct xx_process_structs_snapshot {
    uint64_t pid, start_time;
    unsigned pointer_size; /* Target width in bytes: 4/8, or 0 if unknown. Query records can still be present. */
    char name[256], path[4096], architecture[32], warning[1024];
    bool truncated;
    xx_process_structure *structures;
    size_t structure_count;
} xx_process_structs_snapshot;

typedef struct xx_process_structs_options {
    /* Zero means unlimited. Positive values opt into partial discovery. */
    size_t max_structures, max_threads, max_modules;
} xx_process_structs_options;

/* Read-only inspection of a native process. PID zero means self.
 * expected_start_time==0 accepts the identity observed at opening; otherwise
 * uses the same OS identity token as xxwidgets_process_info. The target is not
 * suspended: fields form a best-effort snapshot, not an atomic process image.
 * Unknown private layouts are not guessed. Inaccessible fields stay explicitly
 * unavailable. A successful partial result reports limitations in warning.
 * NULL options collects all discoverable structures, threads and modules.
 * Each zero limit is unlimited; positive limits bound discovery and omitted
 * records set truncated. Allocation failures return OUT_OF_MEMORY.
 * The caller owns *out and releases it with xx_process_structs_free. */
XXFC_API xx_process_structs_status xx_process_structs_read(uint64_t pid, uint64_t expected_start_time, const xx_process_structs_options *options,
                                                           xx_process_structs_snapshot **out);
XXFC_API void xx_process_structs_free(xx_process_structs_snapshot *);
XXFC_API const char *xx_process_structs_status_string(xx_process_structs_status);
#ifdef __cplusplus
}
#endif
#endif
