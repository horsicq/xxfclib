/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Eschalon Setup ARCV 2.00 reader.  The container's optional prefix-XOR
 * scrambling has no header flag, so compact LZHUF members are probed in the
 * three possible archive-wide readings before extraction.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcv2/xx_arcv2.h"

#include "xxfclib/algo/arcv2/xx_arcv2_lzhuf.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define ARCV2_ARCHIVE_HEADER_SIZE 14U
#define ARCV2_BLOCK_PREFIX_SIZE 17U
#define ARCV2_BLOCK_FIXED_SIZE 45U
#define ARCV2_MAX_MEMBERS 200000U
#define ARCV2_MAX_PROBES 8U
#define ARCV2_MAX_PROBE_PACKED (16U * 1024U * 1024U)
#define ARCV2_MAX_PROBE_PLAIN (64U * 1024U * 1024U)

#define ARCV2_FLAG_WHOLE 0x01U
#define ARCV2_FLAG_SPLIT_HEAD 0x02U
#define ARCV2_FLAG_SPLIT_TAIL 0x08U
#define ARCV2_FLAG_STORED 0x10U
#define ARCV2_FLAG_COMPRESSED 0x20U
#define ARCV2_FLAG_KNOWN 0x3bU

typedef enum arcv2_scramble_e {
    ARCV2_SCRAMBLE_NONE = 0,
    ARCV2_SCRAMBLE_RELEASE = 1,
    ARCV2_SCRAMBLE_TRIAL = 2
} arcv2_scramble;

typedef struct arcv2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t data_size;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t flags;
    uint32_t attributes;
    uint32_t dos_datetime;
    uint32_t packed_crc32;
} arcv2_member;

typedef struct arcv2_stream_s {
    arcv2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t volume_flags;
    uint16_t disk_number;
    arcv2_scramble scramble;
} arcv2_stream;

static uint16_t arcv2_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t arcv2_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool arcv2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool arcv2_valid_volume_flags(uint32_t flags) {
    return flags == 1U || flags == 2U || flags == 4U || flags == 8U;
}

static bool arcv2_valid_flags(uint32_t flags) {
    uint32_t method;
    uint32_t span;
    if ((flags & ~ARCV2_FLAG_KNOWN) != 0U) return false;
    method = flags & (ARCV2_FLAG_STORED | ARCV2_FLAG_COMPRESSED);
    span = flags & (ARCV2_FLAG_WHOLE | ARCV2_FLAG_SPLIT_HEAD |
                    ARCV2_FLAG_SPLIT_TAIL);
    return (method == ARCV2_FLAG_STORED || method == ARCV2_FLAG_COMPRESSED) &&
           (span == ARCV2_FLAG_WHOLE || span == ARCV2_FLAG_SPLIT_HEAD ||
            span == ARCV2_FLAG_SPLIT_TAIL);
}

static bool arcv2_is_stored(const arcv2_member *member) {
    return member && (member->flags & ARCV2_FLAG_STORED) != 0U;
}

static bool arcv2_is_compressed(const arcv2_member *member) {
    return member && (member->flags & ARCV2_FLAG_COMPRESSED) != 0U;
}

static bool arcv2_is_split(const arcv2_member *member) {
    return member && (member->flags &
                      (ARCV2_FLAG_SPLIT_HEAD | ARCV2_FLAG_SPLIT_TAIL)) != 0U;
}

static char *arcv2_name(const uint8_t *raw, size_t size) {
    char *result;
    size_t index;
    if (!raw || size == 0U) return NULL;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t value = raw[index];
        if (value < 0x20U || value > 0x7eU) {
            xx_mem_free(result);
            return NULL;
        }
        result[index] = (value == '/' || value == '\\' || value == ':' ||
                         value == '<' || value == '>' || value == '"' ||
                         value == '|' || value == '?' || value == '*')
                            ? '_'
                            : (char)value;
    }
    while (size != 0U && (result[size - 1U] == ' ' ||
                          result[size - 1U] == '.'))
        --size;
    if ((size == 1U && result[0] == '.') ||
        (size == 2U && result[0] == '.' && result[1] == '.'))
        size = 0U;
    if (size == 0U) result[size++] = '_';
    result[size] = 0;
    return result;
}

