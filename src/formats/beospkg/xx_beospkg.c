/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BeOS SoftwareValet installer package (.pkg).  This is the classic BeOS
 * container, not the later Haiku .hpkg -- no sample in the corpus carries the
 * "hpkg" magic, so that dialect is deliberately not claimed here.
 *
 * After the eight-byte signature "AlB" 1A FF 0A 0D 00 the file is a tree of
 * seven-byte tagged records:
 *
 *   +0  4  tag, four ASCII characters
 *   +4  1  always zero
 *   +5  2  payload type, LITTLE endian (the only little-endian field)
 *
 * Payload types: 0x101 is four bytes, 0x102 eight, 0x200 and 0x500 a u32be
 * length followed by that many bytes, 0x300 a twenty-byte descriptor
 * (u64 compressed, u64 original, u32 method) followed by the compressed
 * bytes, and 0x400 opens a nested group that runs to a record whose tag and
 * type are both zero.  Everything else numeric is BIG endian.
 *
 * The tree lives at the offset the top-level "COff" record gives; each "FilI"
 * carries "Name", "OffT" and "OrgS", and at OffT sits a "FiDa" group holding
 * a "FiMF" descriptor whose method 2 means zlib.  The descriptor tag is FiMF,
 * not FiFM.
 *
 * Layout ported from XArchive packages/xbeospackage.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/beospkg/xx_beospkg.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef BEOSPKG
#define XX_BEOSPKG_FILE_TYPE XX_FILE_TYPE_BEOSPKG
#else
#define XX_BEOSPKG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BEOS_TAG_SIZE 7
#define BEOS_CHUNK_DESCRIPTOR_SIZE 20
#define BEOS_MAX_MEMBERS 100000U
#define BEOS_MAX_DEPTH 64
#define BEOS_MAX_STRING 0x400
#define BEOS_MAX_INPUT ((int64_t)256 * 1024 * 1024)
#define BEOS_MAX_OUTPUT ((int64_t)256 * 1024 * 1024)
#define BEOS_METHOD_ZLIB 2U

#define BEOS_TYPE_U32 0x101U
#define BEOS_TYPE_U64 0x102U
#define BEOS_TYPE_STRING 0x200U
#define BEOS_TYPE_CHUNK 0x300U
#define BEOS_TYPE_GROUP 0x400U
#define BEOS_TYPE_BLOB 0x500U

typedef struct beos_member_s {
    char *name;
    int64_t header_offset;
    int64_t stream_offset;
    int64_t compressed_size;
    uint64_t uncompressed_size;
} beos_member;

typedef struct beos_stream_s {
    beos_member *items;
    size_t count;
    size_t index;
    uint8_t *image;
    int64_t image_size;
    int64_t archive_size;
} beos_stream;

typedef struct beos_tag_s {
    uint8_t id[4];
    uint16_t type;
    int64_t payload_offset;
} beos_tag;

static bool beos_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static uint32_t beos_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint64_t beos_be64(const uint8_t *bytes) {
    return ((uint64_t)beos_be32(bytes) << 32U) | (uint64_t)beos_be32(bytes + 4);
}

static bool beos_tag_is(const beos_tag *tag, const char *id) {
    return xx_rt_memcmp(tag->id, id, 4U) == 0;
}

static bool beos_tag_is_terminator(const beos_tag *tag) {
    return tag->type == 0U && tag->id[0] == 0U && tag->id[1] == 0U &&
           tag->id[2] == 0U && tag->id[3] == 0U;
}

static bool beos_read_tag(const uint8_t *image, int64_t size, int64_t offset,
                          beos_tag *tag) {
    if (!tag || !beos_range_within(size, offset, BEOS_TAG_SIZE)) return false;
    if (image[offset + 4] != 0U) return false;
    xx_rt_memcpy(tag->id, image + offset, 4U);
    tag->type = (uint16_t)((uint16_t)image[offset + 5] |
                           ((uint16_t)image[offset + 6] << 8U));
    tag->payload_offset = offset + BEOS_TAG_SIZE;
    return true;
}

