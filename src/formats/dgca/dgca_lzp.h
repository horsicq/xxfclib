/* Independent LZP reconstruction from numeric interoperability facts.
 * SPDX-License-Identifier: MIT. */
#ifndef XFU_DGCA_LZP_H
#define XFU_DGCA_LZP_H
#include "dgca_native.h"
#include <stddef.h>
#include <stdint.h>

/* Buffers are borrowed and must not overlap. Dictionary allocation is charged
 * through callbacks and released on every path. Returns DG_OK, DG_FORMAT,
 * DG_MEMORY, or DG_CANCELLED. No filesystem IO.
 * params: highest nibble is integer update shift; next nibble is log2(minmatch).
 */
dg_status dg_lzp_expand(const dg_callbacks *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char *, size_t, uint32_t params);
#endif
