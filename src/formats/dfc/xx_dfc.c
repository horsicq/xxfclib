/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DFC fixed-directory layout is established by two local disk archives:
 * a four-byte count/version prefix, then 35-byte records with a Pascal name,
 * u32 offset/plain/packed sizes, complemented plaintext CRC32 and method.
 * Payload extents are contiguous. Method 0 is a complete PKWARE DCL stream;
 * method 1 stores bytes verbatim. Both are checked against the directory CRC.
 */
#include "xxfclib/formats/dfc/xx_dfc.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "../xx_payload_members.h"

#define DFC_ENTRY_SIZE 35U
#define DFC_COPY_CHUNK 16384U
#define DFC_MAX_MEMBER_SIZE (256U * 1024U * 1024U)
#define DFC_METHOD_DCL 0U
#define DFC_METHOD_STORED 1U

#ifdef DFC
#define DFC_FILE_TYPE XX_FILE_TYPE_DFC
#else
#define DFC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dfc_name(const uint8_t *entry, char *out) {
    size_t length = entry[0], i;
    if (length == 0U || length > 12U) return false;
    for (i = 0U; i < length; ++i) {
        uint8_t c = entry[i + 1U];
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
    uint8_t prefix[4];
    uint8_t entry[DFC_ENTRY_SIZE];
    int64_t span = pm_available(format), expected;
    uint16_t count;
    size_t i, j;
    if (span < DFC_ENTRY_SIZE + sizeof(prefix) ||
        !pm_read(format, 0, prefix, sizeof(prefix))) return false;
    count = pm_le16(prefix);
    if (pm_le16(prefix + 2U) != 1U) return false;
    if (count == 0U || (uint64_t)count * DFC_ENTRY_SIZE + 4U > (uint64_t)span)
        return false;
    expected = (int64_t)count * DFC_ENTRY_SIZE + 4;
    for (i = 0U; i < count; ++i) {
        char name[96];
        uint32_t offset, packed, plain;
        uint8_t method;
        bool duplicate = false;
        pm_member *member;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!pm_read(format, 4 + (int64_t)i * DFC_ENTRY_SIZE,
                     entry, sizeof(entry)) || !dfc_name(entry, name))
            return false;
        offset = pm_le32(entry + 13U);
        plain = pm_le32(entry + 17U);
        packed = pm_le32(entry + 21U);
        method = entry[31U];
        if ((int64_t)offset != expected ||
            (int64_t)packed > span - expected ||
            (method != DFC_METHOD_DCL && method != DFC_METHOD_STORED) ||
            (method == DFC_METHOD_STORED && packed != plain) ||
            (method == DFC_METHOD_DCL && (!packed || !plain ||
                packed > DFC_MAX_MEMBER_SIZE || plain > DFC_MAX_MEMBER_SIZE)))
            return false;
        if (method == DFC_METHOD_DCL) {
            uint8_t prelude[2];
            if (packed < sizeof(prelude) ||
                !pm_read(format, offset, prelude, sizeof(prelude)) ||
                prelude[0] > 1U || prelude[1] < 4U || prelude[1] > 6U)
                return false;
        }
        for (j = 0U; j < stream->count; ++j)
            if (xx_rt_strcmp(stream->items[j].name, name) == 0)
                duplicate = true;
        if (!pm_add(format, stream, name, offset, packed)) return false;
        member = &stream->items[stream->count - 1U];
        /* A second observed disk contains repeated INDEX.FIL entries.
         * Keep the first exact name; pm_add's index prefix distinguishes
         * later copies so extraction cannot silently overwrite one. */
        if (!duplicate) {
            xx_rt_strncpy(member->name, name, sizeof(member->name) - 1U);
            member->name[sizeof(member->name) - 1U] = 0;
        }
        member->size = plain;
        expected += packed;
    }
    stream->size = expected;
    return true;
}

static bool dfc_record(xx_archive_record_state *state) {
    pm_stream *stream = (pm_stream *)state->internal_state;
    const pm_member *member = &stream->items[stream->index];
    xx_archive_record *record = &state->current_record;
    uint8_t entry[DFC_ENTRY_SIZE];
    uint32_t complemented_crc;
    if (!pm_read(state->format, 4 + (int64_t)stream->index * DFC_ENTRY_SIZE,
                 entry, sizeof(entry))) return false;
    complemented_crc = pm_le32(entry + 25U);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = state->format->base_address + 4 +
                            (int64_t)stream->index * DFC_ENTRY_SIZE;
    record->header_size = DFC_ENTRY_SIZE;
    record->data_offset = member->offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        (uint64_t)(~complemented_crc)) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false))
        return false;
    /* DFC's explicit method distinguishes a stored member even when a
     * compressed stream happens to have the same packed and plain lengths. */
    return entry[31U] == DFC_METHOD_DCL ||
           xx_archive_record_set_meta_u64(record,
                                           XX_META_ID_COMPRESSION_METHOD, 0U);
}

