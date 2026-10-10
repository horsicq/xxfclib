/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TRS-80 JV3 floppy images.  Written from the published layout (Tim Mann's
 * "TRS-80 disk image formats" page); libdsk, HxC and MAME floptool were used
 * only as black-box oracles and no code was taken from them.
 *
 * A block is 2901 sector headers of three bytes (cylinder, sector ID,
 * flags) and one write-protect byte (0xFF writable, 0x00 protected; in any
 * later block the byte is padding).  The data of the 2901 sectors follows in
 * header order.  A free header has cylinder = sector = 0xFF and flags with
 * the top six bits set; its data area is present only while the file goes
 * on.  A second block follows only after the data of all 2901 entries of
 * the first.  Size codes differ between used (0=256 1=128 2=1024 3=512) and
 * free (0=512 1=1024 2=128 3=256) headers.
 *
 * There is no magic.  The probe is bounded: a size test (the whole file is
 * a multiple of 128 bytes), then one 8704-byte header read per block, at
 * most JV3_MAX_BLOCKS blocks, and no data reads.  It requires the data to
 * end exactly on a sector boundary at end of file, no used header after
 * that point, a 0x00/0xFF write-protect byte and few duplicate sector IDs.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trs_80_jv3/xx_trs_80_jv3.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef TRS_80_JV3
#define XX_TRS_80_JV3_FILE_TYPE XX_FILE_TYPE_TRS_80_JV3
#else
#define XX_TRS_80_JV3_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define JV3_ENTRIES 2901U
#define JV3_HEADER_SIZE (JV3_ENTRIES * 3U + 1U) /* 8704 */
#define JV3_MAX_BLOCKS 4U
#define JV3_MIN_SIZE ((int64_t)JV3_HEADER_SIZE + 128)
#define JV3_MAX_SIZE ((int64_t)16 * 1024 * 1024)
#define JV3_MAX_SECTORS (JV3_ENTRIES * JV3_MAX_BLOCKS)
#define JV3_FREE_FLAGS 0xfcU
#define JV3_SIDE 0x10U
#define JV3_ID_BITS (256U * 2U * 256U)

typedef struct jv3_sector_s {
    int64_t offset; /* absolute device offset of the sector data */
    uint32_t size;
    uint32_t order; /* position in the file, keeps the sort stable */
    uint8_t cylinder;
    uint8_t side;
    uint8_t id;
} jv3_sector;

typedef struct jv3_member_s {
    char *name;
    size_t first;
    size_t count;
    uint64_t size;
    int64_t header_offset;
} jv3_member;

typedef struct jv3_stream_s {
    jv3_sector *sectors;
    size_t sector_count;
    jv3_member *members;
    size_t member_count;
    size_t index;
    int64_t archive_size;
    bool write_protected;
} jv3_stream;

static bool jv3_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static uint32_t jv3_sector_size(bool is_free, uint8_t flags)
{
    static const uint32_t used_sizes[4] = {256U, 128U, 1024U, 512U};
    static const uint32_t free_sizes[4] = {512U, 1024U, 128U, 256U};
    return is_free ? free_sizes[flags & 3U] : used_sizes[flags & 3U];
}

/* "track" + cylinder (at least two digits) + "_" + side + ".bin".  Built
 * from numbers only, so safe by construction. */