static bool beos_skip_payload(const uint8_t *image, int64_t size,
                              int64_t offset, uint16_t type, int64_t *next,
                              int32_t depth) {
    if (!next || depth > BEOS_MAX_DEPTH) return false;
    if (type == BEOS_TYPE_U32) {
        if (!beos_range_within(size, offset, 4)) return false;
        *next = offset + 4;
        return true;
    }
    if (type == BEOS_TYPE_U64) {
        if (!beos_range_within(size, offset, 8)) return false;
        *next = offset + 8;
        return true;
    }
    if (type == BEOS_TYPE_STRING || type == BEOS_TYPE_BLOB) {
        uint32_t length;
        if (!beos_range_within(size, offset, 4)) return false;
        length = beos_be32(image + offset);
        if (length > (uint32_t)INT32_MAX ||
            !beos_range_within(size, offset + 4, (int64_t)length))
            return false;
        *next = offset + 4 + (int64_t)length;
        return true;
    }
    if (type == BEOS_TYPE_CHUNK) {
        uint64_t compressed;
        if (!beos_range_within(size, offset, BEOS_CHUNK_DESCRIPTOR_SIZE))
            return false;
        compressed = beos_be64(image + offset);
        if (compressed > (uint64_t)INT64_MAX ||
            !beos_range_within(size, offset + BEOS_CHUNK_DESCRIPTOR_SIZE,
                               (int64_t)compressed))
            return false;
        *next = offset + BEOS_CHUNK_DESCRIPTOR_SIZE + (int64_t)compressed;
        return true;
    }
    if (type == BEOS_TYPE_GROUP) {
        int64_t cursor = offset;
        uint32_t guard;
        for (guard = 0U; guard < BEOS_MAX_MEMBERS; ++guard) {
            beos_tag tag;
            if (!beos_read_tag(image, size, cursor, &tag)) return false;
            if (beos_tag_is_terminator(&tag)) {
                *next = tag.payload_offset;
                return true;
            }
            if (!beos_skip_payload(image, size, tag.payload_offset, tag.type,
                                   &cursor, depth + 1))
                return false;
        }
        return false;
    }
    return false;
}

/* Reads the fields of one FilI / ScrI / FDat group.  *name is owned by the
 * caller; the remaining outputs stay at -1 when the field is absent. */
static bool beos_read_fields(const uint8_t *image, int64_t size, int64_t offset,
                             char **name, int64_t *data_offset,
                             int64_t *original_size, int64_t *next) {
    int64_t cursor = offset;
    uint32_t guard;
    if (name) *name = NULL;
    if (data_offset) *data_offset = -1;
    if (original_size) *original_size = -1;
    for (guard = 0U; guard < BEOS_MAX_MEMBERS; ++guard) {
        beos_tag tag;
        if (!beos_read_tag(image, size, cursor, &tag)) goto fail;
        if (beos_tag_is_terminator(&tag)) {
            if (next) *next = tag.payload_offset;
            return true;
        }
        if (beos_tag_is(&tag, "Name") && tag.type == BEOS_TYPE_STRING) {
            uint32_t length;
            if (!beos_range_within(size, tag.payload_offset, 4)) goto fail;
            length = beos_be32(image + tag.payload_offset);
            if (length > (uint32_t)BEOS_MAX_STRING ||
                !beos_range_within(size, tag.payload_offset + 4,
                                   (int64_t)length))
                goto fail;
            if (name) {
                char *value = (char *)xx_mem_alloc((size_t)length + 1U);
                if (!value) goto fail;
                if (length != 0U)
                    xx_rt_memcpy(value, image + tag.payload_offset + 4,
                                 (size_t)length);
                value[length] = 0;
                if (*name) xx_str_free(*name);
                *name = value;
            }
            cursor = tag.payload_offset + 4 + (int64_t)length;
            continue;
        }
        if (beos_tag_is(&tag, "OffT") && tag.type == BEOS_TYPE_U64) {
            uint64_t value;
            if (!beos_range_within(size, tag.payload_offset, 8)) goto fail;
            value = beos_be64(image + tag.payload_offset);
            if (value > (uint64_t)INT64_MAX) goto fail;
            if (data_offset) *data_offset = (int64_t)value;
            cursor = tag.payload_offset + 8;
            continue;
        }
        if (beos_tag_is(&tag, "OrgS") && tag.type == BEOS_TYPE_U64) {
            uint64_t value;
            if (!beos_range_within(size, tag.payload_offset, 8)) goto fail;
            value = beos_be64(image + tag.payload_offset);
            if (value > (uint64_t)INT64_MAX) goto fail;
            if (original_size) *original_size = (int64_t)value;
            cursor = tag.payload_offset + 8;
            continue;
        }
        if (!beos_skip_payload(image, size, tag.payload_offset, tag.type,
                               &cursor, 1))
            goto fail;
    }
fail:
    if (name && *name) {
        xx_str_free(*name);
        *name = NULL;
    }
    return false;
}

