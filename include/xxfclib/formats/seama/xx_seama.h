/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_seama.h @brief SEAMA (Seattle Image Archive) firmware reader. */

/* SEAMA is the firmware wrapper used by Realtek and Ralink based D-Link
 * routers.  A SEAMA file is a chain of "entities"; each entity is a small
 * header, an optional MD5 digest, a block of NUL separated key=value metadata
 * and finally the image payload.  The payloads are ordinary blobs that other
 * readers here already decode - a uImage, an LZMA kernel, a squashfs - so
 * this reader validates the chain, verifies each digest and publishes every
 * payload as its own archive record for the caller to recurse into.
 *
 * Everything is BIG endian.
 *
 *   entity
 *     +0   u32  magic, 0x5EA3A417
 *     +4   u16  reserved, always written as zero
 *     +6   u16  metasize, the size of the metadata block
 *     +8   u32  size, the size of the image payload
 *     +12  16 bytes of MD5 digest, PRESENT ONLY WHEN size != 0
 *     then metasize bytes of metadata, then size bytes of payload
 *
 * The digest covers the payload bytes only - not the header and not the
 * metadata.  The "header only" entity, with size == 0 and therefore no
 * digest, is how D-Link prefixes a signed image with its board metadata; that
 * two-entity arrangement is what binwalk reports as a DLOB header and is
 * handled by xx_dlob as well as here.
 *
 * Sources: OpenWrt tools/firmware-utils/src/seama.c and seama.h for the
 * layout and for the rule that the digest is written only when size > 0, and
 * binwalk's src/structures/seama.rs for cross-checking the field offsets.
 */

#ifndef XXFCLIB_FORMAT_SEAMA_H
#define XXFCLIB_FORMAT_SEAMA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_SEAMA_MAGIC UINT32_C(0x5EA3A417)
#define XX_SEAMA_HEADER_SIZE 12U
#define XX_SEAMA_DIGEST_SIZE 16U
/** A real image carries one or two entities; this is the hard stop. */
#define XX_SEAMA_MAX_ENTITIES 16U

typedef struct xx_seama xx_seama;
typedef struct xx_seama xx_seama_t;
typedef struct xx_seama XSeama;

struct xx_seama {
    Abstractformat format;
    uint64_t number_of_records;  /**< Payload entities published. */
    uint64_t number_of_members;
    uint64_t number_of_entities; /**< Entities in the chain, payload or not. */
    uint32_t meta_size;          /**< First entity's metasize. */
    uint32_t image_size;         /**< First payload entity's size. */
    int64_t archive_end;         /**< End of the last entity, or -1. */
    void *internal;
};

XXFC_API void xx_seama_init(xx_seama *seama, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_seama *xx_seama_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_seama_destroy(xx_seama *seama);
XXFC_API void xx_seama_free(xx_seama *seama);

XXFC_API bool xx_seama_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_seama_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_seama_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_seama_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_seama_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_seama_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_seama_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_seama_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_seama_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_seama_get_number_of_records(const xx_seama *seama);
XXFC_API uint64_t xx_seama_get_number_of_members(const xx_seama *seama);
XXFC_API uint64_t xx_seama_get_number_of_entities(const xx_seama *seama);
XXFC_API uint32_t xx_seama_get_meta_size(const xx_seama *seama);
XXFC_API uint32_t xx_seama_get_image_size(const xx_seama *seama);
XXFC_API int64_t xx_seama_get_archive_end(const xx_seama *seama);

static inline Abstractformat *xx_seama_to_format(xx_seama *seama) {
    return seama ? &seama->format : NULL;
}
static inline void XSeama_init(xx_seama *seama, xx_io_device *dev,
                               int64_t base_address) {
    xx_seama_init(seama, dev, base_address);
}
static inline xx_seama *XSeama_create(xx_io_device *dev, int64_t base_address) {
    return xx_seama_create(dev, base_address);
}
static inline void XSeama_free(xx_seama *seama) { xx_seama_free(seama); }
static inline bool XSeama_is_valid(xx_seama *seama, xx_pd_struct *pd) {
    return seama ? xx_format_is_valid(&seama->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SEAMA_H */
