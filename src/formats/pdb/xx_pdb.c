/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Palm OS database container (.pdb record databases, .prc resource
 * databases).  The layout is the documented PalmOS one; xx_pdb.h carries the
 * field table.  Ported from XArchive's packages/xpalmdatabase.cpp.
 *
 * The container has no magic: acceptance rests on the structural walk - a
 * plausible 32-byte name, no reserved attribute bit, two printable 4CCs, and
 * an entry list whose offsets are monotonic, start right behind the table and
 * stop short of EOF.  Blocks carry no stored size, so that walk is what
 * gives every member its extent.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pdb/xx_pdb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PDB is registered there. */
#ifdef PDB
#define XX_PDB_FILE_TYPE XX_FILE_TYPE_PDB
#else
#define XX_PDB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PDB_HEADER_SIZE 78
#define PDB_NAME_SIZE 32
#define PDB_RECORD_ENTRY_SIZE 8
#define PDB_RESOURCE_ENTRY_SIZE 10
#define PDB_ATTR_RESOURCE 0x0001U
/* PalmOS defines bits 0..11, 14 and 15; 0x7000 is reserved and is always
 * clear in a real database, so a set bit there is a cheap reject. */
#define PDB_ATTR_RESERVED_MASK 0x7000U
#define PDB_MAX_ENTRIES 65535
/* PalmOS aligns the first block to an even offset, so writers emit either
 * nothing or a two-byte filler between the entry list and the first block. */
#define PDB_MAX_FILLER 2
/* "record_" + 5 digits + ".bin", or a sanitised 4CC + '_' + 5 digits +
 * ".bin"; the widest of those is 17 bytes plus its terminator. */
#define PDB_NAME_BUFFER 24

typedef enum pdb_kind_e {
    PDB_KIND_APPINFO = 0,
    PDB_KIND_SORTINFO,
    PDB_KIND_RECORD,
    PDB_KIND_RESOURCE
} pdb_kind;

typedef struct pdb_member_s {
    char name[PDB_NAME_BUFFER];
    int64_t offset;
    int64_t size;
    uint32_t attributes; /**< Record attribute byte, or resource id. */
    uint32_t id;         /**< Record uniqueID, or resource type 4CC. */
    pdb_kind kind;
} pdb_member;

typedef struct pdb_stream_s {
    pdb_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t header_size;
    bool is_resource_database;
} pdb_stream;

static uint16_t pdb_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t pdb_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool pdb_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pdb_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* The 32-byte name buffer is read at its FIELD size, never size - 1: a name
 * that fills the buffer completely puts its terminator in the last byte, and
 * reading one short would reject the record. */
static bool pdb_check_name(const uint8_t *name) {
    int32_t terminator = -1;
    int32_t index;
    if (name[0] == 0U) return false;
    for (index = 0; index < PDB_NAME_SIZE; ++index) {
        if (name[index] == 0U) {
            terminator = index;
            break;
        }
        /* Control characters never appear in a PalmOS database name; high
         * bytes do, because national character sets are stored raw. */
        if (name[index] < 0x20U) return false;
    }
    if (terminator >= 0)
        /* Everything past the terminator must be padding.  Stale bytes there
         * would mean this is not a PalmOS header at all. */
        for (index = terminator; index < PDB_NAME_SIZE; ++index)
            if (name[index] != 0U) return false;
    return true;
}

static bool pdb_is_printable_4cc(uint32_t value) {
    int32_t index;
    for (index = 0; index < 4; ++index) {
        uint8_t c = (uint8_t)((value >> (8U * (3U - (unsigned)index))) & 0xffU);
        if (c < 0x20U || c > 0x7eU) return false;
    }
    return true;
}

/* Write `value` as five decimal digits at `out`, returning the count. */
static size_t pdb_write_number(char *out, uint32_t value) {
    size_t index;
    uint32_t divisor = 10000U;
    for (index = 0U; index < 5U; ++index) {
        out[index] = (char)('0' + (char)((value / divisor) % 10U));
        divisor /= 10U;
    }
    return 5U;
}

static size_t pdb_write_literal(char *out, const char *text) {
    size_t index = 0U;
    while (text[index] != 0) {
        out[index] = text[index];
        ++index;
    }
    return index;
}

