/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_protobuf_wire.h
 * @brief Protobuf wire-format primitives shared by the format readers.
 *
 * Several readers decode protobuf wire data: the CRX proof/signed-header
 * blocks, the ZIP module-output cache, the scanner's module-output
 * validation and the vt/output module-data decoders. Each used to carry its
 * own copy of a varint loop and a field skipper; the copies disagreed on
 * details, so each distinct behavior has its own primitive here, transcribed
 * from the consumer it serves:
 *
 * - xx_protobuf_wire_varint(): the canonical 64-bit varint of rust-protobuf
 *   3.7.2 (at most 10 bytes, the 10th byte may only carry bit 0). Every
 *   consumer's loop is behaviorally identical to this one.
 * - xx_protobuf_wire_varint32(): the 32-bit length varint of rust-protobuf
 *   3.7.2 (at most 5 bytes, the 5th byte may only carry bits 0..3), used for
 *   length-delimited field headers.
 * - xx_protobuf_wire_skip_field_vt(): lenient field skip (depth limit 64,
 *    unvalidated tags, any END_GROUP ends a group) for the vt module-data
 *    walker.
 * - xx_protobuf_wire_skip_field_crx(): validated field skip (rejects field
 *   number 0 and numbers above 0x1fffffff, depth limit 500) for CRX proofs.
 * - xx_protobuf_wire_skip_field_zip(): field skip whose END_GROUP must match
 *   the opening field number (depth limit 64) for the ZIP output cache.
 * - xx_protobuf_wire_skip_field_checked(): field skip reporting the exact
 *   rust-protobuf 3.7.2 error class ("Unexpected EOF" vs "Incorrect varint"
 *   vs "Truncated message" vs "Recursion limit exceeded" vs "Unexpected wire
 *   type" vs "Incorrect tag") for the module-output validator, including its
 *   logical-size limit and 500-deep recursion cap.
 *
 * All functions are header-only so that translation units named in the
 * per-family build lists (die_engine.cmake, xxfclib_zip.cmake,
 * xxfclib_family_sources.cmake, xxfclib_dotnet_emul.cmake) can use them
 * without gaining a link dependency on a new compilation unit.
 */

#ifndef XXFCLIB_ALGO_PROTOBUF_XX_PROTOBUF_WIRE_H
#define XXFCLIB_ALGO_PROTOBUF_XX_PROTOBUF_WIRE_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical 64-bit varint, rust-protobuf 3.7.2 semantics: at most 10 bytes,
 * and the tenth byte may only be 0 or 1. On failure *at is left past every
 * byte that was consumed, exactly like the loops this replaces. */
static inline bool xx_protobuf_wire_varint(const uint8_t *data, size_t size,
                                           size_t *at, uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        uint8_t byte;
        if (*at >= size) {
            *value = 0;
            return false;
        }
        byte = data[(*at)++];
        if (shift == 63 && byte > 1) {
            *value = 0;
            return false;
        }
        result |= (uint64_t)(byte & 0x7fU) << shift;
        if (!(byte & 0x80U)) {
            *value = result;
            return true;
        }
    }
    *value = 0;
    return false;
}

/* 32-bit length varint, rust-protobuf 3.7.2 semantics: at most 5 bytes, and
 * the fifth byte may only carry bits 0..3. */
static inline bool xx_protobuf_wire_varint32(const uint8_t *data, size_t size,
                                             size_t *at, uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
        uint8_t byte;
        if (*at >= size) {
            *value = 0;
            return false;
        }
        byte = data[(*at)++];
        if (i == 4 && byte > 0x0f) {
            *value = 0;
            return false;
        }
        result |= (uint64_t)(byte & 0x7fU) << (7 * i);
        if (!(byte & 0x80U)) {
            *value = result;
            return true;
        }
    }
    *value = 0;
    return false;
}

/* Lenient field skip used by the vt module-data walker: wire types 0, 1, 2,
 * 3 and 5, recursion past depth 64 fails, tags are not validated, and any
 * END_GROUP wire type ends a group. */
static inline bool xx_protobuf_wire_skip_field_vt(const uint8_t *data,
                                                  size_t size, size_t *at,
                                                  unsigned wire,
                                                  unsigned depth)
{
    uint64_t n;
    if (depth > 64) return false;
    switch (wire) {
    case 0:
        return xx_protobuf_wire_varint(data, size, at, &n);
    case 1:
        if (size - *at < 8) return false;
        *at += 8;
        return true;
    case 2:
        if (!xx_protobuf_wire_varint(data, size, at, &n) || n > size - *at)
            return false;
        *at += (size_t)n;
        return true;
    case 3:
        while (*at < size) {
            if (!xx_protobuf_wire_varint(data, size, at, &n)) return false;
            if ((n & 7) == 4) return true;
            if (!xx_protobuf_wire_skip_field_vt(data, size, at,
                                                (unsigned)(n & 7), depth + 1))
                return false;
        }
        return false;
    case 4:
        return true;
    case 5:
        if (size - *at < 4) return false;
        *at += 4;
        return true;
    default:
        return false;
    }
}

/* Validated field skip used by the CRX reader: rejects field number 0 and
 * field numbers above 0x1fffffff, recursion past depth 500 fails, and any
 * END_GROUP wire type ends a group. */
