/* SPDX-License-Identifier: MIT. Historical encoder, RAM-only bounded API. */
#ifndef XFU_FEAD_ZLIB_ENCODER_H
#define XFU_FEAD_ZLIB_ENCODER_H
#include "fead_restore.h"
bool fead_zlib_encode_bounded(const fead_restore_context *ctx,
 const uint8_t *plain, size_t length, uint8_t *output, size_t capacity,
 unsigned level, unsigned window, unsigned memory, unsigned flush,
 size_t *written);
#endif
