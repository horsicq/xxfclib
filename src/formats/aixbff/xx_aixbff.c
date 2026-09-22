/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/aixbff/xx_aixbff.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/unixpack/xx_unixpack.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_BFF_VOLUME_MAGIC UINT32_C(0xea6b0009)
#define XX_BFF_VOLUME_HEADER_SIZE 0x48U
#define XX_BFF_FIXED_HEADER_SIZE 0x40U
#define XX_BFF_TRAILER_SIZE 40U
#define XX_BFF_MAGIC_STORED 0xea6bU
#define XX_BFF_MAGIC_PACKED 0xea6cU
#define XX_BFF_RECORD_MEMBER 0x0bU
#define XX_BFF_RECORD_TERMINATOR 0x07U
#define XX_BFF_MODE_MASK UINT32_C(0xf000)
#define XX_BFF_MODE_DIRECTORY UINT32_C(0x4000)
#define XX_BFF_MODE_REGULAR UINT32_C(0x8000)
#define XX_BFF_MAX_MEMBERS 200000U

typedef struct xx_bff_member_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t original_size;
    uint32_t mode;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint16_t magic;
    bool folder;
    char *name;
} xx_bff_member;

typedef struct xx_bff_stream_s {
    xx_bff_member *items;
    size_t count;
    size_t index;
    int64_t archive_end;
} xx_bff_stream;

static uint16_t xx_bff_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t xx_bff_u32(const uint8_t *p) {
    return (uint32_t)xx_bff_u16(p) |
           ((uint32_t)xx_bff_u16(p + 2U) << 16U);
}

static int64_t xx_bff_align(int64_t value, int64_t alignment) {
    int64_t remainder;
    if (value < 0 || alignment <= 0) return -1;
    remainder = value % alignment;
    if (remainder == 0) return value;
    if (value > INT64_MAX - (alignment - remainder)) return -1;
    return value + alignment - remainder;
}

