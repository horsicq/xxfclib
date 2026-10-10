/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the classic Mac OS resource fork.
 *
 * Layout (Inside Macintosh: More Macintosh Toolbox, "Resource Manager"):
 *
 *   offset 0   uint32be  offset from fork start to the resource data area
 *   offset 4   uint32be  offset from fork start to the resource map
 *   offset 8   uint32be  length of the resource data area
 *   offset 12  uint32be  length of the resource map
 *
 * The data area is a sequence of resources, each a uint32be length followed by
 * that many bytes.  The map begins with a 16-byte copy of the header, a 4-byte
 * next-map handle, a 2-byte file reference number and 2-byte fork attributes,
 * then a uint16be offset to the type list and a uint16be offset to the name
 * list, both relative to the start of the map.
 *
 * The type list is a uint16be "number of types minus one" followed by 8-byte
 * entries: a four-character type, a uint16be "number of resources minus one",
 * and a uint16be offset to that type's reference list relative to the start of
 * the type list.  Reference-list entries are 12 bytes: int16be resource id,
 * uint16be offset into the name list (0xFFFF when unnamed), one attribute
 * byte, a 24-bit offset into the data area, and a 4-byte in-memory handle that
 * is zero on disk.  Name-list entries are a length byte followed by the name.
 *
 * There is NO magic here - the header is four plausible-looking numbers - so
 * the reader is deliberately strict: every offset and length must be
 * internally consistent and land inside the file, the whole map must parse,
 * and every resource's own length word must fit the data area.
 *
 * An EMPTY fork - type count 0xFFFF, which is how "minus one plus one" spells
 * zero - is a real thing: a bare "Icon\r" fork carries nothing but its header
 * and map.  It has no resource to corroborate that header, so it is accepted
 * only when the header proves itself: one of the two areas at the canonical
 * 256 byte fork start, and the data area and the map tiling the file exactly
 * in either order with no slack.  Four arbitrary numbers essentially never do
 * that, and it is the same rule the reference unpacker applies.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/resourcefork/xx_resourcefork.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef RESOURCEFORK
#define XX_RESOURCEFORK_FILE_TYPE XX_FILE_TYPE_RESOURCEFORK
#else
#define XX_RESOURCEFORK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RSRC_HEADER_SIZE 16U
#define RSRC_MAP_MIN_SIZE 30U
#define RSRC_MAP_TYPE_LIST_OFFSET 24U
#define RSRC_MAP_NAME_LIST_OFFSET 26U
#define RSRC_TYPE_ENTRY_SIZE 8U
#define RSRC_REF_ENTRY_SIZE 12U
#define RSRC_MAX_MAP_SIZE UINT32_C(0x04000000)
#define RSRC_MAX_MEMBERS 1048576U
#define RSRC_NAME_CAPACITY 800U

typedef struct rsrc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    uint32_t type;
    uint32_t data_area_offset;
    int32_t id;
    uint8_t attributes;
} rsrc_member;

typedef struct rsrc_stream_s {
    rsrc_member *items;
    size_t count;
    size_t index;
    uint64_t type_count;
    int64_t archive_size;
} rsrc_stream;

static bool rsrc_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
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

/* Keep printable ASCII safe in a path component; resource names with high
 * Mac Roman bytes are converted to UTF-8 separately below. */
static char rsrc_safe_char(uint8_t c)
{
    if (c < 0x20U || c > 0x7EU) return '_';
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return '_';
    return (char)c;
}

/* Resource names are Mac Roman, as in classic HFS. Keep their Unicode names
 * instead of collapsing all high bytes to '_', which can also create false
 * filename collisions. */
static const uint16_t rsrc_mac_roman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED,
    0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022,
    0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F,
    0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0,
    0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01,
    0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xFFFD, 0x00D2,
    0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

/* The reference reader displays the four raw type bytes through Windows-1252, while it decodes
 * Pascal resource names as Mac Roman. The hexadecimal prefix still preserves
 * the exact bytes when a display character is unavailable. */
