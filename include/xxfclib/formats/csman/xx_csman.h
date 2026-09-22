/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_csman.h @brief CSMAN DAT configuration store reader. */

/* A CSMAN ".dat" file is the configuration store used by Broadcom/Ubiquiti
 * style firmware; binwalk recognises it as the "CSman DAT file" signature.
 * It is a 16-byte header followed by a flat key/value table that may be
 * stored raw or as a single zlib stream.
 *
 *   header (16 bytes)
 *     +0   u16  magic, the ASCII bytes "CS" (little endian image) or "SC"
 *               (big endian image); the byte order of every later field
 *               follows from which of the two spellings appears
 *     +2   u16  unknown
 *     +4   u32  compressed size, the length of the data region that follows
 *     +8   u32  unknown
 *     +12  u32  decompressed size
 *
 *   entry, repeated inside the (possibly inflated) data region
 *     +0   u32  key
 *     +4   u16  size
 *     +6   `size` value bytes
 *
 * The table ends at a bare u32 zero where the next key would be. When the
 * compressed and decompressed sizes differ the data region is a zlib stream
 * and its first byte must be 0x78; the reader inflates it into a bounded
 * buffer and enumerates the entries from there. Records are therefore served
 * out of memory in the compressed case, and data_offset is -1 for them; in
 * the raw case data_offset is the real device offset of the value bytes.
 *
 * Every size in the file is attacker controlled: the data region, the inflate
 * output and the entry count are each capped, and a zero-key terminator is
 * required before the table is accepted.
 */

#ifndef XXFCLIB_FORMAT_CSMAN_H
#define XXFCLIB_FORMAT_CSMAN_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_csman xx_csman;
typedef struct xx_csman xx_csman_t;
typedef struct xx_csman XCsman;

struct xx_csman {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t compressed_size;    /**< Declared length of the data region. */
    uint32_t decompressed_size;  /**< Declared plain length of the table. */
    bool is_compressed;          /**< True when the data region is zlib. */
    int64_t archive_end;         /**< base_address + 16 + compressed_size. */
    void *internal;
};

XXFC_API void xx_csman_init(xx_csman *csman, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_csman *xx_csman_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_csman_destroy(xx_csman *csman);
XXFC_API void xx_csman_free(xx_csman *csman);

XXFC_API bool xx_csman_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_csman_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_csman_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_csman_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_csman_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_csman_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_csman_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_csman_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_csman_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_csman_get_number_of_records(const xx_csman *csman);
XXFC_API uint32_t xx_csman_get_compressed_size(const xx_csman *csman);
XXFC_API uint32_t xx_csman_get_decompressed_size(const xx_csman *csman);
XXFC_API bool xx_csman_get_is_compressed(const xx_csman *csman);
XXFC_API int64_t xx_csman_get_archive_end(const xx_csman *csman);

static inline Abstractformat *xx_csman_to_format(xx_csman *csman) {
    return csman ? &csman->format : NULL;
}
static inline void XCsman_init(xx_csman *csman, xx_io_device *dev,
                               int64_t base_address) {
    xx_csman_init(csman, dev, base_address);
}
static inline xx_csman *XCsman_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_csman_create(dev, base_address);
}
static inline void XCsman_free(xx_csman *csman) { xx_csman_free(csman); }
static inline bool XCsman_is_valid(xx_csman *csman, xx_pd_struct *pd) {
    return csman ? xx_format_is_valid(&csman->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CSMAN_H */
