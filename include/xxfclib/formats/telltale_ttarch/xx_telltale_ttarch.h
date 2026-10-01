/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_telltale_ttarch.h @brief Telltale Games TTARCH2 archive reader. */

#ifndef XXFCLIB_FORMAT_TELLTALE_TTARCH_H
#define XXFCLIB_FORMAT_TELLTALE_TTARCH_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Telltale Tool ".ttarch2" archive (2012 and later games).
 *
 * All integers are little-endian.  The archive is an inner directory stream
 * ("4ATT" or "3ATT") that is stored bare, or wrapped:
 *
 *   "NCTT"  u32 magic, u64 inner size, the inner stream (not compressed)
 *   "ZCTT"  u32 magic, u32 chunk size (0x10000 in the games), u32 chunk
 *           count N, (N + 1) u64 chunk offsets; chunk i is the next
 *           off[i + 1] - off[i] bytes after the table and inflates to one
 *           chunk of the inner stream (the last one may be shorter)
 *   "ECTT"  as ZCTT, but every chunk is Blowfish-encrypted with a per-game
 *           key before it is inflated
 *
 * Inner stream:
 *
 *   "4ATT"  u32 magic, u32 name table size, u32 file count
 *   "3ATT"  u32 magic, u32 (unknown), u32 name table size, u32 file count
 *   then file_count 28-byte entries:
 *     u64 CRC-64 of the name, u64 data offset, u32 size, u32 (unknown),
 *     u16 name page, u16 name offset inside the page
 *   then the name table (NUL-terminated names in 0x10000-byte pages; a
 *   name starts at page * 0x10000 + offset), then the file data.  Data
 *   offsets count from the end of the name table.
 *
 * Supported: bare 3ATT/4ATT, NCTT, and ZCTT whose chunks are Deflate (raw
 * or zlib-wrapped).  ECTT (game-keyed Blowfish) and ZCTT chunks packed with
 * Oodle are recognised and measured but list no members.  The older,
 * magic-less TTARCH (version 1..9) encrypts its whole directory with a
 * per-game Blowfish key and is not handled.
 */
typedef struct xx_telltale_ttarch {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t wrapper;       /**< XX_TELLTALE_TTARCH_WRAP_* */
    uint32_t inner_version; /**< 3 or 4; 0 when the inner stream is unread */
    uint32_t chunk_size;    /**< ZCTT/ECTT only */
    uint32_t chunk_count;   /**< ZCTT/ECTT only */
    int64_t inner_size;     /**< -1 when the inner stream cannot be decoded */
    bool members_unavailable; /**< encrypted or non-Deflate chunks */
} xx_telltale_ttarch;

typedef xx_telltale_ttarch xx_telltale_ttarch_t;

#define XX_TELLTALE_TTARCH_WRAP_NONE 0U
#define XX_TELLTALE_TTARCH_WRAP_NCTT 1U
#define XX_TELLTALE_TTARCH_WRAP_ZCTT 2U
#define XX_TELLTALE_TTARCH_WRAP_ECTT 3U

XXFC_API void xx_telltale_ttarch_init(xx_telltale_ttarch *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_telltale_ttarch *xx_telltale_ttarch_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_telltale_ttarch_destroy(xx_telltale_ttarch *archive);
XXFC_API void xx_telltale_ttarch_free(xx_telltale_ttarch *archive);

XXFC_API bool xx_telltale_ttarch_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_telltale_ttarch_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_telltale_ttarch_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_telltale_ttarch_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_telltale_ttarch_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_telltale_ttarch_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_telltale_ttarch_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_telltale_ttarch_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_telltale_ttarch_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TELLTALE_TTARCH_H */