static bool xx_bff_read(xx_io_device *device, int64_t offset, void *data,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET)) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void xx_bff_stream_free(void *pointer) {
    xx_bff_stream *stream = (xx_bff_stream *)pointer;
    size_t i;
    if (!stream) return;
    for (i = 0U; i < stream->count; ++i)
        if (stream->items[i].name) xx_str_free(stream->items[i].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_bff_name_valid(const uint8_t *name, size_t size) {
    size_t i;
    if (!name || size == 0U) return false;
    for (i = 0U; i < size; ++i)
        if (name[i] < 0x20U || name[i] > 0x7eU) return false;
    return true;
}

static bool xx_bff_parse(Abstractformat *format, xx_bff_stream **result) {
    uint8_t volume[XX_BFF_VOLUME_HEADER_SIZE];
    xx_bff_stream *stream;
    int64_t total;
    int64_t relative_size;
    int64_t offset;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    relative_size = total - format->base_address;
    if (relative_size < (int64_t)(XX_BFF_VOLUME_HEADER_SIZE +
                                  XX_BFF_FIXED_HEADER_SIZE + 8U) ||
        !xx_bff_read(format->device, format->base_address, volume,
                     sizeof(volume)) ||
        xx_bff_u32(volume) != XX_BFF_VOLUME_MAGIC) return false;
    stream = (xx_bff_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    offset = XX_BFF_VOLUME_HEADER_SIZE;
    while (stream->count < XX_BFF_MAX_MEMBERS) {
        uint8_t lead[4];
        uint8_t *header = NULL;
        uint8_t words;
        uint8_t type;
        uint16_t magic;
        int64_t name_area;
        int64_t header_size;
        int64_t next;
        size_t name_size;
        uint32_t kind;
        xx_bff_member *grown;
        xx_bff_member *member;
        if (offset < 0 || offset > relative_size || relative_size - offset < 4 ||
            !xx_bff_read(format->device, format->base_address + offset, lead,
                         sizeof(lead))) goto fail;
        words = lead[0];
        type = lead[1];
        magic = xx_bff_u16(lead + 2U);
        if (magic != XX_BFF_MAGIC_STORED && magic != XX_BFF_MAGIC_PACKED)
            goto fail;
        if (type == XX_BFF_RECORD_TERMINATOR) {
            int64_t padded;
            if (stream->count == 0U) goto fail;
            padded = xx_bff_align(offset + 4, 1024);
            stream->archive_end = format->base_address +
                                  (padded < 0 || padded > relative_size
                                       ? offset + 4
                                       : padded);
            *result = stream;
            return true;
        }
        if (type != XX_BFF_RECORD_MEMBER || words < 9U) goto fail;
        name_area = (int64_t)words * 8;
        header_size = name_area + XX_BFF_TRAILER_SIZE;
        if (name_area > SIZE_MAX || offset > relative_size ||
            header_size > relative_size - offset) goto fail;
        header = (uint8_t *)xx_mem_alloc((size_t)name_area);
        if (!header ||
            !xx_bff_read(format->device, format->base_address + offset,
                         header, (size_t)name_area)) {
            if (header) xx_mem_free(header);
            goto fail;
        }
        name_size = 0U;
        while (XX_BFF_FIXED_HEADER_SIZE + name_size < (size_t)name_area &&
               header[XX_BFF_FIXED_HEADER_SIZE + name_size] != 0U) ++name_size;
        if (XX_BFF_FIXED_HEADER_SIZE + name_size >= (size_t)name_area ||
            !xx_bff_name_valid(header + XX_BFF_FIXED_HEADER_SIZE, name_size) ||
            XX_BFF_FIXED_HEADER_SIZE +
                    (size_t)xx_bff_align((int64_t)name_size + 1, 8) !=
                (size_t)name_area) {
            xx_mem_free(header);
            goto fail;
        }
        grown = (xx_bff_member *)xx_mem_realloc(
            stream->items, (stream->count + 1U) * sizeof(*grown));
        if (!grown) {
            xx_mem_free(header);
            goto fail;
        }
        stream->items = grown;
        member = &stream->items[stream->count];
        xx_mem_zero(member, sizeof(*member));
        member->name = (char *)xx_mem_alloc(name_size + 1U);
        if (!member->name) {
            xx_mem_free(header);
            goto fail;
        }
        xx_mem_copy(member->name, header + XX_BFF_FIXED_HEADER_SIZE, name_size);
        member->name[name_size] = '\0';
        member->header_offset = format->base_address + offset;
        member->header_size = header_size;
        member->data_offset = member->header_offset + header_size;
        member->magic = magic;
        member->mode = xx_bff_u32(header + 0x0cU);
        member->original_size = xx_bff_u32(header + 0x18U);
        member->atime = xx_bff_u32(header + 0x1cU);
        member->mtime = xx_bff_u32(header + 0x20U);
        member->ctime = xx_bff_u32(header + 0x24U);
        member->packed_size = xx_bff_u32(header + 0x38U);
        kind = member->mode & XX_BFF_MODE_MASK;
        member->folder = kind == XX_BFF_MODE_DIRECTORY;
        if (kind != XX_BFF_MODE_DIRECTORY && kind != XX_BFF_MODE_REGULAR) {
            xx_mem_free(header);
            goto fail;
        }
        if (member->folder) {
            member->packed_size = 0U;
            member->original_size = 0U;
        } else if (magic == XX_BFF_MAGIC_STORED &&
                   member->packed_size != member->original_size) {
            xx_mem_free(header);
            goto fail;
        }
        if ((uint64_t)member->packed_size >
            (uint64_t)(relative_size - offset - header_size)) {
            xx_mem_free(header);
            goto fail;
        }
        ++stream->count;
        xx_mem_free(header);
        next = xx_bff_align(offset + header_size + member->packed_size, 8);
        if (next <= offset) goto fail;
        offset = next;
    }
fail:
    xx_bff_stream_free(stream);
    return false;
}

static bool xx_bff_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
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

static const xx_var *xx_bff_option(const xx_list_s *options, uint32_t id) {
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool xx_bff_safe_name(const char *name) {
    const char *part;
    const char *p;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (name[1] == ':' && ((name[0] >= 'A' && name[0] <= 'Z') ||
                           (name[0] >= 'a' && name[0] <= 'z'))) return false;
    part = name;
    for (p = name;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c && c < 32U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t n = (size_t)(p - part);
            /* ".." would climb out of the destination directory. A single "."
             * only names the current directory, and every BFF member path
             * starts with one ("./usr/lpp/..."), so rejecting it would make
             * the whole format unextractable. An empty component is a doubled
             * separator unless it is the trailing one, which is how BFF spells
             * a directory member. */
            if (n == 2U && part[0] == '.' && part[1] == '.') return false;
            if (n == 0U && c != 0U) return false;
            if (!c) return true;
            part = p + 1;
        }
    }
}

static bool xx_bff_set_record(xx_archive_record *record,
                              const xx_bff_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->magic) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->mtime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_bff_decode(Abstractformat *format, const xx_bff_member *member,
                          uint8_t **data, size_t *size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool ok = false;
    if (member->folder) {
        *data = NULL;
        *size = 0U;
        return true;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size
                                         ? member->packed_size
                                         : 1U);
    plain = (uint8_t *)xx_mem_alloc(member->original_size
                                        ? member->original_size
                                        : 1U);
    if (!packed || !plain ||
        (member->packed_size &&
         !xx_bff_read(format->device, member->data_offset, packed,
                      member->packed_size))) goto done;
    if (member->magic == XX_BFF_MAGIC_STORED) {
        if (member->packed_size != member->original_size) goto done;
        if (member->original_size) xx_mem_copy(plain, packed, member->original_size);
        written = member->original_size;
    } else if (!xx_unixpack_decode_raw(packed, member->packed_size, plain,
                                       member->original_size, &written)) {
        goto done;
    }
    if (written != member->original_size || (pd && xx_pd_is_stopped(pd)))
        goto done;
    *data = plain;
    *size = written;
    plain = NULL;
    ok = true;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return ok;
}

void xx_aixbff_init(xx_aixbff *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.file_type = XX_FILE_TYPE_AIXBFF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "bff");
    archive->format.check_is_valid = xx_aixbff_check_is_valid;
    archive->format.handle_base_info = xx_aixbff_handle_base_info;
    archive->format.get_format_size = xx_aixbff_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_aixbff_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_aixbff_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_aixbff_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_aixbff_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_aixbff_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_aixbff_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_aixbff *xx_aixbff_create(xx_io_device *device, int64_t base_address) {
    xx_aixbff *archive = (xx_aixbff *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_aixbff_init(archive, device, base_address);
    return archive;
}

void xx_aixbff_destroy(xx_aixbff *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_aixbff_free(xx_aixbff *archive) {
    if (!archive) return;
    xx_aixbff_destroy(archive);
    xx_mem_free(archive);
}

bool xx_aixbff_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_bff_stream *stream;
    (void)pd;
    if (!xx_bff_parse(format, &stream)) return false;
    xx_bff_stream_free(stream);
    return true;
}

bool xx_aixbff_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_bff_stream *stream;
    xx_aixbff *archive;
    (void)pd;
    if (!format || !xx_bff_parse(format, &stream)) return false;
    archive = (xx_aixbff *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = stream->archive_end;
    format->format_size = stream->archive_end - format->base_address;
    format->number_of_archive_records = stream->count;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_bff_stream_free(stream);
    return true;
}

int64_t xx_aixbff_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aixbff_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_aixbff_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aixbff_handle_base_info(format, pd))
               ? ((xx_aixbff *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_aixbff_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_bff_stream *stream;
    (void)pd;
    if (!xx_bff_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bff_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_bff_stream_free;
    state->total_records = stream->count;
    if (!xx_bff_copy_options(&state->options, options) ||
        (stream->count &&
         !xx_bff_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_aixbff_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_aixbff_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bff_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xx_bff_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = xx_bff_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_aixbff_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bff_stream *stream;
    const xx_bff_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool ok = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xx_bff_stream *)state->internal_state) ||
        stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bff_safe_name(member->name)) return false;
    option = xx_bff_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        ok = xx_bff_decode(format, member, &plain, &plain_size, pd);
        if (plain) xx_mem_free(plain);
        return ok;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\')
        path = xx_str_concat3(base, "/", member->name);
    else
        path = xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        ok = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_bff_decode(format, member, &plain, &plain_size, pd) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = xx_io_file_open(path, "wb");
        size_t at = 0U;
        if (!output) goto done;
        ok = true;
        while (at < plain_size) {
            ssize_t amount = xx_io_write(output, plain + at, plain_size - at);
            if (amount <= 0 || (size_t)amount > plain_size - at) {
                ok = false;
                break;
            }
            at += (size_t)amount;
        }
        xx_io_close(output);
    }
done:
    if (!ok && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}

void xx_aixbff_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
