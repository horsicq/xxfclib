/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nexas_pac.h @brief Native NeXAS PAC archive reader. */
#ifndef XXFCLIB_FORMAT_NEXAS_PAC_H
#define XXFCLIB_FORMAT_NEXAS_PAC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NeXAS PAC archives: 12-byte header with positive LE count and mode 0..4.
 * Old directories use fixed 32/64-byte CP932 names and offset/plain/packed
 * lengths; newer 64-byte-name directories are inverted explicit-tree Huffman
 * data preceding a packed-index-size footer. The caller device must end at
 * that footer for new indexes, which have no separate archive extent field.
 * Modes: 0 stored, 1 zero-filled FEE LZSS, 2 explicit-tree MSB Huffman,
 * 3 RFC1950 zlib, 4 stored when lengths match and otherwise RFC1950 zlib.
 * Unknown modes and PACK are refused. Each index/name/member range is checked
 * before enumeration. Payload decoding enforces exact output/packed extent;
 * zlib checks its Adler-32. Empty members and byte-exact payloads are retained.
 * CP932 bytes and '%' use reversible "%XX" escapes; double-byte trail
 * backslashes stay escaped, real path separators become '/'. ASCII-case
 * aliases use "%_<index>" before extensions; unsafe filesystem paths remain
 * listable but cannot create files. Every known input cursor is preserved.
 * Files use exclusive short sibling stages and explicit overwrite publication.
 * Limits: 0xFFFFF members, 16 MiB raw/decoded directory and 256 MiB packed
 * compressed members; output streams to a bounded buffer. Options cap member
 * output and combined dynamic packed/buffer workspace. Encryption and other
 * title-specific transforms are not implemented by this archive reader.
 */
typedef struct xx_nexas_pac {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t method;
    uint32_t name_width;
    bool index_compressed;
} xx_nexas_pac;

typedef xx_nexas_pac xx_nexas_pac_t;
typedef xx_nexas_pac XNexasPac;

XXFC_API void xx_nexas_pac_init(xx_nexas_pac *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_nexas_pac *xx_nexas_pac_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nexas_pac_destroy(xx_nexas_pac *archive);
XXFC_API void xx_nexas_pac_free(xx_nexas_pac *archive);
XXFC_API bool xx_nexas_pac_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nexas_pac_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nexas_pac_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_nexas_pac_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_nexas_pac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nexas_pac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nexas_pac_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Copy the current member to a caller-owned device; NULL verifies it only.
 * Destination must differ from the input device. Options limiting member
 * size and extraction buffers are also honored by this direct C API. */
XXFC_API bool xx_nexas_pac_unpack_current_archive_record_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);
XXFC_API bool xx_nexas_pac_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nexas_pac_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif
#endif
