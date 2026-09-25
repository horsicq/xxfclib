/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield MultiPlatform (ISMP, "InstallShield Java Edition") native
 * launcher.  xx_installshield_multiplatform.h carries the layout.
 *
 * The launcher is a PE, ELF or XCOFF executable; the builder appends the
 * stored resources (instructions.txt, Verify.jar, launch.txt, JVM
 * descriptions, a bundled JVM, setup.jar), a big-endian index and a footer
 * that ends the file with 0xCA82CA82.  Because the index is found from the
 * end of the file, one reader covers every carrier, and nothing of the
 * executable is parsed, executed or emulated.
 *
 * The layout was measured on six launchers (three PE32, two i386 ELF, one
 * AIX XCOFF; ISMP 5.x era).  U3's "SFX ISNI" handler was read to confirm
 * the entry shape (a 15-byte fixed part, the name, then a 0/1 flag that
 * adds 8 bytes); the code here is written from the layout, not from it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_multiplatform/xx_installshield_multiplatform.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder: the enumerator lives in the shared xxfc_defs.h,
 * so its alias macro is tested and the real type is picked up as soon as
 * it is registered there. */
#ifdef INSTALLSHIELD_MULTIPLATFORM
#define XX_INSTALLSHIELD_MULTIPLATFORM_FILE_TYPE \
    XX_FILE_TYPE_INSTALLSHIELD_MULTIPLATFORM
#else
#define XX_INSTALLSHIELD_MULTIPLATFORM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ISMP_FOOTER ((size_t)XX_INSTALLSHIELD_MULTIPLATFORM_FOOTER_SIZE)
#define ISMP_COUNT_SIZE 4U
/* type u8, id u32, size u32, offset u32, name length u16 */
#define ISMP_ENTRY_FIXED 15U
/* The fixed part, a name of at least one byte and the flag. */
#define ISMP_ENTRY_MIN (ISMP_ENTRY_FIXED + 2U)
#define ISMP_EXTRA 8U
/* Real indexes hold 4..8 entries in 134..238 bytes.  These caps only bound
 * what a hostile footer can make the reader read, allocate and compare. */
#define ISMP_MAX_COUNT 1024U
#define ISMP_MAX_INDEX (256U * 1024U)
#define ISMP_RENAME_ATTEMPTS 4U
#define ISMP_FOLD_ANY 0xFFFFFFFFU

typedef struct ismp_member_s {
    char *name;            /* UTF-8; the name listed and written */
    int64_t header_offset; /* absolute */
    int64_t header_size;
    int64_t data_offset;   /* absolute */
    int64_t size;
    uint64_t extra;
    uint32_t id;
    uint32_t hash;
    uint8_t type;
    bool has_extra;
    bool safe;        /* the name can be a file name */
    bool extractable; /* safe and distinct from every earlier output name */
} ismp_member;

typedef struct ismp_index_s {
    ismp_member *items;
    size_t count;
    size_t index;
    int64_t index_offset; /* from the base address */
    int64_t index_size;   /* index start to end of file */
    int64_t format_size;
} ismp_index;

static uint32_t ismp_be16(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1];
}

