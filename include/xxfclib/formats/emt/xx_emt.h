/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_emt.h
 *  @brief "EMT" compressed diskette image (EMT4OS2 / EMT4PM / EMT4PMW).
 */

/* WHERE THE LAYOUT COMES FROM.  U3's recognition predicate (FUN_006716d0,
 * reached from VMT slot 0 at 0x00671a70) is four byte tests, and all four are
 * reproduced here exactly:
 *
 *   byte @ 0x00 == '\\' (0x5c)
 *   byte @ 0x02 == 'z'  (0x7a)
 *   byte @ 0x58 == '1'  (0x31)
 *   u32  @ 0x5c == 0x346e026c   (bytes 6c 02 6e 34)
 *
 * The rest of the header is EBCDIC, which is what identifies the tool:
 *
 *   0x00  3    5c 5c 7a
 *   0x03  4    EBCDIC "EMT "
 *   0x07  6    EBCDIC digits, "001001" in every sample
 *   0x0d  ..   EBCDIC text, mostly 0x40 (space) padding
 *   0x40  16   EBCDIC product banner: " EMT4OS2 V.2.32 ",
 *              " EMT4PM  V.1.11 ", " EMT4PMW V.1.24 "
 *   0x50  ..   binary, including the two fields U3 tests at 0x58 and 0x5c
 *
 * The banner is decoded here and published as the format version, which is
 * the one genuinely descriptive thing the header carries.
 *
 * WHAT THE FILE IS.  A compressed diskette image.  The reference unpacker
 * turns PSINFO.EMT (1036640 bytes) into a single 1474560-byte output - an
 * exact 1.44 MB floppy - and names it after the host file, because nothing
 * inside the container supplies a name.
 *
 * TRACK AND CODEC. The compressed LOADDSKF derivative contains one RLE
 * record per physical track. F1 escapes [value][count]; literal bytes pass
 * through. Each non-final record expands to a 126-byte descriptor, 9,216
 * bytes of disk data and two following CRC bytes; the final record omits the
 * CRC bytes. Cylinder/head fields in the descriptor identify each track.
 * The boot sector's BPB supplies sector size and total sector count, which
 * bound the image and determine the number of tracks. The reader exports one
 * synthetic disk.img member; the container itself has no member filename.
 * The selected TSENG_D1.EMT decodes byte-for-byte to the 1,474,560-byte
 * reference floppy image. The track scheme also appears in XArchive's
 * xlegacystorearchive.cpp and xdecompress.cpp under the same MIT license.
 *
 * DISPATCH NOTE.  Two of U3's four tests live at offsets 0x58 and 0x5c, past
 * the end of a 64-byte magic window, so a prefilter can only use the first
 * six bytes - 5c ?? 7a plus EBCDIC "EMT".  The reader itself checks all four.
 */

#ifndef XXFCLIB_FORMAT_EMT_H
#define XXFCLIB_FORMAT_EMT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes read and tested by the reader. */
#define XX_EMT_HEADER_SIZE 0x60U
/** Offset and width of the EBCDIC product banner. */
#define XX_EMT_BANNER_OFFSET 0x40U
#define XX_EMT_BANNER_SIZE 16U

typedef struct xx_emt xx_emt;
typedef struct xx_emt xx_emt_t;
typedef struct xx_emt XEmt;

struct xx_emt {
    Abstractformat format; /**< Base format structure (first member). */
    /** Product banner, translated from EBCDIC and trimmed. */
    char banner[XX_EMT_BANNER_SIZE + 1];
};

XXFC_API void xx_emt_init(xx_emt *image, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_emt *xx_emt_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_emt_destroy(xx_emt *image);
XXFC_API void xx_emt_free(xx_emt *image);

XXFC_API bool xx_emt_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_emt_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_emt_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);

/** Product banner, or an empty string before handle_base_info. */
XXFC_API const char *xx_emt_get_banner(const xx_emt *image);

static inline Abstractformat *xx_emt_to_format(xx_emt *image) {
    return image ? &image->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_emt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_emt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_emt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_emt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_emt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_EMT_H */