static bool arcv2_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    for (at = name; *at; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value < 0x20U || value == ':' || value == '<' || value == '>' ||
            value == '"' || value == '|' || value == '?' || value == '*' ||
            value == '/' || value == '\\')
            return false;
    }
    return xx_rt_strcmp(name, ".") != 0 && xx_rt_strcmp(name, "..") != 0;
}

static void arcv2_stream_free(void *opaque) {
    arcv2_stream *stream = (arcv2_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool arcv2_add_member(arcv2_stream *stream,
                             const arcv2_member *member) {
    arcv2_member *grown;
    if (!stream || !member || stream->count >= ARCV2_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (arcv2_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool arcv2_parse(Abstractformat *format, arcv2_stream **result,
                        xx_pd_struct *pd) {
    uint8_t archive_header[ARCV2_ARCHIVE_HEADER_SIZE];
    arcv2_stream *stream = NULL;
    int64_t total;
    int64_t size;
    int64_t offset;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(ARCV2_ARCHIVE_HEADER_SIZE +
                         ARCV2_BLOCK_FIXED_SIZE + 1U) ||
        !arcv2_read_at(format->device, format->base_address, archive_header,
                       sizeof(archive_header)) ||
        xx_rt_memcmp(archive_header, "ARCV", 4U) != 0 ||
        arcv2_le16(archive_header + 4U) != 0x0200U ||
        arcv2_le16(archive_header + 6U) != ARCV2_ARCHIVE_HEADER_SIZE ||
        !arcv2_valid_volume_flags(arcv2_le32(archive_header + 8U)))
        return false;
    stream = (arcv2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->volume_flags = arcv2_le32(archive_header + 8U);
    stream->disk_number = arcv2_le16(archive_header + 12U);
    stream->scramble = ARCV2_SCRAMBLE_NONE;
    offset = ARCV2_ARCHIVE_HEADER_SIZE;
    while (offset < size) {
        uint8_t prefix[ARCV2_BLOCK_PREFIX_SIZE];
        uint8_t header[ARCV2_BLOCK_FIXED_SIZE + UINT8_MAX];
        uint16_t header_size;
        uint8_t name_size;
        uint32_t flags;
        uint32_t bytes_on_volume;
        arcv2_member member;
        const uint8_t *tail;
        int64_t relative_data_offset;
        if ((pd && xx_pd_is_stopped(pd)) || stream->count >= ARCV2_MAX_MEMBERS ||
            offset > size - (int64_t)sizeof(prefix) ||
            !arcv2_read_at(format->device, format->base_address + offset,
                           prefix, sizeof(prefix)) ||
            xx_rt_memcmp(prefix, "BLCK", 4U) != 0 ||
            arcv2_le16(prefix + 4U) != 0x0200U)
            goto fail;
        header_size = arcv2_le16(prefix + 6U);
        flags = arcv2_le32(prefix + 8U);
        bytes_on_volume = arcv2_le32(prefix + 12U);
        name_size = prefix[16U];
        if (name_size == 0U || header_size != ARCV2_BLOCK_FIXED_SIZE + name_size ||
            !arcv2_valid_flags(flags) ||
            offset > size - (int64_t)header_size ||
            !arcv2_read_at(format->device, format->base_address + offset,
                           header, header_size))
            goto fail;
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = arcv2_name(header + ARCV2_BLOCK_PREFIX_SIZE, name_size);
        tail = header + ARCV2_BLOCK_PREFIX_SIZE + name_size;
        member.header_offset = format->base_address + offset;
        member.header_size = header_size;
        relative_data_offset = offset + header_size;
        member.data_offset = format->base_address + relative_data_offset;
        member.data_size = bytes_on_volume;
        member.original_size = arcv2_le32(tail);
        member.compressed_size = arcv2_le32(tail + 4U);
        member.attributes = arcv2_le32(tail + 8U);
        member.dos_datetime = arcv2_le32(tail + 12U);
        member.flags = flags;
        member.packed_crc32 = arcv2_le32(tail + 24U);
        if (!member.name || relative_data_offset > size ||
            member.data_size > (uint64_t)(size - relative_data_offset) ||
            (!arcv2_is_split(&member) &&
             member.compressed_size != member.data_size) ||
            (arcv2_is_stored(&member) && !arcv2_is_split(&member) &&
             member.original_size != member.data_size) ||
            (arcv2_is_compressed(&member) && !arcv2_is_split(&member) &&
             member.original_size == 0U) ||
            !arcv2_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
        offset = relative_data_offset + member.data_size;
    }
    if (stream->count == 0U || offset != size) goto fail;
    stream->archive_size = offset;
    *result = stream;
    return true;
fail:
    arcv2_stream_free(stream);
    return false;
}

static uint8_t arcv2_scramble_seed(arcv2_scramble scramble) {
    return scramble == ARCV2_SCRAMBLE_TRIAL ? 0xabU : 0x56U;
}

static bool arcv2_probe_member(Abstractformat *format,
                               const arcv2_member *member,
                               arcv2_scramble scramble) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    uint8_t *overrun = NULL;
    size_t written = 0U;
    bool exact;
    bool extended;
    bool result = false;
    if (!format || !member || !arcv2_is_compressed(member) ||
        arcv2_is_split(member) || member->data_size == 0U ||
        member->original_size == 0U || member->data_size > ARCV2_MAX_PROBE_PACKED ||
        member->original_size > ARCV2_MAX_PROBE_PLAIN)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->data_size);
    plain = (uint8_t *)xx_mem_alloc(member->original_size);
    overrun = (uint8_t *)xx_mem_alloc((size_t)member->original_size + 1U);
    if (!packed || !plain || !overrun ||
        !arcv2_read_at(format->device, member->data_offset, packed,
                       member->data_size))
        goto done;
    if (scramble != ARCV2_SCRAMBLE_NONE &&
        !xx_arcv2_xor_delta_decode(packed, member->data_size,
                                   arcv2_scramble_seed(scramble)))
        goto done;
    exact = xx_arcv2_lzhuf_decode_memory(packed, member->data_size, plain,
                                         member->original_size, false, &written) &&
            written == member->original_size;
    extended = xx_arcv2_lzhuf_decode_memory(
        packed, member->data_size, overrun, (size_t)member->original_size + 1U,
        false, &written);
    result = exact && !extended;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    if (overrun) xx_mem_free(overrun);
    return result;
}

static void arcv2_probe_scramble(Abstractformat *format, arcv2_stream *stream) {
    static const arcv2_scramble modes[] = { ARCV2_SCRAMBLE_NONE,
                                             ARCV2_SCRAMBLE_RELEASE,
                                             ARCV2_SCRAMBLE_TRIAL };
    uint32_t votes[sizeof(modes) / sizeof(modes[0])] = { 0U };
    size_t member_index;
    size_t attempts = 0U;
    uint32_t best_votes = 0U;
    int best_index = -1;
    bool tied = false;
    if (!format || !stream) return;
    for (member_index = 0U;
         member_index < stream->count && attempts < ARCV2_MAX_PROBES;
         ++member_index) {
        const arcv2_member *member = &stream->items[member_index];
        int winner = -1;
        unsigned holding = 0U;
        unsigned mode_index;
        if (!arcv2_is_compressed(member) || arcv2_is_split(member) ||
            member->data_size == 0U || member->original_size == 0U ||
            member->data_size > ARCV2_MAX_PROBE_PACKED ||
            member->original_size > ARCV2_MAX_PROBE_PLAIN)
            continue;
        ++attempts;
        for (mode_index = 0U; mode_index < sizeof(modes) / sizeof(modes[0]);
             ++mode_index) {
            if (arcv2_probe_member(format, member, modes[mode_index])) {
                ++holding;
                winner = (int)mode_index;
            }
        }
        if (holding == 1U) ++votes[winner];
    }
    for (member_index = 0U; member_index < sizeof(modes) / sizeof(modes[0]);
         ++member_index) {
        if (votes[member_index] > best_votes) {
            best_votes = votes[member_index];
            best_index = (int)member_index;
            tied = false;
        } else if (votes[member_index] == best_votes && best_votes != 0U) {
            tied = true;
        }
    }
    if (best_index >= 0 && !tied) stream->scramble = modes[best_index];
}

static bool arcv2_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *arcv2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool arcv2_set_record(xx_archive_record *record,
                             const arcv2_member *member) {
    uint32_t method;
    if (!record || !member) return false;
    method = arcv2_is_stored(member) ? 0U : 1U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_datetime) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->packed_crc32) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool arcv2_decode_member(Abstractformat *format,
                                const arcv2_member *member,
                                arcv2_scramble scramble, uint8_t **plain,
                                size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || arcv2_is_split(member))
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->data_size != 0U ?
                                         member->data_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U ?
                                         member->original_size : 1U);
    if (!packed || !output ||
        (member->data_size != 0U &&
         !arcv2_read_at(format->device, member->data_offset, packed,
                        member->data_size)) ||
        xx_crc32(XX_CRC_TYPE_CRC32_JAMCRC, packed, member->data_size) !=
            member->packed_crc32)
        goto done;
    if (scramble != ARCV2_SCRAMBLE_NONE &&
        !xx_arcv2_xor_delta_decode(packed, member->data_size,
                                   arcv2_scramble_seed(scramble)))
        goto done;
    if (arcv2_is_stored(member)) {
        if (member->data_size != member->original_size) goto done;
        if (member->original_size != 0U)
            xx_rt_memcpy(output, packed, member->original_size);
        written = member->original_size;
        decoded = true;
    } else {
        decoded = xx_arcv2_lzhuf_decode_memory(packed, member->data_size,
                                                output, member->original_size,
                                                false, &written);
    }
    if (!decoded || written != member->original_size) goto done;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_arcv2_init(xx_arcv2 *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARCV2;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arcv");
    xx_format_set_extension(&archive->format, "arv");
    archive->format.check_is_valid = xx_arcv2_check_is_valid;
    archive->format.handle_base_info = xx_arcv2_handle_base_info;
    archive->format.get_format_size = xx_arcv2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arcv2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arcv2_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_arcv2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arcv2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arcv2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arcv2_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arcv2 *xx_arcv2_create(xx_io_device *device, int64_t base_address) {
    xx_arcv2 *archive = (xx_arcv2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arcv2_init(archive, device, base_address);
    return archive;
}

void xx_arcv2_destroy(xx_arcv2 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arcv2_free(xx_arcv2 *archive) {
    if (!archive) return;
    xx_arcv2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arcv2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    arcv2_stream *stream;
    if (!arcv2_parse(format, &stream, pd)) return false;
    arcv2_stream_free(stream);
    return true;
}

bool xx_arcv2_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    arcv2_stream *stream;
    xx_arcv2 *archive;
    int64_t total;
    if (!format || !arcv2_parse(format, &stream, pd)) return false;
    archive = (xx_arcv2 *)format;
    total = xx_io_total_size(format->device);
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->volume_flags = stream->volume_flags;
    archive->disk_number = stream->disk_number;
    archive->scramble = (uint8_t)stream->scramble;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = archive->archive_end < total ? archive->archive_end : -1;
    format->overlay_size = archive->archive_end < total ?
                               total - archive->archive_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    arcv2_stream_free(stream);
    return true;
}

int64_t xx_arcv2_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcv2_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_arcv2_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcv2_handle_base_info(format, pd))
               ? ((xx_arcv2 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_arcv2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    arcv2_stream *stream;
    xx_archive_record_state *state;
    if (!arcv2_parse(format, &stream, pd)) return NULL;
    arcv2_probe_scramble(format, stream);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arcv2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = arcv2_stream_free;
    state->total_records = stream->count;
    if (!arcv2_copy_options(&state->options, options) ||
        !arcv2_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_arcv2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_arcv2_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    arcv2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (arcv2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = arcv2_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_arcv2_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    arcv2_stream *stream;
    arcv2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (arcv2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!arcv2_safe_output_name(member->name) ||
        !arcv2_decode_member(format, member, stream->scramble, &plain,
                             &plain_size))
        goto done;
    path_option = arcv2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arcv2_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