static char *jv3_make_name(unsigned cylinder, unsigned side)
{
    char buffer[24];
    char digits[4];
    size_t used = 0U, count = 0U;
    char *result;
    static const char prefix[] = "track";
    static const char suffix[] = ".bin";
    size_t index;
    for (index = 0U; prefix[index]; ++index) buffer[used++] = prefix[index];
    do {
        digits[count++] = (char)('0' + (cylinder % 10U));
        cylinder /= 10U;
    } while (cylinder != 0U && count < 3U);
    while (count < 2U) digits[count++] = '0';
    while (count != 0U) buffer[used++] = digits[--count];
    buffer[used++] = '_';
    buffer[used++] = (char)('0' + (side & 1U));
    for (index = 0U; suffix[index]; ++index) buffer[used++] = suffix[index];
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void jv3_stream_free(void *opaque)
{
    jv3_stream *stream = (jv3_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->member_count; ++index)
        if (stream->members[index].name) xx_mem_free(stream->members[index].name);
    if (stream->members) xx_mem_free(stream->members);
    if (stream->sectors) xx_mem_free(stream->sectors);
    xx_mem_free(stream);
}

static bool jv3_less(const jv3_sector *a, const jv3_sector *b)
{
    if (a->cylinder != b->cylinder) return a->cylinder < b->cylinder;
    if (a->side != b->side) return a->side < b->side;
    if (a->id != b->id) return a->id < b->id;
    return a->order < b->order;
}

static void jv3_sift(jv3_sector *items, size_t root, size_t count)
{
    for (;;) {
        size_t child = root * 2U + 1U;
        jv3_sector swap;
        if (child >= count) return;
        if (child + 1U < count && jv3_less(&items[child], &items[child + 1U])) ++child;
        if (!jv3_less(&items[root], &items[child])) return;
        swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

/* Heap sort: bounded O(n log n) for any input; keys are unique thanks to
 * `order`, so the result is deterministic. */
static void jv3_sort(jv3_sector *items, size_t count)
{
    size_t index;
    if (count < 2U) return;
    for (index = count / 2U; index > 0U; --index) jv3_sift(items, index - 1U, count);
    for (index = count - 1U; index > 0U; --index) {
        jv3_sector swap = items[0];
        items[0] = items[index];
        items[index] = swap;
        jv3_sift(items, 0U, index);
    }
}

/* Walks the blocks.  With `result` NULL it only validates. */
static bool jv3_parse(Abstractformat *format, jv3_stream **result)
{
    uint8_t *header = NULL;
    uint8_t *seen = NULL;
    jv3_stream *stream = NULL;
    jv3_sector *sectors = NULL;
    size_t sector_count = 0U, duplicates = 0U;
    int64_t total, size, position = 0;
    unsigned blocks = 0U;
    bool write_protected = false;
    bool ended = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < JV3_MIN_SIZE || size > JV3_MAX_SIZE || (size % 128) != 0) return false;
    header = (uint8_t *)xx_mem_alloc(JV3_HEADER_SIZE);
    seen = (uint8_t *)xx_mem_calloc(1U, JV3_ID_BITS / 8U);
    if (!header || !seen) goto fail;
    if (result) {
        sectors = (jv3_sector *)xx_mem_alloc(JV3_MAX_SECTORS * sizeof(*sectors));
        if (!sectors) goto fail;
    }
    while (position < size && !ended) {
        uint32_t entry;
        if (blocks >= JV3_MAX_BLOCKS || size - position < (int64_t)JV3_HEADER_SIZE ||
            !jv3_read_at(format->device, format->base_address + position, header, JV3_HEADER_SIZE))
            goto fail;
        if (blocks == 0U) {
            uint8_t protect = header[JV3_HEADER_SIZE - 1U];
            if (protect != 0x00U && protect != 0xffU) goto fail;
            write_protected = protect == 0x00U;
        }
        ++blocks;
        position += JV3_HEADER_SIZE;
        for (entry = 0U; entry < JV3_ENTRIES; ++entry) {
            uint8_t cylinder = header[entry * 3U];
            uint8_t id = header[entry * 3U + 1U];
            uint8_t flags = header[entry * 3U + 2U];
            bool is_free = cylinder == 0xffU;
            uint32_t sector_size;
            if (is_free && (id != 0xffU || (flags & JV3_FREE_FLAGS) != JV3_FREE_FLAGS)) goto fail;
            sector_size = jv3_sector_size(is_free, flags);
            if (position == size) ended = true;
            if (ended) {
                /* Past end of data only free headers may remain. */
                if (!is_free) goto fail;
                continue;
            }
            if (size - position < (int64_t)sector_size) goto fail;
            if (!is_free) {
                uint8_t side = (uint8_t)((flags & JV3_SIDE) ? 1U : 0U);
                uint32_t bit = ((uint32_t)cylinder << 9U) | ((uint32_t)side << 8U) | id;
                if (seen[bit >> 3U] & (uint8_t)(1U << (bit & 7U))) ++duplicates;
                else seen[bit >> 3U] |= (uint8_t)(1U << (bit & 7U));
                if (sector_count >= JV3_MAX_SECTORS) goto fail;
                if (sectors) {
                    jv3_sector *sector = &sectors[sector_count];
                    sector->offset = format->base_address + position;
                    sector->size = sector_size;
                    sector->order = (uint32_t)sector_count;
                    sector->cylinder = cylinder;
                    sector->side = side;
                    sector->id = id;
                }
                ++sector_count;
            }
            position += sector_size;
        }
    }
    /* A zero-filled or repetitive file parses as one sector ID over and
     * over; real images repeat IDs only on a few protected tracks. */
    if (sector_count == 0U || duplicates * 4U > sector_count) goto fail;
    xx_mem_free(header);
    header = NULL;
    xx_mem_free(seen);
    seen = NULL;
    if (!result) return true;

    stream = (jv3_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->sectors = sectors;
    stream->sector_count = sector_count;
    sectors = NULL;
    stream->archive_size = size;
    stream->write_protected = write_protected;
    jv3_sort(stream->sectors, stream->sector_count);
    {
        size_t index = 0U;
        /* At most 256 cylinders x 2 sides. */
        stream->members = (jv3_member *)xx_mem_calloc(512U, sizeof(jv3_member));
        if (!stream->members) goto fail;
        while (index < stream->sector_count) {
            const jv3_sector *first = &stream->sectors[index];
            jv3_member *member;
            size_t end = index;
            uint64_t bytes = 0U;
            if (stream->member_count >= 512U) goto fail;
            while (end < stream->sector_count && stream->sectors[end].cylinder == first->cylinder && stream->sectors[end].side == first->side) {
                bytes += stream->sectors[end].size;
                ++end;
            }
            member = &stream->members[stream->member_count];
            member->name = jv3_make_name(first->cylinder, first->side);
            if (!member->name) goto fail;
            member->first = index;
            member->count = end - index;
            member->size = bytes;
            member->header_offset = format->base_address;
            ++stream->member_count;
            index = end;
        }
    }
    *result = stream;
    return true;
fail:
    if (header) xx_mem_free(header);
    if (seen) xx_mem_free(seen);
    if (sectors) xx_mem_free(sectors);
    jv3_stream_free(stream);
    return false;
}

static bool jv3_extract(Abstractformat *format, const jv3_stream *stream, const jv3_member *member, uint8_t **plain, size_t *plain_size)
{
    uint8_t *output;
    size_t index, used = 0U;
    if (!format || !stream || !member || !plain || !plain_size || member->size == 0U || member->size > (uint64_t)JV3_MAX_SIZE || member->first > stream->sector_count ||
        member->count > stream->sector_count - member->first)
        return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    for (index = 0U; index < member->count; ++index) {
        const jv3_sector *sector = &stream->sectors[member->first + index];
        if (sector->size > (size_t)member->size - used || !jv3_read_at(format->device, sector->offset, output + used, sector->size)) {
            xx_mem_free(output);
            return false;
        }
        used += sector->size;
    }
    if (used != (size_t)member->size) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = used;
    return true;
}

static bool jv3_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *jv3_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool jv3_set_record(xx_archive_record *record, const jv3_stream *stream, const jv3_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = JV3_HEADER_SIZE;
    record->data_offset = stream->sectors[member->first].offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_trs_80_jv3_init(xx_trs_80_jv3 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TRS_80_JV3_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-trs80-jv3");
    xx_format_set_extension(&archive->format, "jv3");
    archive->format.check_is_valid = xx_trs_80_jv3_check_is_valid;
    archive->format.handle_base_info = xx_trs_80_jv3_handle_base_info;
    archive->format.get_format_size = xx_trs_80_jv3_get_format_size;
    archive->format.get_number_of_archive_records = xx_trs_80_jv3_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_trs_80_jv3_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_trs_80_jv3_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_trs_80_jv3_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_trs_80_jv3_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_trs_80_jv3_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_trs_80_jv3 *xx_trs_80_jv3_create(xx_io_device *device, int64_t base_address)
{
    xx_trs_80_jv3 *archive = (xx_trs_80_jv3 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_trs_80_jv3_init(archive, device, base_address);
    return archive;
}

void xx_trs_80_jv3_destroy(xx_trs_80_jv3 *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_trs_80_jv3_free(xx_trs_80_jv3 *archive)
{
    if (!archive) return;
    xx_trs_80_jv3_destroy(archive);
    xx_mem_free(archive);
}

bool xx_trs_80_jv3_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    (void)pd;
    return jv3_parse(format, NULL);
}

bool xx_trs_80_jv3_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    jv3_stream *stream;
    xx_trs_80_jv3 *archive;
    (void)pd;
    if (!format || !jv3_parse(format, &stream)) return false;
    archive = (xx_trs_80_jv3 *)format;
    archive->number_of_records = stream->member_count;
    archive->number_of_sectors = stream->sector_count;
    archive->write_protected = stream->write_protected;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->member_count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    jv3_stream_free(stream);
    return true;
}

int64_t xx_trs_80_jv3_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_trs_80_jv3_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_trs_80_jv3_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_trs_80_jv3_handle_base_info(format, pd)) ? ((xx_trs_80_jv3 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_trs_80_jv3_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    jv3_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!jv3_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        jv3_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = jv3_stream_free;
    state->total_records = (int64_t)stream->member_count;
    if (!jv3_copy_options(&state->options, options) || !jv3_set_record(&state->current_record, stream, &stream->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_trs_80_jv3_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_trs_80_jv3_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    jv3_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (jv3_stream *)state->internal_state) || ++stream->index >= stream->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = jv3_set_record(&state->current_record, stream, &stream->members[stream->index]);
    return state->has_record;
}

bool xx_trs_80_jv3_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    jv3_stream *stream;
    jv3_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (jv3_stream *)state->internal_state) || stream->index >= stream->member_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->members[stream->index];
    if (!jv3_extract(format, stream, member, &plain, &plain_size)) goto done;
    path_option = jv3_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_trs_80_jv3_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
