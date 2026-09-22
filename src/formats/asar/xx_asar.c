/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ASAR, Electron's archive format.
 *
 *   Chromium Pickle header, 16 bytes, all u32 LE:
 *     0   4                       payload size of the first pickle
 *     4   header_size             payload size of the second pickle
 *     8   4 + align4(json_size)   the pickled string's own length field
 *     12  json_size               bytes of JSON that follow
 *
 *   JSON directory at offset 16, then the blob at 8 + header_size.
 *
 * The three size fields are redundant by construction -- a Pickle string is a
 * four-byte length followed by bytes padded to a four-byte boundary -- and
 * checking that they agree is most of what makes an ASAR file identifiable.
 * There is no magic number: byte 0 is simply the number 4.
 *
 * The directory is a tree of objects. An entry with "files" is a folder, one
 * with "link" is a symlink, and anything else is a file located by "offset"
 * and "size". Nothing is compressed; extraction is a byte-range copy.
 *
 * Two details that look like mistakes but are not:
 *
 * "offset" is a decimal STRING, not a number. JSON numbers are doubles, and
 * an archive can exceed 2^53 bytes, so Electron writes the offset as text.
 * Parsing it as a number would silently round on large archives.
 *
 * An entry marked "unpacked" has no bytes in the archive at all: they live in
 * a sibling "<name>.unpacked" directory. Such entries are still enumerated --
 * omitting them would misreport the tree -- but cannot be extracted from an
 * xx_io_device, which has no path of its own to resolve the sibling against.
 * The flag is inherited by everything below a folder that carries it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/asar/xx_asar.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/json/xx_json.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_ASAR_HEADER_SIZE 16
#define XX_ASAR_MAX_JSON_SIZE ((int64_t)16 * 1024 * 1024)
/* The largest integer a JSON double represents exactly. */
#define XX_ASAR_MAX_FILE_SIZE ((int64_t)9007199254740991)
#define XX_ASAR_MAX_TREE_DEPTH 256
#define XX_ASAR_MAX_RECORDS 1000000
#define XX_ASAR_COPY_CHUNK (64 * 1024)

typedef struct xx_asar_member_s {
    char *name;
    char *link_target; /* NULL unless this is a symlink */
    int64_t offset;    /* absolute, or 0 for folders/links/external */
    int64_t size;
    bool is_folder;
    bool is_link;
    bool is_external; /* "unpacked": bytes are outside the archive */
} xx_asar_member;

typedef struct xx_asar_stream_s {
    xx_asar_member *items;
    size_t count;
    size_t index;
    int64_t json_offset;
    int64_t json_size;
    int64_t blob_offset;
    int64_t archive_size;
} xx_asar_stream;

/* ------------------------------------------------------------ tree walk -- */

static bool xx_asar_component_ok(const char *component) {
    if (!component || !component[0]) return false;
    if (xx_str_cmp(component, ".") == 0 || xx_str_cmp(component, "..") == 0) {
        return false;
    }
    return !xx_str_chr(component, '/') && !xx_str_chr(component, '\\');
}

/*
 * Normalise a symlink target the way the reference does: backslashes become
 * slashes, "." segments drop out, ".." pops, and anything absolute or escaping
 * the archive root is refused.
 */
