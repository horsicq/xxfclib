/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_data_signature.c
 * @brief Buffer-side signature matching.
 *
 * Both halves of signature handling live here: the notation the databases
 * are written in, and the matching of what it parses to. The record semantics
 * are the ones the DIE engine established, which is what those databases were
 * written against. Three things differ from it, all deliberate:
 *
 *   - the subject is a buffer rather than a mapped file, so this can run over
 *     a decompressed member or a window as easily as over a whole file;
 *   - address translation arrives as callbacks, so a caller with its own
 *     layout can supply one; xx_data_sig_context_from_memory_map() binds them
 *     to the xx_memory_map a format reader already publishes, which is the
 *     path nearly every caller wants;
 *   - the real-mode segment wrap is a field rather than a test on the file
 *     type, because a bare buffer does not know what it came from. The
 *     memory-map adapter sets it from the map's file_type, restoring the
 *     reference's rule wherever a map is available.
 *
 * Integer reads go through xx_data_get_*, which already bounds-check and
 * assemble bytes without casting pointers. A read that falls outside the
 * buffer yields zero there, so every record still checks its own extent
 * before trusting a value.
 */

#include "xxfclib/formats/xx_data_signature.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/xx_memory_map.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* A byte class, expressed once. The predicates below are the whole of what
 * distinguishes the character-class records from one another. */
static bool sig_byte_is_ansi(uint8_t value) {
    return value >= 0x20U && value <= 0x7eU;
}

static bool sig_byte_ok(uint8_t value, xx_data_sig_kind kind) {
    switch (kind) {
        case XX_DATA_SIG_NOT_NULL:
            return value != 0U;
        case XX_DATA_SIG_ANSI:
            return sig_byte_is_ansi(value);
        case XX_DATA_SIG_NOT_ANSI:
            return !sig_byte_is_ansi(value);
        case XX_DATA_SIG_NOT_ANSI_AND_NULL:
            return !sig_byte_is_ansi(value) && value != 0U;
        case XX_DATA_SIG_ANSI_NUMBER:
            return value >= (uint8_t)'0' && value <= (uint8_t)'9';
        default:
            return false;
    }
}

/* Containment written as a subtraction: `offset + size` would overflow on an
 * offset a signature is free to contain. */
static bool sig_range_within(size_t data_size, int64_t offset, int64_t size) {
    if (offset < 0 || size < 0) return false;
    if ((uint64_t)offset > (uint64_t)data_size) return false;
    return (uint64_t)size <= (uint64_t)data_size - (uint64_t)offset;
}

bool xx_data_class_check(const void *data, size_t data_size, int64_t offset,
                         int64_t window, xx_data_sig_kind kind) {
    const uint8_t *bytes;
    int64_t index;

    if (!data || !sig_range_within(data_size, offset, window)) return false;
    bytes = (const uint8_t *)data + offset;

    switch (kind) {
        case XX_DATA_SIG_NOT_NULL:
            for (index = 0; index < window; ++index) {
                if (bytes[index] == 0U) return false;
            }
            return true;
        case XX_DATA_SIG_ANSI:
            for (index = 0; index < window; ++index) {
                if (!sig_byte_is_ansi(bytes[index])) return false;
            }
            return true;
        case XX_DATA_SIG_NOT_ANSI:
            for (index = 0; index < window; ++index) {
                if (sig_byte_is_ansi(bytes[index])) return false;
            }
            return true;
        case XX_DATA_SIG_NOT_ANSI_AND_NULL:
            for (index = 0; index < window; ++index) {
                if (sig_byte_is_ansi(bytes[index]) || bytes[index] == 0U) return false;
            }
            return true;
        case XX_DATA_SIG_ANSI_NUMBER:
            for (index = 0; index < window; ++index) {
                if (bytes[index] < (uint8_t)'0' || bytes[index] > (uint8_t)'9') return false;
            }
            return true;
        default:
            return false;
    }
}

