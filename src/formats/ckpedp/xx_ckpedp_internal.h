/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared implementation of the related CKP and EdgeDataPak index layouts.
 */
#ifndef XX_CKPEDP_INTERNAL_H
#define XX_CKPEDP_INTERNAL_H

#include "xxfclib/formats/xx_format.h"

void xx_ckpedp_init(Abstractformat *format, xx_io_device *device, int64_t base_address, xx_file_type_t type);
void xx_ckpedp_destroy(Abstractformat *format);
bool xx_ckpedp_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#endif