static inline bool xx_protobuf_wire_skip_field_crx(const uint8_t *data,
                                                   size_t size, size_t *at,
                                                   unsigned depth)
{
    if (depth > 500) return false;
    while (*at < size) {
        uint64_t tag, value;
        unsigned wire;
        if (!xx_protobuf_wire_varint(data, size, at, &tag) ||
            (tag >> 3) == 0 || (tag >> 3) > 0x1fffffffU)
            return false;
        wire = (unsigned)(tag & 7);
        if (wire == 4) return true;
        if (wire == 0) {
            if (!xx_protobuf_wire_varint(data, size, at, &value)) return false;
        } else if (wire == 1 || wire == 5) {
            size_t width = wire == 1 ? 8 : 4;
            if (width > size - *at) return false;
            *at += width;
        } else if (wire == 2) {
            if (!xx_protobuf_wire_varint(data, size, at, &value) ||
                value > size - *at)
                return false;
            *at += (size_t)value;
        } else if (wire == 3) {
            if (!xx_protobuf_wire_skip_field_crx(data, size, at, depth + 1))
                return false;
        } else {
            return false;
        }
    }
    return false;
}

/* Field skip used by the ZIP module-output cache: wire type 4 ends a group
 * only when its field number matches the group's, recursion at depth 64 or
 * deeper fails, tags are not validated. */
static inline bool xx_protobuf_wire_skip_field_zip(const uint8_t *data,
                                                   size_t size, size_t *at,
                                                   uint64_t field,
                                                   unsigned wire,
                                                   unsigned depth)
{
    uint64_t value;
    switch (wire) {
    case 0:
        return xx_protobuf_wire_varint(data, size, at, &value);
    case 1:
        if (size - *at < 8) return false;
        *at += 8;
        return true;
    case 2:
        if (!xx_protobuf_wire_varint(data, size, at, &value) ||
            value > size - *at)
            return false;
        *at += (size_t)value;
        return true;
    case 3:
        if (depth >= 64) return false;
        while (*at < size) {
            uint64_t key;
            if (!xx_protobuf_wire_varint(data, size, at, &key)) return false;
            if ((key & 7) == 4) return (key >> 3) == field;
            if (!xx_protobuf_wire_skip_field_zip(data, size, at, key >> 3,
                                                 (unsigned)(key & 7),
                                                 depth + 1))
                return false;
        }
        return false;
    case 5:
        if (size - *at < 4) return false;
        *at += 4;
        return true;
    default:
        return false;
    }
}

/* Error classes of the module-output field skip, named after the
 * rust-protobuf 3.7.2 texts the validator reports. */
typedef enum {
    XX_PROTOBUF_WIRE_SKIP_OK = 0,
    XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF,
    XX_PROTOBUF_WIRE_SKIP_INCORRECT_VARINT,
    XX_PROTOBUF_WIRE_SKIP_TRUNCATED_MESSAGE,
    XX_PROTOBUF_WIRE_SKIP_RECURSION_LIMIT,
    XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_WIRE,
    XX_PROTOBUF_WIRE_SKIP_INCORRECT_TAG
} xx_protobuf_wire_skip_code;

/* Field skip with the module-output validator's exact error classes:
 * distinguishes "Unexpected EOF" (ran out of input fewer than 10 bytes into
 * a varint) from "Incorrect varint", applies the logical-size limit before
 * the buffer limit ("Truncated message"), validates tags inside groups,
 * recursion past depth 500 is "Recursion limit exceeded", wire type 4 is
 * "Unexpected wire type". */
static inline xx_protobuf_wire_skip_code xx_protobuf_wire_skip_field_checked(
    const uint8_t *data, size_t size, size_t *at, unsigned wire,
    unsigned depth, uint64_t logical_size)
{
    uint64_t n;
    size_t start = *at;
    if (depth > 500) return XX_PROTOBUF_WIRE_SKIP_RECURSION_LIMIT;
    switch (wire) {
    case 0:
        if (xx_protobuf_wire_varint(data, size, at, &n))
            return XX_PROTOBUF_WIRE_SKIP_OK;
        return *at == size && *at - start < 10
                   ? XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF
                   : XX_PROTOBUF_WIRE_SKIP_INCORRECT_VARINT;
    case 1:
        if (size - *at < 8) return XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF;
        *at += 8;
        return XX_PROTOBUF_WIRE_SKIP_OK;
    case 2:
        if (!xx_protobuf_wire_varint(data, size, at, &n))
            return *at == size && *at - start < 10
                       ? XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF
                       : XX_PROTOBUF_WIRE_SKIP_INCORRECT_VARINT;
        if (n > logical_size - *at) return XX_PROTOBUF_WIRE_SKIP_TRUNCATED_MESSAGE;
        if (n > size - *at) return XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF;
        *at += (size_t)n;
        return XX_PROTOBUF_WIRE_SKIP_OK;
    case 3:
        while (*at < size) {
            start = *at;
            if (!xx_protobuf_wire_varint(data, size, at, &n))
                return *at == size && *at - start < 10
                           ? XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF
                           : XX_PROTOBUF_WIRE_SKIP_INCORRECT_VARINT;
            if (!(n >> 3) || n >> 3 > 0x1fffffffU || (n & 7) > 5)
                return XX_PROTOBUF_WIRE_SKIP_INCORRECT_TAG;
            if ((n & 7) == 4) return XX_PROTOBUF_WIRE_SKIP_OK;
            {
                xx_protobuf_wire_skip_code result =
                    xx_protobuf_wire_skip_field_checked(data, size, at,
                                                        (unsigned)(n & 7),
                                                        depth + 1,
                                                        logical_size);
                if (result != XX_PROTOBUF_WIRE_SKIP_OK) return result;
            }
        }
        return XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF;
    case 4:
        return XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_WIRE;
    case 5:
        if (size - *at < 4) return XX_PROTOBUF_WIRE_SKIP_UNEXPECTED_EOF;
        *at += 4;
        return XX_PROTOBUF_WIRE_SKIP_OK;
    default:
        return XX_PROTOBUF_WIRE_SKIP_INCORRECT_TAG;
    }
}

#ifdef __cplusplus
}
#endif

#endif
