/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pchrom.h @brief Intel PCH SPI flash image (flash descriptor) reader. */

/* An Intel "descriptor mode" SPI flash image: the first 4 KiB of the flash
 * is the flash descriptor, and the descriptor's region table splits the rest
 * of the chip(s) into regions - BIOS, Intel ME, GbE, platform data, EC and so
 * on.  This reader validates the descriptor, reports the image size (the end
 * of the furthest region) and publishes every used region as a record.
 *
 *   descriptor (little endian throughout)
 *     +0x000  16 bytes reserved vector, 0xFF on x86 boards
 *     +0x010  FLVALSIG  5A A5 F0 0F  (u32 0x0FF0A55A)
 *     +0x014  FLMAP0    FCBA[7:0] NC[9:8] FRBA[23:16] NR[26:24]
 *     +0x018  FLMAP1    FMBA[7:0] NM[9:8] FISBA[23:16] ISL[31:24]
 *     +0x01C  FLMAP2    FMSBA[7:0] MSL[15:8]
 *     +0x030  FLCOMP    component section (FCBA = 3); bits 19:17 are the
 *                       read clock, 000b (20 MHz) on version 1 descriptors
 *     +0x040  FLREG0..  region section (FRBA = 4), one u32 per region:
 *                       base in bits 14:0, limit in bits 30:16, both in
 *                       4 KiB units, region = [base << 12, (limit + 1) << 12)
 *     +FMBA<<4          master section, which bounds the region table
 *
 *   region slots: 0 descriptor, 1 BIOS, 2 ME, 3 GbE, 4 PDR, 5 DevExp1,
 *   6 BIOS2, 7 microcode, 8 EC, 9 DevExp2, 10 IE, 11/12 10GbE, 13/14
 *   reserved, 15 PTT.  Version 1 descriptors define slots 0..6, version 2
 *   slots 0..15 (UEFITool's split).  A slot is unused when it reads
 *   0xFFFFFFFF, when its limit is zero (slots 1..15), or when base > limit.
 *
 * Sources: binwalk's src/signatures/pchrom.rs and src/structures/pchrom.rs
 * (magic, the FLMAP0 checks FCBA == 3, NC <= 1, FRBA == 4 with NR == 0,
 * and "image size = furthest region end"); the region layout and slot names
 * follow UEFITool's descriptor.h / ffsparser.cpp and coreboot's ifdtool, and
 * the records are split the way uefi-firmware-parser's flash.py splits them
 * (region-<name>.fd).  See xx_pchrom.c for where binwalk's size arithmetic
 * is not followed and why.
 */

#ifndef XXFCLIB_FORMAT_PCHROM_H
#define XXFCLIB_FORMAT_PCHROM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PCHROM_SIGNATURE_OFFSET 0x10U
#define XX_PCHROM_DESCRIPTOR_SIZE 0x1000U
#define XX_PCHROM_MAX_REGIONS 16U

typedef struct xx_pchrom xx_pchrom;
typedef struct xx_pchrom xx_pchrom_t;
typedef struct xx_pchrom XPchrom;

struct xx_pchrom {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t flmap0;             /**< Descriptor map word 0 (FCBA/NC/FRBA/NR). */
    uint32_t flmap1;             /**< Descriptor map word 1 (FMBA/NM/FISBA/ISL). */
    uint32_t flcomp;             /**< First component section word. */
    uint32_t descriptor_version; /**< 1 or 2, from the FLCOMP read clock. */
    uint32_t number_of_components; /**< NC + 1. */
    uint32_t region_mask;        /**< Bit n set when region slot n is used. */
    int64_t image_end;           /**< base_address + image size, or -1. */
    void *internal;
};

XXFC_API void xx_pchrom_init(xx_pchrom *pchrom, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_pchrom *xx_pchrom_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_pchrom_destroy(xx_pchrom *pchrom);
XXFC_API void xx_pchrom_free(xx_pchrom *pchrom);

XXFC_API bool xx_pchrom_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pchrom_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_pchrom_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_pchrom_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pchrom_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pchrom_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pchrom_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pchrom_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pchrom_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_pchrom_get_number_of_records(const xx_pchrom *pchrom);
XXFC_API uint64_t xx_pchrom_get_number_of_members(const xx_pchrom *pchrom);
XXFC_API uint32_t xx_pchrom_get_descriptor_version(const xx_pchrom *pchrom);
XXFC_API uint32_t xx_pchrom_get_region_mask(const xx_pchrom *pchrom);
XXFC_API int64_t xx_pchrom_get_image_end(const xx_pchrom *pchrom);
/** Record name used for region slot @p index ("region-bios.fd", ...), or NULL. */
XXFC_API const char *xx_pchrom_region_name(uint32_t index);

static inline Abstractformat *xx_pchrom_to_format(xx_pchrom *pchrom) {
    return pchrom ? &pchrom->format : NULL;
}
static inline void XPchrom_init(xx_pchrom *pchrom, xx_io_device *dev,
                                int64_t base_address) {
    xx_pchrom_init(pchrom, dev, base_address);
}
static inline xx_pchrom *XPchrom_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_pchrom_create(dev, base_address);
}
static inline void XPchrom_free(xx_pchrom *pchrom) { xx_pchrom_free(pchrom); }
static inline bool XPchrom_is_valid(xx_pchrom *pchrom, xx_pd_struct *pd) {
    return pchrom ? xx_format_is_valid(&pchrom->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCHROM_H */
