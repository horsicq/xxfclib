/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
/* Shared internal helpers for the YARA-X-exact PE translation units
 * (xx_pe_yx.c and xx_pe_yx_authenticode.c). Each includer gets its own
 * copy; the functions are tiny and free-standing. */
#ifndef XX_PE_YX_INTERNAL_H
#define XX_PE_YX_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Mirrors the reference module's PE test: 'MZ', e_lfanew inside the buffer,
 * and the NT signature at e_lfanew. */
static inline bool xx_pe_yx_is_pe(const uint8_t *data, size_t size)
{
    uint32_t pe_offset;
    const uint8_t *pe;
    if (!data || size < 64 || data[0] != 'M' || data[1] != 'Z') return false;
    pe_offset = (uint32_t)data[60] | ((uint32_t)data[61] << 8) |
                ((uint32_t)data[62] << 16) | ((uint32_t)data[63] << 24);
    if ((uint64_t)pe_offset > size || size - (size_t)pe_offset < 24) return false;
    pe = data + (size_t)pe_offset;
    return pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0;
}

static inline uint64_t xx_pe_yx_read(const uint8_t *data, size_t offset,
                                     unsigned width)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= (uint64_t)data[offset + i] << (8 * i);
    return value;
}

/* Section table location and clamped count (96 sections, file-bounded), as
 * the reference module computes them. */
static inline size_t xx_pe_yx_sections(const uint8_t *data, size_t size,
                                       size_t pe_offset,
                                       size_t *sections_offset)
{
    size_t offset = pe_offset + 24;
    size_t count;
    uint64_t optional_size = xx_pe_yx_read(data, pe_offset + 20, 2);
    if (optional_size > size - offset) return 0;
    offset += (size_t)optional_size;
    count = (size_t)xx_pe_yx_read(data, pe_offset + 6, 2);
    if (count > 96) count = 96;
    if (count > (size - offset) / 40) count = (size - offset) / 40;
    if (sections_offset) *sections_offset = offset;
    return count;
}

/* Reads a 4-byte optional-header field with the reference module's rules: 0
 * when the optional header is truncated, false only when the input is not a
 * PE. `offset32`/`offset64` are relative to the optional header start. */
static inline bool xx_pe_yx_optional_field32(const uint8_t *data, size_t size,
                                             unsigned offset32,
                                             unsigned offset64,
                                             uint64_t *value)
{
    size_t pe_offset, optional_offset, offset;
    uint16_t magic = 0;
    bool is_64bit, optional_valid;
    if (!value || !xx_pe_yx_is_pe(data, size)) return false;
    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    optional_offset = pe_offset + 24;
    if (size - optional_offset >= 2)
        magic = (uint16_t)xx_pe_yx_read(data, optional_offset, 2);
    is_64bit = magic == 0x20b;
    optional_valid = size - optional_offset >= (is_64bit ? 112u : 96u);
    if (!optional_valid) {
        *value = 0;
        return true;
    }
    offset = optional_offset + (is_64bit ? offset64 : offset32);
    if (offset > size || 4u > size - offset) return false;
    *value = xx_pe_yx_read(data, offset, 4);
    return true;
}

#endif /* XX_PE_YX_INTERNAL_H */
