/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FORMAT_ADDITIONAL_H
#define XX_FORMAT_ADDITIONAL_H
#include "xxfclib/formats/xx_format.h"
/* Strongly anchored formats only. Geometry-only readers require explicit selection. */
xx_file_type_t xx_format_detect_additional(xx_io_device *device);
#endif
