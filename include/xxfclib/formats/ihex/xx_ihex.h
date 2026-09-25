/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ihex.h @brief Intel HEX (IHex) text image reader. */

/* Intel HEX - an ASCII transport encoding for a sparse binary image, the
 * sibling of Motorola S-record.  Every line is
 *
 *   : <count> <address> <type> <data...> <checksum>
 *
 * with two hex digits per byte after the colon.  count is the number of data
 * bytes, the address is a 16-bit offset, and the checksum is the two's
 * complement of the low byte of the sum of every other byte on the line, so
 *
 *   (count + address bytes + type + data bytes + checksum) & 0xFF == 0
 *
 * and a line is 11 + 2*count characters.
 *
 * Record types
 *   00  data, at base + address
 *   01  end of file, no data
 *   02  extended segment address: base = value << 4       (2 data bytes)
 *   03  start segment address, CS:IP entry point            (4 data bytes)
 *   04  extended linear address: base = value << 16        (2 data bytes)
 *   05  start linear address, 32-bit entry point            (4 data bytes)
 * Any other type (Samsung's 20/22 among them) is refused.  The most recent
 * 02 or 04 record sets the base (the address field of 01..05 is ignored).
 *
 * The reader publishes the image the way 7-Zip's IHex handler does: every run
 * of data records that continues exactly where the previous one ended is one
 * block, and each block is one archive record.  Records are never merged out
 * of text order and gaps are never filled, so nothing is allocated for the
 * address space: the parse keeps one small descriptor per block and
 * extraction streams each block from the text again.  A block is named after
 * its start address, "08000000.bin"; when two blocks start at the same
 * address the later ones become "08000000_<block index>.bin".
 *
 * There is no binary magic, but the first record is self-describing: a colon,
 * a hex count consistent with the type, hex digits and a checksum.
 * xx_ihex_check_magic() tests exactly that on the detector's 64-byte window.
 * check_is_valid() then walks only the lines that start in the first 64 KiB
 * of text: each must be a well formed record, and the text read must carry
 * at least one data byte.  handle_base_info() applies the same strict rules
 * to the same lines; past them the first line that breaks a rule ends the
 * text and the rest is overlay, so every file check_is_valid() accepts can be
 * opened.
 *
 * Lines may end in LF, CR LF or CR; surrounding blanks are ignored.  The EOF
 * record ends the image: blank lines after it are part of the text and the
 * first other byte starts the overlay.  A Ctrl-Z at the start of a line ends
 * the text as well (a trailing run of Ctrl-Z padding is counted as text).  A
 * file without an EOF record ends at the end of the device.
 */

#ifndef XXFCLIB_FORMAT_IHEX_H
#define XXFCLIB_FORMAT_IHEX_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest accepted line, including surrounding blanks; a legal record is at
 *  most 11 + 2*255 = 521 characters. */
#define XX_IHEX_MAX_LINE_LENGTH 600U

/** Blocks accepted per file.  Past the strict window the record that would
 *  open one block more ends the text. */
#define XX_IHEX_MAX_BLOCKS 65536U

typedef struct xx_ihex xx_ihex;
typedef struct xx_ihex xx_ihex_t;
typedef struct xx_ihex XIhex;

struct xx_ihex {
    Abstractformat format;
    uint64_t number_of_lines;        /**< Records accepted, all types. */
    uint64_t number_of_data_records; /**< Type 00 records carrying data. */
    uint64_t data_bytes;             /**< Payload bytes carried by them. */
    uint64_t number_of_blocks;       /**< Archive records published. */
    uint64_t load_address;           /**< Lowest addressed byte. */
    uint64_t end_address;            /**< Highest addressed byte. */
    uint32_t entry_point;            /**< Type 05 EIP, or type 03 CS:IP as
                                          (CS << 16) | IP; 0 when absent. */
    uint8_t entry_type;              /**< 3 or 5; 0 when absent. */
    bool has_entry_point;
    bool has_eof_record;
    bool has_segment_records;        /**< At least one type 02 record. */
    bool has_linear_records;         /**< At least one type 04 record. */
    int64_t stream_end;              /**< base_address + format_size, or -1. */
    void *internal;
};

XXFC_API void xx_ihex_init(xx_ihex *archive, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_ihex *xx_ihex_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ihex_destroy(xx_ihex *archive);
XXFC_API void xx_ihex_free(xx_ihex *archive);

XXFC_API bool xx_ihex_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ihex_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ihex_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_ihex_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ihex_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ihex_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ihex_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ihex_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ihex_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decode block `index` and write its bytes to destination (NULL only
 *  verifies that the block decodes). */
XXFC_API bool xx_ihex_unpack_block_to_device(xx_ihex *archive, uint64_t index,
                                             xx_io_device *destination,
                                             xx_pd_struct *pd);

/** Bounded probe: true when the first non-empty line of the device is a
 *  well formed Intel HEX record whose checksum verifies.  Reads at most
 *  4 * XX_IHEX_MAX_LINE_LENGTH bytes. */
XXFC_API bool xx_ihex_probe_device(xx_io_device *dev, int64_t base_address);

/** Detector prefilter over the first magic_size (<= 64) bytes of a file: a
 *  colon, a type 00 or 02..05 record whose hex count fits the type (a data
 *  record must carry data; a file that opens with the EOF record has nothing
 *  to detect), hex digits for as much of the first record as the window
 *  holds and, when the record ends inside the window, a verified checksum
 *  followed by a line end. */
XXFC_API bool xx_ihex_check_magic(const uint8_t *magic, size_t magic_size);

XXFC_API uint64_t xx_ihex_get_number_of_lines(const xx_ihex *archive);
XXFC_API uint64_t xx_ihex_get_data_bytes(const xx_ihex *archive);
XXFC_API uint64_t xx_ihex_get_number_of_blocks(const xx_ihex *archive);
/** Start address and size of block `index`; 0 when out of range. */
XXFC_API uint64_t xx_ihex_get_block_address(const xx_ihex *archive,
                                            uint64_t index);
XXFC_API uint64_t xx_ihex_get_block_size(const xx_ihex *archive,
                                         uint64_t index);
XXFC_API uint64_t xx_ihex_get_load_address(const xx_ihex *archive);
XXFC_API uint32_t xx_ihex_get_entry_point(const xx_ihex *archive);
XXFC_API bool xx_ihex_get_has_entry_point(const xx_ihex *archive);
XXFC_API int64_t xx_ihex_get_stream_end(const xx_ihex *archive);

static inline Abstractformat *xx_ihex_to_format(xx_ihex *archive) {
    return archive ? &archive->format : NULL;
}
static inline void XIhex_init(xx_ihex *archive, xx_io_device *dev,
                              int64_t base_address) {
    xx_ihex_init(archive, dev, base_address);
}
static inline xx_ihex *XIhex_create(xx_io_device *dev, int64_t base_address) {
    return xx_ihex_create(dev, base_address);
}
static inline void XIhex_free(xx_ihex *archive) { xx_ihex_free(archive); }
static inline bool XIhex_is_valid(xx_ihex *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IHEX_H */
