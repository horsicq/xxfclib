/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_compaq_softpaq.h @brief Compaq SoftPaq v1 self-extractor reader. */

#ifndef XXFCLIB_FORMAT_SFX_COMPAQ_SOFTPAQ_H
#define XXFCLIB_FORMAT_SFX_COMPAQ_SOFTPAQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A first-generation Compaq SoftPaq (SPnnnn.EXE, 1993..1995).
 *
 * The file is a PKLITE-compressed DOS extractor ("US_PCU.DOS") with its
 * members stored uncompressed behind it and a flat directory at the very end.
 * The later "[FIT]" layout is a different container (see softpaq2).
 *
 * Extractor stub.  Six builds are known; each is recognised by fixed bytes of
 * its MZ header and relocation table, and the file must be larger than the
 * stub itself (XX_SFX_COMPAQ_SOFTPAQ_STUB_* give the values):
 *
 *   build  u32 @0x10    u32 @0x20    u32 @0x24    stub size
 *     1    0xB03F6B6C   0x22BF02E6   0x23490000   0x3CBD
 *     2    0x9D6C6B6C   0x236302F0   0x23ED0000   0x3D5D
 *     3    0x0E936B6C   0x236302F0   0x23ED0000   0x3E01
 *     4    0xBB4B6B6C   0x236302F0   0x23ED0000   0x3E0D
 *     5    0x1FD96B6C   0x23AD03E4   0x24370000   0x4EA5
 *     6    "\x06US_PCU\0" at 0xD0 (the program's own name) 0x3D8B
 *
 * Payload.  From the end of the stub the file holds the members' bytes back
 * to back (each run is preceded by one 0xFF byte in the corpus), then the
 * directory, then one closing "FIT" record.  All offsets are relative to the
 * first byte of the stub, and all integers are little endian.
 *
 * Directory record, 21 bytes (layout 1) or 25 bytes (layout 2):
 *
 *   +0x00 char  name[12]  8.3 name, space padded, no NUL needed
 *   +0x0c u8    0
 *   +0x0d i32   size      stored size, >= 0
 *   +0x11 i32   offset    of the member's bytes, >= 0
 *   +0x15 u16   DOS date  layout 2 only, 0 when absent
 *   +0x17 u16   DOS time  layout 2 only
 *
 * The closing record has the same shape, the name "FIT001" or "FIT002"
 * (digit and layout are not tied together), size 0, and in the offset field
 * the start of the directory.  It ends at most 0xE7 bytes before the end of
 * the file; the reader searches the last 256 bytes backwards for the record
 * end and prefers layout 2 at each position, which is how the Compaq tools'
 * reference extractor locates it.  The directory must tile the range from
 * its start to the closing record exactly, every member must end before the
 * directory, and record +0x0c must be 0.
 *
 * The first directory record always describes the extractor itself
 * ("US_PCU.DOS", offset 0, the stub size) and is not published as a member.
 *
 * Records: the remaining directory entries in order.  Every member is stored
 * (XX_META_ID_COMPRESSION_METHOD 0); layout 2 records with a non-zero date
 * carry XX_META_ID_LAST_MOD_DATE / XX_META_ID_LAST_MOD_TIME, which are also
 * applied to the extracted file.
 *
 * Names: the 12-byte field up to its first NUL, trailing spaces and dots
 * removed.  Bytes outside 0x21..0x7E other than an inner space, and the
 * characters % / \ : * ? " < > |, become %XX, so a name can never carry a
 * path.  An empty name becomes "record<N>".  A Windows device name (CON,
 * PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$, with any
 * extension) is listed but refused on extraction.  Names that collide
 * case-insensitively get "_<N>" in front of the extension, N being the
 * member's index, so no member overwrites another.
 */
typedef struct xx_sfx_compaq_softpaq {
    Abstractformat format;
    uint64_t number_of_records; /**< Published members (stub record excluded). */
    int64_t directory_offset;   /**< Directory start, relative to the base. */
    int64_t trailer_offset;     /**< Closing FIT record, relative to the base. */
    uint32_t record_size;       /**< 21 or 25. */
    uint32_t stub_build;        /**< 1..6, see the table above. */
} xx_sfx_compaq_softpaq;

typedef xx_sfx_compaq_softpaq xx_sfx_compaq_softpaq_t;

#define XX_SFX_COMPAQ_SOFTPAQ_RECORD_SHORT 21U
#define XX_SFX_COMPAQ_SOFTPAQ_RECORD_LONG 25U
/** The directory search window at the end of the file. */
#define XX_SFX_COMPAQ_SOFTPAQ_TAIL 0x100U
/** Offset of the program name that identifies stub build 6. */
#define XX_SFX_COMPAQ_SOFTPAQ_NAME_AT 0xD0U
/** Directory entries accepted, the extractor's own entry included. */
#define XX_SFX_COMPAQ_SOFTPAQ_MAX_RECORDS 4096U

XXFC_API void xx_sfx_compaq_softpaq_init(xx_sfx_compaq_softpaq *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_sfx_compaq_softpaq *xx_sfx_compaq_softpaq_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_compaq_softpaq_destroy(xx_sfx_compaq_softpaq *archive);
XXFC_API void xx_sfx_compaq_softpaq_free(xx_sfx_compaq_softpaq *archive);

XXFC_API bool xx_sfx_compaq_softpaq_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_sfx_compaq_softpaq_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_compaq_softpaq_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_compaq_softpaq_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_compaq_softpaq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_compaq_softpaq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_compaq_softpaq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_compaq_softpaq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_compaq_softpaq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_COMPAQ_SOFTPAQ_H */
