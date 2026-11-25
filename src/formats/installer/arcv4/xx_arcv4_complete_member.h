/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_ARCV4_COMPLETE_MEMBER_H
#define XX_ARCV4_COMPLETE_MEMBER_H
#include "xxfclib/formats/xx_format.h"
bool xx_arcv4_recover_split_prefix(Abstractformat *source, int64_t at, uint32_t packed, uint32_t raw, uint32_t method, const char *name, uint8_t **plain, size_t *size,
                                   xx_pd_struct *pd);
#endif
