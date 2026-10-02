/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_7ZIP_BRANCH_H
#define XX_7ZIP_BRANCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool xx_7zip_branch_properties_supported(size_t properties_size);
bool xx_7zip_branch_method_supported(uint64_t method, size_t properties_size);
bool xx_7zip_branch_decode(uint64_t method, const uint8_t *properties,
                           size_t properties_size, const uint8_t *input,
                           size_t input_size, uint8_t *output,
                           size_t output_size);
/* Streaming converters leave the final incomplete instruction in the
 * caller's buffer. The caller retains that suffix for its next chunk. */
typedef struct xx_7zip_branch_state_s {
    uint64_t method;
    uint32_t ip;
    uint32_t x86_mask;
} xx_7zip_branch_state;
bool xx_7zip_branch_state_init(xx_7zip_branch_state *state, uint64_t method,
                               const uint8_t *properties, size_t properties_size);
size_t xx_7zip_branch_process(xx_7zip_branch_state *state, uint8_t *data,
                               size_t size, bool final);
bool xx_7zip_bcj2_decode(const uint8_t *const inputs[4],
                         const size_t input_sizes[4], const uint8_t *properties,
                         size_t properties_size, uint8_t *output,
                         size_t output_size);

#endif /* XX_7ZIP_BRANCH_H */
