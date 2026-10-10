/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only CP/M2.2 and CP/M3 directory/allocation reader.
 * Geometry and the BIOS disk parameter block are NOT inferred from bytes.
 * init_ex copies an explicit geometry, DPB and zero-based logical-to-physical
 * sector permutation. init uses the declared IBM3740 77x26x128, skew6 preset;
 * that default is not an autodetection claim. Tracks are in image order;
 * callers flatten sides and supply the sector order for their image.
 *
 * Files are listed as USER00..USER15/<safe ASCII8.3 name>. Multi-extent files,
 * 8/16-bit fragmented allocation, empty files and random-write gaps are read.
 * Unwritten blocks/extents are exported as zero bytes; CP/M itself reports an
 * unwritten-record error there. Allocation slack is omitted. RECORDS mode
 * uses128-byte granularity and leaves S1 uninterpreted. LAST_RECORD_USED is an
 * explicit promise that S1 is a valid byte count (0 or128 means a full final
 * record;1..127 gives exact length). CP/M3 itself does not maintain this byte
 * count when writing, so stale counts cannot be discovered automatically.
 * CP/M3 RC values above128 mean128 records, as specified by the BDOS manual.
 * Text EOF bytes are never stripped. Standard F1..F4 and T1..T3 attributes
 * are reported in ATTRIBUTES bits3..6 and0..2 respectively. Labels/date
 * metadata are recognized (unused SFCB subfields are ignored; all-E5 date
 * fields are treated as unstamped formatter metadata);
 * password entries and nonstandard filename attributes/extensions are refused.
 *
 * Bounds:65536 blocks/slots subject to the DPB's16 directory-reservation bits,
 * physical sectors/track<=256, sector size128..4096, block size1..32KiB,
 * logical extents<=512(CP/M2.2)/2048(CP/M3), work<=4 million. No diskdefs/flux,
 * deleted recovery, directory writing, CP/M1 or CP/M86 extension support.
 * Input cursors, short I/O and cancellation are supported. Output uses an
 * exclusive short sibling stage. MAX_MEMBER_SIZE bounds the reported logical
 * file length; MEMORY_LIMIT includes retained directory/member capacities,
 * iterator and copy buffer, excluding parsing transients/generic metadata.
 * Primary Digital Research CP/M Operating System Manual, section6 DPB:
 * https://www.cpm.z80.de/manuals/archive/cpm22htm/ch6.htm
 * CP/M3 Programmer's Guide, section2.3.12:
 * https://www.cpm.z80.de/manuals/cpm3-pgr.pdf
 * Layout implementation evidence: https://www.moria.de/~michael/cpmtools/
 */
#ifndef XXFCLIB_FORMAT_CPM_H
#define XXFCLIB_FORMAT_CPM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cpm_version_e {
    XX_CPM_VERSION_22 = 22,
    XX_CPM_VERSION_3 = 3
} xx_cpm_version;
typedef enum xx_cpm_length_mode_e {
    XX_CPM_LENGTH_RECORDS = 0,
    XX_CPM_LENGTH_LAST_RECORD_USED = 1
} xx_cpm_length_mode;
typedef struct xx_cpm_geometry_s {
    uint32_t tracks;
    uint16_t physical_sectors_per_track, physical_sector_size;
    uint16_t spt, dsm, drm, off;
    uint8_t bsh, blm, exm, al0, al1;
    xx_cpm_version version;
    xx_cpm_length_mode length_mode;
    uint16_t sector_order[256];
} xx_cpm_geometry;
typedef struct xx_cpm {
    Abstractformat format;
    xx_cpm_geometry geometry;
    uint64_t number_of_records, volume_size;
    uint32_t allocation_block_size, allocation_block_count;
    char volume_name[16];
} xx_cpm;
typedef xx_cpm xx_cpm_t;
typedef xx_cpm XCPM;
XXFC_API void xx_cpm_geometry_ibm3740(xx_cpm_geometry *);
XXFC_API void xx_cpm_init(xx_cpm *, xx_io_device *, int64_t);
XXFC_API void xx_cpm_init_ex(xx_cpm *, xx_io_device *, int64_t, const xx_cpm_geometry *);
XXFC_API xx_cpm *xx_cpm_create(xx_io_device *, int64_t);
XXFC_API void xx_cpm_destroy(xx_cpm *);
XXFC_API void xx_cpm_free(xx_cpm *);
XXFC_API bool xx_cpm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cpm_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cpm_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cpm_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cpm_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cpm_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cpm_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cpm_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cpm_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cpm_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cpm_to_format(xx_cpm *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
