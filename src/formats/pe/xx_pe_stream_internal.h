/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PE_STREAM_INTERNAL_H
#define XX_PE_STREAM_INTERNAL_H
#include "xxfclib/formats/pe/xx_pe.h"

bool xx_pe_stream_prepare(Abstractformat *format, xx_pd_struct *pd);
bool xx_pe_stream_read(Abstractformat *format, int64_t absolute_offset, void *buffer, size_t size, xx_pd_struct *pd);
bool xx_pe_stream_rva(Abstractformat *format, uint64_t rva, uint64_t size, int64_t *absolute_offset, xx_pd_struct *pd);
#endif
