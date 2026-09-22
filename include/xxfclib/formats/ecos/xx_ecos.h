/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ecos.h @brief eCos MIPS kernel image, found by its exception
 *  handler. */

/* This is NOT a container format and it has no header.  eCos firmware images
 * for MIPS - Broadcom and Realtek routers, cable modems, a lot of set-top
 * boxes - begin with the kernel's exception-handler stub, and that stub is a
 * fixed sequence of MIPS instructions.  binwalk's src/signatures/ecos.rs
 * detects a kernel by recognising the opcodes; there is no structure module
 * and no extractor, because there is nothing structured to parse.
 *
 *   mfc0  $k0, Cause      # cause of last exception
 *   nop                   # some eCos versions omit this
 *   andi  $k0, 0x7F
 *   li    $k1, 0xXXXXXXXX # address varies, so the match stops here
 *   add   $k1, $k0
 *   lw    $k1, 0($k1)
 *   jr    $k1
 *   nop
 *
 * Four byte sequences follow from that, two endiannesses times with-nop and
 * without-nop:
 *
 *   big    endian, with nop:    40 1A 68 00 00 00 00 00 33 5A 00 7F
 *   big    endian, no nop:      40 1A 68 00 33 5A 00 7F
 *   little endian, with nop:    00 68 1A 40 00 00 00 00 7F 00 5A 33
 *   little endian, no nop:      00 68 1A 40 7F 00 5A 33
 *
 * The first byte tells the two endiannesses apart: a little-endian image has
 * a zero there.
 *
 * WHAT THIS READER DOES NOT DO.  It publishes no archive records, because an
 * eCos image has no members: it is one flat kernel blob with the code, the
 * data and usually a compressed rootfs concatenated at an offset that only
 * the vendor's flash layout knows.  Nothing in the instruction stream says
 * where that is, and inventing a split would be worse than publishing none.
 * So this is a detection-only reader: it says "this is an eCos MIPS kernel,
 * and it is big or little endian", and that is the whole of its contract.
 *
 * WEAK-ISH MAGIC, AND A POSITION CAVEAT.  Eight to twelve bytes of specific
 * opcodes is not weak in the false-positive sense - random data will not
 * produce them - but binwalk finds this stub at an ARBITRARY offset inside a
 * firmware image, whereas this reader, like every other in this library,
 * checks at its base address.  A whole-file detector will therefore only fire
 * on an image that BEGINS with the handler, which is the common case for a
 * carved kernel but not for a full flash dump.  For a flash dump the caller
 * has to probe candidate offsets itself.  That is a real limitation and it is
 * stated rather than papered over.
 */

#ifndef XXFCLIB_FORMAT_ECOS_H
#define XXFCLIB_FORMAT_ECOS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest of the four opcode patterns. */
#define XX_ECOS_MAX_PATTERN_SIZE 12U

typedef struct xx_ecos xx_ecos;
typedef struct xx_ecos xx_ecos_t;
typedef struct xx_ecos XEcos;

struct xx_ecos {
    Abstractformat format;
    uint32_t pattern_size; /**< 8 or 12: whether the nop is present. */
    bool big_endian;
    bool has_nop;
};

XXFC_API void xx_ecos_init(xx_ecos *ecos, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_ecos *xx_ecos_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ecos_destroy(xx_ecos *ecos);
XXFC_API void xx_ecos_free(xx_ecos *ecos);

XXFC_API bool xx_ecos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ecos_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ecos_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);

XXFC_API bool xx_ecos_is_big_endian(const xx_ecos *ecos);
XXFC_API bool xx_ecos_has_nop(const xx_ecos *ecos);
XXFC_API uint32_t xx_ecos_get_pattern_size(const xx_ecos *ecos);

static inline Abstractformat *xx_ecos_to_format(xx_ecos *ecos) {
    return ecos ? &ecos->format : NULL;
}
static inline void XEcos_init(xx_ecos *ecos, xx_io_device *dev,
                              int64_t base_address) {
    xx_ecos_init(ecos, dev, base_address);
}
static inline xx_ecos *XEcos_create(xx_io_device *dev, int64_t base_address) {
    return xx_ecos_create(dev, base_address);
}
static inline void XEcos_free(xx_ecos *ecos) { xx_ecos_free(ecos); }
static inline bool XEcos_is_valid(xx_ecos *ecos, xx_pd_struct *pd) {
    return ecos ? xx_format_is_valid(&ecos->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ECOS_H */
