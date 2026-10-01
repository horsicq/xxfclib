/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * East Point EPFS. The header/FAT and LZW parameters are documented at
 * https://moddingwiki.shikadi.net/wiki/EPF_Format . This is a new C
 * implementation, validated against every member of KINGPIN.EPF in the
 * local ARC corpus. It does not copy the GPL Camoto implementation.
 */
#include "xxfclib/formats/epf/xx_epf.h"
#include "../xx_payload_members.h"

#define EPF_HEADER_SIZE 11U
#define EPF_ENTRY_SIZE 22U
#define EPF_DICTIONARY_SIZE 16384U
#define EPF_MAX_MEMBER_SIZE (256U * 1024U * 1024U)
#define EPF_MAX_TOTAL_PLAIN (512U * 1024U * 1024U)

#ifdef EPF
#define EPF_FILE_TYPE XX_FILE_TYPE_EPF
#else
#define EPF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Codewords are packed most-significant bit first, continuously across
 * bytes. Width grows when the last non-reserved dictionary slot is used.
 * EOF and reset are the highest and second-highest code at the current
 * width; reset clears the dictionary without changing the width. */
static bool epf_lzw_decode(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           xx_pd_struct *pd) {
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL, *stack = NULL;
    size_t bit = 0U, written = 0U;
    unsigned width = 9U, next = 256U;
    int previous = -1;
    bool eof = false, ok = false;

    if ((!input && input_size) || (!output && output_size) ||
        input_size > SIZE_MAX / 8U) return false;
    prefix = (uint16_t *)xx_mem_alloc(EPF_DICTIONARY_SIZE * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc(EPF_DICTIONARY_SIZE);
    stack = (uint8_t *)xx_mem_alloc(EPF_DICTIONARY_SIZE + 1U);
    if (!prefix || !suffix || !stack) goto done;

    while (bit + width <= input_size * 8U) {
        unsigned code = 0U, cursor, count = 0U, first, i;
        bool special = false;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        for (i = 0U; i < width; ++i, ++bit)
            code = (code << 1U) |
                   ((unsigned)(input[bit >> 3U] >> (7U - (bit & 7U))) & 1U);
        if (code == (1U << width) - 1U) { eof = true; break; }
        if (code == (1U << width) - 2U) {
            next = 256U;
            previous = -1;
            continue;
        }
        if (code == next && previous >= 0) {
            cursor = (unsigned)previous;
            special = true;
        } else if (code < next) {
            cursor = code;
        } else {
            goto done;
        }
        while (cursor >= 256U) {
            if (cursor >= next || count >= EPF_DICTIONARY_SIZE) goto done;
            stack[count++] = suffix[cursor];
            cursor = prefix[cursor];
        }
        if (count >= EPF_DICTIONARY_SIZE) goto done;
        stack[count++] = (uint8_t)cursor;
        first = stack[count - 1U];
        if ((size_t)count > output_size - written) goto done;
        for (i = count; i > 0U; --i) output[written++] = stack[i - 1U];
        if (special) {
            if (written >= output_size) goto done;
            output[written++] = (uint8_t)first;
        }
        if (previous >= 0 && next <= (1U << width) - 3U) {
            prefix[next] = (uint16_t)previous;
            suffix[next] = (uint8_t)first;
            ++next;
        }
        previous = (int)code;
        if (next > (1U << width) - 3U && width < 14U) ++width;
    }
    ok = eof && written == output_size;
done:
    xx_mem_free(stack);
    xx_mem_free(suffix);
    xx_mem_free(prefix);
    return ok;
}

static bool epf_name(const uint8_t *field, char *out) {
    size_t length = 0U, i;
    while (length < 13U && field[length]) ++length;
    if (length == 0U || length == 13U || length >= 96U) return false;
    for (i = 0U; i < length; ++i) {
        uint8_t c = field[i];
        if (c <= 0x20U || c >= 0x7fU || c == '/' || c == '\\' ||
            c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*') return false;
        out[i] = (char)c;
    }
    out[length] = 0;
    if (out[0] == '.' && (length == 1U ||
        (length == 2U && out[1] == '.'))) return false;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[EPF_HEADER_SIZE], entry[EPF_ENTRY_SIZE];
    int64_t span = pm_available(format), payload = EPF_HEADER_SIZE;
    uint32_t fat;
    uint16_t count;
    size_t i;
    uint64_t plain_total = 0U;

    if (span < (int64_t)EPF_HEADER_SIZE ||
        !pm_read(format, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "EPFS", 4U) != 0 || header[8] != 0U)
        return false;
    fat = pm_le32(header + 4U);
    count = pm_le16(header + 9U);
    if (fat < EPF_HEADER_SIZE || (int64_t)fat > span ||
        (uint64_t)count * EPF_ENTRY_SIZE > (uint64_t)(span - (int64_t)fat))
        return false;
    for (i = 0U; i < count; ++i) {
        char name[96];
        uint8_t *packed_bytes = NULL, *plain_bytes = NULL;
        uint32_t packed, plain;
        pm_member *member;
        size_t j;
        bool decoded;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!pm_read(format, (int64_t)fat + (int64_t)i * EPF_ENTRY_SIZE,
                     entry, sizeof(entry)) || !epf_name(entry, name) ||
            entry[13] > 1U) return false;
        packed = pm_le32(entry + 14U);
        plain = pm_le32(entry + 18U);
        if (payload > (int64_t)fat ||
            packed > (uint32_t)((int64_t)fat - payload) ||
            plain > EPF_MAX_MEMBER_SIZE ||
            plain_total + plain > EPF_MAX_TOTAL_PLAIN ||
            (entry[13] == 0U && plain != packed)) return false;
        for (j = 0U; j < stream->count; ++j)
            if (xx_rt_strcmp(stream->items[j].name, name) == 0) return false;
        if (!pm_add(format, stream, name, payload, packed)) return false;
        member = &stream->items[stream->count - 1U];
        xx_rt_strncpy(member->name, name, sizeof(member->name) - 1U);
        member->name[sizeof(member->name) - 1U] = 0;
        member->size = plain;
        if (entry[13] != 0U) {
            packed_bytes = (uint8_t *)xx_mem_alloc(packed ? packed : 1U);
            plain_bytes = (uint8_t *)xx_mem_alloc(plain ? plain : 1U);
            if (!packed_bytes || !plain_bytes) {
                xx_mem_free(plain_bytes);
                xx_mem_free(packed_bytes);
                return false;
            }
            decoded = pm_read(format, payload, packed_bytes, packed) &&
                      epf_lzw_decode(packed_bytes, packed, plain_bytes,
                                     plain, pd);
            xx_mem_free(packed_bytes);
            if (!decoded) {
                xx_mem_free(plain_bytes);
                return false;
            }
            member->memory = plain_bytes;
        }
        plain_total += plain;
        payload += packed;
    }
    stream->size = (int64_t)fat + (int64_t)count * EPF_ENTRY_SIZE;
    return true;
}

static bool epf_record(xx_archive_record_state *state) {
    pm_stream *stream = (pm_stream *)state->internal_state;
    const pm_member *member = &stream->items[stream->index];
    xx_archive_record *record = &state->current_record;
    int64_t fat = stream->size - (int64_t)stream->count * EPF_ENTRY_SIZE;
    if (!pm_record(state)) return false;
    record->header_offset = state->format->base_address + fat +
                            (int64_t)stream->index * EPF_ENTRY_SIZE;
    record->header_size = EPF_ENTRY_SIZE;
    return xx_archive_record_set_meta_u64(record,
        XX_META_ID_COMPRESSION_METHOD, member->memory ? 1U : 0U);
}

static xx_archive_record_state *epf_create_records(Abstractformat *format,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd) {
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !epf_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

static bool epf_next(Abstractformat *format, xx_archive_record_state *state,
                     xx_pd_struct *pd) {
    if (!pm_next(format, state, pd)) return false;
    state->has_record = epf_record(state);
    return state->has_record;
}

void xx_epf_init(xx_epf *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, EPF_FILE_TYPE, "epf");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format, "application/x-eastpoint-epf");
    archive->format.create_archive_records_reading = epf_create_records;
    archive->format.archive_record_move_to_next = epf_next;
}
xx_epf *xx_epf_create(xx_io_device *device, int64_t base_address) {
    xx_epf *archive = (xx_epf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_epf_init(archive, device, base_address);
    return archive;
}
void xx_epf_destroy(xx_epf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_epf_free(xx_epf *archive) {
    if (!archive) return;
    xx_epf_destroy(archive);
    xx_mem_free(archive);
}
bool xx_epf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}
bool xx_epf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return pm_handle(format, pd);
}