static xx_archive_record_state *dfc_create_records(Abstractformat *format,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd) {
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !dfc_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

static bool dfc_next(Abstractformat *format, xx_archive_record_state *state,
                     xx_pd_struct *pd) {
    if (!pm_next(format, state, pd)) return false;
    state->has_record = dfc_record(state);
    return state->has_record;
}

/* Standard CRC32 state is complemented in the DFC directory. This checks
 * the candidate stored bytes before creating any output file. */
static bool dfc_stored_crc(Abstractformat *format, const pm_member *member,
                           uint32_t wanted, xx_pd_struct *pd) {
    uint8_t buffer[DFC_COPY_CHUNK];
    int64_t left = member->packed_size, offset = member->offset;
    int64_t saved = xx_io_tell(format->device);
    uint32_t crc = 0U;
    bool ok = true;
    while (left > 0) {
        size_t size = left > DFC_COPY_CHUNK ? DFC_COPY_CHUNK : (size_t)left;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, offset - format->base_address, buffer, size)) {
            ok = false;
            break;
        }
        crc = xx_crc32_calc(crc, buffer, size);
        offset += (int64_t)size;
        left -= (int64_t)size;
    }
    if (saved >= 0) (void)xx_io_seek64(format->device, saved, SEEK_SET);
    return ok && (crc ^ UINT32_MAX) == wanted;
}

static bool dfc_unpack(Abstractformat *format, xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    pm_stream *stream;
    pm_member *member;
    uint8_t entry[DFC_ENTRY_SIZE];
    uint32_t wanted;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pm_stream *)state->internal_state)) return false;
    member = &stream->items[stream->index];
    if (!pm_read(format, 4 + (int64_t)stream->index * DFC_ENTRY_SIZE,
                 entry, sizeof(entry)))
        return false;
    wanted = pm_le32(entry + 25U);
    if (entry[31U] == DFC_METHOD_STORED) {
        if (!dfc_stored_crc(format, member, wanted, pd)) return false;
    } else if (entry[31U] == DFC_METHOD_DCL) {
        const xx_var *limit;
        uint8_t *packed = NULL, *plain = NULL;
        size_t consumed = 0U, produced = 0U, written = 0U;
        bool valid = false;
        if (member->size <= 0 || member->packed_size <= 0 ||
            member->size > DFC_MAX_MEMBER_SIZE ||
            member->packed_size > DFC_MAX_MEMBER_SIZE) return false;
        limit = xx_format_resolve_extra_parameter(
            format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if (limit && (uint64_t)member->size > xx_var_get_u64(limit))
            return false;
        limit = xx_format_resolve_extra_parameter(
            format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
        if (limit && (uint64_t)member->size > xx_var_get_u64(limit))
            return false;
        if (!member->memory) {
            packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
            plain = (uint8_t *)xx_mem_alloc((size_t)member->size);
            if (packed && plain &&
                !(pd && xx_pd_is_stopped(pd)) &&
                pm_read(format, member->offset - format->base_address,
                        packed, (size_t)member->packed_size) &&
                xx_dcl_scan_memory(packed, (size_t)member->packed_size,
                                   (size_t)member->size, &consumed,
                                   &produced) &&
                consumed == (size_t)member->packed_size &&
                produced == (size_t)member->size &&
                xx_dcl_decode_memory(packed, (size_t)member->packed_size,
                                     plain, (size_t)member->size, &written) &&
                written == (size_t)member->size &&
                xx_crc32_calc(0U, plain, written) == ~wanted)
                valid = true;
            xx_mem_free(packed);
            if (!valid) { xx_mem_free(plain); return false; }
            member->memory = plain;
        }
    } else return false;
    return pm_unpack(format, state, pd);
}

void xx_dfc_init(xx_dfc *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, DFC_FILE_TYPE, "dfc");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format, "application/x-dfc-disk");
    archive->format.create_archive_records_reading = dfc_create_records;
    archive->format.archive_record_move_to_next = dfc_next;
    archive->format.unpack_current_archive_record = dfc_unpack;
}
xx_dfc *xx_dfc_create(xx_io_device *device, int64_t base_address) {
    xx_dfc *archive = (xx_dfc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dfc_init(archive, device, base_address);
    return archive;
}
void xx_dfc_destroy(xx_dfc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_dfc_free(xx_dfc *archive) {
    if (!archive) return;
    xx_dfc_destroy(archive);
    xx_mem_free(archive);
}
bool xx_dfc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}
bool xx_dfc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return pm_handle(format, pd);
}
