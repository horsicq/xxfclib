/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMS DataBase: the OpenVMS PCSI$ product kit that a "OpenVMS DCX PCSI"
 * container expands to, and which is also shipped uncompressed as .PCSI.
 *
 *   +0x00  u32le 0x8074FFFF      (ff ff 74 80)
 *   +0x04  u32le 0x018080A0
 *   +0x08  u32le 0x00018101
 *
 * Two bytes are skipped and the walk then descends through the constructed
 * elements 0x74 -> 0xA2 -> 0x61 -> 0xAF -> 0xA8.  A kit with no 0xAF or no
 * 0xA8 is well formed and simply holds no files.  Inside 0xA8 each member is
 *
 *   0x30 constructed
 *     0x80 ...          skipped
 *     0x81 len          the file name
 *     0xA2 constructed
 *       0x04 len=0xAB   the attribute record
 *         +0x13 u32le   allocated blocks
 *         +0x1f u16le   bytes used in the last block
 *       EOC
 *     0xA3 constructed  the 0x04 chunks holding the file, or
 *     EOC               when the file is empty
 *
 * and the size is (blocks - 1) * 0x200 + bytes-in-last-block.
 *
 * Tag reading is deliberately not standard BER.  The reference reader always
 * takes TWO bytes -- a tag byte and a length-form byte -- and only then
 * decides: form < 0x80 is a definite length, 0x80 is constructed with
 * indefinite length terminated by an end-of-contents marker (a tag byte of
 * 0x00), 0x81 takes one more byte which must be >= 0x80, and 0x82 takes two
 * more big-endian bytes which must be >= 0x100.  Those >= tests are what stop
 * ordinary data from parsing as a plausible element chain.
 *
 * TRAP: after the 0xA3 element's own end-of-contents marker there is ONE MORE
 * marker to consume, the enclosing 0x30's.  Leaving it in the stream makes the
 * next member's tag read land on it, the walk sees a 0x00 and stops, and the
 * kit yields exactly one file however many it holds.
 *
 * Nothing is compressed: a member's bytes are the contents of the 0x04 chunks
 * inside its 0xA3 element, concatenated and cut to the declared length.
 *
 * Ported from XArchive packages/xvmsdatabasearchive.cpp and
 * Algos/xvmsdatabasedecoder.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmsdb/xx_vmsdb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef VMSDB
#define XX_VMSDB_FILE_TYPE XX_FILE_TYPE_VMSDB
#else
#define XX_VMSDB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VMSDB_MIN_SIZE 0x16
#define VMSDB_MAGIC_SIZE 12U
#define VMSDB_ATTRIBUTE_SIZE 0xab
#define VMSDB_BLOCK_SIZE 0x200
#define VMSDB_MAX_MEMBERS 200000U
#define VMSDB_MAX_NAME_SIZE 4096
#define VMSDB_MAX_DEPTH 64
#define VMSDB_MAX_INPUT ((int64_t)512 * 1024 * 1024)
#define VMSDB_COPY_BUFFER 65536U

#define VMSDB_TAG_EOC 0x00U
#define VMSDB_TAG_OCTETSTRING 0x04U
#define VMSDB_TAG_MEMBER 0x30U
#define VMSDB_TAG_PRODUCT 0x61U
#define VMSDB_TAG_ROOT 0x74U
#define VMSDB_TAG_NAME 0x81U
#define VMSDB_TAG_SKIPPED 0x80U
#define VMSDB_TAG_KIT 0xa2U
#define VMSDB_TAG_CONTENT 0xa3U
#define VMSDB_TAG_FILES 0xa8U
#define VMSDB_TAG_FILELIST 0xafU

typedef struct vmsdb_tag_s {
    uint8_t tag;
    int64_t length;
    bool constructed;
    int32_t header_size;
} vmsdb_tag;

typedef struct vmsdb_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    uint64_t uncompressed_size;
    bool stored;
} vmsdb_member;

