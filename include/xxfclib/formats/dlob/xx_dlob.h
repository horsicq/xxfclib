/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dlob.h @brief D-Link DLOB firmware header reader. */

/* "DLOB" is the name binwalk gives to the two-header arrangement D-Link ships
 * on its SEAMA based routers.  It is not a separate container format: it is a
 * SEAMA chain whose FIRST entity carries no payload at all, only a metadata
 * block naming the board (the "DLOB" / "wrgac.." signature strings), followed
 * by a SECOND entity holding the real firmware - usually a uImage or an LZMA
 * kernel plus squashfs, all of which other readers here decode.
 *
 * Both entities are BIG endian and use the SEAMA layout:
 *
 *   entity
 *     +0   u32  magic, 0x5EA3A417
 *     +4   u16  reserved, zero
 *     +6   u16  metasize
 *     +8   u32  size
 *     +12  16 bytes of MD5 digest, PRESENT ONLY WHEN size != 0
 *     then metasize bytes of metadata, then size bytes of payload
 *
 * So the first entity is exactly twelve bytes plus its metadata, and the
 * second is twenty-eight bytes plus its metadata plus the payload.  That is
 * the structure binwalk's src/structures/dlob.rs encodes, and its
 * src/signatures/dlob.rs confirms the detection magic is the SEAMA magic
 * \x5e\xa3\xa4\x17 - the two formats are told apart by the first entity's
 * zero size, not by a distinct magic.
 *
 * This reader deliberately requires that shape: first entity size == 0, second
 * entity size != 0.  A plain single-entity SEAMA image is the job of xx_seama.
 */

#ifndef XXFCLIB_FORMAT_DLOB_H
#define XXFCLIB_FORMAT_DLOB_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DLOB_MAGIC UINT32_C(0x5EA3A417)
#define XX_DLOB_HEADER_SIZE 12U
#define XX_DLOB_DIGEST_SIZE 16U

typedef struct xx_dlob xx_dlob;
typedef struct xx_dlob xx_dlob_t;
typedef struct xx_dlob XDlob;

struct xx_dlob {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t outer_meta_size; /**< First entity's metasize. */
    uint32_t inner_meta_size; /**< Second entity's metasize. */
    uint32_t image_size;      /**< Second entity's payload size. */
    int64_t data_offset;      /**< Start of the payload, or -1. */
    int64_t archive_end;      /**< End of the payload, or -1. */
    void *internal;
};

XXFC_API void xx_dlob_init(xx_dlob *dlob, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_dlob *xx_dlob_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dlob_destroy(xx_dlob *dlob);
XXFC_API void xx_dlob_free(xx_dlob *dlob);

XXFC_API bool xx_dlob_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dlob_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dlob_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_dlob_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dlob_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dlob_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dlob_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dlob_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dlob_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dlob_get_number_of_records(const xx_dlob *dlob);
XXFC_API uint64_t xx_dlob_get_number_of_members(const xx_dlob *dlob);
XXFC_API uint32_t xx_dlob_get_image_size(const xx_dlob *dlob);
XXFC_API int64_t xx_dlob_get_archive_end(const xx_dlob *dlob);

static inline Abstractformat *xx_dlob_to_format(xx_dlob *dlob) {
    return dlob ? &dlob->format : NULL;
}
static inline void XDlob_init(xx_dlob *dlob, xx_io_device *dev,
                              int64_t base_address) {
    xx_dlob_init(dlob, dev, base_address);
}
static inline xx_dlob *XDlob_create(xx_io_device *dev, int64_t base_address) {
    return xx_dlob_create(dev, base_address);
}
static inline void XDlob_free(xx_dlob *dlob) { xx_dlob_free(dlob); }
static inline bool XDlob_is_valid(xx_dlob *dlob, xx_pd_struct *pd) {
    return dlob ? xx_format_is_valid(&dlob->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DLOB_H */
