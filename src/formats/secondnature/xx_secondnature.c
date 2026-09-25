/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Second Nature Software Inc. screen-saver resource modules
 * (.SNX picture, .REF reference/credits, .BMX splash).
 *
 *   banner, 32 bytes at 0x00:
 *     0x00  "Second Nature Software Inc. ", 28 bytes
 *     0x1c  three-letter kind: "SNX", "REF" or "BMX"
 *     0x1f  NUL terminator
 *
 * In .BMX the banner and directory are stored plainly; in .SNX and .REF the
 * whole header/directory region is stored as its ONE'S COMPLEMENT, so the
 * banner reads AC 9A 9C 90 ... on disk. Member payloads are never
 * complemented, in any kind.
 *
 * All integers are little-endian and every name field is 13 bytes wide, NUL
 * terminated, with stale bytes of a longer previous name left after the
 * terminator ("2080.JIF\0JIF\0" is a real corpus entry).
 *
 *   BMX  0x20  u16 member count
 *        0x22  count entries of 21 bytes: name[13], u32 offset, u32 size
 *
 *   REF  0x20  u16 text-member count
 *        0x22  u16 image-member count
 *        0x24  (text + image) entries of 31 bytes: name[13], u16 kind,
 *              u32 offset, u32 size, u16 rect[4] (screen placement)
 *        The table is padded to six slots on every known sample, so member
 *        data starts at 222; that constant is not assumed here.
 *
 *   SNX  0x20  title[64], 0x60 caption[128], 0xe0 description[128]
 *        0x160 u16 sequence id, u16 width, u16 height
 *        0x166 exactly three 21-byte slots, same shape as BMX. There is no
 *              count field. Slot 2 usually names the companion .REF with a
 *              zero offset and size, which is a reference, not a member.
 *
 * Members are STORED; the format has no compression and no method field.
 * Member offsets are absolute and unordered, so the archive ends at the
 * furthest member extent rather than at the last entry.
 *
 * The 28-byte banner literal - in its plain or complemented form - plus the
 * terminating NUL and the closed set of three kinds is what keeps a stray
 * file from parsing; the directory shape is chosen by the kind and cannot
 * be guessed from the data.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/secondnature/xx_secondnature.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SECONDNATURE_COPY_CHUNK (64 * 1024)

typedef struct xx_secondnature_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_secondnature_member;

