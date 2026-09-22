/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Visioneer / ScanSoft PaperPort desktop files ("~DESKTOP.nnn", ".max").
 * Ported from XArchive's documents/xpaperport.cpp and cross-checked against
 * U3's PaperPort handler (class jlb, VMT 006c20c8).
 *
 *   file header, 200 bytes at offset 0:
 *     0x00   3  char[3]  "ViG"
 *     0x03   1  u8       variant letter, 'C' 'D' 'E' or 'F'
 *     0x05   1  u8       0x1a
 *     0xa4   4  i32 LE   offset of the root object
 *
 *   every object is a 32-byte chunk header plus its payload:
 *     +0x00  2  char[2]  "VZ"
 *     +0x02  4  i32 LE   payload size, counted past this header
 *     +0x1a  2  u16 LE   chunk type: 0x8000 root, 0x4000 item, 0x1000 image
 *
 *   the root payload opens with 22 bytes whose u16 at +0 and u16 at +10 are
 *   both the page count, then one 12-byte entry per page whose i32 at +4
 *   points at that page's item object.  An item payload is 62 bytes whose
 *   i32 at +34 points at the page's image object.
 *
 * Recognition walks that whole tree: "ViG" plus the variant letter plus the
 * 0x1a, then the root chunk's type, then the duplicated page count (the two
 * copies must agree -- that is what makes a six-byte signature safe), then
 * for every page the item chunk's type and the image chunk's type.  A file
 * that reaches the end of that walk is a PaperPort desktop.
 *
 * Each page becomes one member: the raw image object payload, named
 * "<page>.ppobj".
 *
 * RENDERING IS NOT IMPLEMENTED.  A PaperPort image object is a tiled raster
 * whose 1-bpp tiles use a CCITT-style run-length codec and whose 8/24-bpp
 * tiles are JPEG; the reference implementation turns that into a BMP.  This
 * reader stops at the container and hands out the object bytes verbatim,
 * which is exactly what the file holds and is lossless; it does not claim to
 * produce a page image.
 *
 * All 4 corpus samples in F:\ARC\ARC\PAPERPORT parse.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/paperport/xx_paperport.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef PAPERPORT
#define XX_PAPERPORT_FILE_TYPE XX_FILE_TYPE_PAPERPORT
#else
#define XX_PAPERPORT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PAPERPORT_METHOD_STORE 0U
#define XX_PAPERPORT_FILE_HEADER_SIZE 200
#define XX_PAPERPORT_ROOT_POINTER_OFFSET 164
#define XX_PAPERPORT_CHUNK_HEADER_SIZE 32
#define XX_PAPERPORT_ROOT_PREFIX_SIZE 22
#define XX_PAPERPORT_ROOT_ENTRY_SIZE 12
#define XX_PAPERPORT_ITEM_RECORD_SIZE 62
#define XX_PAPERPORT_OBJECT_HEADER_SIZE 0x88
#define XX_PAPERPORT_CHUNK_ROOT 0x8000U
#define XX_PAPERPORT_CHUNK_ITEM 0x4000U
#define XX_PAPERPORT_CHUNK_IMAGE 0x1000U
/* The root payload must be bigger than 0x35 for the count block to fit. */
#define XX_PAPERPORT_MIN_ROOT_PAYLOAD 0x36
/* The page count is a u16, so it is its own ceiling. */
#define XX_PAPERPORT_MAX_PAGES 0xffffU

typedef struct xx_paperport_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_paperport_member;

