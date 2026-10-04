/* SPDX-License-Identifier: MIT. Original disk wrapper adapter state. */
#ifndef XX_DISK_ADDITIONS_H
#define XX_DISK_ADDITIONS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Companion devices are borrowed and must outlive all active iterators. */
typedef struct xx_disk_additions_info {
 Abstractformat format;
 xx_io_device *companion, *subchannel;
 uint32_t sample_hz, data_bit, index_bit;
 const xx_list_s *parse_options;
 bool incomplete, reading_records;
 xx_io_device *track_sources[256];
 bool owns_tracks[256], owns_companion, owns_subchannel;
} xx_disk_additions_info;
#ifdef __cplusplus
}
#endif
#endif
