/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield-family single-container install data files: the *.EX$, *.DL$,
 * *.HL$, *.$$$, *.??$ and *.CMP members shipped on 16-bit InstallShield
 * distribution disks.  These are the installer's DATA files, not the
 * self-extracting SETUP.EXE.  Layout from the XArchive reference module
 * installers/xis11.cpp, extended here to cover the second generation.
 *
 * Archive header (13 bytes, little endian):
 *   +0  uint32 magic    always 0x8C135D65
 *   +4  uint32 format   0x00010108 (generation 1) or 0x00030108 (generation 3)
 *   +8  uint8  variant  1 or 2
 *   +9  uint32 reserved always 0
 *
 * Member header, immediately after the archive header and then at every
 * next_offset.  Generation 1 is 12 bytes, generation 3 is 16:
 *   +0  uint8  flags        0x0E on generation 1, 0x12 on generation 3
 *   +1  int32  packed_size  bytes of payload, >= 0
 *   +5  int32  next_offset  file offset of the next member, 0 = last
 *   gen 1: +9 uint16 reserved            +11 uint8 name_length
 *   gen 3: +9 uint16 dos_date  +11 uint16 dos_time  +13 uint16 reserved
 *          +15 uint8 name_length
 * followed by name_length name bytes and one separator byte, after which the
 * payload starts.  The chain must tile the file exactly: the last member ends
 * at end of file, every other one ends exactly at its next_offset.  That is
 * what makes detection safe -- a coincidental magic match cannot survive it.
 *
 * Nothing stores the plaintext length, so it is discovered by decoding.
 * Generation 1 uses a headerless 12-bit block-mode Unix-compress LZW stream;
 * generation 3 uses a raw PKWARE DCL implode stream, which xx_dcl already
 * both measures and decodes.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/is11/xx_is11.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef IS11
#define XX_IS11_FILE_TYPE XX_FILE_TYPE_IS11
#else
#define XX_IS11_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS11_HEADER_SIZE 13
#define IS11_MAGIC UINT32_C(0x8c135d65)
#define IS11_FORMAT_GEN1 UINT32_C(0x00010108)
#define IS11_FORMAT_GEN3 UINT32_C(0x00030108)
#define IS11_MEMBER_HEADER_GEN1 12
#define IS11_MEMBER_HEADER_GEN3 16
/* Every archive in the reference corpus is a short chain (one to four
 * members); the ceiling only exists to bound a corrupt next_offset ring. */
#define IS11_MAX_MEMBERS 4096U
/* A 12-bit dictionary cannot legitimately expand a member by more than a few
 * hundred times.  This cap is what keeps a small header from asking for a
 * large allocation. */
#define IS11_MAX_UNCOMPRESSED (64 * 1024 * 1024)
#define IS11_MAX_NAME 255U

/* LZW parameters: LSB-first codes, 9..12 bits, clear code 256, first
 * assignable slot 257, codes emitted in groups of eight. */
#define IS11_LZW_CLEAR 256
#define IS11_LZW_FIRST 257
#define IS11_LZW_MINBITS 9
#define IS11_LZW_MAXBITS 12
#define IS11_LZW_MAXCODE (1 << IS11_LZW_MAXBITS)

typedef struct is11_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size; /**< -1 while unresolved. */
    uint16_t dos_date;
    uint16_t dos_time;
    uint8_t flags;
} is11_member;

typedef struct is11_stream_s {
    is11_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t generation;
    uint8_t variant;
} is11_stream;

static uint16_t is11_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is11_le32(const uint8_t *bytes) {
    return (uint32_t)is11_le16(bytes) | ((uint32_t)is11_le16(bytes + 2U) << 16U);
}

static bool is11_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool is11_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* ------------------------------------------------------------- LZW ------ */

typedef struct is11_bits_s {
    const uint8_t *data;
    int64_t total_bits;
    int64_t position;
} is11_bits;