static uint32_t ismp_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool ismp_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static size_t ismp_put_utf8(char *out, uint32_t c) {
    if (c < 0x80U) {
        out[0] = (char)c;
        return 1U;
    }
    if (c < 0x800U) {
        out[0] = (char)(0xC0U | (c >> 6U));
        out[1] = (char)(0x80U | (c & 0x3FU));
        return 2U;
    }
    if (c < 0x10000U) {
        out[0] = (char)(0xE0U | (c >> 12U));
        out[1] = (char)(0x80U | ((c >> 6U) & 0x3FU));
        out[2] = (char)(0x80U | (c & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (c >> 18U));
    out[1] = (char)(0x80U | ((c >> 12U) & 0x3FU));
    out[2] = (char)(0x80U | ((c >> 6U) & 0x3FU));
    out[3] = (char)(0x80U | (c & 0x3FU));
    return 4U;
}

/* Characters that never belong in a flat output name. */
static bool ismp_char_unsafe(uint32_t c) {
    return c < 0x20U || (c >= 0x7FU && c <= 0x9FU) || c == '/' ||
           c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
           c == '<' || c == '>' || c == '|';
}

/* Decodes one Java modified UTF-8 string (DataOutputStream.writeUTF) of
 * @p length bytes.  Returns false for byte sequences readUTF rejects, and
 * for a raw zero byte, which writeUTF never produces.  When @p out is not
 * NULL it receives standard UTF-8 (never longer than the input) plus a
 * terminator: surrogate pairs are joined, a lone surrogate becomes U+FFFD
 * and an encoded U+0000 becomes '_'; either one, or any character that
 * cannot be part of a file name, clears @p safe. */
static bool ismp_decode_name(const uint8_t *in, size_t length, char *out,
                             bool *safe) {
    size_t at = 0U, written = 0U;
    uint32_t high = 0U;
    bool ok_name = true;
    while (at < length) {
        uint32_t b = in[at], c;
        if (b == 0U) return false;
        if (b < 0x80U) {
            c = b;
            at += 1U;
        } else if ((b & 0xE0U) == 0xC0U) {
            if (length - at < 2U || (in[at + 1U] & 0xC0U) != 0x80U)
                return false;
            c = ((b & 0x1FU) << 6U) | (in[at + 1U] & 0x3FU);
            at += 2U;
        } else if ((b & 0xF0U) == 0xE0U) {
            if (length - at < 3U || (in[at + 1U] & 0xC0U) != 0x80U ||
                (in[at + 2U] & 0xC0U) != 0x80U)
                return false;
            c = ((b & 0x0FU) << 12U) | ((uint32_t)(in[at + 1U] & 0x3FU) << 6U) |
                (in[at + 2U] & 0x3FU);
            at += 3U;
        } else {
            return false;
        }
        if (high != 0U) {
            if (c >= 0xDC00U && c <= 0xDFFFU) {
                c = 0x10000U + ((high - 0xD800U) << 10U) + (c - 0xDC00U);
                high = 0U;
                if (out) written += ismp_put_utf8(out + written, c);
                continue;
            }
            if (out) written += ismp_put_utf8(out + written, 0xFFFDU);
            ok_name = false;
            high = 0U;
        }
        if (c >= 0xD800U && c <= 0xDBFFU) {
            high = c;
            continue;
        }
        if (c >= 0xDC00U && c <= 0xDFFFU) {
            c = 0xFFFDU;
            ok_name = false;
        } else if (c == 0U) {
            c = '_';
            ok_name = false;
        } else if (ismp_char_unsafe(c)) {
            ok_name = false;
        }
        if (out) written += ismp_put_utf8(out + written, c);
    }
    if (high != 0U) {
        if (out) written += ismp_put_utf8(out + written, 0xFFFDU);
        ok_name = false;
    }
    if (out) out[written] = 0;
    if (safe) *safe = ok_name;
    return true;
}

static char ismp_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Device names are matched on the part before the first dot, trailing
 * spaces removed, as Windows does. */
static bool ismp_stem_is(const char *name, size_t stem, const char *device) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!device[index] || ismp_upper(name[index]) != device[index])
            return false;
    return device[stem] == 0;
}

static bool ismp_name_is_device(const char *name) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t length = xx_str_len(name);
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ismp_stem_is(name, stem, devices[index])) return true;
    if ((stem == 4U || stem == 5U) &&
        ((ismp_upper(name[0]) == 'C' && ismp_upper(name[1]) == 'O' &&
          ismp_upper(name[2]) == 'M') ||
         (ismp_upper(name[0]) == 'L' && ismp_upper(name[1]) == 'P' &&
          ismp_upper(name[2]) == 'T'))) {
        const uint8_t *tail = (const uint8_t *)name + 3U;
        /* COM0..COM9, LPT0..LPT9, and the superscript 1, 2, 3 forms. */
        if (stem == 4U && tail[0] >= '0' && tail[0] <= '9') return true;
        if (stem == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U))
            return true;
    }
    return false;
}

/* The decoded name, already free of separators and control characters,
 * must also be something that opens exactly that file. */
static bool ismp_name_safe(const char *name) {
    size_t length, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index)
        if (name[index] != '.' && name[index] != ' ') meaningful = true;
    if (!meaningful) return false;
    /* Windows drops a trailing dot or space, so "a." would open "a". */
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    return !ismp_name_is_device(name);
}