static bool beos_add_member(beos_stream *stream, const beos_member *member) {
    beos_member *grown;
    if (!stream || !member || stream->count >= BEOS_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (beos_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static char *beos_join(const char *prefix, const char *name) {
    if (!prefix || !prefix[0]) return xx_str_dup(name ? name : "");
    if (!name || !name[0]) return xx_str_dup(prefix);
    return xx_str_concat3(prefix, "/", name);
}

/* Makes a joined package path safe for a host filesystem: separators are
 * normalised, traversal components are removed, bytes a filesystem cannot
 * carry become '_', and trailing spaces or dots are trimmed per component.
 * BeOS allowed names this reader's host does not. */
static char *beos_normalize_name(const char *input) {
    const uint8_t *bytes = (const uint8_t *)input;
    size_t size = input ? xx_str_len(input) : 0U;
    size_t in = 0U, out = 0U;
    char *name;
    if (!input || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (in < size) {
        size_t start, end, component_start;
        while (in < size && (bytes[in] == '/' || bytes[in] == '\\')) ++in;
        start = in;
        while (in < size && bytes[in] != '/' && bytes[in] != '\\') ++in;
        end = in;
        if (end == start || (end - start == 1U && bytes[start] == '.')) continue;
        if (end - start == 2U && bytes[start] == '.' && bytes[start + 1U] == '.') {
            if (out != 0U) {
                while (out != 0U && name[out - 1U] != '/') --out;
                if (out != 0U) --out;
            }
            continue;
        }
        if (out != 0U) name[out++] = '/';
        component_start = out;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[out++] = '_';
            else
                name[out++] = (char)c;
        }
        while (out > component_start &&
               (name[out - 1U] == ' ' || name[out - 1U] == '.'))
            --out;
        if (out == component_start) name[out++] = '_';
    }
    if (out == 0U) name[out++] = '_';
    name[out] = 0;
    return name;
}

static bool beos_walk_folder(const uint8_t *image, int64_t size, int64_t offset,
                             const char *prefix, beos_stream *stream,
                             int64_t *next, int32_t depth) {
    beos_tag tag_data;
    char *folder_name = NULL;
    char *path = NULL;
    int64_t cursor = 0;
    uint32_t guard;
    if (!stream || depth > BEOS_MAX_DEPTH) return false;
    if (!beos_read_tag(image, size, offset, &tag_data)) return false;
    if (!beos_tag_is(&tag_data, "FDat")) return false;
    if (!beos_read_fields(image, size, tag_data.payload_offset, &folder_name,
                          NULL, NULL, &cursor))
        return false;
    path = folder_name && folder_name[0] ? beos_join(prefix, folder_name)
                                         : xx_str_dup(prefix ? prefix : "");
    if (folder_name) xx_str_free(folder_name);
    if (!path) return false;

    for (guard = 0U; guard < BEOS_MAX_MEMBERS; ++guard) {
        beos_tag tag;
        if (!beos_read_tag(image, size, cursor, &tag)) goto fail;
        if (beos_tag_is_terminator(&tag)) {
            if (next) *next = tag.payload_offset;
            xx_str_free(path);
            return true;
        }
        if (beos_tag_is(&tag, "FldI")) {
            if (!beos_walk_folder(image, size, tag.payload_offset, path, stream,
                                  &cursor, depth + 1))
                goto fail;
            continue;
        }
        if (beos_tag_is(&tag, "FilI") || beos_tag_is(&tag, "ScrI")) {
            char *entry = NULL;
            int64_t data_offset = -1;
            int64_t original_size = -1;
            if (!beos_read_fields(image, size, tag.payload_offset, &entry,
                                  &data_offset, &original_size, &cursor)) {
                if (entry) xx_str_free(entry);
                goto fail;
            }
            if (data_offset >= 0 && stream->count < BEOS_MAX_MEMBERS) {
                beos_member member;
                xx_mem_zero(&member, sizeof(member));
                {
                    char *joined = beos_join(path, entry);
                    member.name = joined ? beos_normalize_name(joined) : NULL;
                    if (joined) xx_str_free(joined);
                }
                member.header_offset = data_offset;
                member.stream_offset = -1;
                member.compressed_size = 0;
                member.uncompressed_size =
                    original_size < 0 ? 0U : (uint64_t)original_size;
                if (!member.name || !beos_add_member(stream, &member)) {
                    if (member.name) xx_str_free(member.name);
                    if (entry) xx_str_free(entry);
                    goto fail;
                }
            }
            if (entry) xx_str_free(entry);
            continue;
        }
        if (!beos_skip_payload(image, size, tag.payload_offset, tag.type,
                               &cursor, depth + 1))
            goto fail;
    }
fail:
    xx_str_free(path);
    return false;
}

static void beos_stream_free(void *opaque) {
    beos_stream *stream = (beos_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->image) xx_mem_free(stream->image);
    xx_mem_free(stream);
}

static bool beos_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool beos_parse(Abstractformat *format, beos_stream **result,
                       xx_pd_struct *pd) {
    static const uint8_t signature[8] = {'A', 'l', 'B', 0x1aU,
                                         0xffU, 0x0aU, 0x0dU, 0x00U};
    beos_stream *stream = NULL;
    beos_tag root;
    beos_tag tree;
    int64_t total, size, cursor, tree_offset = -1;
    int64_t after_tree = 0;
    size_t index;
    uint32_t guard;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 16 || size > BEOS_MAX_INPUT) return false;

    stream = (beos_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->image = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!stream->image ||
        !beos_read_at(format->device, format->base_address, stream->image,
                      (size_t)size))
        goto fail;
    stream->image_size = size;
    if (xx_rt_memcmp(stream->image, signature, sizeof(signature)) != 0)
        goto fail;

    if (!beos_read_tag(stream->image, size, 8, &root)) goto fail;
    if (!beos_tag_is(&root, "PhIn") || root.type != BEOS_TYPE_GROUP) goto fail;

    cursor = root.payload_offset;
    for (guard = 0U; guard < BEOS_MAX_MEMBERS; ++guard) {
        beos_tag tag;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!beos_read_tag(stream->image, size, cursor, &tag)) goto fail;
        if (beos_tag_is_terminator(&tag)) goto fail;
        if (beos_tag_is(&tag, "COff")) {
            uint64_t value;
            if (tag.type != BEOS_TYPE_U64 ||
                !beos_range_within(size, tag.payload_offset, 8))
                goto fail;
            value = beos_be64(stream->image + tag.payload_offset);
            if (value > (uint64_t)INT64_MAX) goto fail;
            tree_offset = (int64_t)value;
            break;
        }
        if (!beos_skip_payload(stream->image, size, tag.payload_offset,
                               tag.type, &cursor, 1))
            goto fail;
    }
    if (tree_offset < 0 || tree_offset >= size) goto fail;

    if (!beos_read_tag(stream->image, size, tree_offset, &tree)) goto fail;
    if (!beos_tag_is(&tree, "FldI")) goto fail;
    if (!beos_walk_folder(stream->image, size, tree.payload_offset, "", stream,
                          &after_tree, 0))
        goto fail;
    if (stream->count == 0U) goto fail;

    /* Resolve each member's deflate stream: FiDa group, then the FiMF chunk. */
    for (index = 0U; index < stream->count; ++index) {
        beos_member *member = &stream->items[index];
        beos_tag fida;
        beos_tag chunk;
        const uint8_t *descriptor;
        uint64_t compressed, original;
        uint32_t method;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!beos_read_tag(stream->image, size, member->header_offset, &fida))
            goto fail;
        if (!beos_tag_is(&fida, "FiDa")) goto fail;
        if (!beos_read_tag(stream->image, size, fida.payload_offset, &chunk))
            goto fail;
        if (!beos_tag_is(&chunk, "FiMF") || chunk.type != BEOS_TYPE_CHUNK)
            goto fail;
        if (!beos_range_within(size, chunk.payload_offset,
                               BEOS_CHUNK_DESCRIPTOR_SIZE))
            goto fail;
        descriptor = stream->image + chunk.payload_offset;
        compressed = beos_be64(descriptor);
        original = beos_be64(descriptor + 8);
        method = beos_be32(descriptor + 16);
        if (method != BEOS_METHOD_ZLIB) goto fail;
        if (compressed > (uint64_t)INT64_MAX ||
            original > (uint64_t)BEOS_MAX_OUTPUT)
            goto fail;
        member->stream_offset =
            chunk.payload_offset + BEOS_CHUNK_DESCRIPTOR_SIZE;
        if (!beos_range_within(size, member->stream_offset,
                               (int64_t)compressed))
            goto fail;
        member->compressed_size = (int64_t)compressed;
        member->uncompressed_size = original;
        if (member->stream_offset + (int64_t)compressed > stream->archive_size)
            stream->archive_size = member->stream_offset + (int64_t)compressed;
    }
    if (stream->archive_size < after_tree) stream->archive_size = after_tree;
    if (stream->archive_size > size) stream->archive_size = size;
    *result = stream;
    return true;
fail:
    beos_stream_free(stream);
    return false;
}