static int32_t is11_read_code(is11_bits *bits, int32_t width) {
    int32_t code = 0;
    int32_t index;
    if (width < 1 || width > IS11_LZW_MAXBITS ||
        bits->position > bits->total_bits - width)
        return -1;
    for (index = 0; index < width; ++index) {
        int64_t bit = bits->position + index;
        uint8_t byte = bits->data[bit >> 3];
        code |= (int32_t)((byte >> (bit & 7)) & 1U) << index;
    }
    bits->position += width;
    return code;
}

/* compress(1) emits codes in groups of eight.  A width change or a clear
 * abandons the rest of the current group, so the next code starts on the
 * following group boundary.  The group origin is bit 0 of the member. */
static bool is11_align_group(is11_bits *bits, int32_t width,
                             int64_t *group_start) {
    int64_t group_bits;
    int64_t used;
    int64_t skip;
    if (width < IS11_LZW_MINBITS || width > IS11_LZW_MAXBITS ||
        bits->position < *group_start)
        return false;
    group_bits = (int64_t)width * 8;
    used = bits->position - *group_start;
    skip = (group_bits - (used % group_bits)) % group_bits;
    if (skip < 0 || bits->position > bits->total_bits - skip) return false;
    bits->position += skip;
    *group_start = bits->position;
    return true;
}

/* Decodes the whole member.  When @p output is NULL the plaintext is measured
 * and discarded, which is how a container that stores no plaintext length
 * still learns one.  The limit is enforced before every append, so a corrupt
 * stream cannot drive the buffer past it. */
static bool is11_lzw_decode(const uint8_t *input, size_t input_size,
                            uint8_t **output, size_t *produced,
                            size_t limit) {
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    uint8_t *out = NULL;
    size_t out_size = 0U;
    size_t out_capacity = 0U;
    is11_bits bits;
    int64_t group_start = 0;
    int32_t next_code = IS11_LZW_FIRST;
    int32_t width = IS11_LZW_MINBITS;
    int32_t max_value = 1 << IS11_LZW_MINBITS;
    int32_t code;
    uint8_t final_char;
    int32_t old_code;
    bool result = true;
    int32_t index;

    if (produced) *produced = 0U;
    if (output) *output = NULL;
    if (!input || input_size == 0U || limit == 0U) return false;

    prefix = (uint16_t *)xx_mem_calloc(IS11_LZW_MAXCODE, sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_calloc(IS11_LZW_MAXCODE, 1U);
    stack = (uint8_t *)xx_mem_alloc(IS11_LZW_MAXCODE);
    if (!prefix || !suffix || !stack) {
        result = false;
        goto done;
    }
    for (index = 0; index < 256; ++index) suffix[index] = (uint8_t)index;

    out_capacity = input_size < 4096U ? 4096U : input_size * 2U;
    if (out_capacity > limit) out_capacity = limit;
    out = (uint8_t *)xx_mem_alloc(out_capacity);
    if (!out) {
        result = false;
        goto done;
    }

    bits.data = input;
    bits.total_bits = (int64_t)input_size * 8;
    bits.position = 0;

    /* The first code addresses the pristine dictionary, so only a literal is
     * meaningful there. */
    old_code = is11_read_code(&bits, width);
    if (old_code < 0 || old_code >= 256) {
        result = false;
        goto done;
    }
    final_char = (uint8_t)old_code;
    out[out_size++] = final_char;

    while (result) {
        int32_t in_code;
        int32_t stack_top = 0;
        code = is11_read_code(&bits, width);
        if (code < 0) break; /* End of the bounded member. */
        if (code == IS11_LZW_CLEAR) {
            if (!is11_align_group(&bits, width, &group_start)) {
                result = false;
                break;
            }
            next_code = IS11_LZW_FIRST;
            width = IS11_LZW_MINBITS;
            max_value = 1 << width;
            code = is11_read_code(&bits, width);
            if (code < 0) break;
            /* A clear immediately after a clear terminates the stream. */
            if (code == IS11_LZW_CLEAR) break;
            if (code >= 256) {
                result = false;
                break;
            }
            old_code = code;
            final_char = (uint8_t)code;
            if (out_size >= limit) {
                result = false;
                break;
            }
            if (out_size == out_capacity) {
                size_t grown = out_capacity * 2U;
                uint8_t *bigger;
                if (grown > limit) grown = limit;
                bigger = (uint8_t *)xx_mem_realloc(out, grown);
                if (!bigger) {
                    result = false;
                    break;
                }
                out = bigger;
                out_capacity = grown;
            }
            out[out_size++] = final_char;
            continue;
        }

        in_code = code;
        /* KwKwK: the encoder may reference the entry it is about to create. */
        if (code >= next_code) {
            if (code > next_code || code >= IS11_LZW_MAXCODE) {
                result = false;
                break;
            }
            stack[stack_top++] = final_char;
            code = old_code;
        }
        while (code >= 256) {
            if (code >= next_code || code >= IS11_LZW_MAXCODE ||
                stack_top >= IS11_LZW_MAXCODE) {
                result = false;
                break;
            }
            stack[stack_top++] = suffix[code];
            code = (int32_t)prefix[code];
        }
        if (!result) break;
        if (code < 0 || code >= 256 || stack_top >= IS11_LZW_MAXCODE) {
            result = false;
            break;
        }
        final_char = suffix[code];
        stack[stack_top++] = final_char;

        if (out_size > limit - (size_t)stack_top) {
            result = false;
            break;
        }
        if (out_size + (size_t)stack_top > out_capacity) {
            size_t grown = out_capacity;
            uint8_t *bigger;
            while (grown < out_size + (size_t)stack_top) {
                if (grown > limit / 2U) {
                    grown = limit;
                    break;
                }
                grown *= 2U;
            }
            if (grown < out_size + (size_t)stack_top) {
                result = false;
                break;
            }
            bigger = (uint8_t *)xx_mem_realloc(out, grown);
            if (!bigger) {
                result = false;
                break;
            }
            out = bigger;
            out_capacity = grown;
        }
        for (index = stack_top - 1; index >= 0; --index)
            out[out_size++] = stack[index];

        if (next_code < IS11_LZW_MAXCODE) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = final_char;
            ++next_code;
            if (next_code >= max_value && width < IS11_LZW_MAXBITS) {
                if (!is11_align_group(&bits, width, &group_start)) {
                    result = false;
                    break;
                }
                ++width;
                max_value = 1 << width;
            }
        }
        old_code = in_code;
    }

done:
    if (prefix) xx_mem_free(prefix);
    if (suffix) xx_mem_free(suffix);
    if (stack) xx_mem_free(stack);
    if (!result) {
        if (out) xx_mem_free(out);
        return false;
    }
    if (produced) *produced = out_size;
    if (output)
        *output = out;
    else if (out)
        xx_mem_free(out);
    return true;
}

