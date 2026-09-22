/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_huf.h @brief HUF archive shared-tree Huffman decoder. */

#ifndef XXFCLIB_ALGO_HUF_H
#define XXFCLIB_ALGO_HUF_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_HUF_MAX_SYMBOLS 256U
#define XX_HUF_MAX_NODES ((XX_HUF_MAX_SYMBOLS * 2U) - 1U)

typedef struct xx_huf_node_s {
    int16_t child_zero;
    int16_t child_one;
    uint8_t symbol;
    bool is_leaf;
} xx_huf_node;

typedef struct xx_huf_tree_s {
    xx_huf_node nodes[XX_HUF_MAX_NODES];
    uint16_t node_count;
} xx_huf_tree;

/**
 * Rebuild the HUF archive's LSB-first, pre-order shared Huffman tree.
 * tree_bytes_used includes the final partially consumed source byte.
 */
XXFC_API bool xx_huf_build_tree(const uint8_t *tree_data, size_t tree_size,
                                 const uint8_t *symbols,
                                 size_t symbol_count, xx_huf_tree *tree,
                                 size_t *tree_bytes_used);

/** Decode exactly output_size bytes from an HUF member stream. */
XXFC_API bool xx_huf_decode_memory(const xx_huf_tree *tree,
                                   const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *input_bytes_used);

/**
 * Decode an HUF-coded NUL-terminated member name. The terminator is consumed
 * but excluded from output; output is always NUL-terminated on success.
 */
XXFC_API bool xx_huf_decode_cstring(const xx_huf_tree *tree,
                                    const uint8_t *input, size_t input_size,
                                    char *output, size_t output_capacity,
                                    size_t max_characters,
                                    size_t *output_length,
                                    size_t *input_bytes_used);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_HUF_H */