static bool beos_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static bool beos_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *beos_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool beos_set_record(xx_archive_record *record,
                            const beos_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = BEOS_TAG_SIZE + BEOS_TAG_SIZE +
                          BEOS_CHUNK_DESCRIPTOR_SIZE;
    record->data_offset = member->stream_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          BEOS_METHOD_ZLIB) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_beospkg_init(xx_beospkg *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BEOSPKG_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-beos-package");
    xx_format_set_extension(&archive->format, "pkg");
    archive->format.check_is_valid = xx_beospkg_check_is_valid;
    archive->format.handle_base_info = xx_beospkg_handle_base_info;
    archive->format.get_format_size = xx_beospkg_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_beospkg_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_beospkg_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_beospkg_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_beospkg_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_beospkg_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_beospkg_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_beospkg *xx_beospkg_create(xx_io_device *device, int64_t base_address) {
    xx_beospkg *archive = (xx_beospkg *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_beospkg_init(archive, device, base_address);
    return archive;
}

void xx_beospkg_destroy(xx_beospkg *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_beospkg_free(xx_beospkg *archive) {
    if (!archive) return;
    xx_beospkg_destroy(archive);
    xx_mem_free(archive);
}

bool xx_beospkg_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    beos_stream *stream;
    if (!beos_parse(format, &stream, pd)) return false;
    beos_stream_free(stream);
    return true;
}

bool xx_beospkg_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    beos_stream *stream;
    xx_beospkg *archive;
    if (!format || !beos_parse(format, &stream, pd)) return false;
    archive = (xx_beospkg *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    beos_stream_free(stream);
    return true;
}

int64_t xx_beospkg_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_beospkg_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_beospkg_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_beospkg_handle_base_info(format, pd))
               ? ((xx_beospkg *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_beospkg_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    beos_stream *stream;
    xx_archive_record_state *state;
    if (!beos_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        beos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = beos_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!beos_copy_options(&state->options, options) ||
        !beos_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_beospkg_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_beospkg_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    beos_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (beos_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        beos_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_beospkg_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    beos_stream *stream;
    beos_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (beos_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!beos_safe_output_name(member->name) ||
        member->uncompressed_size > (uint64_t)BEOS_MAX_OUTPUT)
        return false;
    plain_size = (size_t)member->uncompressed_size;
    plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!plain) goto done;
    {
        size_t produced = 0U;
        if (!xx_zlib_stream_decode_memory(stream->image + member->stream_offset,
                                          (size_t)member->compressed_size,
                                          plain, plain_size, &produced) ||
            produced != plain_size)
            goto done;
    }
    path_option = beos_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
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

void xx_beospkg_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