typedef struct vmsdb_stream_s {
    vmsdb_member *items;
    size_t count;
    size_t index;
    uint8_t *image;
    int64_t image_size;
    int64_t base_address;
} vmsdb_stream;

typedef struct vmsdb_cursor_s {
    const uint8_t *image;
    int64_t size;
    int64_t position;
} vmsdb_cursor;

static uint32_t vmsdb_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint16_t vmsdb_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool vmsdb_parse_tag(const uint8_t *data, int64_t available,
                            vmsdb_tag *tag) {
    uint8_t form;
    if (!data || !tag || available < 2) return false;
    xx_mem_zero(tag, sizeof(*tag));
    tag->tag = data[0];
    form = data[1];
    if (form == 0x80U) {
        tag->constructed = true;
        tag->length = 0;
        tag->header_size = 2;
    } else if (form == 0x81U) {
        int64_t length;
        if (available < 3) return false;
        length = data[2];
        /* A one-byte long form below 0x80 would have been the short form;
         * refusing it is what keeps arbitrary data from parsing. */
        if (length < 0x80) return false;
        tag->length = length;
        tag->header_size = 3;
    } else if (form == 0x82U) {
        int64_t length;
        if (available < 4) return false;
        length = ((int64_t)data[2] << 8) | (int64_t)data[3];
        if (length < 0x100) return false;
        tag->length = length;
        tag->header_size = 4;
    } else if (form >= 0x80U) {
        return false;
    } else {
        tag->length = form;
        tag->header_size = 2;
    }
    return true;
}

static bool vmsdb_cursor_skip(vmsdb_cursor *cursor, int64_t size) {
    if (size < 0 || size > cursor->size - cursor->position) return false;
    cursor->position += size;
    return true;
}

static bool vmsdb_cursor_read(vmsdb_cursor *cursor, uint8_t *buffer,
                              int64_t size) {
    if (size < 0 || size > cursor->size - cursor->position) return false;
    if (buffer && size != 0)
        xx_rt_memcpy(buffer, cursor->image + cursor->position, (size_t)size);
    cursor->position += size;
    return true;
}

/* Reads an element header and steps over it. */
static bool vmsdb_cursor_tag(vmsdb_cursor *cursor, vmsdb_tag *tag) {
    int64_t available = cursor->size - cursor->position;
    if (available > 4) available = 4;
    if (available < 2) return false;
    if (!vmsdb_parse_tag(cursor->image + cursor->position, available, tag))
        return false;
    return vmsdb_cursor_skip(cursor, tag->header_size);
}

/* A constructed element has no length, so the only way past it is to walk its
 * children to the end-of-contents marker. */
static bool vmsdb_skip_element(vmsdb_cursor *cursor, const vmsdb_tag *tag,
                               int32_t depth) {
    if (depth > VMSDB_MAX_DEPTH) return false;
    if (!tag->constructed) return vmsdb_cursor_skip(cursor, tag->length);
    for (;;) {
        vmsdb_tag inner;
        if (!vmsdb_cursor_tag(cursor, &inner)) return false;
        if (inner.tag == VMSDB_TAG_EOC) return true;
        if (!vmsdb_skip_element(cursor, &inner, depth + 1)) return false;
    }
}

static bool vmsdb_enter_element(vmsdb_cursor *cursor, uint8_t wanted) {
    for (;;) {
        vmsdb_tag tag;
        if (!vmsdb_cursor_tag(cursor, &tag)) return false;
        if (tag.tag == VMSDB_TAG_EOC) return false;
        if (tag.constructed && tag.tag == wanted) return true;
        if (!vmsdb_skip_element(cursor, &tag, 0)) return false;
    }
}

/* Names come out of the kit as flat VMS specifications such as
 * "[AMDS]AMDS$COMM.EXE".  Only the bytes a host filesystem cannot carry are
 * replaced; the name is never split into directories. */