/* Read the pointer a jump record stores. Width is validated by the caller. */
static bool sig_read_pointer(const void *data, size_t data_size,
                             int64_t offset, int address_size,
                             bool big_endian, bool sign_extend,
                             bool past_end_as_zero, uint64_t *out) {
    if (!sig_range_within(data_size, offset, address_size)) {
        /* The field is not there. Zero is what the DIE engine's readers
         * answer for it, so a caller reproducing that engine takes the zero
         * and lets the record continue; everyone else fails the match. */
        if (past_end_as_zero && address_size >= 1 && address_size <= 8) {
            *out = 0U;
            return true;
        }
        return false;
    }
    switch (address_size) {
        case 1:
            *out = sign_extend
                       ? (uint64_t)(int64_t)xx_data_get_i8(data, data_size,
                                                           (size_t)offset)
                       : (uint64_t)xx_data_get_u8(data, data_size,
                                                  (size_t)offset);
            return true;
        case 2:
            /* Unsigned even when sign-extending: the reference reads the
             * 16-bit relative form as unsigned, and a 16-bit displacement
             * wrapping inside its segment is the intended behaviour. */
            *out = (uint64_t)xx_data_get_u16(data, data_size, (size_t)offset,
                                             big_endian);
            return true;
        case 4:
            *out = sign_extend
                       ? (uint64_t)(int64_t)xx_data_get_i32(
                             data, data_size, (size_t)offset, big_endian)
                       : (uint64_t)xx_data_get_u32(data, data_size,
                                                   (size_t)offset, big_endian);
            return true;
        case 8:
            *out = (uint64_t)xx_data_get_u64(data, data_size, (size_t)offset,
                                             big_endian);
            return true;
        default:
            return false;
    }
}

/* A relative jump: the displacement is measured from the end of the field. */
static bool sig_apply_rel_offset(const void *data, size_t data_size,
                                 const xx_data_sig_record *record,
                                 const xx_data_sig_context *context,
                                 int64_t *cursor) {
    uint64_t raw = 0U;
    int64_t displacement;
    int64_t target;

    if (!sig_read_pointer(data, data_size, *cursor, record->address_size,
                          context && context->big_endian, true,
                          context && context->read_past_end_as_zero, &raw)) {
        return false;
    }
    displacement = (int64_t)record->address_size + (int64_t)raw;

    if (context && context->segment_wrap) {
        /* Real mode: the jump stays inside the 64 KiB segment it started in,
         * so the high bits of the cursor are preserved and only the low word
         * advances. */
        int64_t base = *cursor & ~(int64_t)0xffff;
        int64_t within = *cursor & (int64_t)0xffff;
        *cursor = base + ((within + displacement) & (int64_t)0xffff);
        return true;
    }

    if (!context || !context->offset_to_address || !context->address_to_offset) {
        /* No layout to resolve against. Refusing beats guessing: a wrong
         * target would match arbitrary bytes further along. */
        return false;
    }
    {
        uint64_t address = context->offset_to_address(context->context,
                                                      *cursor);
        if (address == UINT64_MAX) return false;
        target = context->address_to_offset(context->context,
                                            address + (uint64_t)displacement);
    }
    if (target < 0) return false;
    *cursor = target;
    return true;
}

/* An absolute jump: the stored value is an address, not a displacement. */
static bool sig_apply_address(const void *data, size_t data_size,
                              const xx_data_sig_record *record,
                              const xx_data_sig_context *context,
                              int64_t *cursor) {
    uint64_t address = 0U;
    int64_t target;

    if (!sig_read_pointer(data, data_size, *cursor, record->address_size,
                          context && context->big_endian, false,
                          context && context->read_past_end_as_zero,
                          &address)) {
        return false;
    }
    if (!context || !context->address_to_offset) return false;
    target = context->address_to_offset(context->context, address);
    if (target < 0) return false;
    *cursor = target;
    return true;
}

/* ------------------------------------------------- memory-map adapter -- */

/* The map's own converters already do the work: they skip virtual-only
 * records, guard the offset arithmetic, and report failure as
 * XX_INVALID_ADDRESS / -1, which is exactly this module's callback contract.
 * These two exist only to carry the map through the void* context. */
static uint64_t sig_map_offset_to_address(void *context, int64_t offset) {
    return xx_memory_map_offset_to_address((const xx_memory_map *)context,
                                           offset);
}

static int64_t sig_map_address_to_offset(void *context, uint64_t address) {
    return xx_memory_map_address_to_offset((const xx_memory_map *)context,
                                           address);
}

/* The same pair under the first-match rule. Two functions rather than one
 * carrying a mode, because the callback signature has only the map to pass
 * and wrapping it in a second struct would outlive its usefulness. */