/* A 4CC becomes a filename token: letters, digits, '_' and '-' survive,
 * everything else becomes '_'. */
static size_t pdb_write_token(char *out, uint32_t value) {
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        uint8_t c = (uint8_t)((value >> (8U * (3U - (unsigned)index))) & 0xffU);
        bool keep = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') || c == '_' || c == '-';
        out[index] = keep ? (char)c : '_';
    }
    return 4U;
}

static void pdb_stream_free(void *opaque) {
    pdb_stream *stream = (pdb_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool pdb_parse(Abstractformat *format, pdb_stream **result) {
    uint8_t header[PDB_HEADER_SIZE];
    uint8_t *entries = NULL;
    pdb_stream *stream = NULL;
    pdb_member *items = NULL;
    int64_t total, size, app_info, sort_info, entry_size, table_size;
    int64_t header_size, previous;
    uint32_t attributes, type, creator;
    int32_t entry_count, index;
    size_t count = 0U, capacity;
    bool resource_database;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)PDB_HEADER_SIZE + 1 ||
        !pdb_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (!pdb_check_name(header)) return false;
    attributes = pdb_be16(header + 0x20U);
    if ((attributes & PDB_ATTR_RESERVED_MASK) != 0U) return false;
    app_info = (int64_t)pdb_be32(header + 0x34U);
    sort_info = (int64_t)pdb_be32(header + 0x38U);
    type = pdb_be32(header + 0x3cU);
    creator = pdb_be32(header + 0x40U);
    /* Both 4CCs are registered ASCII identifiers on PalmOS; nothing
     * legitimate stores binary there, and requiring them is what keeps this
     * permissive "header plus offset table" shape from matching unrelated
     * files. */
    if (!pdb_is_printable_4cc(type) || !pdb_is_printable_4cc(creator))
        return false;
    resource_database = (attributes & PDB_ATTR_RESOURCE) != 0U;
    entry_count = (int32_t)pdb_be16(header + 0x4cU);
    if (entry_count < 1 || entry_count > PDB_MAX_ENTRIES) return false;
    entry_size = resource_database ? PDB_RESOURCE_ENTRY_SIZE
                                   : PDB_RECORD_ENTRY_SIZE;
    table_size = entry_size * (int64_t)entry_count;
    if (!pdb_range_within(size, PDB_HEADER_SIZE, table_size)) return false;
    header_size = (int64_t)PDB_HEADER_SIZE + table_size;

    entries = (uint8_t *)xx_mem_alloc((size_t)table_size);
    /* Two extra slots hold the optional appInfo and sortInfo blocks. */
    capacity = (size_t)entry_count + 2U;
    items = (pdb_member *)xx_mem_alloc(capacity * sizeof(*items));
    if (!entries || !items ||
        !pdb_read_at(format->device, format->base_address + PDB_HEADER_SIZE,
                     entries, (size_t)table_size))
        goto fail;

    /* Block offsets are collected in file order: appInfo, sortInfo, then the
     * records or resources.  Sizes are implicit - each block runs to the next
     * offset and the last one runs to EOF - so the walk has to be monotonic
     * to make any sense at all. */
    if (app_info != 0) {
        pdb_member *member = &items[count++];
        xx_mem_zero(member, sizeof(*member));
        member->kind = PDB_KIND_APPINFO;
        member->offset = app_info;
        member->name[pdb_write_literal(member->name, "appinfo.bin")] = 0;
    }
    if (sort_info != 0) {
        pdb_member *member = &items[count++];
        xx_mem_zero(member, sizeof(*member));
        member->kind = PDB_KIND_SORTINFO;
        member->offset = sort_info;
        member->name[pdb_write_literal(member->name, "sortinfo.bin")] = 0;
    }
    for (index = 0; index < entry_count; ++index) {
        const uint8_t *entry = entries + entry_size * (int64_t)index;
        pdb_member *member = &items[count++];
        size_t at = 0U;
        xx_mem_zero(member, sizeof(*member));
        if (resource_database) {
            uint32_t resource_type = pdb_be32(entry);
            uint32_t resource_id = (uint32_t)pdb_be16(entry + 4U);
            if (!pdb_is_printable_4cc(resource_type)) goto fail;
            member->kind = PDB_KIND_RESOURCE;
            member->offset = (int64_t)pdb_be32(entry + 6U);
            member->id = resource_type;
            member->attributes = resource_id;
            at += pdb_write_token(member->name + at, resource_type);
            member->name[at++] = '_';
            at += pdb_write_number(member->name + at, resource_id);
            at += pdb_write_literal(member->name + at, ".bin");
        } else {
            member->kind = PDB_KIND_RECORD;
            member->offset = (int64_t)pdb_be32(entry);
            member->attributes = (uint32_t)entry[4];
            member->id = ((uint32_t)entry[5] << 16U) |
                         ((uint32_t)entry[6] << 8U) | (uint32_t)entry[7];
            at += pdb_write_literal(member->name + at, "record_");
            at += pdb_write_number(member->name + at, (uint32_t)index);
            at += pdb_write_literal(member->name + at, ".bin");
        }
        member->name[at] = 0;
    }

    /* Monotonic, in range, and starting right behind the entry table apart
     * from the two-byte alignment filler PalmOS writers emit. */
    if (items[0].offset - header_size < 0 ||
        items[0].offset - header_size > PDB_MAX_FILLER) goto fail;
    previous = header_size;
    for (index = 0; (size_t)index < count; ++index) {
        if (items[index].offset < previous || items[index].offset > size)
            goto fail;
        previous = items[index].offset;
    }
    /* The final block must actually contain something; a database whose last
     * entry points at EOF is truncated, not merely empty. */
    if (previous >= size) goto fail;
    for (index = 0; (size_t)index < count; ++index) {
        int64_t next = ((size_t)index + 1U < count) ? items[index + 1].offset
                                                    : size;
        items[index].size = next - items[index].offset;
        items[index].offset += format->base_address;
    }

    stream = (pdb_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->items = items;
    stream->count = count;
    stream->archive_size = size;
    stream->header_size = header_size;
    stream->is_resource_database = resource_database;
    xx_mem_free(entries);
    *result = stream;
    return true;
fail:
    if (entries) xx_mem_free(entries);
    if (items) xx_mem_free(items);
    return false;
}

static bool pdb_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pdb_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pdb_set_record(xx_archive_record *record,
                           const pdb_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->offset;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_pdb_init(xx_pdb *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_PDB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/vnd.palm");
    xx_format_set_extension(&archive->format, "pdb");
    archive->format.check_is_valid = xx_pdb_check_is_valid;
    archive->format.handle_base_info = xx_pdb_handle_base_info;
    archive->format.get_format_size = xx_pdb_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pdb_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pdb_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pdb_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pdb_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pdb_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pdb_free_archive_records_reading;
    archive->header_size = -1;
}

xx_pdb *xx_pdb_create(xx_io_device *device, int64_t base_address) {
    xx_pdb *archive = (xx_pdb *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pdb_init(archive, device, base_address);
    return archive;
}

void xx_pdb_destroy(xx_pdb *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pdb_free(xx_pdb *archive) {
    if (!archive) return;
    xx_pdb_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pdb_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pdb_stream *stream;
    (void)pd;
    if (!pdb_parse(format, &stream)) return false;
    pdb_stream_free(stream);
    return true;
}

bool xx_pdb_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pdb_stream *stream;
    xx_pdb *archive;
    (void)pd;
    if (!format || !pdb_parse(format, &stream)) return false;
    archive = (xx_pdb *)format;
    archive->number_of_records = stream->count;
    archive->header_size = stream->header_size;
    archive->is_resource_database = stream->is_resource_database;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    pdb_stream_free(stream);
    return true;
}

int64_t xx_pdb_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pdb_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_pdb_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pdb_handle_base_info(format, pd))
               ? ((xx_pdb *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_pdb_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pdb_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!pdb_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pdb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pdb_stream_free;
    state->total_records = stream->count;
    if (!pdb_copy_options(&state->options, options) ||
        !pdb_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pdb_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pdb_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    pdb_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pdb_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pdb_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pdb_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    pdb_stream *stream;
    pdb_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pdb_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || (uint64_t)member->size > (uint64_t)SIZE_MAX)
        return false;
    plain_size = (size_t)member->size;
    plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!plain) return false;
    if (plain_size != 0U &&
        !pdb_read_at(format->device, member->offset, plain, plain_size))
        goto done;
    path_option = pdb_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_pdb_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
