/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GoldSrc (Half-Life) BSP version 30: the embedded miptex textures and the
 * entity lump as members.  xx_goldsrc_bsp.h carries the field table.
 * Written from the published v30 layout (Valve's bspfile.h structures);
 * HLLib's CBSPFile was used only as a black-box oracle.
 *
 * The file has no magic beyond the version word, so acceptance rests on the
 * lump directory: every non-empty lump lies behind the header and inside the
 * file, every fixed-record lump is a whole number of records, the world
 * model exists and the texture lump's directory fits.  A texture slot that
 * is malformed on its own (bad offsets, a palette past its lump) is skipped
 * rather than failing the map, and counted.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/goldsrc_bsp/xx_goldsrc_bsp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as GOLDSRC_BSP is registered. */
#ifdef GOLDSRC_BSP
#define XX_GOLDSRC_BSP_FILE_TYPE XX_FILE_TYPE_GOLDSRC_BSP
#else
#define XX_GOLDSRC_BSP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GS_VERSION 30
#define GS_LUMPS 15
#define GS_HEADER_SIZE (4 + GS_LUMPS * 8)
#define GS_LUMP_ENTITIES 0
#define GS_LUMP_PLANES 1
#define GS_LUMP_TEXTURES 2
#define GS_LUMP_MODELS 14
#define GS_PLANE_SIZE 20
#define GS_MODEL_SIZE 64
/* MAX_MAP_TEXTURES is 512 in the Half-Life tools and a few thousand in the
 * extended compilers; a directory beyond this is not a map. */
#define GS_MAX_TEXTURES 16384U
#define GS_MIPTEX_HEADER 40U
#define GS_NAME_FIELD 16U
#define GS_MAX_PALETTE 256U
/* 15 safe name bytes, "_" + 5-digit slot + "_" + 3-digit retry, ".mip". */
#define GS_NAME_BUFFER 48
#define GS_MAX_RETRY 999U

/* Record size of each lump, 0 for free-form lumps. */
static const uint32_t gs_record_size[GS_LUMPS] = {0U, GS_PLANE_SIZE, 0U, 12U, 0U, 24U, 40U, 20U, 0U, 8U, 28U, 2U, 4U, 4U, GS_MODEL_SIZE};

typedef struct gs_header_s {
    int64_t offset[GS_LUMPS];
    int64_t length[GS_LUMPS];
    int64_t input_size;
    int64_t archive_size;
    int entities; /**< Lump holding the entity text (0, or 1 for Blue Shift). */
    uint32_t textures;
} gs_header;

typedef struct gs_member_s {
    char name[GS_NAME_BUFFER];
    int64_t offset; /**< Absolute device offset. */
    int64_t size;
    uint32_t hash;
} gs_member;

typedef struct gs_stream_s {
    gs_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t textures, embedded, skipped;
} gs_stream;

static int64_t gs_s32(const uint8_t *bytes)
{
    return (int64_t)(int32_t)xx_data_get_u32(bytes, 4, 0, false);
}

static bool gs_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    const size_t capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (capacity && request > capacity) request = capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Header                                                                  */

static bool gs_parse_header(Abstractformat *format, gs_header *out)
{
    uint8_t raw[GS_HEADER_SIZE];
    gs_header h;
    int64_t total;
    int i;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&h, sizeof(h));
    h.input_size = total - format->base_address;
    if (h.input_size < GS_HEADER_SIZE || !gs_read_at(format->device, format->base_address, raw, sizeof(raw)) || xx_data_get_u32(raw, 4, 0, false) != GS_VERSION)
        return false;
    h.archive_size = GS_HEADER_SIZE;
    for (i = 0; i < GS_LUMPS; ++i) {
        int64_t offset = gs_s32(raw + 4 + i * 8);
        int64_t length = gs_s32(raw + 8 + i * 8);
        if (offset < 0 || length < 0 || offset > h.input_size) return false;
        if (length > 0) {
            if (offset < GS_HEADER_SIZE || length > h.input_size - offset) return false;
            if (offset + length > h.archive_size) h.archive_size = offset + length;
        }
        if (gs_record_size[i] && i != GS_LUMP_PLANES && length % (int64_t)gs_record_size[i] != 0) return false;
        h.offset[i] = offset;
        h.length[i] = length;
    }
    /* Planes normally sit in lump 1; Blue Shift swapped them with the
     * entities in lump 0. */
    if (h.length[GS_LUMP_PLANES] % GS_PLANE_SIZE == 0) h.entities = GS_LUMP_ENTITIES;
    else if (h.length[GS_LUMP_ENTITIES] % GS_PLANE_SIZE == 0) h.entities = GS_LUMP_PLANES;
    else return false;
    /* Every map has the world model. */
    if (h.length[GS_LUMP_MODELS] < GS_MODEL_SIZE) return false;
    if (h.length[GS_LUMP_TEXTURES] > 0) {
        uint8_t count[4];
        int64_t n;
        if (h.length[GS_LUMP_TEXTURES] < 4 || !gs_read_at(format->device, format->base_address + h.offset[GS_LUMP_TEXTURES], count, 4U)) return false;
        n = gs_s32(count);
        if (n < 0 || n > (int64_t)GS_MAX_TEXTURES || 4 + 4 * n > h.length[GS_LUMP_TEXTURES]) return false;
        h.textures = (uint32_t)n;
    }
    *out = h;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char gs_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool gs_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || gs_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* Windows device names, with any extension. */
static bool gs_is_device(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (gs_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((gs_upper(name[0]) == 'C' && gs_upper(name[1]) == 'O' && gs_upper(name[2]) == 'M') ||
            (gs_upper(name[0]) == 'L' && gs_upper(name[1]) == 'P' && gs_upper(name[2]) == 'T'));
}

/* The 16-byte name field reduced to a file-name stem: printable ASCII other
 * than path and wildcard punctuation is kept, anything else becomes '_', a
 * trailing run of dots or spaces becomes '_' (so ".." cannot survive), an
 * empty name becomes "texture", and a device name gets a leading '_'. */
static void gs_texture_stem(const uint8_t *field, char *out)
{
    size_t length = 0U, index;
    char buffer[GS_NAME_FIELD + 2U];
    while (length < GS_NAME_FIELD - 1U && field[length]) {
        char c = (char)field[length];
        if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7EU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
        buffer[length++] = c;
    }
    buffer[length] = 0;
    for (index = length; index > 0U; --index) {
        if (buffer[index - 1U] != '.' && buffer[index - 1U] != ' ') break;
        buffer[index - 1U] = '_';
    }
    if (length == 0U) {
        xx_rt_snprintf(out, GS_NAME_BUFFER, "texture");
        return;
    }
    xx_rt_snprintf(out, GS_NAME_BUFFER, "%s%s", gs_is_device(buffer) ? "_" : "", buffer);
}

static uint32_t gs_hash(const char *name)
{
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)gs_upper(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool gs_same_name(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (gs_upper(*a) != gs_upper(*b)) return false;
    return *a == *b;
}

typedef struct gs_names_s {
    uint32_t *slots; /**< Member index + 1, 0 for empty. */
    size_t mask;
} gs_names;

static bool gs_name_taken(const gs_names *names, const gs_member *items, const char *name, uint32_t hash)
{
    size_t at = hash & names->mask, probes;
    for (probes = 0U; probes <= names->mask; ++probes) {
        uint32_t slot = names->slots[at];
        if (!slot) return false;
        if (items[slot - 1U].hash == hash && gs_same_name(items[slot - 1U].name, name)) return true;
        at = (at + 1U) & names->mask;
    }
    return true;
}

static void gs_name_insert(gs_names *names, size_t index, uint32_t hash)
{
    size_t at = hash & names->mask;
    while (names->slots[at]) at = (at + 1U) & names->mask;
    names->slots[at] = (uint32_t)index + 1U;
}

/* Give stream->items[count] a case-insensitively unique name: the stem plus
 * extension, else stem_<slot>, else stem_<slot>_<n>. */
static bool gs_assign_name(gs_stream *stream, gs_names *names, const char *stem, const char *extension, uint32_t slot)
{
    gs_member *member = &stream->items[stream->count];
    uint32_t retry;
    for (retry = 0U; retry <= GS_MAX_RETRY + 1U; ++retry) {
        if (retry == 0U) xx_rt_snprintf(member->name, GS_NAME_BUFFER, "%s%s", stem, extension);
        else if (retry == 1U) xx_rt_snprintf(member->name, GS_NAME_BUFFER, "%s_%05u%s", stem, (unsigned)slot, extension);
        else xx_rt_snprintf(member->name, GS_NAME_BUFFER, "%s_%05u_%03u%s", stem, (unsigned)slot, (unsigned)(retry - 1U), extension);
        member->hash = gs_hash(member->name);
        if (!gs_name_taken(names, stream->items, member->name, member->hash)) {
            gs_name_insert(names, stream->count, member->hash);
            ++stream->count;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Texture lump                                                            */

/* Extent of the miptex at @p at (lump relative) inside a lump of @p lump
 * bytes, or 0 when it carries no pixels or is malformed (*bad set). */
static int64_t gs_miptex_extent(xx_io_device *device, int64_t lump_base, int64_t lump, int64_t at, uint8_t *head, bool *bad)
{
    uint64_t width, height, avail, end = GS_MIPTEX_HEADER, palette;
    uint8_t count[2];
    uint32_t level, colors;
    *bad = true;
    if (at < 0 || at > lump - (int64_t)GS_MIPTEX_HEADER || !gs_read_at(device, lump_base + at, head, GS_MIPTEX_HEADER)) return 0;
    avail = (uint64_t)(lump - at);
    width = xx_data_get_u32(head + 16, 4, 0, false);
    height = xx_data_get_u32(head + 20, 4, 0, false);
    if (xx_data_get_u32(head + 24, 4, 0, false) == 0U) {
        *bad = false; /* External (WAD) texture: nothing embedded. */
        return 0;
    }
    if (width == 0U || height == 0U || width > avail || height > avail || width * height > avail) return 0;
    for (level = 0U; level < 4U; ++level) {
        uint64_t offset = xx_data_get_u32(head + 24 + level * 4U, 4, 0, false);
        uint64_t size = (width >> level) * (height >> level);
        if (offset < GS_MIPTEX_HEADER || offset > avail || size > avail - offset) return 0;
        if (offset + size > end) end = offset + size;
    }
    palette = (uint64_t)xx_data_get_u32(head + 36, 4, 0, false) + (width >> 3U) * (height >> 3U);
    if (palette > avail || avail - palette < 2U || !gs_read_at(device, lump_base + at + (int64_t)palette, count, 2U)) return 0;
    colors = (uint32_t)count[0] | ((uint32_t)count[1] << 8U);
    if (colors > GS_MAX_PALETTE || avail - palette - 2U < colors * 3U) return 0;
    if (palette + 2U + colors * 3U > end) end = palette + 2U + colors * 3U;
    *bad = false;
    return (int64_t)end;
}

static void gs_stream_free(void *opaque)
{
    gs_stream *stream = (gs_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool gs_parse(Abstractformat *format, gs_stream **result, xx_pd_struct *pd)
{
    gs_header h;
    gs_stream *stream = NULL;
    gs_names names = {NULL, 0U};
    uint8_t *table = NULL;
    size_t capacity, slots = 4U, index;
    int64_t lump_base = 0, lump = 0, ent_offset, ent_size;
    uint8_t head[GS_MIPTEX_HEADER], last;
    char stem[GS_NAME_BUFFER];
    if (result) *result = NULL;
    if (!gs_parse_header(format, &h)) return false;
    stream = (gs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->archive_size = h.archive_size;
    stream->textures = h.textures;
    capacity = (size_t)h.textures + 1U;
    while (slots < capacity * 2U) slots <<= 1U;
    stream->items = (gs_member *)xx_mem_calloc(capacity, sizeof(gs_member));
    names.slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    names.mask = slots - 1U;
    if (!stream->items || !names.slots) goto fail;

    ent_offset = format->base_address + h.offset[h.entities];
    ent_size = h.length[h.entities];
    if (ent_size > 0 && gs_read_at(format->device, ent_offset + ent_size - 1, &last, 1U) && last == 0U) --ent_size;
    if (ent_size > 0) {
        stream->items[0].offset = ent_offset;
        stream->items[0].size = ent_size;
        if (!gs_assign_name(stream, &names, "entities", ".ent", 0U)) goto fail;
    }

    if (h.textures) {
        lump_base = format->base_address + h.offset[GS_LUMP_TEXTURES];
        lump = h.length[GS_LUMP_TEXTURES];
        table = (uint8_t *)xx_mem_alloc((size_t)h.textures * 4U);
        if (!table || !gs_read_at(format->device, lump_base + 4, table, (size_t)h.textures * 4U)) goto fail;
    }
    for (index = 0U; index < h.textures; ++index) {
        int64_t at = gs_s32(table + index * 4U), extent;
        bool bad;
        if ((index & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (at == -1) continue;
        if (at < 4 + 4 * (int64_t)h.textures) {
            ++stream->skipped;
            continue;
        }
        extent = gs_miptex_extent(format->device, lump_base, lump, at, head, &bad);
        if (bad) {
            ++stream->skipped;
            continue;
        }
        if (extent == 0) continue;
        ++stream->embedded;
        stream->items[stream->count].offset = lump_base + at;
        stream->items[stream->count].size = extent;
        gs_texture_stem(head, stem);
        if (!gs_assign_name(stream, &names, stem, ".mip", (uint32_t)index)) goto fail;
    }
    if (table) xx_mem_free(table);
    xx_mem_free(names.slots);
    if (result) *result = stream;
    else gs_stream_free(stream);
    return true;
fail:
    if (table) xx_mem_free(table);
    if (names.slots) xx_mem_free(names.slots);
    gs_stream_free(stream);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool gs_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *gs_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool gs_set_record(xx_archive_record *record, const gs_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->offset;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Read a member through without writing it (verification only). */
static bool gs_verify_range(xx_io_device *device, int64_t offset, int64_t size, xx_pd_struct *pd)
{
    uint8_t buffer[4096];
    while (size > 0) {
        size_t chunk = size > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)size;
        if ((pd && xx_pd_is_stopped(pd)) || !gs_read_at(device, offset, buffer, chunk)) return false;
        offset += (int64_t)chunk;
        size -= (int64_t)chunk;
    }
    return true;
}

void xx_goldsrc_bsp_init(xx_goldsrc_bsp *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GOLDSRC_BSP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-goldsrc-bsp");
    xx_format_set_extension(&archive->format, "bsp");
    archive->format.check_is_valid = xx_goldsrc_bsp_check_is_valid;
    archive->format.handle_base_info = xx_goldsrc_bsp_handle_base_info;
    archive->format.get_format_size = xx_goldsrc_bsp_get_format_size;
    archive->format.get_number_of_archive_records = xx_goldsrc_bsp_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_goldsrc_bsp_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_goldsrc_bsp_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_goldsrc_bsp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_goldsrc_bsp_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_goldsrc_bsp_free_archive_records_reading;
}

xx_goldsrc_bsp *xx_goldsrc_bsp_create(xx_io_device *device, int64_t base_address)
{
    xx_goldsrc_bsp *archive = (xx_goldsrc_bsp *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_goldsrc_bsp_init(archive, device, base_address);
    return archive;
}

void xx_goldsrc_bsp_destroy(xx_goldsrc_bsp *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_goldsrc_bsp_free(xx_goldsrc_bsp *archive)
{
    if (!archive) return;
    xx_goldsrc_bsp_destroy(archive);
    xx_mem_free(archive);
}

bool xx_goldsrc_bsp_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    gs_header h;
    (void)pd;
    return gs_parse_header(format, &h);
}

bool xx_goldsrc_bsp_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    gs_stream *stream;
    xx_goldsrc_bsp *archive;
    if (!format || !gs_parse(format, &stream, pd)) return false;
    archive = (xx_goldsrc_bsp *)format;
    archive->number_of_records = stream->count;
    archive->number_of_textures = stream->textures;
    archive->embedded_textures = stream->embedded;
    archive->skipped_textures = stream->skipped;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    gs_stream_free(stream);
    return true;
}

int64_t xx_goldsrc_bsp_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_goldsrc_bsp_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_goldsrc_bsp_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_goldsrc_bsp_handle_base_info(format, pd)) ? ((xx_goldsrc_bsp *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_goldsrc_bsp_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    gs_stream *stream;
    xx_archive_record_state *state;
    if (!gs_parse(format, &stream, pd)) return NULL;
    if (stream->count == 0U) {
        gs_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gs_stream_free;
    state->total_records = stream->count;
    if (!gs_copy_options(&state->options, options) || !gs_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_goldsrc_bsp_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_goldsrc_bsp_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    gs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (gs_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = gs_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_goldsrc_bsp_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    gs_stream *stream;
    gs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (gs_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->offset < 0 || member->size < 0) return false;
    path_option = gs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return gs_verify_range(format->device, member->offset, member->size, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* member->name was built by gs_texture_stem / gs_assign_name: no
     * separators, drive colons, control bytes, trailing dots or device
     * names can reach it. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* The store helper removes its own output on failure (and only that). */
    result = xx_store_unpack_device_to_file(format->device, member->offset, member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_goldsrc_bsp_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
