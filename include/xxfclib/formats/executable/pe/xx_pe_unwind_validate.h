/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_PE_UNWIND_VALIDATE_H
#define XXFCLIB_PE_UNWIND_VALIDATE_H

#include "xxfclib/formats/executable/pe/xx_pe_inspect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XX_PE_AMD64_UNWIND_VALID = 0,
    XX_PE_AMD64_UNWIND_INCOMPLETE_ENTRY = 1,
    XX_PE_AMD64_UNWIND_UNMAPPED_BODY = 2,
    XX_PE_AMD64_UNWIND_INVALID_UNWIND = 3
} xx_pe_amd64_unwind_status;

typedef struct {
    xx_pe_amd64_unwind_status status;
    uint32_t entry_index;
    uint32_t begin_rva;
    uint32_t end_rva;
    uint32_t unwind_rva;
} xx_pe_amd64_unwind_result;

typedef enum {
    XX_PE_GUARD_CF_VALID = 0,
    XX_PE_GUARD_CF_OUT_OF_ORDER = 1,
    XX_PE_GUARD_CF_NOT_EXECUTABLE = 2
} xx_pe_guard_cf_status;

typedef struct {
    xx_pe_guard_cf_status status;
    uint32_t entry_index;
    uint32_t function_rva;
} xx_pe_guard_cf_result;

/* UNWIND_INFO flags, as the AMD64 exception data defines them. */
#define XX_PE_AMD64_UNW_EHANDLER   0x01u
#define XX_PE_AMD64_UNW_UHANDLER   0x02u
#define XX_PE_AMD64_UNW_CHAININFO  0x04u

typedef struct {
    uint32_t begin_rva;
    uint32_t end_rva;
    uint32_t unwind_rva;
    /* What the entry's UNWIND_INFO declares, when it is readable:
     * unwind_read is 0 when it is not, and the fields below are then 0. */
    uint32_t primary_begin_rva;  /* The entry this one continues, or its own begin. */
    uint32_t frame_allocation;   /* Bytes the prologue subtracts from RSP. */
    uint16_t prolog_size;        /* Bytes of prologue the unwind data covers. */
    uint8_t unwind_version;
    uint8_t unwind_flags;        /* XX_PE_AMD64_UNW_*. */
    uint8_t frame_register;      /* 0 when the frame is RSP-relative, else the register. */
    uint8_t frame_offset;        /* Scaled by 16, as the format stores it. */
    uint8_t pushed_registers;    /* Count of UWOP_PUSH_NONVOL in the prologue. */
    uint8_t unwind_read;
} xx_pe_amd64_function;

/* The AMD64 exception directory's own statement of where each function begins
 * and ends: one RUNTIME_FUNCTION record per entry, in table order.
 *
 * Reads at most capacity entries into functions and reports the number the
 * directory describes in *total, so a caller can size its own array and read
 * again. A trailing all-zero record ends the table, as some linkers pad it.
 *
 * Each entry also carries what its UNWIND_INFO declares: the flags, the
 * prologue's length, the frame register where one is established, the bytes
 * the prologue allocates, and -- for a chained entry -- the primary entry it
 * continues. A chained entry describes a *part* of a function rather than a
 * function of its own, which a caller walking the table as a function list
 * must account for; primary_begin_rva follows the chain to the entry that
 * does begin the function. Where the unwind data is not readable, unwind_read
 * is 0 and those fields are zero rather than guessed; begin and end still
 * state what the directory says.
 *
 * device offsets must be relative to the same PE image as pe->map. Neither the
 * device cursor nor the inspection is changed. Returns 1 when the directory
 * was read, 0 when it is absent, not file-backed, or malformed (an entry whose
 * end does not follow its begin). This reports what the table says; use
 * xx_pe_inspect_validate_amd64_unwind to judge whether it is well formed. */
XXFC_API int xx_pe_inspect_amd64_functions(const xx_pe_inspection *pe, xx_io_device *device,
                                          xx_pe_amd64_function *functions, uint32_t capacity,
                                          uint32_t *total);

/* Implements the AMD64 exception-table checks in DIE's generic heuristic,
 * including its permissive DWORD-aligned pointers and strict WORD-aligned
 * version-1 compatibility path. This is not a general Windows ABI verifier.
 *
 * device offsets must be relative to the same PE image as pe->map. Neither
 * the device cursor nor the inspection is changed. The caller establishes
 * AMD64 and optional-header directory-slot presence before calling.
 *
 * Returns 1 when validation completed, with the first failure in result.
 * Returns 0 when unavailable (invalid input, non-file-backed directory,
 * unsupported address precision, allocation or I/O failure); callers must
 * use their existing validator, not interpret this as valid metadata.
 * Entry fields are zero for VALID and INCOMPLETE_ENTRY. */
XXFC_API int xx_pe_inspect_validate_amd64_unwind(const xx_pe_inspection *pe, xx_io_device *device,
                                               uint32_t directory_rva, uint32_t directory_size,
                                               xx_pe_amd64_unwind_result *result);

/* Implements the Guard CF function-table checks in DIE's generic heuristic.
 * Each entry begins with a little-endian function RVA and may contain up to
 * fifteen bytes of GuardFlags-defined metadata. Equal RVAs are allowed; a
 * descending RVA or an RVA outside file-backed executable code is reported.
 *
 * The table is streamed in entry-aligned chunks and neither the device cursor
 * nor the inspection is changed. Returns 1 when validation completed, with
 * the first failure in result. Returns 0 when native validation is unavailable
 * (invalid input, table mapping, address precision, allocation or I/O); script
 * callers must then use their existing validator. */
XXFC_API int xx_pe_inspect_validate_guard_cf_function_table(const xx_pe_inspection *pe,
                                                            xx_io_device *device,
                                                            uint32_t table_rva,
                                                            uint32_t entry_count,
                                                            uint32_t entry_size,
                                                            xx_pe_guard_cf_result *result);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_PE_UNWIND_VALIDATE_H */
