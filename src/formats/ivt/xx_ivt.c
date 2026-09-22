/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MediaView titles (*.ivt).
 *
 * File header, 8 bytes at the base address:
 *
 *   0x00  u32 LE  magic, 0x01045F3F
 *   0x04  i32 LE  directory offset, from the base address
 *
 * The directory is a b-tree whose 48-byte header is:
 *
 *   0x00  u16 LE  magic, 0x293B
 *   0x04  u16 LE  page size, always 0x2000
 *   0x26  u32 LE  total pages
 *   0x2A  u16 LE  level count
 *   0x2C  u32 LE  total entries
 *
 * The pages follow, each exactly 0x2000 bytes. The page whose ordinal equals
 * the level count is the index page and carries no file entries; every other
 * page is a leaf. A leaf opens with a 12-byte node header whose first u16 is
 * the page's unused tail, and then packs entries end to end:
 *
 *   u8       name length, never zero
 *   n bytes  the internal name
 *   LEB128   absolute file offset, from the base address
 *   LEB128   declared stream size
 *   u8       zero
 *
 * The entries plus the recorded unused tail must fill the page to exactly
 * 0x2000, and the leaves together must hold exactly the number of entries the
 * b-tree header declares. Those two identities are what make an 8-byte magic
 * plus a 2-byte one safe to detect.
 *
 * A member's bytes are raw unless they open with a 12-byte MSZIP header:
 *
 *   0x00  "mszp" or "nszp"
 *   0x04  u32 LE  uncompressed size
 *   0x08  u32 LE  reserved
 *
 * after which come MSZIP blocks of [u16 block uncompressed][u16 block
 * compressed]["CK"][raw DEFLATE], where the compressed count includes the
 * "CK" signature and every block inherits the previous 32 KiB of output as
 * its dictionary -- the scheme CAB uses.
 *
 * Entries whose declared size is zero, and the internal files that carry the
 * title's own indexes rather than content, are skipped rather than published:
 * they are not members a caller can extract anything meaningful from.
 *
 * Internal names routinely contain characters no host file system accepts --
 * topic streams are named ">00000001" -- so they are mapped to '_'.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ivt/xx_ivt.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ivt/xx_ivt.h"
#include <stdio.h>

#define XX_IVT_COPY_CHUNK (64 * 1024)

typedef struct xx_ivt_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ivt_member;

