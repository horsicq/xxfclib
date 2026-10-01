/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * INSA version 1 consists of u32 decoded size + LArc-compatible LH1 stream
 * records.  Each stream has no packed length; the shortest input prefix
 * producing the declared decoded size ends at its final coded byte.
 * XArchive's LH1 stream measurement confirms this framing.  The native
 * xx_lzh1 decoder accepts exactly that prefix and rejects whole trailing
 * bytes.  All eleven MOLYBALL.DAT outputs matched U3 byte-for-byte.
 */
#include "xxfclib/formats/insa/xx_insa.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "../xx_payload_members.h"

#define INSA_MAX_PACKED (16U * 1024U * 1024U)
#define INSA_MAX_MEMBER (64U * 1024U * 1024U)
#define INSA_MAX_TOTAL (128U * 1024U * 1024U)
#define INSA_MAX_RECORDS 1024U

#ifdef INSA
#define INSA_FILE_TYPE XX_FILE_TYPE_INSA
#else
#define INSA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t version[2];
    int64_t span = pm_available(format), pos = 2;
    uint64_t total_plain = 0U;
    if (span < 7 || span > INSA_MAX_PACKED ||
        !pm_read(format, 0, version, sizeof(version)) ||
        pm_le16(version) != 1U)
        return false;
    while (pos < span) {
        uint8_t field[4];
        uint8_t *packed = NULL, *plain = NULL;
        uint32_t raw;
        size_t remaining, low, high, written = 0U;
        char name[32];
        pm_member *member;
        if ((pd && xx_pd_is_stopped(pd)) ||
            stream->count >= INSA_MAX_RECORDS || span - pos < 5 ||
            !pm_read(format, pos, field, sizeof(field)))
            return false;
        raw = pm_le32(field);
        pos += 4;
        remaining = (size_t)(span - pos);
        if (raw == 0U || raw > INSA_MAX_MEMBER ||
            total_plain > INSA_MAX_TOTAL - raw ||
            remaining == 0U || remaining > INSA_MAX_PACKED)
            return false;
        packed = (uint8_t *)xx_mem_alloc(remaining);
        plain = (uint8_t *)xx_mem_alloc(raw);
        if (!packed || !plain ||
            !pm_read(format, pos, packed, remaining)) {
            if (packed) xx_mem_free(packed);
            if (plain) xx_mem_free(plain);
            return false;
        }
        /* Output count is monotonic in the available input prefix, even
         * though the decoder returns false for a prefix with extra bytes.
         * Binary search locates the first prefix that fills the output. */
        (void)xx_lzh1_decode_memory(packed, remaining, plain, raw, &written);
        if (written != raw) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        low = 1U;
        high = remaining;
        while (low < high) {
            size_t middle = low + (high - low) / 2U;
            if (pd && xx_pd_is_stopped(pd)) break;
            written = 0U;
            (void)xx_lzh1_decode_memory(packed, middle, plain, raw,
                                        &written);
            if (written == raw) high = middle;
            else low = middle + 1U;
        }
        written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_lzh1_decode_memory(packed, low, plain, raw, &written) ||
            written != raw) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        xx_mem_free(packed);
        xx_rt_snprintf(name, sizeof(name), "File_%u.bin",
                       (unsigned)(stream->count + 1U));
        if (!pm_add(format, stream, name, pos, (int64_t)low)) {
            xx_mem_free(plain);
            return false;
        }
        member = &stream->items[stream->count - 1U];
        xx_rt_snprintf(member->name, sizeof(member->name), "%s", name);
        member->packed_size = (int64_t)low;
        member->size = raw;
        member->memory = plain;
        total_plain += raw;
        pos += (int64_t)low;
    }
    if (stream->count == 0U || pos != span) return false;
    stream->size = span;
    return true;
}

static bool insa_record(xx_archive_record_state *state) {
    pm_stream *stream = (pm_stream *)state->internal_state;
    const pm_member *member = &stream->items[stream->index];
    if (!pm_record(state)) return false;
    state->current_record.header_offset = member->offset - 4;
    state->current_record.header_size = 4;
    return xx_archive_record_set_meta_u64(&state->current_record,
        XX_META_ID_COMPRESSION_METHOD, 1U);
}
static xx_archive_record_state *insa_create_records(Abstractformat *format,
                                                     const xx_list_s *options,
                                                     xx_pd_struct *pd) {
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !insa_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}
static bool insa_next(Abstractformat *format, xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    if (!pm_next(format, state, pd)) return false;
    state->has_record = insa_record(state);
    return state->has_record;
}

void xx_insa_init(xx_insa *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, INSA_FILE_TYPE, "dat");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format, "application/x-insa");
    archive->format.create_archive_records_reading = insa_create_records;
    archive->format.archive_record_move_to_next = insa_next;
}
xx_insa *xx_insa_create(xx_io_device *device, int64_t base_address) {
    xx_insa *archive = (xx_insa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_insa_init(archive, device, base_address);
    return archive;
}
void xx_insa_destroy(xx_insa *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_insa_free(xx_insa *archive) {
    if (!archive) return;
    xx_insa_destroy(archive);
    xx_mem_free(archive);
}
bool xx_insa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}
bool xx_insa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return pm_handle(format, pd);
}
