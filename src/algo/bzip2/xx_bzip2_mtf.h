/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BZIP2_MTF_H
#define XX_BZIP2_MTF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Private block kernel. in_use contains 256 byte flags and must cover every
 * source symbol. The initial alphabet is ordered by byte value, matching the
 * BZip2 symbol map. Output has count naturally aligned int elements.
 *
 * Invalid arguments/backends fail before output mutation. A missing source
 * symbol fails safely but may leave a partial output. For count 0, NULL source
 * and output are permitted; the map is still required. No DLL exports. */
bool xx_bzip2_mtf_encode(const uint8_t *src, size_t count, int *output,
                          const uint8_t in_use[256]);

/* CPU selection is independent of the library's mutable global SIMD flags.
 * Explicit selection is per call for internal tests and measurements only. */
enum xx_bzip2_mtf_backend {
    XX_BZIP2_MTF_AUTO = 0,
    XX_BZIP2_MTF_SCALAR = 1,
    XX_BZIP2_MTF_SSE2 = 2
};
unsigned xx_bzip2_mtf_backend_capabilities(void);
int xx_bzip2_mtf_selected_backend(void);
const char *xx_bzip2_mtf_backend_name(int backend);
bool xx_bzip2_mtf_encode_backend(const uint8_t *src, size_t count, int *output,
                                  const uint8_t in_use[256], int backend);

#endif /* XX_BZIP2_MTF_H */
