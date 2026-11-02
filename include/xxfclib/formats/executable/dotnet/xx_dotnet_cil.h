/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_DOTNET_CIL_H
#define XXFCLIB_DOTNET_CIL_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DOTNET_CIL_MAX_CODE UINT32_C(0x1000000)
#define XX_DOTNET_CIL_MAX_SWITCH UINT32_C(0x10000)

typedef enum xx_dotnet_cil_status {
    XX_DOTNET_CIL_OK = 0,
    XX_DOTNET_CIL_INVALID_ARGUMENT,
    XX_DOTNET_CIL_TRUNCATED,
    XX_DOTNET_CIL_INVALID_OPCODE,
    XX_DOTNET_CIL_SWITCH_LIMIT,
    XX_DOTNET_CIL_CAPACITY,
    XX_DOTNET_CIL_NOT_BRANCH,
    XX_DOTNET_CIL_TARGET_INDEX,
    XX_DOTNET_CIL_INVALID_TARGET,
    XX_DOTNET_CIL_INVALID_SPAN,
    XX_DOTNET_CIL_BRANCH_IN_SPAN
} xx_dotnet_cil_status;

typedef struct xx_dotnet_cil_instruction {
    uint32_t offset;        /* Method-relative opcode offset. */
    uint32_t opcode;        /* 0xfe00 + second byte for prefixed opcodes. */
    uint32_t width;         /* Complete instruction size. */
    uint32_t operand;       /* Method-relative operand offset. */
    uint32_t operand_width;
    uint32_t value;         /* Unsigned four-byte operand, zero otherwise. */
    uint32_t target_count;  /* One relative branch, switch count, or zero. */
} xx_dotnet_cil_instruction;

/* No instruction executes, no CLR is loaded, and these functions allocate no
 * memory. Buffers are borrowed. Nonempty input requires a non-NULL code buffer;
 * NULL/zero-length input is accepted by validate as an empty IL sequence.
 * A successful decode checks opcode/operand extent, not stack or token types.
 * Failure clears supplied instruction/target outputs. */
XXFC_API const char *xx_dotnet_cil_status_string(xx_dotnet_cil_status status);
/* Operand size: -1 invalid, -2 switch, otherwise 0/1/2/4/8 bytes. */
XXFC_API int xx_dotnet_cil_operand_width(unsigned opcode_byte, bool prefixed);
XXFC_API bool xx_dotnet_cil_is_branch(uint32_t opcode);
XXFC_API xx_dotnet_cil_status xx_dotnet_cil_decode(const uint8_t *code,
    size_t length, uint32_t offset, xx_dotnet_cil_instruction *instruction);
/* Decode a relative branch/switch target. Signed output can identify an invalid
 * negative destination; validate additionally requires instruction boundaries.
 * target_index must be zero for an ordinary branch. */
XXFC_API xx_dotnet_cil_status xx_dotnet_cil_target(const uint8_t *code,
    size_t length, uint32_t offset, uint32_t target_index, int64_t *target);
/* boundary_map is writable caller-owned scratch, one byte per input byte,
 * disjoint from code. Each instruction start is 1 and other bytes are 0.
 * Empty input permits a NULL map. The map may be partial on failure and must
 * not be treated as valid unless this returns OK. */
XXFC_API xx_dotnet_cil_status xx_dotnet_cil_validate(const uint8_t *code,
    size_t length, uint8_t *boundary_map, size_t map_capacity);
/* Revalidate the original bytes and require [start,end) to be nonempty and
 * instruction-aligned, with no branch/switch destination strictly inside it.
 * EH policy belongs to the caller, which owns the method-body context. */
XXFC_API xx_dotnet_cil_status xx_dotnet_cil_safe_span(const uint8_t *code,
    size_t length, uint32_t start, uint32_t end,
    uint8_t *boundary_map, size_t map_capacity);

#ifdef __cplusplus
}
#endif
#endif