static char *xx_asar_normalize_link(const char *target) {
    char *work;
    char *result;
    size_t length;
    size_t out_length = 0U;
    size_t i;

    if (!target || !target[0]) return NULL;
    length = xx_str_len(target);
    work = (char *)xx_mem_alloc(length + 1U);
    result = (char *)xx_mem_alloc(length + 1U);
    if (!work || !result) {
        xx_mem_free(work);
        xx_mem_free(result);
        return NULL;
    }
    for (i = 0U; i < length; ++i) {
        work[i] = target[i] == '\\' ? '/' : target[i];
    }
    work[length] = '\0';
    /* Absolute, or a drive letter: outside the archive either way. */
    if (work[0] == '/' ||
        (length >= 2U && work[1] == ':' &&
         ((work[0] >= 'A' && work[0] <= 'Z') ||
          (work[0] >= 'a' && work[0] <= 'z')))) {
        goto fail;
    }
    result[0] = '\0';
    {
        size_t cursor = 0U;
        while (cursor < length) {
            size_t end = cursor;
            size_t part_length;
            while (end < length && work[end] != '/') ++end;
            part_length = end - cursor;
            if (part_length == 0U) goto fail; /* empty segment */
            if (part_length == 1U && work[cursor] == '.') {
                /* drop */
            } else if (part_length == 2U && work[cursor] == '.' &&
                       work[cursor + 1U] == '.') {
                char *last;
                if (out_length == 0U) goto fail; /* escapes the root */
                result[out_length] = '\0';
                last = xx_str_rchr(result, '/');
                out_length = last ? (size_t)(last - result) : 0U;
            } else {
                size_t j;
                for (j = 0U; j < part_length; ++j) {
                    char c = work[cursor + j];
                    if (c == '\0') goto fail;
                }
                if (out_length != 0U) result[out_length++] = '/';
                for (j = 0U; j < part_length; ++j) {
                    result[out_length++] = work[cursor + j];
                }
            }
            cursor = end < length ? end + 1U : end;
        }
    }
    if (out_length == 0U) goto fail;
    result[out_length] = '\0';
    xx_mem_free(work);
    return result;

fail:
    xx_mem_free(work);
    xx_mem_free(result);
    return NULL;
}

typedef struct xx_asar_walk_s {
    xx_asar_member *items;
    size_t count;
    int64_t blob_offset;
    int64_t file_size;
    int64_t max_end; /* furthest byte any member reaches */
} xx_asar_walk;

static bool xx_asar_append(xx_asar_walk *walk, const xx_asar_member *member) {
    xx_asar_member *grown;

    if (walk->count >= XX_ASAR_MAX_RECORDS) return false;
    grown = (xx_asar_member *)xx_mem_realloc(
        walk->items, sizeof(*walk->items) * (walk->count + 1U));
    if (!grown) return false;
    walk->items = grown;
    walk->items[walk->count++] = *member;
    return true;
}

/*
 * Validate an "integrity" object: SHA256, a hash, a positive block size, and
 * exactly the number of block hashes the file size implies. The block count is
 * the part worth checking -- it ties the integrity data to the declared size,
 * so the two cannot drift apart unnoticed.
 */
