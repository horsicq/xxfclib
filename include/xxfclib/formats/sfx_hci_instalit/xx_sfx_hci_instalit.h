/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_hci_instalit.h
 *  @brief HCI Instalit / "Shadow" installer: the Win16 SFX and its data
 *         volumes (*.001).
 */

#ifndef XXFCLIB_FORMAT_SFX_HCI_INSTALIT_H
#define XXFCLIB_FORMAT_SFX_HCI_INSTALIT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Instalit (HCI) installer: a Win16 "Shadow" SFX executable or one
 *        of the numbered data volumes it installs from.
 *
 * Nothing is executed: the executable is parsed only as far as its NE
 * resource table and the footer at the end of the file.
 *
 * SFX executable (MZ + NE, module description "Stub to copy Instalit/Shadow
 * and execute it"):
 *   - NE resources of the custom type "EXEFILE" carry the installer engine
 *     (SHADOW, CLEANUP, DOSEXEC, BWCCDLL).  The type and resource names are
 *     either Pascal strings in the resource table or, in later stubs, integer
 *     ids named by an RT_NAMETABLE (type 15) resource whose entries are
 *     {u16 size, u16 type, u16 id, type name NUL, id name NUL}.  Each
 *     resource is
 *       +0 u32 CRC-32 of the unpacked data
 *       +4 u32 packed size
 *       +8 PKWARE DCL implode stream (header 00|01, 04..06)
 *     padded to the resource alignment; older stubs keep the executables
 *     raw (the resource starts with "MZ") instead.
 *   - An optional 29-byte footer closes the file (see below).  Its pointers
 *     lead to "\x08[SCRIPT]" (the install script, every byte XOR 0x67, up to
 *     [PVL] or the footer), "\x05[PVL]" (the packed member data) and
 *     "\x05[PVM]" (the catalog).
 *
 * Data volume (*.001): the packed member data from offset 0, the catalog
 * "\x05[PVM]" and records, disk labels, and the same 29-byte footer.
 *
 * Footer, the last 29 bytes of the file:
 *   +0  u8     0x0C
 *   +1  char   12 ASCII digits (the builder serial, "934730434875" in every
 *              known file; any digits are accepted)
 *   +13 u32    [SCRIPT] offset (in a volume: the offset in the setup program)
 *   +17 u32    end of the catalog records
 *   +21 u32    [PVL] offset; 0 in a data volume (data starts at offset 0)
 *   +25 u32    [PVM] offset
 *
 * Catalog: records follow "\x05[PVM]" back to back up to the footer's end
 * pointer.  A record starting 0x1C is 55 or 59 bytes (the stride is the one
 * that tiles the catalog), a record starting 0x1A is 47 bytes:
 *   +0x00 u8    0x1C / 0x1A
 *   +0x01 u32   data offset, relative to the start of the member data
 *   +0x05 u8    method: 0xF0 PKWARE DCL, 0xD0 Instalit LZH, 0xE0 Instalit
 *               LZW, 0xC8 stored (packed size equals unpacked size); any
 *               other method is refused
 *   +0x06 char  name[13], DOS 8.3, NUL terminated
 *   +0x13 u8    DOS attributes
 *   +0x14 u32   packed size
 *   +0x18 u16   DOS date, +0x1A u16 DOS time
 *   0x1C: +0x1C u32 CRC-32, +0x20 u8[3] disk, +0x23 u32 unpacked size
 *   0x1A: +0x1C u16 CRC-16/ARC, +0x1E u8[3] disk, +0x21 u32 unpacked size
 *
 * A data volume is one disk of a set.  Only members whose data lies wholly
 * inside this file (same disk as the first record, range below [PVM]) are
 * listed; members on other disks, or that continue on the next disk, are
 * counted in @c members_elsewhere.
 *
 * Instalit LZH (method 0xD0): u16 node count (2..0x275), u8 node width
 * (1..10), the node table, then an LSB-first stream of tree-coded symbols
 * (0..255 literal, 256 end, 257..314 match of length symbol - 254).  A match
 * position is a fixed 64-symbol prefix code (the canonical code for lengths
 * 3, 4x3, 5x8, 6x12, 7x24, 8x16, each length's codes assigned in order of
 * their bit-reversed value) followed by 0..7 low bits, the count growing
 * with the history until the 8 KiB window, preset to spaces, has filled.
 *
 * Instalit LZW (method 0xE0): an LSB-first stream of tokens.  Bit 0 starts a
 * literal (8 more bits).  Bit 1 starts a code of bit_length(tokens - 1)
 * bits: 0 ends the stream, v >= 1 repeats token v-1 followed by the first
 * byte of token v (plain LZW numbering, one entry per token).  The decoded
 * bytes are then run-length expanded: 0x90 n repeats the previous byte n-1
 * more times, 0x90 0 is a literal 0x90.  The model matches every stream in
 * the known files up to about 640 tokens; a longer stream that departs from
 * it fails its CRC-16 and is refused rather than extracted wrongly.
 */
typedef struct xx_sfx_hci_instalit {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t kind;              /**< XX_SFX_HCI_INSTALIT_KIND_*. */
    uint32_t exefile_count;     /**< EXEFILE resources listed. */
    uint32_t catalog_records;   /**< Records in the [PVM] catalog. */
    uint32_t record_size;       /**< Catalog stride: 47, 55, 59 or 0. */
    uint32_t members_elsewhere; /**< Catalog records not held by this file. */
    bool has_footer;
    bool has_script;
    void *internal;             /**< Parsed layout, owned. */
} xx_sfx_hci_instalit;

typedef xx_sfx_hci_instalit xx_sfx_hci_instalit_t;

#define XX_SFX_HCI_INSTALIT_KIND_SFX 1U
#define XX_SFX_HCI_INSTALIT_KIND_VOLUME 2U

/** Name given to the decoded install script of an SFX. */
#define XX_SFX_HCI_INSTALIT_SCRIPT_NAME "SCRIPT.INF"

XXFC_API void xx_sfx_hci_instalit_init(xx_sfx_hci_instalit *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_sfx_hci_instalit *xx_sfx_hci_instalit_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_hci_instalit_destroy(xx_sfx_hci_instalit *archive);
XXFC_API void xx_sfx_hci_instalit_free(xx_sfx_hci_instalit *archive);

XXFC_API bool xx_sfx_hci_instalit_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_sfx_hci_instalit_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_hci_instalit_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_hci_instalit_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_hci_instalit_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfx_hci_instalit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_hci_instalit_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_hci_instalit_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_hci_instalit_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one Instalit LZH (method 0xD0) stream held in memory.
 *
 * @param input        the stream, starting at its node-count field
 * @param input_size   bytes available
 * @param output       receives exactly @p output_size bytes
 * @param output_size  expected unpacked size
 * @param consumed     optional; input bytes the stream occupied
 * @return true when the end symbol follows exactly @p output_size bytes
 */
XXFC_API bool xx_sfx_hci_instalit_lzh_decode_memory(const uint8_t *input,
                                                    size_t input_size,
                                                    uint8_t *output,
                                                    size_t output_size,
                                                    size_t *consumed);

/**
 * @brief Decode one Instalit LZW (method 0xE0) stream held in memory,
 *        including its run-length layer.
 *
 * @param input        the stream
 * @param input_size   bytes available
 * @param output       receives exactly @p output_size bytes
 * @param output_size  expected unpacked size (after run-length expansion)
 * @param consumed     optional; input bytes the stream occupied
 * @return true when the end code follows exactly @p output_size bytes
 */
XXFC_API bool xx_sfx_hci_instalit_lzw_decode_memory(const uint8_t *input,
                                                    size_t input_size,
                                                    uint8_t *output,
                                                    size_t output_size,
                                                    size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_HCI_INSTALIT_H */