typedef struct xx_secondnature_stream_s {
    xx_secondnature_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_secondnature_stream;

static void xx_secondnature_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_secondnature_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_secondnature_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_secondnature_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_secondnature_stream_free(void *pointer) {
    xx_secondnature_stream *stream = (xx_secondnature_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_secondnature_add(xx_secondnature_stream *stream,
                          const xx_secondnature_member *member) {
    xx_secondnature_member *grown = (xx_secondnature_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_secondnature_decode(Abstractformat *self,
                             const xx_secondnature_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_secondnature_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SECONDNATURE_BANNER_SIZE 32
#define XX_SECONDNATURE_BANNER_TEXT_SIZE 28
#define XX_SECONDNATURE_KIND_OFFSET 28
#define XX_SECONDNATURE_NAME_FIELD 13

#define XX_SECONDNATURE_KIND_SNX 1
#define XX_SECONDNATURE_KIND_REF 2
#define XX_SECONDNATURE_KIND_BMX 3

#define XX_SECONDNATURE_COUNT_OFFSET 0x20
#define XX_SECONDNATURE_BMX_DIR_OFFSET 0x22
#define XX_SECONDNATURE_BMX_ENTRY_SIZE 21
#define XX_SECONDNATURE_REF_DIR_OFFSET 0x24
#define XX_SECONDNATURE_REF_ENTRY_SIZE 31
#define XX_SECONDNATURE_SNX_DIR_OFFSET 0x166
#define XX_SECONDNATURE_SNX_ENTRY_SIZE 21
#define XX_SECONDNATURE_SNX_SLOTS 3

#define XX_SECONDNATURE_MAX_MEMBERS 4096
#define XX_SECONDNATURE_REF_MAX_MEMBERS 64
#define XX_SECONDNATURE_MAX_ENTRY_SIZE 31
#define XX_SECONDNATURE_MAX_MEMBER_SIZE 0x10000000

static uint16_t xx_secondnature_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_secondnature_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* .SNX and .REF store the whole header/directory region as its one's
 * complement; member payloads are plain in every kind, which is why the JPEG
 * streams are visible unaltered in a raw dump of a .REF. */
static void xx_secondnature_unmask(uint8_t *data, size_t size,
                                   bool complemented) {
    size_t index;

    if (!complemented) return;
    for (index = 0U; index < size; ++index) {
        data[index] = (uint8_t)(~data[index]);
    }
}

static bool xx_secondnature_name_valid(const uint8_t *name, size_t length) {
    size_t index;

    /* The name must end before the 13th byte: a field filled edge to edge
     * carries no terminator and is misparsed data, not a name. */
    if (length == 0U || length >= (size_t)XX_SECONDNATURE_NAME_FIELD) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t character = name[index];

        if (character < 0x20U || character > 0x7EU) return false;
        /* DOS 8.3 names only - the format has no directories, so rejecting
         * the separators here also makes a path escape unrepresentable. */
        if (character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return false;
        }
    }
    return true;
}

static xx_secondnature_stream *xx_secondnature_parse(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    static const uint8_t banner_text[XX_SECONDNATURE_BANNER_TEXT_SIZE] = {
        'S', 'e', 'c', 'o', 'n', 'd', ' ', 'N', 'a', 't', 'u', 'r', 'e', ' ',
        'S', 'o', 'f', 't', 'w', 'a', 'r', 'e', ' ', 'I', 'n', 'c', '.', ' '};
    static const uint8_t kind_snx[3] = {'S', 'N', 'X'};
    static const uint8_t kind_ref[3] = {'R', 'E', 'F'};
    static const uint8_t kind_bmx[3] = {'B', 'M', 'X'};
    xx_secondnature_stream *stream = NULL;
    uint8_t banner[XX_SECONDNATURE_BANNER_SIZE];
    uint8_t counts[4];
    uint8_t entry[XX_SECONDNATURE_MAX_ENTRY_SIZE];
    bool complemented = false;
    int kind = 0;
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t dir_offset;
    int64_t entry_size;
    int64_t dir_size;
    int64_t dir_end;
    int64_t archive_end;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A banner with nowhere for even one directory entry is not an archive. */
    if (span < XX_SECONDNATURE_BANNER_SIZE + XX_SECONDNATURE_BMX_ENTRY_SIZE) {
        return NULL;
    }
    if (!xx_secondnature_read_at(self, self->base_address, banner,
                                 sizeof(banner))) {
        return NULL;
    }

    /* The banner literal is the format's only real signature. It appears
     * plainly in .BMX and as its one's complement in .SNX/.REF, and which of
     * the two matched also decides how the rest of the header is read - so
     * this test cannot be loosened to a prefix or a case-insensitive match
     * without losing the mask decision with it. */
    if (xx_rt_memcmp(banner, banner_text, sizeof(banner_text)) != 0) {
        xx_secondnature_unmask(banner, sizeof(banner), true);
        if (xx_rt_memcmp(banner, banner_text, sizeof(banner_text)) != 0) {
            return NULL;
        }
        complemented = true;
    }
    /* Exactly 32 bytes: the literal, a three-letter kind and a terminator.
     * The NUL is what stops a longer sentence starting with the literal. */
    if (banner[XX_SECONDNATURE_BANNER_SIZE - 1] != 0U) return NULL;

    if (xx_rt_memcmp(banner + XX_SECONDNATURE_KIND_OFFSET, kind_snx,
                     sizeof(kind_snx)) == 0) {
        kind = XX_SECONDNATURE_KIND_SNX;
    } else if (xx_rt_memcmp(banner + XX_SECONDNATURE_KIND_OFFSET, kind_ref,
                            sizeof(kind_ref)) == 0) {
        kind = XX_SECONDNATURE_KIND_REF;
    } else if (xx_rt_memcmp(banner + XX_SECONDNATURE_KIND_OFFSET, kind_bmx,
                            sizeof(kind_bmx)) == 0) {
        kind = XX_SECONDNATURE_KIND_BMX;
    } else {
        /* The kind set is closed: an unknown kind gives no directory shape,
         * so there is nothing to fall back to. */
        return NULL;
    }

    if (kind == XX_SECONDNATURE_KIND_BMX) {
        if (!xx_secondnature_read_at(
                self, self->base_address + XX_SECONDNATURE_COUNT_OFFSET,
                counts, 2U)) {
            return NULL;
        }
        xx_secondnature_unmask(counts, 2U, complemented);
        count = (int64_t)xx_secondnature_le16(counts);
        if (count < 1 || count > XX_SECONDNATURE_MAX_MEMBERS) return NULL;
        dir_offset = XX_SECONDNATURE_BMX_DIR_OFFSET;
        entry_size = XX_SECONDNATURE_BMX_ENTRY_SIZE;
    } else if (kind == XX_SECONDNATURE_KIND_REF) {
        int64_t text_count;
        int64_t image_count;

        if (!xx_secondnature_read_at(
                self, self->base_address + XX_SECONDNATURE_COUNT_OFFSET,
                counts, 4U)) {
            return NULL;
        }
        xx_secondnature_unmask(counts, 4U, complemented);
        /* Two counts - text members first, then image members - but the
         * entries themselves are one run in that same order, so the sum is
         * the entry count. */
        text_count = (int64_t)xx_secondnature_le16(counts);
        image_count = (int64_t)xx_secondnature_le16(counts + 2);
        if (text_count > XX_SECONDNATURE_REF_MAX_MEMBERS) return NULL;
        if (image_count > XX_SECONDNATURE_REF_MAX_MEMBERS) return NULL;
        count = text_count + image_count;
        if (count < 1 || count > XX_SECONDNATURE_REF_MAX_MEMBERS) return NULL;
        dir_offset = XX_SECONDNATURE_REF_DIR_OFFSET;
        entry_size = XX_SECONDNATURE_REF_ENTRY_SIZE;
    } else {
        /* .SNX carries no count: the table is exactly three slots, and an
         * empty slot is recognised by an empty name or a zero size. */
        count = XX_SECONDNATURE_SNX_SLOTS;
        dir_offset = XX_SECONDNATURE_SNX_DIR_OFFSET;
        entry_size = XX_SECONDNATURE_SNX_ENTRY_SIZE;
    }

    dir_size = count * entry_size;
    if (!xx_secondnature_range_within(span, dir_offset, dir_size)) return NULL;
    dir_end = dir_offset + dir_size;

    stream = (xx_secondnature_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = dir_end;

    for (index = 0; index < count; ++index) {
        xx_secondnature_member member;
        char *name;
        size_t name_length = 0U;
        size_t copy;
        int64_t entry_offset;
        int64_t value_offset;
        int64_t data_offset;
        int64_t data_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = dir_offset + (index * entry_size);
        if (!xx_secondnature_read_at(self, self->base_address + entry_offset,
                                     entry, (size_t)entry_size)) {
            goto fail;
        }
        xx_secondnature_unmask(entry, (size_t)entry_size, complemented);

        while (name_length < (size_t)XX_SECONDNATURE_NAME_FIELD &&
               entry[name_length] != 0U) {
            ++name_length;
        }

        /* .REF puts a u16 member kind between the name and the values; the
         * other two shapes do not. */
        value_offset = (kind == XX_SECONDNATURE_KIND_REF)
                           ? (XX_SECONDNATURE_NAME_FIELD + 2)
                           : XX_SECONDNATURE_NAME_FIELD;
        data_offset = (int64_t)xx_secondnature_le32(entry + value_offset);
        data_size = (int64_t)xx_secondnature_le32(entry + value_offset + 4);

        /* Unused slot: .SNX pads its table to three, and a .REF slot that
         * merely names a companion file carries a zero offset and size. */
        if (name_length == 0U && data_offset == 0 && data_size == 0) continue;
        if (!xx_secondnature_name_valid(entry, name_length)) {
            /* A .SNX slot may name a companion module with no payload; a
             * malformed name anywhere else means this is not our format. */
            if (kind == XX_SECONDNATURE_KIND_SNX && data_size == 0) continue;
            goto fail;
        }
        if (data_size == 0) continue; /* reference-only slot, not a member */
        if (data_size > XX_SECONDNATURE_MAX_MEMBER_SIZE) goto fail;
        /* Offsets are absolute and must land past the directory: a member
         * overlapping its own table is the strongest structural check the
         * directory offers once the banner has matched. */
        if (data_offset < dir_end) goto fail;
        /* The reference implementation tolerates a truncated payload area;
         * this contract does not - an extent past EOF is a rejection. */
        if (!xx_secondnature_range_within(span, data_offset, data_size)) {
            goto fail;
        }

        copy = name_length;
        name = (char *)xx_mem_alloc(copy + 1U);
        if (!name) goto fail;
        for (value_offset = 0; value_offset < (int64_t)copy; ++value_offset) {
            name[value_offset] = (char)entry[value_offset];
        }
        name[copy] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = entry_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_secondnature_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        if (data_offset + data_size > archive_end) {
            archive_end = data_offset + data_size;
        }
    }

    /* A directory of nothing but empty slots is not an archive. */
    if (stream->count == 0U) goto fail;
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_secondnature_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_secondnature_init(xx_secondnature *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SECONDNATURE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-second-nature");
    xx_format_set_extension(&archive->format, "snx");
    archive->format.check_is_valid = xx_secondnature_check_is_valid;
    archive->format.handle_base_info = xx_secondnature_handle_base_info;
    archive->format.get_format_size = xx_secondnature_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_secondnature_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_secondnature_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_secondnature_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_secondnature_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_secondnature_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_secondnature_free_archive_records_reading;
    archive->format.destroy = xx_secondnature_vtable_destroy;
}

xx_secondnature *xx_secondnature_create(xx_io_device *device, int64_t base_address) {
    xx_secondnature *archive = (xx_secondnature *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_secondnature_init(archive, device, base_address);
    return archive;
}

void xx_secondnature_destroy(xx_secondnature *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_secondnature_free(xx_secondnature *archive) {
    if (!archive) return;
    xx_secondnature_destroy(archive);
    xx_mem_free(archive);
}

static void xx_secondnature_vtable_destroy(Abstractformat *self) {
    xx_secondnature_destroy((xx_secondnature *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_secondnature_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_secondnature_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_secondnature_parse(self, pd);
    if (!stream) return false;
    xx_secondnature_stream_free(stream);
    return true;
}

bool xx_secondnature_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_secondnature *archive = (xx_secondnature *)self;
    xx_secondnature_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_secondnature_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_secondnature_stream_free(stream);
    return true;
}

int64_t xx_secondnature_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_secondnature_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_secondnature *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_secondnature_set_record(xx_archive_record *record,
                                 const xx_secondnature_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_secondnature_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
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

static const xx_var *xx_secondnature_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_secondnature_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_secondnature_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_secondnature_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_secondnature_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_secondnature_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_secondnature_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_secondnature_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_secondnature_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_secondnature_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_secondnature_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_secondnature_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_secondnature_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_secondnature_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_secondnature_stream *stream;
    const xx_secondnature_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_secondnature_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_secondnature_path_safe(member->name)) return false;

    path_option = xx_secondnature_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_secondnature_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
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
        !xx_secondnature_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_secondnature_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
