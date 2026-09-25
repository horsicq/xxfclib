/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pjl.h @brief HP Printer Job Language data: validation and size. */

/* A PJL job is plain text sent to a printer ahead of (and between) the page
 * description language streams it carries.  It opens with the Universal
 * Exit Language command, the nine bytes ESC "%-12345X", immediately followed
 * by the first PJL command, "@PJL" ...:
 *
 *     +0x00  1B 25 2D 31 32 33 34 35 58   ESC %-12345X   (UEL)
 *     +0x09  40 50 4A 4C                  @PJL
 *     +0x0D  the rest of the job: PJL command lines (CR LF / LF terminated),
 *            usually an "@PJL ENTER LANGUAGE=..." and then PCL, PCL XL or
 *            PostScript data, and often a closing UEL.
 *
 * There is no header, length or checksum.  The reader follows binwalk's
 * src/signatures/pjl.rs exactly, which is the contract:
 *
 *  - The thirteen bytes ESC "%-12345X@PJL" must sit at the base address
 *    (binwalk pjl_magic()).
 *  - From base + 9 (binwalk PJL_COMMANDS_OFFSET, the "@PJL") the data is
 *    read as a C string: up to the first NUL byte, or to the end of the
 *    input when there is none (binwalk get_cstring).
 *  - Those bytes must be valid UTF-8 as Rust's String::from_utf8 defines
 *    it: no stray continuation bytes, no overlong forms (C0, C1, E0 80-9F,
 *    F0 80-8F), no surrogates (ED A0-BF), nothing above U+10FFFF (F4 90+,
 *    F5-FF), no sequence cut short by the NUL or by the end of the input.
 *    Otherwise binwalk's string is empty and the signature is rejected.
 *  - The string must be non-empty, which the magic already guarantees.
 *
 * SIZE.  binwalk's result.size -- its carve length, counted from the
 * signature's offset -- is the LENGTH OF THAT STRING, i.e. of the bytes
 * from base + 9 up to the NUL.  The nine UEL bytes in front of it are not
 * added.  This reader reports exactly that number as the format size, so
 * the carve [base, base + size) stops nine bytes short of the NUL (or of
 * the end of the input), and everything from there on -- those nine text
 * bytes, the NUL and whatever follows it -- is reported as overlay.  The
 * true end of the text is available from xx_pjl_get_text_end().
 *
 * There is no size cap: binwalk has none, and the scan is a single forward
 * pass through a fixed buffer, polling the stop flag once per chunk.
 *
 * NOT an archive: binwalk registers no extractor for this signature.
 */

#ifndef XXFCLIB_FORMAT_PJL_H
#define XXFCLIB_FORMAT_PJL_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** binwalk pjl_magic(): the UEL followed by the first "@PJL". */
#define XX_PJL_MAGIC "\x1B%-12345X@PJL"
#define XX_PJL_MAGIC_SIZE 13U
/** The Universal Exit Language command alone. */
#define XX_PJL_UEL "\x1B%-12345X"
#define XX_PJL_UEL_SIZE 9U
/** binwalk PJL_COMMANDS_OFFSET: where the "@PJL" (and the string) starts. */
#define XX_PJL_COMMANDS_OFFSET 9U
/** The PJL command prefix. */
#define XX_PJL_COMMAND_PREFIX "@PJL"
#define XX_PJL_COMMAND_PREFIX_SIZE 4U

typedef struct xx_pjl xx_pjl;
typedef struct xx_pjl xx_pjl_t;
typedef struct xx_pjl XPjl;

struct xx_pjl {
    Abstractformat format;
    int64_t text_offset;     /**< base + 9, the first "@PJL"; -1 if unparsed. */
    int64_t text_size;       /**< Bytes from text_offset to the NUL / end;
                                  also the format size (binwalk result.size). */
    int64_t text_end;        /**< text_offset + text_size: the NUL, or the
                                  end of the input when there is none. */
    uint64_t number_of_lines;    /**< LF-separated lines in the text. */
    uint64_t number_of_commands; /**< Lines that begin with "@PJL". */
    uint64_t number_of_uels;     /**< UEL commands in the text, the one
                                      at base not counted. */
    bool has_terminator;     /**< A NUL byte ends the text. */
    bool is_ascii;           /**< No byte of the text is above 0x7F. */
};

XXFC_API void xx_pjl_init(xx_pjl *pjl, xx_io_device *dev, int64_t base_address);
XXFC_API xx_pjl *xx_pjl_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_pjl_destroy(xx_pjl *pjl);
XXFC_API void xx_pjl_free(xx_pjl *pjl);

XXFC_API bool xx_pjl_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pjl_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pjl_get_format_size(Abstractformat *self, xx_pd_struct *pd);

/** Prefilter over the detector's first bytes: the thirteen-byte magic at
 *  offset 0.  Necessary, not sufficient; the probe decides. */
XXFC_API bool xx_pjl_check_magic(const uint8_t *magic, size_t magic_size);

XXFC_API int64_t xx_pjl_get_text_offset(const xx_pjl *pjl);
XXFC_API int64_t xx_pjl_get_text_size(const xx_pjl *pjl);
XXFC_API int64_t xx_pjl_get_text_end(const xx_pjl *pjl);
XXFC_API uint64_t xx_pjl_get_number_of_lines(const xx_pjl *pjl);
XXFC_API uint64_t xx_pjl_get_number_of_commands(const xx_pjl *pjl);
XXFC_API uint64_t xx_pjl_get_number_of_uels(const xx_pjl *pjl);
XXFC_API bool xx_pjl_has_terminator(const xx_pjl *pjl);
XXFC_API bool xx_pjl_is_ascii(const xx_pjl *pjl);

static inline Abstractformat *xx_pjl_to_format(xx_pjl *pjl) {
    return pjl ? &pjl->format : NULL;
}
static inline void XPjl_init(xx_pjl *pjl, xx_io_device *dev,
                             int64_t base_address) {
    xx_pjl_init(pjl, dev, base_address);
}
static inline xx_pjl *XPjl_create(xx_io_device *dev, int64_t base_address) {
    return xx_pjl_create(dev, base_address);
}
static inline void XPjl_free(xx_pjl *pjl) { xx_pjl_free(pjl); }
static inline bool XPjl_is_valid(xx_pjl *pjl, xx_pd_struct *pd) {
    return pjl ? xx_format_is_valid(&pjl->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PJL_H */
