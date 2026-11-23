/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ips.h @brief IPS and IPS32 patch format parser, applier and creator. */

#ifndef XXFCLIB_FORMAT_IPS_H
#define XXFCLIB_FORMAT_IPS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_ips_type_e {
    XX_IPS_TYPE_IPS = 0,
    XX_IPS_TYPE_IPS32 = 1
} xx_ips_type;

typedef struct xx_ips_record_s {
    uint64_t offset;
    uint32_t size;        /**< 0 when RLE. */
    uint32_t rle_size;    /**< Number of bytes repeated when RLE. */
    uint8_t rle_value;    /**< Byte value repeated when RLE. */
    uint8_t *data;        /**< Payload bytes (size bytes) when not RLE. Owned. */
    bool is_rle;
} xx_ips_record;

typedef struct xx_ips_patch_s {
    xx_ips_type type;
    xx_ips_record *records;
    size_t record_count;
    size_t record_capacity;
    bool has_truncation;
    uint64_t truncate_length;
} xx_ips_patch;

XXFC_API xx_ips_patch *xx_ips_patch_create(xx_ips_type type);
XXFC_API void xx_ips_patch_free(xx_ips_patch *patch);

XXFC_API bool xx_ips_patch_add_record(xx_ips_patch *patch, uint64_t offset, const uint8_t *data, uint32_t size);
XXFC_API bool xx_ips_patch_add_rle_record(xx_ips_patch *patch, uint64_t offset, uint8_t value, uint32_t rle_size);
XXFC_API void xx_ips_patch_set_truncation(xx_ips_patch *patch, uint64_t truncate_length);

/**
 * @brief Parse an IPS or IPS32 patch from a device.
 * Enforces strict bounds checking: offset limits (24-bit for IPS, 32-bit for IPS32),
 * truncated records, invalid magic, trailer format, and trailing garbage.
 */
XXFC_API xx_ips_patch *xx_ips_parse_device(xx_io_device *dev, int64_t base_offset, xx_pd_struct *pd, char *error, size_t error_cap);
XXFC_API xx_ips_patch *xx_ips_parse_buffer(const uint8_t *data, size_t size, char *error, size_t error_cap);

/**
 * @brief Apply patch to a buffer. Overwrite and growth only; truncation when present.
 * Allocates/reallocates *data as needed.
 */
XXFC_API bool xx_ips_apply_buffer(const xx_ips_patch *patch, uint8_t **data, size_t *size, char *error, size_t error_cap);

/**
 * @brief Apply patch to an I/O device.
 */
XXFC_API bool xx_ips_apply_device(const xx_ips_patch *patch, xx_io_device *target, xx_pd_struct *pd, char *error, size_t error_cap);

/**
 * @brief Write patch to an I/O device.
 */
XXFC_API bool xx_ips_write_device(const xx_ips_patch *patch, xx_io_device *dest, xx_pd_struct *pd, char *error, size_t error_cap);

/**
 * @brief Create an IPS or IPS32 patch comparing original and modified buffers.
 * Runs of identical bytes are encoded as RLE when shorter.
 * Runs longer than 0xFFFF are split.
 * If force_ips32 is false, but any offset > 0xFFFFFF or hits the 0x454F46 "EOF" ambiguity,
 * IPS32 is written automatically.
 */
XXFC_API xx_ips_patch *xx_ips_create_from_buffers(const uint8_t *orig, size_t orig_size,
                                                  const uint8_t *mod, size_t mod_size,
                                                  bool force_ips32, char *error, size_t error_cap);

XXFC_API xx_ips_patch *xx_ips_create_from_devices(xx_io_device *orig, xx_io_device *mod,
                                                  bool force_ips32, xx_pd_struct *pd,
                                                  char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IPS_H */
