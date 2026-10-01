/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_hxs.h @brief Microsoft Help 2 (.HxS) / Microsoft Reader (.lit)
 *  "ITOLITLS" storage reader. */

#ifndef XXFCLIB_FORMAT_HXS_H
#define XXFCLIB_FORMAT_HXS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An "ITOLITLS" storage file: Microsoft Help 2 titles (.HxS and the
 * .HxI / .HxR / .HxQ / .HxW files next to them) and Microsoft Reader e-books
 * (.lit).  Office ships its .HxS help as a resource-only PE whose ".its"
 * section holds the storage; the reader then starts at the "ITOLITLS"
 * header (base address), and every offset below is from there.  All fields
 * are little endian.
 *
 *   0x00  char[8] "ITOLITLS"
 *   0x08  u32     version 1
 *   0x0C  u32     0x28, offset of the header section table
 *   0x10  u32     5 header sections
 *   0x14  u32     post-header length (0xE8)
 *   0x18  GUID    {0A9007C1-4076-11D3-8789-0000F8105754}
 *   0x28  5 x (u64 offset, u64 size) of the header sections
 *   0x78  post-header: u32 2, u32 0x98 (CAOL offset in the post-header),
 *         directory fields (+0x40: u64 number of directory entries),
 *         directory-index fields; at +0x98 "CAOL", version 2, length 0x50,
 *         and at CAOL+0x30 "ITSF", version 4, length 0x20, 0 or 1, u64 offset
 *         of content section 0, time stamp, language id.
 *
 * Header section 0: u32 0x1FE, 0, u64 file size, 0, 0.  Header section 1:
 * the directory, an "IFCM" header (version 1, chunk size, 0x100000, -1, -1,
 * u32 chunk count, 0) and that many chunks.  "AOLL" (listing) chunks carry a
 * 48-byte header (quickref length at +4) and entries from byte 48 up to
 * (chunk size - quickref length); the chunk's last u16 counts them, and the
 * counts add up to the post-header's entry count.  Other chunks are skipped;
 * header sections 2-4 (directory index, two GUID blocks) are not needed.
 * An entry is
 *
 *   encint name length, UTF-8 name, encint section, encint offset,
 *   encint size
 *
 * (encint: big-endian base-128, high bit set on all bytes but the last).
 * Names starting with '/' are the title's own files ('/'-ended ones are
 * folders); names starting with "::" are storage metadata.
 *
 * Content section 0 is stored.  Every further section is named in
 * "::DataSpace/NameList" and lives in "::DataSpace/Storage/<name>/Content".
 * Its "Transform/List" names the transforms applied; a section decodes when
 * that is exactly the LZX transform {0A9007C6-4076-11D3-8789-0000F8105754}
 * (or HTML Help's {7FC28940-9D31-11D0-9B27-00A0C91E9C7C}).  Its
 * "ControlData" (u32 dword count, "LZXC", version 2 or 3, reset interval and
 * window size in 32 KiB units) and
 * "Transform/<guid>/InstanceData/ResetTable" (version, entry count, entry
 * size 8, table offset 0x28, u64 uncompressed size, u64 compressed size, u64
 * frame size 0x8000, then the compressed offset of every 32 KiB frame)
 * describe one LZX stream that starts over every "reset interval" frames.
 * Sections under DES (DRM-protected .lit books: "EbEncryptDS",
 * "EbEncryptOnlyDS") are listed but not decoded.  A reset group is decoded
 * whole when it is at most 32 MiB, otherwise only its first 32 MiB.
 *
 * The 0x2C-byte CAOL variant (directory entries without offsets) is not
 * accepted.  Records are the '/' entries, folders first, then files in
 * section and offset order, named without the leading '/'.
 */
typedef struct xx_hxs {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t sections;         /**< Content sections, section 0 included. */
    uint32_t lzx_sections;     /**< Sections that decode as LZX. */
    uint32_t chunk_size;       /**< Directory chunk size. */
    uint32_t listing_chunks;   /**< AOLL chunks. */
    uint64_t entries;          /**< All directory entries, "::" ones too. */
    uint64_t folders;          /**< Folder records. */
    uint64_t unsupported;      /**< File records that cannot be decoded. */
    int64_t content_offset;    /**< Content section 0, from base. */
    int64_t declared_size;     /**< File size in header section 0. */
} xx_hxs;

typedef xx_hxs xx_hxs_t;

XXFC_API void xx_hxs_init(xx_hxs *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_hxs *xx_hxs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hxs_destroy(xx_hxs *archive);
XXFC_API void xx_hxs_free(xx_hxs *archive);

XXFC_API bool xx_hxs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hxs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hxs_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_hxs_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hxs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hxs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hxs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hxs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hxs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HXS_H */