static uint32_t ismp_utf8_next(const char *text, size_t *at) {
    const uint8_t *p = (const uint8_t *)text + *at;
    uint32_t c = p[0];
    if (c < 0x80U) {
        *at += 1U;
        return c;
    }
    if ((c & 0xE0U) == 0xC0U) {
        *at += 2U;
        return ((c & 0x1FU) << 6U) | (p[1] & 0x3FU);
    }
    if ((c & 0xF0U) == 0xE0U) {
        *at += 3U;
        return ((c & 0x0FU) << 12U) | ((uint32_t)(p[1] & 0x3FU) << 6U) |
               (p[2] & 0x3FU);
    }
    *at += 4U;
    return ((c & 0x07U) << 18U) | ((uint32_t)(p[1] & 0x3FU) << 12U) |
           ((uint32_t)(p[2] & 0x3FU) << 6U) | (p[3] & 0x3FU);
}

/* How Windows compares names, made conservative: ASCII letters fold to
 * upper case, U+0131 and U+017F fold to the letters they upper-case to, and
 * any other non-ASCII character may equal any other.  Two names that could
 * open the same file always compare equal; the price is a needless rename
 * now and then. */
static uint32_t ismp_fold(uint32_t c) {
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    if (c == 0x131U) return 'I';
    if (c == 0x17FU) return 'S';
    return ISMP_FOLD_ANY;
}

static uint32_t ismp_name_hash(const char *name) {
    uint32_t hash = 2166136261U;
    size_t at = 0U;
    while (name[at]) {
        hash ^= ismp_fold(ismp_utf8_next(name, &at));
        hash *= 16777619U;
    }
    return hash;
}

static bool ismp_name_equal(const char *left, const char *right) {
    size_t a = 0U, b = 0U;
    while (left[a] && right[b])
        if (ismp_fold(ismp_utf8_next(left, &a)) !=
            ismp_fold(ismp_utf8_next(right, &b)))
            return false;
    return !left[a] && !right[b];
}

static size_t ismp_decimal(char *out, size_t value) {
    char digits[24];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "setup.jar" -> "setup_<number>.jar", or "setup_<number>_<attempt>.jar"
 * after the first attempt collided too. */
static char *ismp_renamed(const char *name, size_t number, unsigned attempt) {
    char suffix[56];
    size_t suffix_length = 0U;
    size_t length = xx_str_len(name);
    size_t insert = length, index;
    char *result;
    suffix[suffix_length++] = '_';
    suffix_length += ismp_decimal(suffix + suffix_length, number);
    if (attempt > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += ismp_decimal(suffix + suffix_length, attempt);
    }
    for (index = length; index > 1U; --index)
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    if (length > SIZE_MAX - suffix_length - 1U) return NULL;
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, insert);
    xx_rt_memcpy(result + insert, suffix, suffix_length);
    xx_rt_memcpy(result + insert + suffix_length, name + insert,
                 length - insert);
    result[length + suffix_length] = 0;
    return result;
}

static bool ismp_collides(const ismp_index *index, size_t limit,
                          const char *name, uint32_t hash) {
    size_t other;
    for (other = 0U; other < limit; ++other) {
        const ismp_member *member = &index->items[other];
        if (member->extractable && member->hash == hash &&
            ismp_name_equal(member->name, name))
            return true;
    }
    return false;
}

/* Each safe member keeps its name unless an earlier output name is equal
 * (as Windows compares names); then it gets "_<record number>" before its
 * extension, retried a few times.  One that still collides stays listed but
 * is never written, so no member can overwrite another. */
