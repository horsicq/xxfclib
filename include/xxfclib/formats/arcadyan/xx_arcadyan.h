/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_arcadyan.h @brief Arcadyan obfuscated LZMA firmware reader. */

/* Arcadyan (Astoria / Livebox / o2 Box and friends) ship some of their
 * firmware as an LZMA-alone stream with the first 0x88 bytes shuffled.  There
 * is no header in the usual sense: no magic string, no length field, no
 * checksum.  What identifies the format is that the shuffle moves four
 * recognisable bytes of the LZMA header to a fixed place - the bytes 00 D5 08
 * 00 land at offset 0x68 - and those four bytes are the whole signature.
 *
 * The obfuscation, in the order it has to be undone:
 *
 *   1. bytes [0x04, 0x24) and [0x68, 0x88) are swapped with each other;
 *   2. the 32 bytes now at [0x04, 0x24) get each byte's nibbles swapped;
 *   3. those same 32 bytes are then swapped in adjacent pairs.
 *
 * Bytes [0x00, 0x04), [0x24, 0x68) and everything from 0x88 on are untouched.
 * The LZMA-alone stream starts at offset 4 of the DE-obfuscated data, so the
 * first four bytes of the file are dropped.
 *
 * Working the signature back through the transform shows what it pins down:
 * 00 D5 08 00 becomes the LZMA properties byte 0x5D and the dictionary size
 * 0x00800000, which is exactly the lzma_alone header a stock Arcadyan build
 * emits.  The eight bytes that follow are the declared uncompressed size, and
 * those are attacker controlled - they are bounded here rather than believed.
 *
 * Because the first 0x88 bytes are shuffled, the record this reader publishes
 * does NOT correspond to a contiguous run of bytes on the device.  Reading
 * the record's range straight off the device yields the obfuscated form; it
 * is xx_arcadyan_unpack_current_archive_record() that reassembles the stream.
 *
 * There is no length field, so the format runs to the end of the device.
 * binwalk bounds that span to (0x100, 0x1B0000] bytes and this reader keeps
 * the same bounds: without them any file with four lucky bytes at 0x68 would
 * be claimed.
 *
 * Sources: binwalk src/signatures/arcadyan.rs (magic and the -0x68 offset
 * adjustment) and src/extractors/arcadyan.rs (the de-obfuscator and the size
 * bounds).  Cross-checked against binwalk's own tests/inputs/arcadyan.bin.
 */

#ifndef XXFCLIB_FORMAT_ARCADYAN_H
#define XXFCLIB_FORMAT_ARCADYAN_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Offset of the four signature bytes inside the obfuscated image. */
#define XX_ARCADYAN_MAGIC_OFFSET 0x68U
#define XX_ARCADYAN_MAGIC_SIZE 4U
/** The shuffled prologue; everything past this is stored verbatim. */
#define XX_ARCADYAN_PROLOGUE_SIZE 0x88U
/** Size of each of the two swapped blocks. */
#define XX_ARCADYAN_BLOCK_SIZE 0x20U
/** The LZMA-alone stream starts this far into the de-obfuscated data. */
#define XX_ARCADYAN_LZMA_OFFSET 4U
/** binwalk's span bounds, kept so the two agree on the same file. */
#define XX_ARCADYAN_MIN_SIZE 0x100U
#define XX_ARCADYAN_MAX_SIZE 0x1B0000U
/** Cap on the LZMA header's declared output size, as an expansion guard. */
#define XX_ARCADYAN_MAX_DECODED UINT64_C(0x10000000)

typedef struct xx_arcadyan xx_arcadyan;
typedef struct xx_arcadyan xx_arcadyan_t;
typedef struct xx_arcadyan XArcadyan;

struct xx_arcadyan {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t declared_size; /**< LZMA uncompressed size, UINT64_MAX if unset. */
    uint32_t dictionary_size; /**< LZMA dictionary size from the header. */
    uint8_t properties;       /**< LZMA properties byte; 0x5D in practice. */
    int64_t stream_size;      /**< Bytes of LZMA-alone stream published. */
    int64_t archive_end;      /**< End of the image, or -1. */
    void *internal;
};

XXFC_API void xx_arcadyan_init(xx_arcadyan *arc, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_arcadyan *xx_arcadyan_create(xx_io_device *dev,
                                         int64_t base_address);
XXFC_API void xx_arcadyan_destroy(xx_arcadyan *arc);
XXFC_API void xx_arcadyan_free(xx_arcadyan *arc);

XXFC_API bool xx_arcadyan_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_arcadyan_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_arcadyan_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_arcadyan_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_arcadyan_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_arcadyan_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arcadyan_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arcadyan_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arcadyan_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_arcadyan_get_number_of_records(const xx_arcadyan *arc);
XXFC_API uint64_t xx_arcadyan_get_number_of_members(const xx_arcadyan *arc);
XXFC_API int64_t xx_arcadyan_get_stream_size(const xx_arcadyan *arc);
XXFC_API int64_t xx_arcadyan_get_archive_end(const xx_arcadyan *arc);

/**
 * @brief Undo the prologue shuffle in place.
 *
 * @param data  at least XX_ARCADYAN_PROLOGUE_SIZE bytes of the obfuscated
 *              image, starting at its first byte.
 * @param size  how many bytes @p data holds; the call is a no-op when it is
 *              shorter than the prologue.
 *
 * Only the first 0x88 bytes are touched; the rest of the image is already in
 * clear.  Exposed so a caller can de-obfuscate a buffer it has read itself.
 */
XXFC_API void xx_arcadyan_deobfuscate_prologue(void *data, size_t size);

static inline Abstractformat *xx_arcadyan_to_format(xx_arcadyan *arc) {
    return arc ? &arc->format : NULL;
}
static inline void XArcadyan_init(xx_arcadyan *arc, xx_io_device *dev,
                                  int64_t base_address) {
    xx_arcadyan_init(arc, dev, base_address);
}
static inline xx_arcadyan *XArcadyan_create(xx_io_device *dev,
                                            int64_t base_address) {
    return xx_arcadyan_create(dev, base_address);
}
static inline void XArcadyan_free(xx_arcadyan *arc) { xx_arcadyan_free(arc); }
static inline bool XArcadyan_is_valid(xx_arcadyan *arc, xx_pd_struct *pd) {
    return arc ? xx_format_is_valid(&arc->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ARCADYAN_H */