/* --------------------------------------------------------- container ---- */

/* Names are DOS 8.3 identifiers, occasionally NUL terminated inside the
 * field.  Bytes the host filesystem cannot carry are replaced rather than
 * dropped, so two members can never collapse onto one output file. */
static char *is11_normalize_name(const uint8_t *bytes, size_t size,
                                 size_t index) {
    char *name;
    size_t length = 0U;
    size_t at = 0U;
    size_t position;
    while (length < size && bytes[length] != 0U) ++length;
    while (length > 0U && bytes[length - 1U] == ' ') --length;
    name = (char *)xx_mem_alloc(length + 16U);
    if (!name) return NULL;
    for (position = 0U; position < length; ++position) {
        uint8_t c = bytes[position];
        if (c <= 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            name[at++] = '_';
        else
            name[at++] = (char)c;
    }
    if (at == 0U) {
        /* Nothing usable in the field: fall back to a positional name rather
         * than emitting an empty one. */
        const char *stem = "record";
        size_t stem_length = xx_rt_strlen(stem);
        xx_rt_memcpy(name, stem, stem_length);
        at = stem_length;
        if (index >= 10U) name[at++] = (char)('0' + (index / 10U) % 10U);
        name[at++] = (char)('0' + index % 10U);
    }
    name[at] = 0;
    return name;
}

static bool is11_safe_output_name(const char *name) {
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
        return false;
    return true;
}

static void is11_stream_free(void *opaque) {
    is11_stream *stream = (is11_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is11_add_member(is11_stream *stream, const is11_member *member) {
    is11_member *grown;
    if (!stream || !member || stream->count >= IS11_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (is11_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Reads the packed bytes of one member.  The extent has already been bounded
 * against the device by is11_parse(). */
static uint8_t *is11_read_packed(Abstractformat *format,
                                 const is11_member *member) {
    uint8_t *packed;
    if (member->packed_size <= 0 || member->packed_size > SIZE_MAX) return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return NULL;
    if (!is11_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* Decodes one member.  @p output may be NULL to measure only. */
static bool is11_decode_member(Abstractformat *format, uint32_t generation,
                               const is11_member *member, uint8_t **output,
                               size_t *produced) {
    uint8_t *packed;
    bool result = false;
    if (output) *output = NULL;
    if (produced) *produced = 0U;
    if (member->packed_size == 0) return true;
    packed = is11_read_packed(format, member);
    if (!packed) return false;
    if (generation == 1U) {
        result = is11_lzw_decode(packed, (size_t)member->packed_size, output,
                                 produced, (size_t)IS11_MAX_UNCOMPRESSED);
    } else {
        size_t consumed = 0U;
        size_t plain = 0U;
        if (xx_dcl_scan_memory(packed, (size_t)member->packed_size,
                               (size_t)IS11_MAX_UNCOMPRESSED, &consumed,
                               &plain)) {
            if (!output) {
                if (produced) *produced = plain;
                result = true;
            } else {
                uint8_t *out = (uint8_t *)xx_mem_alloc(plain ? plain : 1U);
                size_t written = 0U;
                if (out && xx_dcl_decode_memory(packed,
                                                (size_t)member->packed_size,
                                                out, plain, &written) &&
                    written == plain) {
                    *output = out;
                    if (produced) *produced = written;
                    result = true;
                } else if (out) {
                    xx_mem_free(out);
                }
            }
        }
    }
    xx_mem_free(packed);
    return result;
}

static bool is11_parse(Abstractformat *format, is11_stream **result,
                       bool resolve_sizes) {
    uint8_t header[IS11_HEADER_SIZE];
    uint8_t member_header[IS11_MEMBER_HEADER_GEN3];
    uint8_t raw_name[IS11_MAX_NAME + 1U];
    is11_stream *stream = NULL;
    int64_t total, available, cursor, base;
    uint32_t format_word;
    int64_t member_header_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < IS11_HEADER_SIZE + IS11_MEMBER_HEADER_GEN1 + 2)
        return false;
    if (!is11_read_at(format->device, base, header, sizeof(header)) ||
        is11_le32(header) != IS11_MAGIC)
        return false;
    format_word = is11_le32(header + 4);
    if (format_word != IS11_FORMAT_GEN1 && format_word != IS11_FORMAT_GEN3)
        return false;
    if (header[8] != 1U && header[8] != 2U) return false;
    if (is11_le32(header + 9) != 0U) return false;

    stream = (is11_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->generation = (format_word == IS11_FORMAT_GEN1) ? 1U : 3U;
    stream->variant = header[8];
    member_header_size = (stream->generation == 1U) ? IS11_MEMBER_HEADER_GEN1
                                                    : IS11_MEMBER_HEADER_GEN3;

    cursor = IS11_HEADER_SIZE;
    for (;;) {
        is11_member member;
        int32_t packed_size;
        int32_t next_offset;
        int64_t name_offset;
        int64_t end;
        size_t name_length;
        if (!is11_range_within(available, cursor, member_header_size) ||
            !is11_read_at(format->device, base + cursor, member_header,
                          (size_t)member_header_size))
            goto fail;
        packed_size = (int32_t)is11_le32(member_header + 1);
        next_offset = (int32_t)is11_le32(member_header + 5);
        name_length = member_header[member_header_size - 1];
        if (packed_size < 0 || next_offset < 0 || name_length == 0U) goto fail;

        name_offset = cursor + member_header_size;
        /* One separator byte follows the name before the stream starts. */
        if (!is11_range_within(available, name_offset,
                               (int64_t)name_length + 1) ||
            !is11_read_at(format->device, base + name_offset, raw_name,
                          name_length))
            goto fail;
        if (raw_name[0] < 0x20U) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = base + cursor;
        member.header_size = member_header_size + (int64_t)name_length + 1;
        member.data_offset = base + name_offset + (int64_t)name_length + 1;
        member.packed_size = (int64_t)packed_size;
        member.unpacked_size = -1;
        member.flags = member_header[0];
        if (stream->generation == 3U) {
            member.dos_date = is11_le16(member_header + 9);
            member.dos_time = is11_le16(member_header + 11);
        }
        if (!is11_range_within(available, member.data_offset - base,
                               member.packed_size))
            goto fail;
        member.name = is11_normalize_name(raw_name, name_length,
                                          stream->count);
        if (!member.name) goto fail;
        if (!is11_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
        end = (member.data_offset - base) + member.packed_size;

        /* The chain has to tile the file exactly.  This is the rule that
         * makes the detector safe: a coincidental magic match cannot survive
         * it. */
        if (next_offset == 0) {
            if (end != available) goto fail;
            break;
        }
        if ((int64_t)next_offset != end) goto fail;
        cursor = end;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = available;

    if (resolve_sizes) {
        size_t index;
        for (index = 0U; index < stream->count; ++index) {
            size_t produced = 0U;
            if (is11_decode_member(format, stream->generation,
                                   &stream->items[index], NULL, &produced))
                stream->items[index].unpacked_size = (int64_t)produced;
        }
    }
    *result = stream;
    return true;
fail:
    is11_stream_free(stream);
    return false;
}

static bool is11_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *is11_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is11_set_record(xx_archive_record *record,
                            const is11_stream *stream,
                            const is11_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        stream->generation) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        member->flags) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* The plaintext length is not stored anywhere, so it is published only
     * when a decode actually produced it. */
    if (member->unpacked_size >= 0 &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->unpacked_size))
        return false;
    if (stream->generation == 3U &&
        (!xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                         member->dos_date) ||
         !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                         member->dos_time)))
        return false;
    return true;
}

void xx_is11_init(xx_is11 *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IS11_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-compressed");
    xx_format_set_extension(&archive->format, "ex$");
    archive->format.check_is_valid = xx_is11_check_is_valid;
    archive->format.handle_base_info = xx_is11_handle_base_info;
    archive->format.get_format_size = xx_is11_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_is11_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_is11_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_is11_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_is11_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_is11_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_is11_free_archive_records_reading;
}

xx_is11 *xx_is11_create(xx_io_device *device, int64_t base_address) {
    xx_is11 *archive = (xx_is11 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_is11_init(archive, device, base_address);
    return archive;
}

void xx_is11_destroy(xx_is11 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_is11_free(xx_is11 *archive) {
    if (!archive) return;
    xx_is11_destroy(archive);
    xx_mem_free(archive);
}

bool xx_is11_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    is11_stream *stream;
    (void)pd;
    if (!is11_parse(format, &stream, false)) return false;
    is11_stream_free(stream);
    return true;
}

bool xx_is11_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    is11_stream *stream;
    xx_is11 *archive;
    (void)pd;
    if (!format) return false;
    if (!is11_parse(format, &stream, false)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_is11 *)format;
    archive->number_of_records = stream->count;
    archive->generation = stream->generation;
    archive->variant = stream->variant;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_IS11_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    is11_stream_free(stream);
    return true;
}

int64_t xx_is11_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is11_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_is11_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is11_handle_base_info(format, pd))
               ? ((xx_is11 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_is11_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is11_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!is11_parse(format, &stream, true)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is11_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is11_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!is11_copy_options(&state->options, options) ||
        !is11_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_is11_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_is11_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    is11_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is11_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = is11_set_record(&state->current_record, stream,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_is11_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    is11_stream *stream;
    is11_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is11_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!is11_safe_output_name(member->name)) return false;
    if (!is11_decode_member(format, stream->generation, member, &plain,
                            &plain_size))
        return false;
    path_option = is11_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_is11_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
