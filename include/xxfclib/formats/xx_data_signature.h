/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_data_signature.h
 * @brief Matching a parsed byte signature against a buffer.
 *
 * A signature is a sequence of records evaluated left to right from a starting
 * offset, each consuming some bytes and moving a cursor. Literal bytes, "any
 * n bytes", "n printable bytes", a bounded search, and two jump forms that
 * follow a pointer stored in the data.
 *
 * This is the buffer-side counterpart to the rest of xx_data: it takes the
 * bytes and a length rather than a file handle, so it can be run over a
 * mapping, a decompressed member, or a window a caller already holds.
 *
 * The two jump records need to convert between file offsets and the addresses
 * an image is loaded at. That mapping belongs to whoever knows the executable
 * layout, so it arrives as a pair of callbacks rather than as a hard
 * dependency. For the common case there is nothing to write:
 * xx_data_sig_context_from_memory_map() wires the context straight to the
 * xx_memory_map any format reader already publishes. A signature that uses no
 * jump record needs neither.
 */

#ifndef XX_DATA_SIGNATURE_H
#define XX_DATA_SIGNATURE_H

#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/xx_memory_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What one signature record asks of the bytes under the cursor. */
typedef enum xx_data_sig_kind_e {
    /** Literal bytes that must match exactly. */
    XX_DATA_SIG_BYTES = 0,
    /** Advance the cursor by `window`, whatever those bytes are. */
    XX_DATA_SIG_SKIP,
    /** `window` bytes, none of them zero. */
    XX_DATA_SIG_NOT_NULL,
    /** `window` bytes, all printable ASCII (0x20..0x7e). */
    XX_DATA_SIG_ANSI,
    /** `window` bytes, none printable ASCII. */
    XX_DATA_SIG_NOT_ANSI,
    /** `window` bytes, none printable ASCII and none zero. */
    XX_DATA_SIG_NOT_ANSI_AND_NULL,
    /** `window` bytes, all ASCII digits. */
    XX_DATA_SIG_ANSI_NUMBER,
    /** Search forward for `data` within `find_delta + data_size` bytes. */
    XX_DATA_SIG_FIND_BYTES,
    /** Read a signed displacement and follow it, relative to the end of the
     *  field -- the x86 call/jmp convention. */
    XX_DATA_SIG_REL_OFFSET,
    /** Read an absolute address and follow it. */
    XX_DATA_SIG_ADDRESS
} xx_data_sig_kind;

/** One record of a parsed signature. */
typedef struct xx_data_sig_record_s {
    xx_data_sig_kind kind;
    /** Literal bytes, for BYTES and FIND_BYTES. Not owned. */
    const uint8_t *data;
    int64_t data_size;
    /** Byte count, for SKIP and the character-class records. */
    int64_t window;
    /** Extra bytes FIND_BYTES may scan past its own length. */
    int64_t find_delta;
    /** Width of the stored pointer: 1, 2, 4 or 8. */
    int address_size;
} xx_data_sig_record;

/** A parsed signature: records evaluated in order. */
typedef struct xx_data_signature_s {
    const xx_data_sig_record *records;
    int count;
} xx_data_signature;

/**
 * @brief Translate a file offset to the address the byte is loaded at.
 * @return the address, or UINT64_MAX when the offset is not mapped.
 */
typedef uint64_t (*xx_data_sig_offset_to_address_fn)(void *context,
                                                     int64_t offset);
/**
 * @brief Translate a loaded address back to a file offset.
 * @return the offset, or -1 when the address is not mapped.
 */
typedef int64_t (*xx_data_sig_address_to_offset_fn)(void *context,
                                                    uint64_t address);

/**
 * @brief How to read integers and follow pointers while matching.
 *
 * Leave the callbacks NULL for a signature with no jump record; a jump
 * record then fails the match rather than guessing a layout.
 */
typedef struct xx_data_sig_context_s {
    bool big_endian;
    /**
     * Wrap a relative jump inside its 64 KiB segment instead of going
     * through the address map. Real-mode images (COM, MS-DOS) need this;
     * for anything else it must stay false, since wrapping would silently
     * turn an out-of-range displacement into a plausible offset.
     */
    bool segment_wrap;
    xx_data_sig_offset_to_address_fn offset_to_address;
    xx_data_sig_address_to_offset_fn address_to_offset;
    /**
     * Read a jump record's stored pointer as zero when the field runs off
     * the end of the buffer, instead of failing the match there.
     *
     * Refusing is the safer default and stays the default: a signature that
     * asks for four bytes it has not got has not been satisfied. The DIE
     * signature databases were written against an engine whose readers
     * answer zero past the end and carry on, so a '$$$$' or '####' record
     * sitting within its own width of the end matches there and not here.
     * Set this only to reproduce that engine.
     */
    bool read_past_end_as_zero;
    /** Passed back to both callbacks; unused by this module. */
    void *context;
} xx_data_sig_context;

