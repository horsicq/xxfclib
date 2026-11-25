/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Atari 8-bit DOS 2.x filesystem in standard ATR containers.
 *
 * Supported explicit variants: DOS 2.0S, 720x128; DOS 2.5 enhanced density,
 * 1040x128; and DOS 2.0D, 720x256 with three short 128-byte boot sectors.
 * The 16-byte ATR header, VTOC at sector 360, 64 root directory slots in
 * sectors 361-368, and 10-bit linked file sectors are parsed natively.
 * DOS 2.5's mirrored extended VTOC at sector 1024 is validated and files
 * linked into sectors 721-1023 can be extracted. Every file ID, allocated
 * sector, chain count, byte count, crosslink and cycle is checked. Empty files
 * retain their allocated zero-byte terminal sector. Deletions are not listed.
 *
 * MyDOS, SpartaDOS, LiteDOS, Atari DOS 3, nonstandard densities/boot layouts,
 * recovered deleted files and ATASCII conversion are outside this variant.
 * The source cursor, short I/O, cancellation, resolved size/memory budgets
 * and exclusive sibling output staging are supported. MEMORY_LIMIT includes
 * retained view/name/chain data, state and copy buffer, excluding parser
 * temporaries and generic record metadata.
 *
 * Primary: Atari 400/800 Technical Reference Notes and DOS 2.5 manual:
 * https://www.atarionline.pl/biblioteka/materialy_ksiazkowe/Atari%20System%20Reference%20Manual.pdf
 * https://www.atarimania.com/8bit/files/Atari_DOS_2.5%20_1050_Disk_Drive_Owners_Manual.pdf
 * Independent producer: NG-atariATR, https://github.com/grzegorzzyla/NG-atariATR
 */
#ifndef XXFCLIB_FORMAT_ATARI_DOS2_H
#define XXFCLIB_FORMAT_ATARI_DOS2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_atari_dos2_variant_e {
    XX_ATARI_DOS2_AUTO = 0,
    XX_ATARI_DOS2_SD = 1,
    XX_ATARI_DOS2_ED = 2,
    XX_ATARI_DOS2_DD = 3
} xx_atari_dos2_variant;
typedef struct xx_atari_dos2_s {
    Abstractformat format;
    xx_atari_dos2_variant variant;
    uint32_t sector_count;
    uint32_t sector_size;
    uint64_t number_of_records;
} xx_atari_dos2;
typedef xx_atari_dos2 xx_atari_dos2_t;
typedef xx_atari_dos2 XATARI_DOS2;
XXFC_API void xx_atari_dos2_init(xx_atari_dos2 *, xx_io_device *, int64_t);
XXFC_API void xx_atari_dos2_init_ex(xx_atari_dos2 *, xx_io_device *, int64_t, xx_atari_dos2_variant);
XXFC_API xx_atari_dos2 *xx_atari_dos2_create(xx_io_device *, int64_t);
XXFC_API void xx_atari_dos2_destroy(xx_atari_dos2 *);
XXFC_API void xx_atari_dos2_free(xx_atari_dos2 *);
XXFC_API bool xx_atari_dos2_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_atari_dos2_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_atari_dos2_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_atari_dos2_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_atari_dos2_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_atari_dos2_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_atari_dos2_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_atari_dos2_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_atari_dos2_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_atari_dos2_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_atari_dos2_to_format(xx_atari_dos2 *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
