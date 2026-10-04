/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT */
#ifndef XFU_FEAD_BCJ2_H
#define XFU_FEAD_BCJ2_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
bool fead_bcj2_decode(const uint8_t *const inputs[4], const size_t sizes[4],
 uint8_t *output, size_t output_size, xx_pd_struct *pd);
#endif