static bool xx_asar_hex64(const char *text) {
    size_t i;

    if (!text) return false;
    for (i = 0U; i < 64U; ++i) {
        char c = text[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return text[64] == '\0';
}

static bool xx_asar_check_integrity(xx_json *json, int64_t file_size) {
    bool has_algorithm = false;
    bool has_hash = false;
    bool has_block_size = false;
    bool has_blocks = false;
    int64_t block_size = 0;
    int64_t blocks = 0;

    if (!xx_json_object_begin(json)) return false;
    if (xx_json_object_empty(json)) return false; /* all four keys required */
    for (;;) {
        char *key = NULL;
        bool ok = true;

        if (!xx_json_object_key(json, &key)) return false;
        if (xx_str_cmp(key, "algorithm") == 0) {
            char *value = NULL;
            ok = xx_json_string(json, &value) &&
                 xx_str_cmp(value, "SHA256") == 0;
            xx_str_free(value);
            has_algorithm = ok;
        } else if (xx_str_cmp(key, "hash") == 0) {
            char *value = NULL;
            ok = xx_json_string(json, &value) && xx_asar_hex64(value);
            xx_str_free(value);
            has_hash = ok;
        } else if (xx_str_cmp(key, "blockSize") == 0) {
            ok = xx_json_integer(json, XX_ASAR_MAX_FILE_SIZE, &block_size) &&
                 block_size > 0;
            has_block_size = ok;
        } else if (xx_str_cmp(key, "blocks") == 0) {
            ok = xx_json_array_begin(json);
            if (ok && !xx_json_array_empty(json)) {
                for (;;) {
                    char *value = NULL;
                    ok = xx_json_string(json, &value) && xx_asar_hex64(value);
                    xx_str_free(value);
                    if (!ok) break;
                    ++blocks;
                    if (xx_json_more(json)) continue;
                    break;
                }
            }
            ok = ok && xx_json_array_end(json);
            has_blocks = ok;
        } else {
            ok = xx_json_skip(json);
        }
        xx_str_free(key);
        if (!ok) return false;
        if (xx_json_more(json)) continue;
        if (!xx_json_object_end(json)) return false;
        break;
    }
    if (!has_algorithm || !has_hash || !has_block_size || !has_blocks) {
        return false;
    }
    return blocks ==
           (file_size == 0 ? 1 : ((file_size - 1) / block_size) + 1);
}

static bool xx_asar_walk_files(xx_json *json, xx_asar_walk *walk,
                               const char *parent, int depth,
                               bool parent_unpacked, xx_pd_struct *pd);

/*
 * One directory entry. Which keys it carries decides what it is, and the keys
 * may arrive in any order -- so "files" and "integrity" are located, skipped,
 * and only revisited once the whole object has been read and the entry's kind
 * and size are known. A cursor is a plain struct, so remembering a position is
 * just a copy.
 */
static bool xx_asar_walk_entry(xx_json *json, xx_asar_walk *walk,
                               const char *path, int depth,
                               bool parent_unpacked, xx_pd_struct *pd) {
    bool has_files = false;
    bool has_link = false;
    bool has_offset = false;
    bool has_size = false;
    bool has_integrity = false;
    bool has_executable = false;
    bool own_unpacked = false;
    bool unpacked;
    char *link_target = NULL;
    int64_t size = 0;
    int64_t offset = 0;
    xx_json files_cursor;
    xx_json integrity_cursor;
    xx_asar_member member;

    xx_mem_zero(&files_cursor, sizeof(files_cursor));
    xx_mem_zero(&integrity_cursor, sizeof(integrity_cursor));

    if (!xx_json_object_begin(json)) return false;
    if (!xx_json_object_empty(json)) {
        for (;;) {
            char *key = NULL;
            bool ok = true;

            if (!xx_json_object_key(json, &key)) {
                xx_str_free(link_target);
                return false;
            }
            if (xx_str_cmp(key, "files") == 0) {
                has_files = true;
                files_cursor = *json;
                ok = xx_json_peek(json) == XX_JSON_TYPE_OBJECT &&
                     xx_json_skip(json);
            } else if (xx_str_cmp(key, "link") == 0) {
                char *raw = NULL;
                has_link = true;
                ok = xx_json_string(json, &raw);
                if (ok) {
                    link_target = xx_asar_normalize_link(raw);
                    ok = link_target != NULL;
                }
                xx_str_free(raw);
            } else if (xx_str_cmp(key, "size") == 0) {
                ok = xx_json_integer(json, XX_ASAR_MAX_FILE_SIZE, &size);
                has_size = ok;
            } else if (xx_str_cmp(key, "offset") == 0) {
                /* A decimal STRING, not a number: see the file comment. */
                char *raw = NULL;
                has_offset = true;
                ok = xx_json_string(json, &raw) && raw[0] != 0;
                if (ok) {
                    size_t i;
                    offset = 0;
                    for (i = 0U; raw[i]; ++i) {
                        if (raw[i] < '0' || raw[i] > '9') {
                            ok = false;
                            break;
                        }
                        if (offset >
                            (XX_ASAR_MAX_FILE_SIZE - (raw[i] - '0')) / 10) {
                            ok = false;
                            break;
                        }
                        offset = offset * 10 + (raw[i] - '0');
                    }
                }
                xx_str_free(raw);
            } else if (xx_str_cmp(key, "integrity") == 0) {
                has_integrity = true;
                integrity_cursor = *json;
                ok = xx_json_peek(json) == XX_JSON_TYPE_OBJECT &&
                     xx_json_skip(json);
            } else if (xx_str_cmp(key, "executable") == 0) {
                has_executable = true;
                ok = xx_json_bool(json, NULL);
            } else if (xx_str_cmp(key, "unpacked") == 0) {
                ok = xx_json_bool(json, &own_unpacked);
            } else {
                ok = xx_json_skip(json);
            }
            xx_str_free(key);
            if (!ok) {
                xx_str_free(link_target);
                return false;
            }
            if (xx_json_more(json)) continue;
            break;
        }
    }
    if (!xx_json_object_end(json)) {
        xx_str_free(link_target);
        return false;
    }

    unpacked = parent_unpacked || own_unpacked;
    xx_mem_zero(&member, sizeof(member));

    if (has_files) {
        /* A folder carries nothing else. */
        if (has_link || has_offset || has_size || has_integrity ||
            has_executable) {
            xx_str_free(link_target);
            return false;
        }
        member.name = xx_str_dup(path);
        member.is_folder = true;
        if (!member.name || !xx_asar_append(walk, &member)) {
            xx_str_free(member.name);
            return false;
        }
        /* The folder record precedes its children, so walk it now. */
        return xx_asar_walk_files(&files_cursor, walk, path, depth + 1,
                                  unpacked, pd);
    }
    if (has_link) {
        if (has_offset || has_size || has_integrity || has_executable) {
            xx_str_free(link_target);
            return false;
        }
        member.name = xx_str_dup(path);
        member.link_target = link_target;
        member.is_link = true;
        if (!member.name || !xx_asar_append(walk, &member)) {
            xx_str_free(member.name);
            xx_str_free(link_target);
            return false;
        }
        return true;
    }

    xx_str_free(link_target);
    /* A file. "size" is mandatory, and "offset" is present exactly when the
     * bytes live inside the archive rather than in a sibling directory. */
    if (!has_size || size < 0 || size > XX_ASAR_MAX_FILE_SIZE ||
        (unpacked ? has_offset : !has_offset)) {
        return false;
    }
    /* Now the size is known, so the integrity block's own block count can be
     * checked against it. */
    if (has_integrity && !xx_asar_check_integrity(&integrity_cursor, size)) {
        return false;
    }
    member.name = xx_str_dup(path);
    if (!member.name) return false;
    member.size = size;
    member.is_external = unpacked;
    if (!unpacked) {
        if (offset > XX_ASAR_MAX_FILE_SIZE - walk->blob_offset) {
            xx_str_free(member.name);
            return false;
        }
        member.offset = walk->blob_offset + offset;
        if (member.offset < walk->blob_offset ||
            member.offset > walk->file_size - size) {
            xx_str_free(member.name);
            return false;
        }
        if (member.offset + size > walk->max_end) {
            walk->max_end = member.offset + size;
        }
    }
    if (!xx_asar_append(walk, &member)) {
        xx_str_free(member.name);
        return false;
    }
    return true;
}

static bool xx_asar_walk_files(xx_json *json, xx_asar_walk *walk,
                               const char *parent, int depth,
                               bool parent_unpacked, xx_pd_struct *pd) {
    if (depth < 0 || depth > XX_ASAR_MAX_TREE_DEPTH) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_json_object_begin(json)) return false;
    if (xx_json_object_empty(json)) return xx_json_object_end(json);

    for (;;) {
        char *key = NULL;
        char *path = NULL;
        bool ok;

        if (!xx_json_object_key(json, &key)) return false;
        if (!xx_asar_component_ok(key)) {
            xx_str_free(key);
            return false;
        }
        path = (parent && parent[0]) ? xx_str_concat3(parent, "/", key)
                                     : xx_str_dup(key);
        xx_str_free(key);
        if (!path) return false;
        ok = xx_asar_walk_entry(json, walk, path, depth, parent_unpacked, pd);
        xx_str_free(path);
        if (!ok) return false;

        if (xx_json_more(json)) continue;
        return xx_json_object_end(json);
    }
}

/* ------------------------------------------------------------- parsing -- */

static bool xx_asar_read_at(Abstractformat *self, int64_t offset,
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

static uint32_t xx_asar_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void xx_asar_stream_free(void *pointer) {
    xx_asar_stream *stream = (xx_asar_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
        xx_str_free(stream->items[index].link_target);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static xx_asar_stream *xx_asar_parse(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t header[XX_ASAR_HEADER_SIZE];
    uint8_t *json_bytes = NULL;
    xx_asar_stream *stream = NULL;
    xx_asar_walk walk;
    xx_json json;
    uint32_t field0;
    uint32_t header_size;
    uint32_t json_string_size;
    uint32_t json_size;
    uint64_t aligned;
    uint64_t expected_string;
    uint64_t expected_header;
    int64_t file_size;
    int64_t span;
    int64_t blob_offset;
    bool found_files = false;

    xx_mem_zero(&walk, sizeof(walk));
    if (!self || !self->device || self->base_address < 0) return NULL;
    file_size = xx_io_total_size(self->device);
    if (file_size < self->base_address) return NULL;
    span = file_size - self->base_address;
    if (span < XX_ASAR_HEADER_SIZE) return NULL;
    if (!xx_asar_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    field0 = xx_asar_u32(header);
    header_size = xx_asar_u32(header + 4);
    json_string_size = xx_asar_u32(header + 8);
    json_size = xx_asar_u32(header + 12);

    /* The first pickle always encodes a single uint32. */
    if (field0 != 4U) return NULL;
    if (json_size == 0U || (int64_t)json_size > XX_ASAR_MAX_JSON_SIZE) {
        return NULL;
    }
    /* A pickled string is a four-byte length plus bytes padded to four. The
     * three size fields must agree, which is what identifies the format. */
    aligned = ((uint64_t)json_size + 3U) & ~(uint64_t)3U;
    expected_string = 4U + aligned;
    expected_header = 4U + expected_string;
    if ((uint64_t)json_string_size != expected_string ||
        (uint64_t)header_size != expected_header) {
        return NULL;
    }
    blob_offset = 8 + (int64_t)header_size;
    if (XX_ASAR_HEADER_SIZE + (int64_t)json_size > blob_offset ||
        blob_offset > span) {
        return NULL;
    }

    json_bytes = (uint8_t *)xx_mem_alloc(json_size);
    if (!json_bytes ||
        !xx_asar_read_at(self, self->base_address + XX_ASAR_HEADER_SIZE,
                         json_bytes, json_size)) {
        xx_mem_free(json_bytes);
        return NULL;
    }

    /* Through the initialiser, not by assigning fields: the cursor carries
     * a nesting depth as well, and leaving it uninitialised makes the depth
     * guard fire on whatever happened to be on the stack. */
    xx_json_init(&json, json_bytes, json_size);
    walk.blob_offset = self->base_address + blob_offset;
    walk.file_size = file_size;
    walk.max_end = self->base_address + blob_offset;

    /* The document is an object carrying a "files" object. */
    if (!xx_json_object_begin(&json) || xx_json_object_empty(&json)) goto fail;
    for (;;) {
        char *key = NULL;
        bool ok;

        if (!xx_json_object_key(&json, &key)) goto fail;
        if (xx_str_cmp(key, "files") == 0) {
            found_files = true;
            ok = xx_asar_walk_files(&json, &walk, NULL, 0, false, pd);
        } else {
            ok = xx_json_skip(&json);
        }
        xx_str_free(key);
        if (!ok) goto fail;
        if (xx_json_more(&json)) continue;
        if (!xx_json_object_end(&json)) goto fail;
        break;
    }
    if (!found_files) goto fail;

    stream = (xx_asar_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = walk.items;
    stream->count = walk.count;
    stream->index = 0U;
    stream->json_offset = XX_ASAR_HEADER_SIZE;
    stream->json_size = json_size;
    stream->blob_offset = blob_offset;
    stream->archive_size = walk.max_end - self->base_address;
    if (stream->archive_size < blob_offset) stream->archive_size = blob_offset;
    xx_mem_free(json_bytes);
    return stream;

fail:
    {
        size_t index;
        for (index = 0U; index < walk.count; ++index) {
            xx_str_free(walk.items[index].name);
            xx_str_free(walk.items[index].link_target);
        }
        xx_mem_free(walk.items);
    }
    xx_mem_free(json_bytes);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

static void xx_asar_vtable_destroy(Abstractformat *self);

void xx_asar_init(xx_asar *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ASAR;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-asar");
    xx_format_set_extension(&archive->format, "asar");
    archive->format.check_is_valid = xx_asar_check_is_valid;
    archive->format.handle_base_info = xx_asar_handle_base_info;
    archive->format.get_format_size = xx_asar_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_asar_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_asar_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_asar_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_asar_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_asar_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_asar_free_archive_records_reading;
    archive->format.destroy = xx_asar_vtable_destroy;
}

xx_asar *xx_asar_create(xx_io_device *device, int64_t base_address) {
    xx_asar *archive = (xx_asar *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_asar_init(archive, device, base_address);
    return archive;
}

void xx_asar_destroy(xx_asar *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_asar_free(xx_asar *archive) {
    if (!archive) return;
    xx_asar_destroy(archive);
    xx_mem_free(archive);
}

static void xx_asar_vtable_destroy(Abstractformat *self) {
    xx_asar_destroy((xx_asar *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_asar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_asar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_asar_parse(self, pd);
    if (!stream) return false;
    xx_asar_stream_free(stream);
    return true;
}

bool xx_asar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_asar *archive = (xx_asar *)self;
    xx_asar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_asar_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->json_offset = stream->json_offset;
    archive->json_size = stream->json_size;
    archive->blob_offset = stream->blob_offset;
    xx_asar_stream_free(stream);
    return true;
}

int64_t xx_asar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_asar_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_asar *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_asar_set_record(xx_archive_record *record,
                               const xx_asar_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = 0;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           (!member->is_link ||
            xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                           member->link_target));
}

static bool xx_asar_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_asar_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_asar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_asar_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_asar_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_asar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_asar_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_asar_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_asar_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_asar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_asar_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_asar_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_asar_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_asar_set_record(&state->current_record,
                                           &stream->items[stream->index]);
    return state->has_record;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_asar_path_safe(const char *name) {
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

bool xx_asar_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_asar_stream *stream;
    const xx_asar_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_asar_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_asar_path_safe(member->name)) return false;

    /* An unpacked entry's bytes are in a sibling "<name>.unpacked" directory,
     * which a device with no path of its own cannot resolve. Refusing is
     * honest; inventing a location would not be. */
    if (member->is_external) return false;

    path_option = xx_asar_get_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: confirm the extent is readable, which is what
         * verifies a member without writing it. */
        uint8_t *buffer;
        int64_t position;
        int64_t end;

        if (member->is_folder) return true;
        if (member->is_link) return false; /* nothing to read */
        buffer = (uint8_t *)xx_mem_alloc(XX_ASAR_COPY_CHUNK);
        if (!buffer) return false;
        position = member->offset;
        end = member->offset + member->size;
        result = true;
        while (result && position < end) {
            size_t take = (size_t)(end - position < XX_ASAR_COPY_CHUNK
                                       ? end - position
                                       : XX_ASAR_COPY_CHUNK);
            result = !(pd && xx_pd_is_stopped(pd)) &&
                     xx_asar_read_at(self, position, buffer, take);
            position += (int64_t)take;
        }
        xx_mem_free(buffer);
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
    /* A symlink is not a byte stream, so there is nothing to extract. The
     * target is on the record as XX_META_ID_LINK_TARGET and creating the link
     * is the caller's decision -- it needs a privilege this library does not
     * ask for on Windows, and a policy this library should not pick. Refusing
     * is honest; returning success having written nothing would leave the
     * caller believing a file exists. */
    if (member->is_link) {
        xx_str_free(target_path);
        return false;
    }
    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        uint8_t *buffer = (uint8_t *)xx_mem_alloc(XX_ASAR_COPY_CHUNK);
        int64_t position = member->offset;
        int64_t end = member->offset + member->size;

        result = output != NULL && buffer != NULL;
        while (result && position < end) {
            size_t take = (size_t)(end - position < XX_ASAR_COPY_CHUNK
                                       ? end - position
                                       : XX_ASAR_COPY_CHUNK);
            size_t written = 0U;

            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_asar_read_at(self, position, buffer, take)) {
                result = false;
                break;
            }
            while (written < take) {
                ssize_t sent =
                    xx_io_write(output, buffer + written, take - written);
                if (sent <= 0 || (size_t)sent > take - written) {
                    result = false;
                    break;
                }
                written += (size_t)sent;
            }
            if (!result) break;
            position += (int64_t)take;
        }
        xx_mem_free(buffer);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_asar_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_asar_get_blob_offset(const xx_asar *archive) {
    return archive ? archive->blob_offset : 0;
}

int64_t xx_asar_get_json_size(const xx_asar *archive) {
    return archive ? archive->json_size : 0;
}
