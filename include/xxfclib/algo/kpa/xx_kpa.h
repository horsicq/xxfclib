/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_kpa.h
 * @brief Known Plaintext Attack (KPA) detection algorithms.
 *
 * Implements high-performance detection of encrypted PE headers embedded in
 * raw buffers, sections, resources, or overlays using KPA on the DOS/PE header.
 * Supports XOR-XNOR, ADD-SUB, and SUB-REV algorithms with repeating key lengths
 * from 1 through 20 bytes.
 */

#ifndef XX_KPA_H
#define XX_KPA_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Scan a byte buffer for an encrypted PE image using KPA proofs.
 *
 * Checks all byte offsets in [0 .. size - 0x100) for repeating key encrypted PE
 * headers across XOR/XNOR, ADD/SUB, and SUB-REV schemes with key lengths 1..20.
 *
 * @param data Pointer to input data buffer.
 * @param size Number of bytes in buffer.
 * @return String literal indicating detected algorithm:
 *         "XOR-XNOR", "ADD-SUB", "SUB-REV", or "" if none detected.
 */
XXFC_API const char *xx_kpa_scan_buffer_encrypted_pe(const void *data, size_t size);

/**
 * @brief Alias for xx_kpa_scan_buffer_encrypted_pe.
 */
XXFC_API const char *xx_scan_buffer_for_encrypted_pe(const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XX_KPA_H */