typedef struct xx_ivt_stream_s {
    xx_ivt_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ivt_stream;

static void xx_ivt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ivt_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ivt_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ivt_path_safe(const char *name) {
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

static void xx_ivt_stream_free(void *pointer) {
    xx_ivt_stream *stream = (xx_ivt_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ivt_add(xx_ivt_stream *stream,
                          const xx_ivt_member *member) {
    xx_ivt_member *grown = (xx_ivt_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IVT_MAGIC 0x01045F3FU
#define XX_IVT_BTREE_MAGIC 0x293BU
#define XX_IVT_PAGE_SIZE 0x2000
#define XX_IVT_FILE_HEADER_SIZE 8
#define XX_IVT_BTREE_HEADER_SIZE 48
#define XX_IVT_NODE_HEADER_SIZE 12
#define XX_IVT_MSZIP_HEADER_SIZE 12
#define XX_IVT_MAX_MEMBERS 500000
#define XX_IVT_MAX_PAGES 65536
#define XX_IVT_MAX_MEMBER_SIZE 0x40000000
#define XX_IVT_METHOD_STORED 0U
#define XX_IVT_METHOD_MSZIP 1U
#define XX_IVT_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ivt_le16(const uint8_t *data);
static uint32_t xx_ivt_le32(const uint8_t *data);
static bool xx_ivt_is_system_name(const char *name, int64_t length);
static char *xx_ivt_make_name(const uint8_t *raw, int64_t length);
static bool xx_ivt_read_leb128(const uint8_t *page, int64_t *position, int64_t *result);
static xx_ivt_stream *xx_ivt_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ivt_decode(Abstractformat *self, const xx_ivt_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* Internal files that carry the title's own indexes rather than content. */
static const char *const xx_ivt_system_names[] = {
    "AlinkInfo", "AlinkList",  "AlinkLookup",      "GRPINF",
    "KeywordInfo", "KeywordList", "KeywordLookup", "STRINGS",
    "TOCIDX",    "TOPICS",     "TitleInformation", "URLTREE",
    "charmap",   "ftindex",    "source.toc",       "stoplist"};

static uint16_t xx_ivt_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ivt_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_ivt_is_system_name(const char *name, int64_t length) {
    size_t count = sizeof(xx_ivt_system_names) /
                   sizeof(xx_ivt_system_names[0]);
    size_t index;

    for (index = 0U; index < count; ++index) {
        const char *candidate = xx_ivt_system_names[index];
        if (xx_rt_strlen(candidate) == (size_t)length &&
            xx_rt_memcmp(candidate, name, (size_t)length) == 0) {
            return true;
        }
    }
    return false;
}

/* The reference reads names as Latin-1 and accepts every byte. This reader is
 * stricter: the name becomes a path on extraction, and a directory page is
 * otherwise only distinguished from arbitrary bytes by its arithmetic. */
static char *xx_ivt_make_name(const uint8_t *raw, int64_t length) {
    char *name = (char *)xx_mem_alloc((size_t)length + 1U);
    int64_t index;

    if (!name) return NULL;
    for (index = 0; index < length; ++index) {
        uint8_t value = raw[index];
        if (value < 0x20U || value > 0x7EU) {
            xx_str_free(name);
            return NULL;
        }
        /* Topic streams are named ">00000001"; these characters are legal in
         * the container and illegal in a host path. */
        if (value == '<' || value == '>' || value == ':' || value == '"' ||
            value == '/' || value == '\\' || value == '|' || value == '?' ||
            value == '*') {
            value = '_';
        }
        name[index] = (char)value;
    }
    name[length] = '\0';
    return name;
}

/* Two LEB128 values per entry. Both are absolute file quantities that the
 * reference keeps in a signed 32-bit range, so a value that needs more than
 * five groups, or that exceeds INT32_MAX, is a malformed page rather than a
 * large file. */
static bool xx_ivt_read_leb128(const uint8_t *page, int64_t *position,
                               int64_t *result) {
    uint64_t value = 0U;
    int shift = 0;

    for (;;) {
        uint8_t byte;
        if (*position >= XX_IVT_PAGE_SIZE) return false;
        byte = page[*position];
        ++(*position);
        if (shift > 28) return false;
        value += (uint64_t)(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) break;
        shift += 7;
    }
    if (value > (uint64_t)0x7FFFFFFF) return false;
    *result = (int64_t)value;
    return true;
}

static xx_ivt_stream *xx_ivt_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_ivt_stream *stream = NULL;
    uint8_t *page = NULL;
    char *name = NULL;
    uint8_t file_header[XX_IVT_FILE_HEADER_SIZE];
    uint8_t btree[XX_IVT_BTREE_HEADER_SIZE];
    uint8_t mszip[XX_IVT_MSZIP_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t pages_offset;
    int64_t total_pages;
    int64_t total_entries;
    int64_t seen_entries = 0;
    int64_t page_index;
    uint16_t levels;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IVT_FILE_HEADER_SIZE + XX_IVT_BTREE_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_ivt_read_at(self, self->base_address, file_header,
                        sizeof(file_header))) {
        return NULL;
    }
    if (xx_ivt_le32(file_header) != XX_IVT_MAGIC) return NULL;

    directory_offset = (int64_t)(int32_t)xx_ivt_le32(file_header + 4);
    /* The directory cannot start inside the header that announces it, and it
     * must leave room for its own b-tree header. */
    if (directory_offset < XX_IVT_FILE_HEADER_SIZE ||
        directory_offset > span - XX_IVT_BTREE_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_ivt_read_at(self, self->base_address + directory_offset, btree,
                        sizeof(btree))) {
        return NULL;
    }
    /* The second magic, and the page size that the whole page arithmetic
     * below depends on. A file that satisfies both and then fails the entry
     * accounting is malformed, not merely a different format. */
    if (xx_ivt_le16(btree) != (uint16_t)XX_IVT_BTREE_MAGIC) return NULL;
    if (xx_ivt_le16(btree + 4) != (uint16_t)XX_IVT_PAGE_SIZE) return NULL;

    total_pages = (int64_t)(int32_t)xx_ivt_le32(btree + 0x26);
    levels = xx_ivt_le16(btree + 0x2A);
    total_entries = (int64_t)(int32_t)xx_ivt_le32(btree + 0x2C);
    if (total_pages <= 0 || total_pages > XX_IVT_MAX_PAGES) return NULL;
    if (total_entries <= 0 || total_entries > XX_IVT_MAX_MEMBERS) return NULL;

    pages_offset = directory_offset + XX_IVT_BTREE_HEADER_SIZE;
    if (!xx_ivt_range_within(span, pages_offset,
                             total_pages * XX_IVT_PAGE_SIZE)) {
        return NULL;
    }

    stream = (xx_ivt_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    page = (uint8_t *)xx_mem_alloc((size_t)XX_IVT_PAGE_SIZE);
    if (!page) goto fail;

    for (page_index = 0; page_index < total_pages; ++page_index) {
        int64_t position;
        int64_t unused;
        int64_t entry_count;
        int64_t entry;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The page whose ordinal equals the level count is the index page: it
         * holds no file entries, and its layout is not the leaf layout. */
        if (page_index == (int64_t)levels) continue;

        if (!xx_ivt_read_at(self,
                            self->base_address + pages_offset +
                                page_index * XX_IVT_PAGE_SIZE,
                            page, (size_t)XX_IVT_PAGE_SIZE)) {
            goto fail;
        }
        unused = (int64_t)xx_ivt_le16(page);
        entry_count = (int64_t)xx_ivt_le16(page + 2);
        if (unused > XX_IVT_PAGE_SIZE - XX_IVT_NODE_HEADER_SIZE) goto fail;
        if (seen_entries + entry_count > total_entries) goto fail;

        position = XX_IVT_NODE_HEADER_SIZE;
        for (entry = 0; entry < entry_count; ++entry) {
            xx_ivt_member member;
            int64_t name_length;
            int64_t data_offset;
            int64_t stream_size;
            int64_t uncompressed_size;
            uint32_t method;

            if (pd && xx_pd_is_stopped(pd)) goto fail;
            if (position >= XX_IVT_PAGE_SIZE) goto fail;
            name_length = (int64_t)page[position];
            ++position;
            if (name_length == 0) goto fail;
            if (position + name_length > XX_IVT_PAGE_SIZE) goto fail;

            name = xx_ivt_make_name(page + position, name_length);
            if (!name) goto fail;
            position += name_length;

            if (!xx_ivt_read_leb128(page, &position, &data_offset) ||
                !xx_ivt_read_leb128(page, &position, &stream_size)) {
                goto fail;
            }
            /* Every entry ends with a zero byte; it is the only marker the
             * leaf layout has that the two LEB128 runs were read correctly. */
            if (position >= XX_IVT_PAGE_SIZE) goto fail;
            if (page[position] != 0U) goto fail;
            ++position;

            ++seen_entries;

            /* A zero-size entry names nothing, and the title's own index
             * files are not extractable content; both are skipped, and both
             * still count towards the entry total. */
            if (stream_size == 0 ||
                xx_ivt_is_system_name(name, name_length)) {
                xx_str_free(name);
                name = NULL;
                continue;
            }
            if (stream_size < 0 || stream_size > XX_IVT_MAX_MEMBER_SIZE) {
                goto fail;
            }
            if (!xx_ivt_range_within(span, data_offset, stream_size)) {
                goto fail;
            }
            if (stream->count >= (size_t)XX_IVT_MAX_MEMBERS) goto fail;

            method = XX_IVT_METHOD_STORED;
            uncompressed_size = stream_size;
            if (stream_size >= XX_IVT_MSZIP_HEADER_SIZE) {
                size_t declared = 0U;
                if (!xx_ivt_read_at(self, self->base_address + data_offset,
                                    mszip, sizeof(mszip))) {
                    goto fail;
                }
                /* The shared header check, not an open-coded magic test: a
                 * member is compressed exactly when the library agrees it has
                 * a well-formed "mszp"/"nszp" header, so the size published
                 * here and the size the decode demands cannot drift apart. */
                if (xx_ivt_declared_size(mszip, sizeof(mszip), &declared)) {
                    if ((int64_t)declared > XX_IVT_MAX_MEMBER_SIZE) goto fail;
                    method = XX_IVT_METHOD_MSZIP;
                    uncompressed_size = (int64_t)declared;
                }
            }

            xx_mem_zero(&member, sizeof(member));
            member.name = name;
            /* The entry lives inside a directory page, so its coordinate is a
             * description of where it sits, not something to seek to. */
            member.header_offset = self->base_address + pages_offset +
                                   page_index * XX_IVT_PAGE_SIZE;
            member.header_size = XX_IVT_PAGE_SIZE;
            member.data_offset = self->base_address + data_offset;
            member.compressed_size = stream_size;
            member.uncompressed_size = uncompressed_size;
            member.method = method;
            /* The directory carries no timestamp for an internal file. */
            member.timestamp = 0U;
            member.is_folder = false;

            if (!xx_ivt_add(stream, &member)) goto fail;
            name = NULL;
        }

        /* The entries plus the page's own recorded free space must fill the
         * page exactly. Together with the entry total below, this is what a
         * random file with the two magics would have to satisfy, and it is
         * the check a later reader will be tempted to drop. */
        if (position + unused != XX_IVT_PAGE_SIZE) goto fail;
    }

    if (seen_entries != total_entries) goto fail;
    /* A directory of nothing but system files and empty entries describes no
     * extractable content. */
    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    xx_mem_free(page);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(page);
    xx_str_free(name);
    xx_ivt_stream_free(stream);
    return NULL;
}


/* The MSZIP header's declared length is attacker-controlled, so it is capped
 * before it becomes an allocation. */

static bool xx_ivt_decode(Abstractformat *self, const xx_ivt_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > (int64_t)XX_IVT_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > (int64_t)XX_IVT_MAX_DECODED) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0 &&
        !xx_ivt_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_IVT_METHOD_STORED) {
        /* A raw internal file: parse published both sizes as the stream
         * length, so a disagreement here would be a short member reported as
         * whole. */
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = (size_t)member->compressed_size;
        return true;
    }
    if (member->method != XX_IVT_METHOD_MSZIP) {
        /* Parse publishes no other method; treating one as stored would hand
         * back MSZIP framing bytes as content. */
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0
                                        ? (size_t)member->uncompressed_size
                                        : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The entry point takes the member INCLUDING its 12-byte header, re-reads
     * the declared size from it and succeeds only when the MSZIP blocks sum
     * to exactly that number, dictionary carried across block boundaries. */
    if (!xx_ivt_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ivt_init(xx_ivt *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IVT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mediaview");
    xx_format_set_extension(&archive->format, "ivt");
    archive->format.check_is_valid = xx_ivt_check_is_valid;
    archive->format.handle_base_info = xx_ivt_handle_base_info;
    archive->format.get_format_size = xx_ivt_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ivt_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ivt_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ivt_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ivt_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ivt_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ivt_free_archive_records_reading;
    archive->format.destroy = xx_ivt_vtable_destroy;
}

xx_ivt *xx_ivt_create(xx_io_device *device, int64_t base_address) {
    xx_ivt *archive = (xx_ivt *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ivt_init(archive, device, base_address);
    return archive;
}

void xx_ivt_destroy(xx_ivt *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ivt_free(xx_ivt *archive) {
    if (!archive) return;
    xx_ivt_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ivt_vtable_destroy(Abstractformat *self) {
    xx_ivt_destroy((xx_ivt *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ivt_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ivt_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ivt_parse(self, pd);
    if (!stream) return false;
    xx_ivt_stream_free(stream);
    return true;
}

bool xx_ivt_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ivt *archive = (xx_ivt *)self;
    xx_ivt_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ivt_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ivt_stream_free(stream);
    return true;
}

int64_t xx_ivt_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ivt_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ivt *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ivt_set_record(xx_archive_record *record,
                                 const xx_ivt_member *member) {
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

static bool xx_ivt_copy_options(xx_list_s *target,
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

static const xx_var *xx_ivt_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ivt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ivt_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ivt_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ivt_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ivt_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ivt_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ivt_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ivt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ivt_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ivt_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ivt_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ivt_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ivt_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ivt_stream *stream;
    const xx_ivt_member *member;
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
    stream = (xx_ivt_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ivt_path_safe(member->name)) return false;

    path_option = xx_ivt_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ivt_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ivt_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ivt_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
