/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_srec.h @brief Motorola S-record (SREC) text image reader. */

/* Motorola S-record - an ASCII transport encoding for a sparse binary image.
 * Every line is
 *
 *   S <type> <byte count> <address> <data...> <checksum>
 *
 * with two hex digits per byte after the leading 'S'.  The byte count covers
 * the address, the data and the checksum, so a line is 4 + 2*count characters.
 * The checksum is the one's complement of the low byte of the sum of the count
 * byte, the address bytes and the data bytes, which means
 *
 *   (count + address bytes + data bytes + checksum) & 0xFF == 0xFF
 *
 * Record types
 *   S0  header text, 16-bit (ignored) address - usually a file name or "HDR"
 *   S1  data, 16-bit address        S5  record count, 16-bit
 *   S2  data, 24-bit address        S6  record count, 24-bit
 *   S3  data, 32-bit address        S7  termination, 32-bit entry point
 *   S4  reserved, refused           S8  termination, 24-bit entry point
 *                                   S9  termination, 16-bit entry point
 *
 * There is no binary magic, but the first record is self-describing: 'S', a
 * type digit, a hex count and at least eight hex digits, and the whole line
 * length follows from the count.  xx_srec_check_magic() tests exactly that on
 * the detector's 64-byte window; the probe then verifies checksums.
 *
 * Detection is bounded: check_is_valid() walks only the lines that start in
 * the first 64 KiB of text.  Each must be a well formed record, the text read
 * must carry at least one data byte (a file of only S0 / count / termination
 * records is never auto-detected), and the span ceiling and density rule must
 * hold for it.  handle_base_info() applies the same strict rules to the same
 * lines; past them it keeps the longest prefix that obeys every rule and
 * reports the rest as overlay.  So every file check_is_valid() accepts can
 * be opened.  A file opened by name may still hold an empty image.
 *
 * Lines may end in LF, CR LF or CR; surrounding blanks are ignored.  As in
 * GNU BFD, the termination record (S7/S8/S9) ends the image: blank lines after
 * it are part of the text and the first other byte starts the overlay.  A
 * Ctrl-Z at the start of a line ends the text as well (a trailing run of
 * Ctrl-Z padding is counted as text).  Records may overlap (the later one
 * wins, as in objcopy) and S5/S6 counts are advisory.
 *
 * The reader reassembles the addressed bytes into one contiguous image and
 * publishes it as a single archive record named "image.bin" (the S0 text is
 * metadata only); an S-record file with no data bytes is valid and has no
 * record.  The S7/S8/S9 entry point and the load address are reported as
 * reader metadata.  Addresses are attacker controlled 32-bit values, so two
 * records four gigabytes apart would otherwise materialise a 4 GiB image from
 * a few dozen bytes of text: the parser enforces both an absolute span
 * ceiling and a density rule before anything is allocated.
 */

#ifndef XXFCLIB_FORMAT_SREC_H
#define XXFCLIB_FORMAT_SREC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest accepted line, including the terminator; a legal line is at most
 *  4 + 2*255 = 514 characters. */
#define XX_SREC_MAX_LINE_LENGTH 600U

typedef struct xx_srec xx_srec;
typedef struct xx_srec xx_srec_t;
typedef struct xx_srec XSrec;

struct xx_srec {
    Abstractformat format;
    uint64_t number_of_lines;        /**< S-records accepted, all types. */
    uint64_t number_of_data_records; /**< S1/S2/S3 records. */
    uint64_t data_bytes;             /**< Payload bytes carried by them. */
    uint64_t image_size;             /**< Contiguous image the reader builds. */
    uint64_t load_address;           /**< Lowest addressed byte. */
    uint64_t end_address;            /**< Highest addressed byte. */
    uint64_t entry_point;            /**< S7/S8/S9 address, 0 when absent. */
    uint64_t declared_record_count;  /**< S5/S6 value, 0 when absent. */
    bool has_entry_point;
    bool has_record_count;
    bool is_contiguous;              /**< Hint: records carry at least as many
                                          bytes as the span (false = gaps). */
    uint8_t address_width;           /**< Widest data address: 2, 3 or 4. */
    int64_t stream_end;              /**< base_address + format_size, or -1. */
    void *internal;
};

XXFC_API void xx_srec_init(xx_srec *archive, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_srec *xx_srec_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_srec_destroy(xx_srec *archive);
XXFC_API void xx_srec_free(xx_srec *archive);

XXFC_API bool xx_srec_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_srec_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_srec_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_srec_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_srec_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_srec_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_srec_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_srec_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_srec_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Reassemble the image and write it to destination. */
XXFC_API bool xx_srec_unpack_to_device(xx_srec *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

/** Bounded probe: true when the first non-empty line of the device is a
 *  well formed S-record whose checksum verifies.  Reads at most
 *  4 * XX_SREC_MAX_LINE_LENGTH bytes. */
XXFC_API bool xx_srec_probe_device(xx_io_device *dev, int64_t base_address);

/** Detector prefilter over the first magic_size (<= 64) bytes of a file:
 *  'S', a type digit 0-3, 5 or 6 (S4 is reserved, and a file that opens with
 *  a termination record has no data to detect), a hex count large enough for
 *  the type's address, hex digits for as much of the first record as the
 *  window holds, and a line end right after it when it ends inside the
 *  window. */
XXFC_API bool xx_srec_check_magic(const uint8_t *magic, size_t magic_size);

XXFC_API uint64_t xx_srec_get_number_of_lines(const xx_srec *archive);
XXFC_API uint64_t xx_srec_get_data_bytes(const xx_srec *archive);
XXFC_API uint64_t xx_srec_get_image_size(const xx_srec *archive);
XXFC_API uint64_t xx_srec_get_load_address(const xx_srec *archive);
XXFC_API uint64_t xx_srec_get_entry_point(const xx_srec *archive);
XXFC_API bool xx_srec_get_has_entry_point(const xx_srec *archive);
XXFC_API uint8_t xx_srec_get_address_width(const xx_srec *archive);
XXFC_API int64_t xx_srec_get_stream_end(const xx_srec *archive);
/** Text of the S0 header record, or NULL when it carried none. */
XXFC_API const char *xx_srec_get_header_text(const xx_srec *archive);

static inline Abstractformat *xx_srec_to_format(xx_srec *archive) {
    return archive ? &archive->format : NULL;
}
static inline void XSrec_init(xx_srec *archive, xx_io_device *dev,
                              int64_t base_address) {
    xx_srec_init(archive, dev, base_address);
}
static inline xx_srec *XSrec_create(xx_io_device *dev, int64_t base_address) {
    return xx_srec_create(dev, base_address);
}
static inline void XSrec_free(xx_srec *archive) { xx_srec_free(archive); }
static inline bool XSrec_is_valid(xx_srec *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SREC_H */