static const uint16_t rsrc_cp1252_controls[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                                  0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                                  0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

/* The reference extractor renders most nonzero control bytes in resource
 * names as DOS graphical characters. Whitespace controls are handled below. */
static const uint16_t rsrc_control_glyphs[32] = {0,      0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2219, 0x25D8, 0x25CB, 0x25D9,
                                                 0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7,
                                                 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC};

static size_t rsrc_put_utf8(char *output, uint32_t codepoint)
{
    if (codepoint < 0x800U) {
        output[0] = (char)(0xC0U | (codepoint >> 6U));
        output[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2U;
    }
    output[0] = (char)(0xE0U | (codepoint >> 12U));
    output[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
    output[2] = (char)(0x80U | (codepoint & 0x3FU));
    return 3U;
}

static size_t rsrc_put_type_char(char *output, uint8_t raw)
{
    uint32_t codepoint;
    if (raw < 0x80U) {
        output[0] = rsrc_safe_char(raw);
        return 1U;
    }
    codepoint = raw < 0xA0U ? rsrc_cp1252_controls[raw - 0x80U] : raw;
    if (codepoint == 0U) {
        output[0] = '_';
        return 1U;
    }
    return rsrc_put_utf8(output, codepoint);
}

static char rsrc_hex_digit(uint32_t value)
{
    return "0123456789ABCDEF"[value & 15U];
}

/* Prefix the printable type with its exact four-byte hexadecimal value.
 * Resource types are case-sensitive on Mac OS, but Windows directory names
 * are not: DLGX/132 and dlgx/132 otherwise overwrite one another. */
static char *rsrc_build_name(uint32_t type, int32_t id, const uint8_t *raw, size_t raw_size)
{
    char buffer[RSRC_NAME_CAPACITY];
    char *name;
    size_t output = 0U, id_end, index, length, display_size;
    if (raw_size > 255U) return NULL;
    for (index = 0U; index < 8U; ++index) buffer[output++] = rsrc_hex_digit(type >> (28U - (uint32_t)index * 4U));
    buffer[output++] = ' ';
    for (index = 0U; index < 4U; ++index) output += rsrc_put_type_char(buffer + output, (uint8_t)(type >> (24U - (uint32_t)index * 8U)));
    while (output > 9U && (buffer[output - 1U] == ' ' || buffer[output - 1U] == '.')) --output;
    if (output == 9U) buffer[output++] = '_';
    buffer[output++] = '/';
    for (index = 0U; index < 4U; ++index) buffer[output++] = rsrc_hex_digit((uint16_t)id >> (12U - (uint32_t)index * 4U));
    id_end = output;
    display_size = raw_size;
    while (display_size != 0U && (raw[display_size - 1U] == '\r' || raw[display_size - 1U] == '\n')) --display_size;
    if (display_size != 0U) {
        buffer[output++] = ' ';
        for (index = 0U; index < display_size; ++index) {
            uint8_t c = raw[index];
            if (c >= 0x80U) output += rsrc_put_utf8(buffer + output, rsrc_mac_roman[c - 0x80U]);
            else if (c == '\t' || c == '\f') buffer[output++] = ' ';
            else if (c == '\n' || c == '\r') buffer[output++] = '_';
            else if (c > 0U && c < 0x20U) output += rsrc_put_utf8(buffer + output, rsrc_control_glyphs[c]);
            else if (c == 0x7FU) output += rsrc_put_utf8(buffer + output, 0x2302U);
            else buffer[output++] = rsrc_safe_char(c);
        }
    }
    while (output > id_end && (buffer[output - 1U] == ' ' || buffer[output - 1U] == '.')) --output;
    if (display_size != 0U && output == id_end) {
        buffer[output++] = ' ';
        buffer[output++] = '_';
    }
    length = output;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_mem_copy(name, buffer, length);
    name[length] = 0;
    return name;
}

static bool rsrc_safe_output_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.')) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void rsrc_stream_free(void *opaque)
{
    rsrc_stream *stream = (rsrc_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool rsrc_add_member(rsrc_stream *stream, const rsrc_member *member)
{
    rsrc_member *grown;
    if (!stream || !member || stream->count >= RSRC_MAX_MEMBERS || stream->count > SIZE_MAX / sizeof(*grown) - 1U) return false;
    grown = (rsrc_member *)xx_mem_realloc(stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* A fork that declares no resources has nothing inside it to check, so the
 * header alone has to carry the decision.  The rule is the one the reference
 * unpacker uses: the classic 256-byte fork start on one of the two areas, a
 * signed-positive data length, a map at least as large as its fixed part, and
 * an exact tiling of the file by the two areas in either order. */
static bool rsrc_empty_fork_is_credible(int64_t size, uint32_t data_offset, uint32_t map_offset, uint32_t data_size, uint32_t map_size)
{
    uint64_t file_size = (uint64_t)size;
    uint64_t data_end = (uint64_t)data_offset + data_size;
    uint64_t map_end = (uint64_t)map_offset + map_size;
    if (data_offset != 256U && map_offset != 256U) return false;
    if (data_size > INT32_MAX || map_size < RSRC_MAP_MIN_SIZE) return false;
    return (data_end == map_offset && map_end == file_size) || (map_end == data_offset && data_end == file_size);
}

static bool rsrc_parse(Abstractformat *format, rsrc_stream **result)
{
    uint8_t header[RSRC_HEADER_SIZE];
    uint8_t *map = NULL;
    rsrc_stream *stream = NULL;
    int64_t total, size;
    uint32_t data_offset, map_offset, data_size, map_size;
    uint32_t type_list, name_list, type_count, type_index;
    uint64_t end;

    if (!format || !format->device || !result || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)RSRC_HEADER_SIZE || !rsrc_read_at(format->device, format->base_address, header, sizeof(header))) return false;

    data_offset = xx_data_get_u32(header, 4, 0, true);
    map_offset = xx_data_get_u32(header + 4U, 4, 0, true);
    data_size = xx_data_get_u32(header + 8U, 4, 0, true);
    map_size = xx_data_get_u32(header + 12U, 4, 0, true);

    /* The four header values must be internally consistent and fit the file:
     * neither area may start inside the 16-byte header, the map must be large
     * enough to hold its own fixed part, and both areas must lie in range. */
    if (data_offset < RSRC_HEADER_SIZE || map_offset < RSRC_HEADER_SIZE || map_size < RSRC_MAP_MIN_SIZE || map_size > RSRC_MAX_MAP_SIZE ||
        (uint64_t)data_offset > (uint64_t)size || (uint64_t)data_size > (uint64_t)size - data_offset || (uint64_t)map_offset > (uint64_t)size ||
        (uint64_t)map_size > (uint64_t)size - map_offset)
        return false;

    map = (uint8_t *)xx_mem_alloc(map_size);
    if (!map) return false;
    if (!rsrc_read_at(format->device, format->base_address + (int64_t)map_offset, map, map_size)) goto fail;

    type_list = xx_data_get_u16(map + RSRC_MAP_TYPE_LIST_OFFSET, 2, 0, true);
    name_list = xx_data_get_u16(map + RSRC_MAP_NAME_LIST_OFFSET, 2, 0, true);
    if ((uint64_t)type_list + 2U > (uint64_t)map_size || (uint64_t)name_list > (uint64_t)map_size) goto fail;
    type_count = xx_data_get_u16(map + type_list, 2, 0, true);
    /* 0xFFFF is the canonical "no types" encoding of count-minus-one. */
    type_count = (type_count == 0xFFFFU) ? 0U : type_count + 1U;
    if ((uint64_t)type_list + 2U + (uint64_t)type_count * RSRC_TYPE_ENTRY_SIZE > (uint64_t)map_size) goto fail;

    stream = (rsrc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->type_count = type_count;

    for (type_index = 0U; type_index < type_count; ++type_index) {
        const uint8_t *entry = map + type_list + 2U + (size_t)type_index * RSRC_TYPE_ENTRY_SIZE;
        uint32_t type = xx_data_get_u32(entry, 4, 0, true);
        uint32_t count = (uint32_t)xx_data_get_u16(entry + 4U, 2, 0, true) + 1U;
        uint32_t ref_offset = xx_data_get_u16(entry + 6U, 2, 0, true);
        uint64_t ref_base = (uint64_t)type_list + ref_offset;
        uint32_t ref_index;
        if (ref_base + (uint64_t)count * RSRC_REF_ENTRY_SIZE > (uint64_t)map_size) goto fail;
        for (ref_index = 0U; ref_index < count; ++ref_index) {
            const uint8_t *ref = map + (size_t)ref_base + (size_t)ref_index * RSRC_REF_ENTRY_SIZE;
            uint8_t length_bytes[4];
            const uint8_t *raw_name = NULL;
            size_t raw_size = 0U;
            rsrc_member member;
            uint32_t name_offset = xx_data_get_u16(ref + 2U, 2, 0, true);
            uint32_t blob_offset = ((uint32_t)ref[5] << 16U) | ((uint32_t)ref[6] << 8U) | (uint32_t)ref[7];
            uint32_t blob_size;

            /* The resource's own length word, and then the blob it announces,
             * must both fit inside the declared data area. */
            if ((uint64_t)blob_offset + 4U > (uint64_t)data_size) goto fail;
            if (!rsrc_read_at(format->device, format->base_address + (int64_t)data_offset + (int64_t)blob_offset, length_bytes, sizeof(length_bytes))) goto fail;
            blob_size = xx_data_get_u32(length_bytes, 4, 0, true);
            if ((uint64_t)blob_size > (uint64_t)data_size - blob_offset - 4U) goto fail;

            if (name_offset != 0xFFFFU) {
                uint64_t position = (uint64_t)name_list + name_offset;
                if (position >= (uint64_t)map_size) goto fail;
                raw_size = map[(size_t)position];
                if (position + 1U + raw_size > (uint64_t)map_size) goto fail;
                raw_name = map + (size_t)position + 1U;
            }

            xx_mem_zero(&member, sizeof(member));
            member.type = type;
            member.id = (int32_t)(int16_t)xx_data_get_u16(ref, 2, 0, true);
            member.attributes = ref[4];
            member.data_area_offset = blob_offset;
            member.header_offset = format->base_address + (int64_t)map_offset + (int64_t)(ref - map);
            member.header_size = (int64_t)RSRC_REF_ENTRY_SIZE;
            member.data_offset = format->base_address + (int64_t)data_offset + (int64_t)blob_offset + 4;
            member.data_size = (int64_t)blob_size;
            member.name = rsrc_build_name(type, member.id, raw_name, raw_size);
            if (!member.name) goto fail;
            if (!rsrc_add_member(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }
    }

    /* An empty fork - type count 0xFFFF, often a bare "Icon\r" fork - carries
     * no resource to corroborate the header, so it is accepted only when the
     * header is self-proving: one of the two areas sits at the canonical 256
     * byte start, and the data area and the map tile the file exactly with no
     * slack in either order.  Four arbitrary numbers almost never do that. */
    if (stream->count == 0U && !rsrc_empty_fork_is_credible(size, data_offset, map_offset, data_size, map_size)) goto fail;
    end = (uint64_t)data_offset + data_size;
    if ((uint64_t)map_offset + map_size > end) end = (uint64_t)map_offset + map_size;
    stream->archive_size = (int64_t)end;
    xx_mem_free(map);
    *result = stream;
    return true;
fail:
    if (map) xx_mem_free(map);
    rsrc_stream_free(stream);
    return false;
}

static bool rsrc_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *rsrc_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rsrc_set_record(xx_archive_record *record, const rsrc_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) && xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, member->type) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_RELATIVE_OFFSET_LOCAL_HEADER, member->data_area_offset) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_resourcefork_init(xx_resourcefork *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_RESOURCEFORK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apple-resource");
    xx_format_set_extension(&archive->format, "rsrc");
    archive->format.check_is_valid = xx_resourcefork_check_is_valid;
    archive->format.handle_base_info = xx_resourcefork_handle_base_info;
    archive->format.get_format_size = xx_resourcefork_get_format_size;
    archive->format.get_number_of_archive_records = xx_resourcefork_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_resourcefork_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_resourcefork_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_resourcefork_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_resourcefork_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_resourcefork_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_resourcefork *xx_resourcefork_create(xx_io_device *device, int64_t base_address)
{
    xx_resourcefork *archive = (xx_resourcefork *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_resourcefork_init(archive, device, base_address);
    return archive;
}

void xx_resourcefork_destroy(xx_resourcefork *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_resourcefork_free(xx_resourcefork *archive)
{
    if (!archive) return;
    xx_resourcefork_destroy(archive);
    xx_mem_free(archive);
}

bool xx_resourcefork_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    rsrc_stream *stream;
    (void)pd;
    if (!rsrc_parse(format, &stream)) return false;
    rsrc_stream_free(stream);
    return true;
}

bool xx_resourcefork_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    rsrc_stream *stream;
    xx_resourcefork *archive;
    (void)pd;
    if (!format || !rsrc_parse(format, &stream)) return false;
    archive = (xx_resourcefork *)format;
    archive->number_of_records = stream->count;
    archive->number_of_types = stream->type_count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    rsrc_stream_free(stream);
    return true;
}

int64_t xx_resourcefork_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_resourcefork_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_resourcefork_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_resourcefork_handle_base_info(format, pd)) ? ((xx_resourcefork *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_resourcefork_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    rsrc_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!rsrc_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rsrc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rsrc_stream_free;
    state->total_records = stream->count;
    if (!rsrc_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    /* An empty fork is a valid archive with nothing in it: hand back a state
     * that reports no current record rather than indexing item zero. */
    if (stream->count == 0U) {
        state->has_record = false;
        return state;
    }
    if (!rsrc_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_resourcefork_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_resourcefork_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rsrc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (rsrc_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = rsrc_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_resourcefork_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rsrc_stream *stream;
    rsrc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (rsrc_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!rsrc_safe_output_name(member->name) || member->data_size < 0 || (uint64_t)member->data_size > SIZE_MAX) goto done;
    plain_size = (size_t)member->data_size;
    plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!plain) goto done;
    if (plain_size != 0U && !rsrc_read_at(format->device, member->data_offset, plain, plain_size)) goto done;
    path_option = rsrc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
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

void xx_resourcefork_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