/**
 * @brief Fill a context from a format reader's memory map.
 *
 * Every xxfclib format reader publishes an xx_memory_map via
 * xx_format_get_memory_map(), and that map already carries what matching a
 * signature needs: the endianness, the file type, and the offset/address
 * translation over its records. This wires the three together so a caller
 * holding a reader does not have to restate any of it.
 *
 * `segment_wrap` is set for the real-mode types (COM, MS-DOS), matching the
 * rule the signature databases were written against. The map is borrowed,
 * not copied, so it must outlive every match made through the context.
 *
 * @return false when either argument is NULL.
 */
XXFC_API bool xx_data_sig_context_from_memory_map(
    xx_data_sig_context *context, const xx_memory_map *map);

/**
 * @brief As above, choosing how the map resolves an overlap and whether a
 *        pointer read past the end fails the match.
 *
 * xx_data_sig_context_from_memory_map() is this with
 * XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL and @p read_past_end_as_zero false.
 * Passing XX_MEMORY_MAP_LOOKUP_FIRST_MATCH and true together reproduces the
 * DIE engine exactly, which is what a caller evaluating the DIE signature
 * databases wants.
 */
XXFC_API bool xx_data_sig_context_from_memory_map_ex(
    xx_data_sig_context *context, const xx_memory_map *map,
    xx_memory_map_lookup_t lookup, bool read_past_end_as_zero);

/**
 * @brief Match `signature` against `data` starting at `offset`.
 *
 * @param end_offset  when non-NULL, receives the cursor position after the
 *                    last record on a match. Untouched on a mismatch.
 * @return true only when every record matched.
 *
 * A record that would read past the end of the buffer is a mismatch, not a
 * truncation: matching fewer bytes than the signature asks for would report a
 * match the data does not support.
 */
XXFC_API bool xx_data_signature_match(const void *data, size_t data_size,
                                      int64_t offset,
                                      const xx_data_signature *signature,
                                      const xx_data_sig_context *context,
                                      int64_t *end_offset);

/**
 * @brief Whether `window` bytes at `offset` all satisfy a character class.
 *
 * Exposed because the classes are useful on their own -- deciding whether a
 * name field holds text, for instance -- and because a caller testing one
 * class should not have to build a one-record signature.
 * An empty window is true; a window running past the end is false.
 */
XXFC_API bool xx_data_class_check(const void *data, size_t data_size,
                                  int64_t offset, int64_t window,
                                  xx_data_sig_kind kind);

/* ========================================================================= */
/* --- The signature notation                                            --- */
/* ========================================================================= */

/**
 * @brief Canonicalise a signature written in the usual notation.
 *
 * The form the signature databases are written in, and the one every tool
 * that ships such a database has settled on:
 *
 *   - `'text'` quoted literals become hex;
 *   - `?` becomes `.`, the single-nibble wildcard;
 *   - spaces are dropped and hex digits are lowercased.
 *
 * The result is the flat lowercase string xx_data_signature_parse() reads.
 * Quoting is all-or-nothing: a signature containing no apostrophe is taken
 * to have no literals, so an apostrophe byte written as hex stays hex.
 *
 * @return the canonical text, owned by the caller and released with
 *         xx_str_free(), or NULL if it could not be allocated.
 */
XXFC_API char *xx_data_sig_normalize(const char *text);

/**
 * @brief Parse canonical text into records.
 *
 * Understands the record forms the notation provides: literal bytes, `.`
 * skips, `*` non-zero runs, the `%%` / `%&` / `!%` / `_%` character classes,
 * `+` forward search, `$` relative jump and `#` absolute jump (with the
 * `#[base]#` spelling accepted and its base consumed, as the databases
 * expect, though nothing reads it back).
 *
 * @param out  filled in even when parsing stops early; check `count`.
 * @return false if a character could not be understood or memory ran out.
 *         Records parsed before that point are still in @p out, which
 *         matches how the databases are evaluated: a signature is used for
 *         as far as it parsed.
 *
 * Release with xx_data_signature_free(). A signature assembled by hand,
 * pointing at records the caller owns, must NOT be passed to that.
 */
XXFC_API bool xx_data_signature_parse(xx_data_signature *out,
                                      const char *normalized);

/** @brief Release a signature produced by xx_data_signature_parse(). */
XXFC_API void xx_data_signature_free(xx_data_signature *signature);

/**
 * @brief Normalise, parse and match in one call.
 * @return true when every record matched at @p offset.
 */
XXFC_API bool xx_data_signature_match_text(const void *data, size_t data_size,
                                           int64_t offset, const char *text,
                                           const xx_data_sig_context *context);

/**
 * @brief Normalise, parse, and search for the first offset that matches.
 *
 * @param offset  where to start.
 * @param length  how far to search; negative means to the end of the buffer.
 *                The range is clamped, so a length running past the end
 *                searches what is there.
 * @return the matching offset, or -1.
 *
 * A signature beginning with literal bytes is found by searching for those
 * bytes and matching the rest only at the hits, which is what makes scanning
 * a whole file with thousands of signatures affordable.
 */
XXFC_API int64_t xx_data_signature_find_text(
    const void *data, size_t data_size, int64_t offset, int64_t length,
    const char *text, const xx_data_sig_context *context);

#ifdef __cplusplus
}
#endif

#endif /* XX_DATA_SIGNATURE_H */
