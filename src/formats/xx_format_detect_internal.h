/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_FORMAT_DETECT_INTERNAL_H
#define XX_FORMAT_DETECT_INTERNAL_H
#include "xxfclib/formats/xx_format.h"
/* Ordered format-owned signature probes shared by full and fast detection. */
xx_file_type_t xx_format_detect_signature_readers(xx_io_device *, xx_pd_struct *);
#endif