static bool ismp_make_unique(ismp_index *index) {
    size_t current;
    for (current = 0U; current < index->count; ++current) {
        ismp_member *member = &index->items[current];
        unsigned attempt;
        member->extractable = false;
        if (!member->safe) continue;
        member->hash = ismp_name_hash(member->name);
        if (!ismp_collides(index, current, member->name, member->hash)) {
            member->extractable = true;
            continue;
        }
        for (attempt = 1U; attempt <= ISMP_RENAME_ATTEMPTS; ++attempt) {
            char *renamed = ismp_renamed(member->name, current + 1U, attempt);
            uint32_t hash;
            if (!renamed) return false;
            hash = ismp_name_hash(renamed);
            if (!ismp_collides(index, current, renamed, hash)) {
                xx_mem_free(member->name);
                member->name = renamed;
                member->hash = hash;
                member->extractable = true;
                break;
            }
            xx_mem_free(renamed);
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Index                                                                   */

static void ismp_index_free(void *opaque) {
    ismp_index *index = (ismp_index *)opaque;
    size_t at;
    if (!index) return;
    if (index->items) {
        for (at = 0U; at < index->count; ++at)
            if (index->items[at].name) xx_mem_free(index->items[at].name);
        xx_mem_free(index->items);
    }
    xx_mem_free(index);
}

/* Validates the footer and the whole index.  With @p out the members are
 * collected too (names decoded and made distinct); the caller frees them
 * with ismp_index_free. */
static bool ismp_parse(Abstractformat *format, ismp_index **out,
                       ismp_index *summary) {
    uint8_t footer[ISMP_FOOTER];
    uint8_t count_bytes[ISMP_COUNT_SIZE];
    uint8_t *buffer = NULL;
    ismp_index *index = NULL;
    int64_t total, size, index_offset, region;
    uint32_t count, entry;
    size_t position;
    bool result = false;
    if (out) *out = NULL;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(ISMP_FOOTER + ISMP_COUNT_SIZE + ISMP_ENTRY_MIN) ||
        !ismp_read_at(format->device,
                      format->base_address + size - (int64_t)ISMP_FOOTER,
                      footer, sizeof(footer)) ||
        ismp_be32(footer + 4U) != XX_INSTALLSHIELD_MULTIPLATFORM_MAGIC)
        return false;
    index_offset = (int64_t)ismp_be32(footer);
    if (index_offset > size - (int64_t)(ISMP_FOOTER + ISMP_COUNT_SIZE +
                                        ISMP_ENTRY_MIN))
        return false;
    region = size - (int64_t)ISMP_FOOTER - index_offset;
    if (region > (int64_t)ISMP_MAX_INDEX ||
        !ismp_read_at(format->device, format->base_address + index_offset,
                      count_bytes, sizeof(count_bytes)))
        return false;
    count = ismp_be32(count_bytes);
    if (count == 0U || count > ISMP_MAX_COUNT ||
        (int64_t)count * (int64_t)ISMP_ENTRY_MIN >
            region - (int64_t)ISMP_COUNT_SIZE)
        return false;
    buffer = (uint8_t *)xx_mem_alloc((size_t)region);
    if (!buffer ||
        !ismp_read_at(format->device, format->base_address + index_offset,
                      buffer, (size_t)region))
        goto done;
    if (out) {
        index = (ismp_index *)xx_mem_calloc(1U, sizeof(*index));
        if (!index) goto done;
        index->items = (ismp_member *)xx_mem_calloc(count, sizeof(ismp_member));
        if (!index->items) goto done;
    }
    position = ISMP_COUNT_SIZE;
    for (entry = 0U; entry < count; ++entry) {
        const uint8_t *fixed;
        uint32_t offset, member_size, name_length;
        size_t start = position;
        bool has_extra, safe = false;
        uint64_t extra = 0U;
        if ((size_t)region - position < ISMP_ENTRY_FIXED) goto done;
        fixed = buffer + position;
        member_size = ismp_be32(fixed + 5U);
        offset = ismp_be32(fixed + 9U);
        name_length = ismp_be16(fixed + 13U);
        position += ISMP_ENTRY_FIXED;
        /* The name and the flag byte after it. */
        if (name_length == 0U || (size_t)name_length >= (size_t)region - position)
            goto done;
        /* Every resource lies before the index. */
        if ((int64_t)offset + (int64_t)member_size > index_offset) goto done;
        if (!ismp_decode_name(buffer + position, name_length, NULL, NULL))
            goto done;
        if (index) {
            ismp_member *member = &index->items[entry];
            member->name = (char *)xx_mem_alloc((size_t)name_length + 1U);
            if (!member->name) goto done;
            index->count = entry + 1U;
            (void)ismp_decode_name(buffer + position, name_length,
                                   member->name, &safe);
        }
        position += name_length;
        if (buffer[position] > 1U) goto done;
        has_extra = buffer[position] == 1U;
        ++position;
        if (has_extra) {
            if ((size_t)region - position < ISMP_EXTRA) goto done;
            extra = ((uint64_t)ismp_be32(buffer + position) << 32U) |
                    (uint64_t)ismp_be32(buffer + position + 4U);
            position += ISMP_EXTRA;
        }
        if (index) {
            ismp_member *member = &index->items[entry];
            member->header_offset =
                format->base_address + index_offset + (int64_t)start;
            member->header_size = (int64_t)(position - start);
            member->data_offset = format->base_address + (int64_t)offset;
            member->size = (int64_t)member_size;
            member->type = fixed[0];
            member->id = ismp_be32(fixed + 1U);
            member->has_extra = has_extra;
            member->extra = extra;
            member->safe = safe && ismp_name_safe(member->name);
        }
    }
    if (index && !ismp_make_unique(index)) goto done;
    if (summary) {
        xx_mem_zero(summary, sizeof(*summary));
        summary->count = count;
        summary->index_offset = index_offset;
        summary->index_size = region + (int64_t)ISMP_FOOTER;
        summary->format_size = size;
    }
    if (index) {
        index->count = count;
        index->index_offset = index_offset;
        index->index_size = region + (int64_t)ISMP_FOOTER;
        index->format_size = size;
        *out = index;
        index = NULL;
    }
    result = true;
done:
    if (index) ismp_index_free(index);
    if (buffer) xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool ismp_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool ismp_set_record(xx_archive_record *record,
                            const ismp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_installshield_multiplatform_init(
    xx_installshield_multiplatform *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_INSTALLSHIELD_MULTIPLATFORM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-multiplatform");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_installshield_multiplatform_check_is_valid;
    archive->format.handle_base_info =
        xx_installshield_multiplatform_handle_base_info;
    archive->format.get_format_size =
        xx_installshield_multiplatform_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_multiplatform_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_multiplatform_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_multiplatform_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_multiplatform_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_multiplatform_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_multiplatform_free_archive_records_reading;
    archive->index_offset = -1;
}

xx_installshield_multiplatform *xx_installshield_multiplatform_create(
    xx_io_device *device, int64_t base_address) {
    xx_installshield_multiplatform *archive =
        (xx_installshield_multiplatform *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_multiplatform_init(archive, device,
                                                     base_address);
    return archive;
}

void xx_installshield_multiplatform_destroy(
    xx_installshield_multiplatform *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_multiplatform_free(
    xx_installshield_multiplatform *archive) {
    if (!archive) return;
    xx_installshield_multiplatform_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_multiplatform_check_is_valid(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    (void)pd;
    return ismp_parse(format, NULL, NULL);
}

bool xx_installshield_multiplatform_handle_base_info(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    xx_installshield_multiplatform *archive;
    ismp_index summary;
    uint8_t head[2];
    (void)pd;
    if (!format || !ismp_parse(format, NULL, &summary)) {
        if (format) {
            format->base_info_handled = false;
            format->format_size = -1;
            format->number_of_archive_records = 0U;
        }
        return false;
    }
    archive = (xx_installshield_multiplatform *)format;
    archive->number_of_records = summary.count;
    archive->index_offset = summary.index_offset;
    archive->index_size = summary.index_size;
    /* A Windows launcher is an .exe, the Unix ones are .bin. */
    if (ismp_read_at(format->device, format->base_address, head,
                     sizeof(head)))
        xx_format_set_extension(format, (head[0] == 'M' && head[1] == 'Z')
                                            ? "exe" : "bin");
    format->number_of_archive_records = summary.count;
    format->format_size = summary.format_size;
    format->file_type = XX_INSTALLSHIELD_MULTIPLATFORM_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_multiplatform_get_format_size(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_multiplatform_handle_base_info(format,
                                                                      pd))
               ? format->format_size
               : -1;
}

uint64_t xx_installshield_multiplatform_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_multiplatform_handle_base_info(format,
                                                                      pd))
               ? ((xx_installshield_multiplatform *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_installshield_multiplatform_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ismp_index *index;
    xx_archive_record_state *state;
    (void)pd;
    if (!ismp_parse(format, &index, NULL)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ismp_index_free(index);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = index;
    state->free_internal = ismp_index_free;
    state->total_records = (int64_t)index->count;
    if (!ismp_copy_options(&state->options, options) ||
        !ismp_set_record(&state->current_record, &index->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_installshield_multiplatform_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_installshield_multiplatform_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ismp_index *index;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(index = (ismp_index *)state->internal_state) ||
        ++index->index >= index->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ismp_set_record(&state->current_record,
                                        &index->items[index->index]);
    return state->has_record;
}

bool xx_installshield_multiplatform_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ismp_index *index;
    const ismp_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(index = (ismp_index *)state->internal_state) ||
        index->index >= index->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &index->items[index->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: the member is readable, the parse bounded it. */
    if (!option) return true;
    if (!member->extractable || !ismp_name_safe(member->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    result = xx_store_unpack_device_to_file(format->device, member->data_offset,
                                            member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_multiplatform_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