static uint64_t sig_map_offset_to_address_first(void *context,
                                                int64_t offset) {
    return xx_memory_map_offset_to_address_ex(
        (const xx_memory_map *)context, offset,
        XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
}

static int64_t sig_map_address_to_offset_first(void *context,
                                               uint64_t address) {
    return xx_memory_map_address_to_offset_ex(
        (const xx_memory_map *)context, address,
        XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
}

bool xx_data_sig_context_from_memory_map(xx_data_sig_context *context,
                                         const xx_memory_map *map) {
    return xx_data_sig_context_from_memory_map_ex(
        context, map, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL, false);
}

bool xx_data_sig_context_from_memory_map_ex(xx_data_sig_context *context,
                                            const xx_memory_map *map,
                                            xx_memory_map_lookup_t lookup,
                                            bool read_past_end_as_zero) {
    bool first = (lookup == XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);

    if (!context || !map) return false;

    context->big_endian = (map->endian == XX_ENDIAN_BIG);
    context->read_past_end_as_zero = read_past_end_as_zero;
    /* Real mode has no address map worth following: a relative jump wraps
     * inside its 64 KiB segment instead. This is the one place the file type
     * changes how a record is evaluated. */
    context->segment_wrap = (map->file_type == XX_FILE_TYPE_COM ||
                             map->file_type == XX_FILE_TYPE_MSDOS);
    context->offset_to_address = first ? sig_map_offset_to_address_first
                                      : sig_map_offset_to_address;
    context->address_to_offset = first ? sig_map_address_to_offset_first
                                       : sig_map_address_to_offset;
    /* Borrowed, not copied: the map must outlive the context. Casting away
     * const is confined to here, and neither callback writes through it. */
    context->context = (void *)(size_t)map;
    return true;
}

bool xx_data_signature_match(const void *data, size_t data_size,
                             int64_t offset,
                             const xx_data_signature *signature,
                             const xx_data_sig_context *context,
                             int64_t *end_offset) {
    int64_t cursor = offset;
    int index;

    if (!data || !signature || !signature->records) return false;
    if (signature->count < 0) return false;

    for (index = 0; index < signature->count; ++index) {
        const xx_data_sig_record *record = &signature->records[index];

        switch (record->kind) {
            case XX_DATA_SIG_BYTES: {
                if (record->data_size <= 0 || !record->data) return false;
                if (!sig_range_within(data_size, cursor, record->data_size)) {
                    return false;
                }
                if (xx_rt_memcmp((const uint8_t *)data + cursor, record->data,
                                 (size_t)record->data_size) != 0) {
                    return false;
                }
                cursor += record->data_size;
                break;
            }

            case XX_DATA_SIG_NOT_NULL:
            case XX_DATA_SIG_ANSI:
            case XX_DATA_SIG_NOT_ANSI:
            case XX_DATA_SIG_NOT_ANSI_AND_NULL:
            case XX_DATA_SIG_ANSI_NUMBER: {
                if (record->window <= 0) return false;
                if (!xx_data_class_check(data, data_size, cursor,
                                         record->window, record->kind)) {
                    return false;
                }
                cursor += record->window;
                break;
            }

            case XX_DATA_SIG_SKIP: {
                if (!sig_range_within(data_size, cursor, record->window)) {
                    return false;
                }
                cursor += record->window;
                break;
            }

            case XX_DATA_SIG_FIND_BYTES: {
                int64_t limit;
                int64_t found;

                if (record->data_size <= 0 || !record->data) return false;
                if (cursor < 0 || (uint64_t)cursor > (uint64_t)data_size) {
                    return false;
                }
                /* The search may run `find_delta` bytes beyond the pattern's
                 * own length, and no further. */
                limit = record->find_delta + record->data_size;
                if (!sig_range_within(data_size, cursor, limit)) {
                    /* A window reaching past the end searches what is there,
                     * matching the reference's clamped search rather than
                     * refusing outright. */
                    limit = (int64_t)data_size - cursor;
                    if (limit < record->data_size) return false;
                }
                found = xx_data_find_bytes_buffer_optimize((const uint8_t *)data + cursor,
                                                           (size_t)limit, 0U, record->data,
                                                           (size_t)record->data_size, NULL);
                if (found < 0) return false;
                cursor += found + record->data_size;
                break;
            }

            case XX_DATA_SIG_REL_OFFSET: {
                if (!sig_apply_rel_offset(data, data_size, record, context,
                                          &cursor)) {
                    return false;
                }
                break;
            }

            case XX_DATA_SIG_ADDRESS: {
                if (!sig_apply_address(data, data_size, record, context,
                                       &cursor)) {
                    return false;
                }
                break;
            }

            default:
                /* An unrecognised record is not something to skip quietly:
                 * the signature meant to constrain the data and this code
                 * cannot say whether it does. */
                return false;
        }
    }

    if (end_offset) *end_offset = cursor;
    return true;
}

/* ========================================================================= */
/* --- The signature notation                                            --- */
/* ========================================================================= */

/* Canonicalising and parsing the text form used to live in the DIE engine,
 * next to a second copy of the matcher below. Only the notation was ever
 * specific to it: the records it produces are this module's, and so is
 * everything that acts on them. */

static int sig_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

char *xx_data_sig_normalize(const char *text) {
    static const char *digits = "0123456789abcdef";
    xx_buf_t buf;
    size_t size = text ? xx_rt_strlen(text) : 0U;
    size_t i;
    bool has_quote = false;
    bool inside = false;

    xx_buf_init(&buf);

    for (i = 0U; i < size; ++i) {
        if (text[i] == '\'') {
            has_quote = true;
            break;
        }
    }

    for (i = 0U; i < size; ++i) {
        char c = text[i];

        if (has_quote && c == '\'') {
            inside = !inside;
        } else if (inside) {
            uint8_t value = (uint8_t)c;
            xx_buf_append_char(&buf, digits[value >> 4]);
            xx_buf_append_char(&buf, digits[value & 0x0f]);
        } else if (c != ' ') {
            if (c == '?') {
                xx_buf_append_char(&buf, '.');
            } else if (c >= 'A' && c <= 'Z') {
                xx_buf_append_char(&buf, (char)(c - 'A' + 'a'));
            } else {
                xx_buf_append_char(&buf, c);
            }
        }
    }

    if (!xx_buf_ok(&buf)) {
        xx_buf_free(&buf);
        return NULL;
    }
    return xx_buf_detach(&buf, NULL);
}

/* Appends one zeroed record. Returns NULL when it could not grow. */
static xx_data_sig_record *sig_add(xx_data_signature *signature) {
    xx_data_sig_record *records;
    size_t count = (size_t)signature->count;

    records = (xx_data_sig_record *)xx_mem_realloc(
        (void *)signature->records, (count + 1U) * sizeof(*records));
    if (!records) return NULL;

    signature->records = records;
    xx_mem_zero(&records[count], sizeof(records[count]));
    signature->count = (int)count + 1;
    return &records[count];
}

/* Reads the run of hex digits at `start` into one BYTES record. Returns the
 * number of characters consumed; 0 when there were none. */
static int sig_bytes(xx_data_signature *signature, const char *text,
                     int start, bool *ok) {
    int count = 0;
    int size = (int)xx_rt_strlen(text);
    int i;
    xx_buf_t buf;

    xx_buf_init(&buf);

    for (i = start; i < size; ++i) {
        char c = text[i];

        if (sig_hex_value(c) >= 0) {
            ++count;
            xx_buf_append_char(&buf, c);
        } else if (c == '.' || c == '$' || c == '#' || c == '*' || c == '!' ||
                   c == '_' || c == '%' || c == '+') {
            break;
        } else {
            *ok = false;
            break;
        }
    }

    if (count && xx_buf_ok(&buf)) {
        xx_data_sig_record *record = sig_add(signature);
        int bytes = count / 2;
        uint8_t *data;
        int j;

        if (!record) {
            *ok = false;
            xx_buf_free(&buf);
            return count;
        }

        data = (uint8_t *)xx_mem_alloc((size_t)(bytes ? bytes : 1));
        if (!data) {
            *ok = false;
            xx_buf_free(&buf);
            return count;
        }

        for (j = 0; j < bytes; ++j) {
            data[j] = (uint8_t)((sig_hex_value(buf.data[j * 2]) << 4) |
                                sig_hex_value(buf.data[j * 2 + 1]));
        }

        record->kind = XX_DATA_SIG_BYTES;
        record->data = data;
        record->data_size = bytes;
        record->window = bytes;
    } else if (count) {
        *ok = false;
    }

    xx_buf_free(&buf);
    return count;
}

/* Length of the run of `c` starting at `start`. */
static int sig_run(const char *text, int start, char c) {
    int count = 0;
    int size = (int)xx_rt_strlen(text);
    int i;

    for (i = start; i < size; ++i) {
        if (text[i] != c) break;
        ++count;
    }
    return count;
}

/* Length of the run of the two-character sequence `pair`. */
static int sig_run2(const char *text, int start, const char *pair) {
    int count = 0;
    int size = (int)xx_rt_strlen(text);
    int i;

    for (i = start; i + 1 < size; i += 2) {
        if (text[i] != pair[0] || text[i + 1] != pair[1]) break;
        count += 2;
    }
    return count;
}

/* One record for a run of a repeated form. Each character is half a byte, so
 * the window is the run length halved -- and an odd run therefore asks for
 * nothing, which the matcher rejects. */
static bool sig_add_run(xx_data_signature *signature, xx_data_sig_kind kind,
                        int count) {
    xx_data_sig_record *record = sig_add(signature);

    if (!record) return false;
    record->kind = kind;
    record->window = count / 2;
    return true;
}

bool xx_data_signature_parse(xx_data_signature *out, const char *normalized) {
    int size;
    int i = 0;
    bool ok = true;

    if (!out) return false;
    out->records = NULL;
    out->count = 0;
    if (!normalized) return false;
    size = (int)xx_rt_strlen(normalized);

    while (i < size) {
        char c = normalized[i];
        char c2 = (i + 1) < size ? normalized[i + 1] : '\0';

        if (c == '.') {
            int count = sig_run(normalized, i, '.');
            if (!sig_add_run(out, XX_DATA_SIG_SKIP, count)) return false;
            i += count;
        } else if (c == '*') {
            int count = sig_run(normalized, i, '*');
            if (!sig_add_run(out, XX_DATA_SIG_NOT_NULL, count)) return false;
            i += count;
        } else if (c == '%' && c2 == '%') {
            int count = sig_run2(normalized, i, "%%");
            if (!sig_add_run(out, XX_DATA_SIG_ANSI, count)) return false;
            i += count;
        } else if (c == '%' && c2 == '&') {
            int count = sig_run2(normalized, i, "%&");
            if (!sig_add_run(out, XX_DATA_SIG_ANSI_NUMBER, count)) return false;
            i += count;
        } else if (c == '!' && c2 == '%') {
            int count = sig_run2(normalized, i, "!%");
            if (!sig_add_run(out, XX_DATA_SIG_NOT_ANSI, count)) return false;
            i += count;
        } else if (c == '_' && c2 == '%') {
            int count = sig_run2(normalized, i, "_%");
            if (!sig_add_run(out, XX_DATA_SIG_NOT_ANSI_AND_NULL, count))
                return false;
            i += count;
        } else if (c == '+') {
            /* The run length sets how far past its own length the search may
             * reach: one '+' is 32 bytes. */
            int count = sig_run(normalized, i, '+');
            xx_data_signature temp;
            bool temp_ok = true;
            int consumed;

            temp.records = NULL;
            temp.count = 0;
            consumed = sig_bytes(&temp, normalized, i + count, &temp_ok);

            if (temp.count) {
                xx_data_sig_record *record = sig_add(out);

                if (!record) {
                    xx_data_signature_free(&temp);
                    return false;
                }
                record->kind = XX_DATA_SIG_FIND_BYTES;
                record->data = temp.records[0].data;
                record->data_size = temp.records[0].data_size;
                record->find_delta = 32 * count;
                /* The bytes move to the new record rather than being copied,
                 * so the temporary must not release them. */
                ((xx_data_sig_record *)temp.records)[0].data = NULL;
                i += count + consumed;
            } else {
                i += count;
            }

            if (!temp_ok) ok = false;
            xx_data_signature_free(&temp);
        } else if (c == '$') {
            int count = sig_run(normalized, i, '$');
            xx_data_sig_record *record = sig_add(out);

            if (!record) return false;
            record->kind = XX_DATA_SIG_REL_OFFSET;
            record->address_size = count / 2;
            i += count;
        } else if (c == '#') {
            int count = 0;
            int address_chars = 0;
            bool in_base = false;
            xx_data_sig_record *record;
            int j;

            /* '#[base]#' -- the base address is consumed so the cursor lands
             * past it, and then dropped. The databases carry it and no
             * engine has ever read it back. */
            for (j = i; j < size; ++j) {
                if (normalized[j] == '#') {
                    ++count;
                    ++address_chars;
                } else if (normalized[j] == '[') {
                    ++count;
                    in_base = true;
                } else if (normalized[j] == ']') {
                    ++count;
                    in_base = false;
                } else if (in_base) {
                    ++count;
                } else {
                    break;
                }
            }

            record = sig_add(out);
            if (!record) return false;
            record->kind = XX_DATA_SIG_ADDRESS;
            record->address_size = address_chars / 2;
            i += count;
        } else {
            int consumed = sig_bytes(out, normalized, i, &ok);

            if (consumed) {
                i += consumed;
            } else {
                break;
            }
        }
    }

    return ok;
}

void xx_data_signature_free(xx_data_signature *signature) {
    int i;

    if (!signature || !signature->records) {
        if (signature) {
            signature->records = NULL;
            signature->count = 0;
        }
        return;
    }

    /* A record borrows its literal bytes -- matching never frees them -- so
     * the parser that allocated them releases them here. */
    for (i = 0; i < signature->count; ++i) {
        xx_mem_free((void *)signature->records[i].data);
    }
    xx_mem_free((void *)signature->records);
    signature->records = NULL;
    signature->count = 0;
}

bool xx_data_signature_match_text(const void *data, size_t data_size,
                                  int64_t offset, const char *text,
                                  const xx_data_sig_context *context) {
    char *normalized = xx_data_sig_normalize(text);
    xx_data_signature signature;
    bool result = false;

    if (!normalized) return false;

    (void)xx_data_signature_parse(&signature, normalized);
    if (signature.count) {
        result = xx_data_signature_match(data, data_size, offset, &signature,
                                         context, NULL);
    }

    xx_data_signature_free(&signature);
    xx_str_free(normalized);
    return result;
}

int64_t xx_data_signature_find_text(const void *data, size_t data_size,
                                    int64_t offset, int64_t length,
                                    const char *text,
                                    const xx_data_sig_context *context) {
    char *normalized = xx_data_sig_normalize(text);
    xx_data_signature signature;
    int64_t result = -1;

    if (!normalized) return -1;

    (void)xx_data_signature_parse(&signature, normalized);

    /* Clamp the window: a negative length means "to the end", and one
     * running past the end searches what is there. */
    if (signature.count && data && offset >= 0 &&
        (uint64_t)offset < (uint64_t)data_size) {
        int64_t available = (int64_t)data_size - offset;

        if (length < 0 || length > available) length = available;

        int anchor_idx = -1;
        int64_t prefix_len = 0;
        int j;

        for (j = 0; j < signature.count; ++j) {
            if (signature.records[j].kind == XX_DATA_SIG_BYTES &&
                signature.records[j].data_size > 0) {
                anchor_idx = j;
                break;
            }
            if (signature.records[j].kind == XX_DATA_SIG_SKIP ||
                signature.records[j].kind == XX_DATA_SIG_NOT_NULL ||
                signature.records[j].kind == XX_DATA_SIG_ANSI ||
                signature.records[j].kind == XX_DATA_SIG_NOT_ANSI ||
                signature.records[j].kind == XX_DATA_SIG_NOT_ANSI_AND_NULL ||
                signature.records[j].kind == XX_DATA_SIG_ANSI_NUMBER) {
                prefix_len += signature.records[j].window;
            } else {
                /* Jumps or relative finds before the anchor prevent simple fixed prefix extraction */
                break;
            }
        }

        if (anchor_idx >= 0) {
            int64_t search = offset + prefix_len;
            int64_t limit = offset + length + prefix_len;

            if (limit > (int64_t)data_size) limit = (int64_t)data_size;

            while (search < limit) {
                int64_t found = xx_data_find_bytes_buffer_optimize(
                    (const uint8_t *)data + search, (size_t)(limit - search),
                    0U, signature.records[anchor_idx].data,
                    (size_t)signature.records[anchor_idx].data_size, NULL);

                if (found < 0) break;
                found += search;

                int64_t candidate = found - prefix_len;
                if (candidate >= offset && candidate < offset + length) {
                    if (xx_data_signature_match(data, data_size, candidate,
                                                &signature, context, NULL)) {
                        result = candidate;
                        break;
                    }
                }
                search = found + 1;
            }
        } else {
            int64_t i;

            for (i = 0; i < length; ++i) {
                if (xx_data_signature_match(data, data_size, offset + i,
                                            &signature, context, NULL)) {
                    result = offset + i;
                    break;
                }
            }
        }
    }

    xx_data_signature_free(&signature);
    xx_str_free(normalized);
    return result;
}