typedef struct xx_paperport_stream_s {
    xx_paperport_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_paperport_stream;

static void xx_paperport_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_paperport_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_paperport_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_paperport_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_paperport_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_paperport_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_paperport_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_paperport_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_paperport_stream_free(void *pointer) {
    xx_paperport_stream *stream = (xx_paperport_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_paperport_add(xx_paperport_stream *stream,
                          const xx_paperport_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_paperport_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_paperport_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Read one "VZ" chunk header and report its payload size and type.  Every
 * read is bounded against the real file before it happens. */
static bool xx_paperport_chunk(Abstractformat *self, int64_t span,
                               int64_t offset, int64_t *size,
                               uint32_t *type) {
    uint8_t head[XX_PAPERPORT_CHUNK_HEADER_SIZE];
    int64_t payload;

    if (offset < 0 || offset > span ||
        span - offset < XX_PAPERPORT_CHUNK_HEADER_SIZE) {
        return false;
    }
    if (!xx_paperport_read_at(self, self->base_address + offset, head,
                              sizeof(head))) {
        return false;
    }
    if (head[0] != 'V' || head[1] != 'Z') return false;
    payload = (int64_t)(int32_t)xx_paperport_le32(head + 2);
    if (payload < XX_PAPERPORT_CHUNK_HEADER_SIZE) return false;
    *size = payload;
    *type = xx_paperport_le16(head + 26);
    return true;
}

/* Pages are anonymous; they are filed under their one-based page number. */
static char *xx_paperport_page_name(uint32_t page) {
    char text[32];
    size_t length = 0U;
    uint32_t scale = 1U;
    const char *suffix = ".ppobj";
    size_t at = 0U;

    while (page / scale >= 10U && scale <= 100000000U) scale *= 10U;
    for (; scale != 0U; scale /= 10U) {
        text[length++] = (char)('0' + ((page / scale) % 10U));
    }
    while (suffix[at]) text[length++] = suffix[at++];
    text[length] = 0;
    return xx_str_dup(text);
}


/* --------------------------------------------------------------- parse -- */

static xx_paperport_stream *xx_paperport_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_paperport_stream *stream = NULL;
    uint8_t head[XX_PAPERPORT_FILE_HEADER_SIZE];
    uint8_t prefix[XX_PAPERPORT_ROOT_PREFIX_SIZE];
    uint8_t entry[XX_PAPERPORT_ROOT_ENTRY_SIZE];
    uint8_t item[XX_PAPERPORT_ITEM_RECORD_SIZE];
    int64_t total;
    int64_t span;
    int64_t root_offset;
    int64_t root_size = 0;
    int64_t root_base;
    int64_t cursor;
    uint32_t root_type = 0U;
    uint32_t count;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PAPERPORT_FILE_HEADER_SIZE +
                   XX_PAPERPORT_CHUNK_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_paperport_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    if (head[0] != 'V' || head[1] != 'i' || head[2] != 'G' ||
        (head[3] != 'C' && head[3] != 'D' && head[3] != 'E' &&
         head[3] != 'F') ||
        head[5] != 0x1aU) {
        return NULL;
    }

    root_offset =
        (int64_t)(int32_t)xx_paperport_le32(head +
                                            XX_PAPERPORT_ROOT_POINTER_OFFSET);
    if (!xx_paperport_chunk(self, span, root_offset, &root_size, &root_type) ||
        root_type != XX_PAPERPORT_CHUNK_ROOT ||
        root_size < XX_PAPERPORT_MIN_ROOT_PAYLOAD) {
        return NULL;
    }
    root_base = root_offset + XX_PAPERPORT_CHUNK_HEADER_SIZE;
    if (root_base > span || span - root_base < XX_PAPERPORT_ROOT_PREFIX_SIZE) {
        return NULL;
    }
    if (!xx_paperport_read_at(self, self->base_address + root_base, prefix,
                              sizeof(prefix))) {
        return NULL;
    }
    /* The page count is stored twice; the two copies must agree.  That is
     * what makes a six-byte signature safe to detect on. */
    count = xx_paperport_le16(prefix);
    if (count < 1U || count > XX_PAPERPORT_MAX_PAGES ||
        xx_paperport_le16(prefix + 10) != count) {
        return NULL;
    }

    stream = (xx_paperport_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    cursor = root_base + XX_PAPERPORT_ROOT_PREFIX_SIZE;
    for (index = 0U; index < count; ++index) {
        xx_paperport_member member;
        int64_t item_offset;
        int64_t item_size = 0;
        int64_t item_base;
        int64_t image_offset;
        int64_t image_size = 0;
        int64_t object_offset;
        uint32_t item_type = 0U;
        uint32_t image_type = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (cursor > span ||
            span - cursor < XX_PAPERPORT_ROOT_ENTRY_SIZE) {
            goto fail;
        }
        if (!xx_paperport_read_at(self, self->base_address + cursor, entry,
                                  sizeof(entry))) {
            goto fail;
        }
        cursor += XX_PAPERPORT_ROOT_ENTRY_SIZE;

        item_offset = (int64_t)(int32_t)xx_paperport_le32(entry + 4);
        if (!xx_paperport_chunk(self, span, item_offset, &item_size,
                                &item_type) ||
            item_type != XX_PAPERPORT_CHUNK_ITEM) {
            goto fail;
        }
        item_base = item_offset + XX_PAPERPORT_CHUNK_HEADER_SIZE;
        if (item_base > span ||
            span - item_base < XX_PAPERPORT_ITEM_RECORD_SIZE) {
            goto fail;
        }
        if (!xx_paperport_read_at(self, self->base_address + item_base, item,
                                  sizeof(item))) {
            goto fail;
        }

        image_offset = (int64_t)(int32_t)xx_paperport_le32(item + 34);
        if (!xx_paperport_chunk(self, span, image_offset, &image_size,
                                &image_type) ||
            image_type != XX_PAPERPORT_CHUNK_IMAGE ||
            image_size < XX_PAPERPORT_OBJECT_HEADER_SIZE) {
            goto fail;
        }
        object_offset = image_offset + XX_PAPERPORT_CHUNK_HEADER_SIZE;
        /* The object payload is bounded against the real file before it is
         * recorded. */
        if (object_offset > span || image_size > span - object_offset) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_paperport_page_name(index + 1U);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + image_offset;
        member.header_size = XX_PAPERPORT_CHUNK_HEADER_SIZE;
        member.data_offset = self->base_address + object_offset;
        member.packed_size = image_size;
        member.unpacked_size = (uint64_t)image_size;
        member.method = XX_PAPERPORT_METHOD_STORE;
        if (!xx_paperport_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_paperport_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read; the length
 * comes from offsets parse already proved lie inside the file. */
static bool xx_paperport_decode(Abstractformat *self,
                             const xx_paperport_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {{
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->packed_size == 0) return true;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!output) return false;
    if (!xx_paperport_read_at(self, member->data_offset, output,
                           (size_t)member->packed_size)) {{
        xx_mem_free(output);
        return false;
    }}
    *out = output;
    *out_size = (size_t)member->packed_size;
    return true;
}}


/* ---------------------------------------------------------- lifecycle --- */

void xx_paperport_init(xx_paperport *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PAPERPORT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-paperport");
    xx_format_set_extension(&archive->format, "max");
    archive->format.check_is_valid = xx_paperport_check_is_valid;
    archive->format.handle_base_info = xx_paperport_handle_base_info;
    archive->format.get_format_size = xx_paperport_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_paperport_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_paperport_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_paperport_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_paperport_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_paperport_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_paperport_free_archive_records_reading;
    archive->format.destroy = xx_paperport_vtable_destroy;
}

xx_paperport *xx_paperport_create(xx_io_device *device, int64_t base_address) {
    xx_paperport *archive = (xx_paperport *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_paperport_init(archive, device, base_address);
    return archive;
}

void xx_paperport_destroy(xx_paperport *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_paperport_free(xx_paperport *archive) {
    if (!archive) return;
    xx_paperport_destroy(archive);
    xx_mem_free(archive);
}

static void xx_paperport_vtable_destroy(Abstractformat *self) {
    xx_paperport_destroy((xx_paperport *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_paperport_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_paperport_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_paperport_parse(self, pd);
    if (!stream) return false;
    xx_paperport_stream_free(stream);
    return true;
}

bool xx_paperport_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_paperport *archive = (xx_paperport *)self;
    xx_paperport_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_paperport_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_paperport_stream_free(stream);
    return true;
}

int64_t xx_paperport_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_paperport_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_paperport *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_paperport_set_record(xx_archive_record *record,
                                 const xx_paperport_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_paperport_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_paperport_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_paperport_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_paperport_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_paperport_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_paperport_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_paperport_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_paperport_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_paperport_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_paperport_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_paperport_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_paperport_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_paperport_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_paperport_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_paperport_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_paperport_stream *stream;
    const xx_paperport_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_paperport_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_paperport_path_safe(member->name)) return false;

    path_option =
        xx_paperport_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_paperport_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_paperport_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_paperport_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