static char *vmsdb_make_name(const uint8_t *bytes, size_t size) {
    size_t start = 0U;
    size_t end = size;
    size_t index;
    char *name;
    while (end > start && (bytes[end - 1U] == ' ' || bytes[end - 1U] == 0U ||
                           bytes[end - 1U] == '\t'))
        --end;
    while (start < end && (bytes[start] == ' ' || bytes[start] == '\t')) ++start;
    if (end == start) return NULL;
    name = (char *)xx_mem_alloc(end - start + 1U);
    if (!name) return NULL;
    for (index = start; index < end; ++index) {
        uint8_t c = bytes[index];
        if (c < 0x20U || c == 0x7fU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            name[index - start] = '_';
        else
            name[index - start] = (char)c;
    }
    name[end - start] = 0;
    if (name[0] == '.' &&
        (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

static void vmsdb_stream_free(void *opaque) {
    vmsdb_stream *stream = (vmsdb_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->image) xx_mem_free(stream->image);
    xx_mem_free(stream);
}

static bool vmsdb_add_member(vmsdb_stream *stream, const vmsdb_member *member) {
    vmsdb_member *grown;
    if (!stream || !member || stream->count >= VMSDB_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (vmsdb_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool vmsdb_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vmsdb_check_magic(const uint8_t *magic) {
    return vmsdb_le32(magic) == UINT32_C(0x8074ffff) &&
           vmsdb_le32(magic + 4) == UINT32_C(0x018080a0) &&
           vmsdb_le32(magic + 8) == UINT32_C(0x00018101);
}

static bool vmsdb_walk_members(vmsdb_stream *stream, int64_t base_address,
                               xx_pd_struct *pd) {
    vmsdb_cursor cursor;
    cursor.image = stream->image;
    cursor.size = stream->image_size;
    /* The reference reader opens by discarding two bytes; the first element it
     * looks at is the 0x74 that follows them. */
    cursor.position = 2;
    if (!vmsdb_enter_element(&cursor, VMSDB_TAG_ROOT)) return false;
    if (!vmsdb_enter_element(&cursor, VMSDB_TAG_KIT)) return false;
    if (!vmsdb_enter_element(&cursor, VMSDB_TAG_PRODUCT)) return false;
    /* A kit without a file list is well formed and simply holds nothing. */
    if (!vmsdb_enter_element(&cursor, VMSDB_TAG_FILELIST) ||
        !vmsdb_enter_element(&cursor, VMSDB_TAG_FILES))
        return true;

    while (stream->count < VMSDB_MAX_MEMBERS) {
        int64_t member_offset = cursor.position;
        vmsdb_tag tag;
        uint8_t attributes[VMSDB_ATTRIBUTE_SIZE];
        uint8_t *raw_name;
        int64_t blocks, last_bytes, declared;
        vmsdb_member member;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_MEMBER)
            break;
        if (!vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_SKIPPED)
            break;
        if (!vmsdb_skip_element(&cursor, &tag, 0)) break;
        if (!vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_NAME ||
            tag.length == 0 || tag.length > VMSDB_MAX_NAME_SIZE)
            break;
        raw_name = (uint8_t *)xx_mem_alloc((size_t)tag.length);
        if (!raw_name) return false;
        if (!vmsdb_cursor_read(&cursor, raw_name, tag.length)) {
            xx_mem_free(raw_name);
            break;
        }
        xx_mem_zero(&member, sizeof(member));
        member.name = vmsdb_make_name(raw_name, (size_t)tag.length);
        xx_mem_free(raw_name);
        if (!member.name) break;
        member.header_offset = base_address + member_offset;

        if (!vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_KIT ||
            !vmsdb_cursor_tag(&cursor, &tag) ||
            tag.tag != VMSDB_TAG_OCTETSTRING ||
            tag.length != VMSDB_ATTRIBUTE_SIZE ||
            !vmsdb_cursor_read(&cursor, attributes, VMSDB_ATTRIBUTE_SIZE) ||
            !vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_EOC) {
            xx_str_free(member.name);
            break;
        }
        blocks = (int64_t)(int32_t)vmsdb_le32(attributes + 0x13);
        last_bytes = (int64_t)vmsdb_le16(attributes + 0x1f);
        /* A block count of zero makes the declared size negative.  The
         * reference does not reject that: its remaining-bytes counter never
         * goes positive and the member comes out empty. */
        declared = (blocks - 1) * VMSDB_BLOCK_SIZE + last_bytes;
        if (declared < 0) declared = 0;

        if (!vmsdb_cursor_tag(&cursor, &tag)) {
            xx_str_free(member.name);
            break;
        }
        if (tag.tag == VMSDB_TAG_CONTENT) {
            int64_t content_offset = cursor.position;
            int64_t total = 0;
            bool ok = true;
            for (;;) {
                vmsdb_tag chunk;
                if (pd && xx_pd_is_stopped(pd)) {
                    xx_str_free(member.name);
                    return false;
                }
                if (!vmsdb_cursor_tag(&cursor, &chunk)) {
                    ok = false;
                    break;
                }
                if (chunk.tag == VMSDB_TAG_EOC) break;
                if (chunk.tag != VMSDB_TAG_OCTETSTRING || chunk.length == 0 ||
                    !vmsdb_cursor_skip(&cursor, chunk.length)) {
                    ok = false;
                    break;
                }
                total += chunk.length;
            }
            if (!ok) {
                xx_str_free(member.name);
                break;
            }
            member.data_offset = base_address + content_offset;
            /* The stream deliberately takes in the end-of-contents marker: it
             * is the chunk walk's stop signal. */
            member.data_size = cursor.position - content_offset;
            member.uncompressed_size =
                (uint64_t)(declared < total ? declared : total);
            member.stored = false;
            if (!vmsdb_add_member(stream, &member)) {
                xx_str_free(member.name);
                return false;
            }
            /* TRAP: the enclosing 0x30's own end-of-contents marker. */
            if (!vmsdb_cursor_tag(&cursor, &tag) || tag.tag != VMSDB_TAG_EOC)
                break;
        } else if (tag.tag == VMSDB_TAG_EOC && declared == 0) {
            /* An empty file writes no content element at all, so the marker
             * just read IS the member's own terminator. */
            member.data_offset = base_address + cursor.position;
            member.data_size = 0;
            member.uncompressed_size = 0U;
            member.stored = true;
            if (!vmsdb_add_member(stream, &member)) {
                xx_str_free(member.name);
                return false;
            }
        } else {
            xx_str_free(member.name);
            break;
        }
    }
    return true;
}

static bool vmsdb_parse(Abstractformat *format, bool walk,
                        vmsdb_stream **result, xx_pd_struct *pd) {
    uint8_t magic[VMSDB_MAGIC_SIZE];
    vmsdb_stream *stream = NULL;
    int64_t total, size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < VMSDB_MIN_SIZE || size > VMSDB_MAX_INPUT) return false;
    if (!vmsdb_read_at(format->device, format->base_address, magic,
                       sizeof(magic)))
        return false;
    if (!vmsdb_check_magic(magic)) return false;

    stream = (vmsdb_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (walk) {
        stream->image = (uint8_t *)xx_mem_alloc((size_t)size);
        if (!stream->image ||
            !vmsdb_read_at(format->device, format->base_address, stream->image,
                           (size_t)size))
            goto fail;
        stream->image_size = size;
        stream->base_address = format->base_address;
        if (!vmsdb_walk_members(stream, format->base_address, pd)) goto fail;
    }
    *result = stream;
    return true;
fail:
    vmsdb_stream_free(stream);
    return false;
}

/* Concatenates the 0x04 chunks of one member and cuts the result to the
 * declared length. */
static bool vmsdb_decode(const vmsdb_stream *stream,
                         const vmsdb_member *member, uint8_t **plain,
                         size_t *plain_size) {
    vmsdb_cursor cursor;
    uint8_t *output;
    int64_t produced = 0;
    int64_t expected;
    if (!stream || !member || !plain || !plain_size) return false;
    expected = (int64_t)member->uncompressed_size;
    if (expected < 0) return false;
    output = (uint8_t *)xx_mem_alloc(expected != 0 ? (size_t)expected : 1U);
    if (!output) return false;
    if (expected == 0) {
        *plain = output;
        *plain_size = 0U;
        return true;
    }
    {
        int64_t relative = member->data_offset - stream->base_address;
        if (relative < 0 || member->data_size < 0 ||
            relative > stream->image_size - member->data_size) {
            xx_mem_free(output);
            return false;
        }
        cursor.image = stream->image;
        cursor.size = relative + member->data_size;
        cursor.position = relative;
    }
    for (;;) {
        vmsdb_tag chunk;
        if (!vmsdb_cursor_tag(&cursor, &chunk)) goto fail;
        if (chunk.tag == VMSDB_TAG_EOC) break;
        if (chunk.tag != VMSDB_TAG_OCTETSTRING || chunk.length == 0) goto fail;
        if (chunk.length > cursor.size - cursor.position) goto fail;
        if (produced < expected) {
            int64_t portion = expected - produced;
            if (portion > chunk.length) portion = chunk.length;
            xx_rt_memcpy(output + produced, cursor.image + cursor.position,
                         (size_t)portion);
            produced += portion;
        }
        if (!vmsdb_cursor_skip(&cursor, chunk.length)) goto fail;
    }
    if (produced != expected) goto fail;
    *plain = output;
    *plain_size = (size_t)produced;
    return true;
fail:
    xx_mem_free(output);
    return false;
}

static bool vmsdb_safe_output_name(const char *name) {
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
        return false;
    for (; *name; ++name) {
        unsigned char c = (unsigned char)*name;
        if (c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*' || c < 0x20U)
            return false;
    }
    return true;
}

static bool vmsdb_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
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

static const xx_var *vmsdb_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vmsdb_set_record(xx_archive_record *record,
                             const vmsdb_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_vmsdb_init(xx_vmsdb *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMSDB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vms-pcsi-kit");
    xx_format_set_extension(&archive->format, "pcsi");
    archive->format.check_is_valid = xx_vmsdb_check_is_valid;
    archive->format.handle_base_info = xx_vmsdb_handle_base_info;
    archive->format.get_format_size = xx_vmsdb_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmsdb_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmsdb_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmsdb_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmsdb_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmsdb_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmsdb_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmsdb *xx_vmsdb_create(xx_io_device *device, int64_t base_address) {
    xx_vmsdb *archive = (xx_vmsdb *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmsdb_init(archive, device, base_address);
    return archive;
}

void xx_vmsdb_destroy(xx_vmsdb *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmsdb_free(xx_vmsdb *archive) {
    if (!archive) return;
    xx_vmsdb_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmsdb_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vmsdb_stream *stream;
    if (!vmsdb_parse(format, false, &stream, pd)) return false;
    vmsdb_stream_free(stream);
    return true;
}

bool xx_vmsdb_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vmsdb_stream *stream;
    xx_vmsdb *archive;
    if (!format || !vmsdb_parse(format, true, &stream, pd)) return false;
    archive = (xx_vmsdb *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->image_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    vmsdb_stream_free(stream);
    return true;
}

int64_t xx_vmsdb_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmsdb_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmsdb_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmsdb_handle_base_info(format, pd))
               ? ((xx_vmsdb *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmsdb_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vmsdb_stream *stream;
    xx_archive_record_state *state;
    if (!vmsdb_parse(format, true, &stream, pd)) return NULL;
    if (stream->count == 0U) {
        vmsdb_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vmsdb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vmsdb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!vmsdb_copy_options(&state->options, options) ||
        !vmsdb_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmsdb_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmsdb_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    vmsdb_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vmsdb_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        vmsdb_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_vmsdb_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    vmsdb_stream *stream;
    vmsdb_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vmsdb_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!vmsdb_safe_output_name(member->name)) return false;
    if (member->stored) {
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        plain_size = 0U;
    } else if (!vmsdb_decode(stream, member, &plain, &plain_size)) {
        return false;
    }
    path_option = vmsdb_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        created = destination != NULL;
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
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmsdb_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
