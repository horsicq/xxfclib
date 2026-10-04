/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_LEGACY_ARCHIVE_ENGINE_H
#define XXFCLIB_LEGACY_ARCHIVE_ENGINE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Windows RAM file API compatibility bridge for original UnUHARC 0.6b. */
XXFC_API Abstractformat *xx_uharc_payload_create(xx_io_device *device,int64_t base_address);
/** Independent native DGCA reader; borrowed source IO and bounded RAM payloads. */
XXFC_API Abstractformat *xx_dgca_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_legacy_archive_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
#endif
