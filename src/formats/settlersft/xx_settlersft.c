/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Blue Byte / Funatics "Settlers" resource files (SPAx.PA and friends).
 * Ported from XArchive's games/xsettlersft.cpp, cross-checked against U3's
 * Settlers_FT handler (class yob, VMT 00701688).
 *
 *   header, 8 bytes at offset 0:
 *     0x00   4  u32 LE   total file size -- the archive's own length
 *     0x04   4  u32 LE   slot count
 *
 *   slot table, 8 bytes per slot, starting at 8:
 *     +0x00  4  u32 LE   member size
 *     +0x04  4  u32 LE   absolute member offset, 0 for an unused slot
 *
 * The format has no magic at all, so recognition rests on arithmetic:
 *   - the declared size must equal the real file size exactly;
 *   - slot 0 must start immediately behind the table and be non-empty;
 *   - slot 1 must start immediately behind slot 0 and be non-empty;
 *   - one slot must be exactly 768 bytes -- the VGA palette every archive of
 *     this family carries, and without which the reference decoder gives up.
 * Those four together are a far stronger test than any 4-byte signature, and
 * they are what this reader uses as its prefilter substitute.
 *
 * Members are stored.  Bitmap and sprite records are converted to BMP using
 * the archive palette; other records retain their stored bytes.
 *
 * All 5 corpus samples in F:\ARC\ARC\SETTLERS_FT parse.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/settlersft/xx_settlersft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef SETTLERSFT
#define XX_SETTLERSFT_FILE_TYPE XX_FILE_TYPE_SETTLERSFT
#else
#define XX_SETTLERSFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SETTLERSFT_METHOD_STORE 0U
#define XX_SETTLERSFT_METHOD_IMAGE 1U
#define XX_SETTLERSFT_HEADER_SIZE 8
#define XX_SETTLERSFT_ENTRY_SIZE 8
#define XX_SETTLERSFT_PALETTE_SIZE 768
/* The reference implementation's ceilings, kept verbatim.  The real bound is
 * that the table must fit in the file, which caps the count at size/8. */
#define XX_SETTLERSFT_MAX_ENTRIES 0x100000U
#define XX_SETTLERSFT_MAX_ENTRY_SIZE 0x1000000
#define XX_SETTLERSFT_KIND_BIN 0U
#define XX_SETTLERSFT_KIND_BITMAP 1U
#define XX_SETTLERSFT_KIND_MASK 2U
#define XX_SETTLERSFT_KIND_PALETTE 3U
#define XX_SETTLERSFT_KIND_XMI 4U
#define XX_SETTLERSFT_BMP_HEADER_SIZE 54U
#define XX_SETTLERSFT_BMP_PALETTE_SIZE 1024U

typedef struct xx_settlersft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    uint32_t kind;
    uint32_t slot_index;
    bool has_crc;
    bool is_folder;
} xx_settlersft_member;

typedef struct xx_settlersft_stream_s {
    xx_settlersft_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
    uint8_t palette[XX_SETTLERSFT_PALETTE_SIZE];
} xx_settlersft_stream;

static void xx_settlersft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_settlersft_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_settlersft_path_safe(const char *path) {
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
static XXFC_MAYBE_UNUSED char *xx_settlersft_make_name(const uint8_t *raw, size_t size,
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

static void xx_settlersft_stream_free(void *pointer) {
    xx_settlersft_stream *stream = (xx_settlersft_stream *)pointer;
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
static bool xx_settlersft_add(xx_settlersft_stream *stream,
                          const xx_settlersft_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_settlersft_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_settlersft_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Slots are anonymous; the one-based slot index is their stable identity. */
static char *xx_settlersft_slot_name(uint32_t index, uint32_t kind) {
    char text[48];
    const char *dir = "";
    const char *ext = ".bin";
    int length;
    if (kind == XX_SETTLERSFT_KIND_BITMAP) { dir = "Bitmaps/"; ext = ".bmp"; }
    else if (kind == XX_SETTLERSFT_KIND_MASK) { dir = "Masks/"; ext = ".bmp"; }
    else if (kind == XX_SETTLERSFT_KIND_PALETTE) { dir = "Palettes/"; ext = ".pal"; }
    else if (kind == XX_SETTLERSFT_KIND_XMI) { dir = "XMIDI/"; ext = ".xmi"; }
    length = snprintf(text, sizeof(text), "%s%04u%s", dir, index, ext);
    return length > 0 && (size_t)length < sizeof(text) ? xx_str_dup(text) : NULL;
}

/* The same bounded row walk classifies and converts a sprite.  A row ends
 * only on a zero run and must fill exactly its declared width. */
static bool xx_settlersft_rle(const uint8_t *source, size_t size, int type,
                              uint32_t width, uint32_t height,
                              const uint8_t *palette, uint8_t *pixels,
                              xx_pd_struct *pd) {
    size_t pos = 0U;
    uint32_t y;
    if (!source || width == 0U || width > 1024U || height == 0U) return false;
    for (y = 0U; y < height; ++y) {
        uint32_t x = 0U;
        uint8_t *row = pixels ? pixels + (size_t)y * width * 4U : NULL;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (row) xx_mem_zero(row, (size_t)width * 4U);
        for (;;) {
            uint32_t run, skip;
            if (size - pos < 2U) return false;
            skip = source[pos++];
            run = source[pos++];
            if (skip > width - x) return false;
            x += skip;
            if (run == 0U) break;
            if (run > width - x) return false;
            if (type == 2) {
                if (row) xx_rt_memset(row + (size_t)x * 4U, 0xff, run * 4U);
                x += run;
            } else {
                uint32_t i;
                if (run > size - pos) return false;
                for (i = 0U; i < run; ++i) {
                    uint8_t colour = source[pos++];
                    if (row && palette) {
                        row[(size_t)x * 4U] = palette[(size_t)colour * 3U + 2U];
                        row[(size_t)x * 4U + 1U] = palette[(size_t)colour * 3U + 1U];
                        row[(size_t)x * 4U + 2U] = palette[(size_t)colour * 3U];
                        row[(size_t)x * 4U + 3U] = 0xff;
                    }
                    ++x;
                }
            }
        }
        if (x != width) return false;
    }
    return pos == size;
}

static uint32_t xx_settlersft_classify(const uint8_t *p, size_t size,
                                       uint64_t *output_size,
                                       xx_pd_struct *pd) {
    uint32_t width, height;
    *output_size = size;
    if (size >= 11U) {
        width = xx_data_get_u16(p + 2, 2, 0, false);
        height = xx_data_get_u16(p + 4, 2, 0, false);
        if ((int16_t)width > 0 && (int16_t)height > 0 &&
            xx_settlersft_rle(p + 10, size - 10U,
                              (int16_t)xx_data_get_u16(p, 2, 0, false), width, height,
                              NULL, NULL, pd)) {
            *output_size = XX_SETTLERSFT_BMP_HEADER_SIZE +
                           (uint64_t)width * height * 4U;
            return XX_SETTLERSFT_KIND_MASK;
        }
        if (xx_data_get_u16(p, 2, 0, false) == 1U && (int16_t)width > 0 &&
            (int16_t)height > 0 && xx_data_get_u16(p + 6, 2, 0, false) == 0U &&
            xx_data_get_u16(p + 8, 2, 0, false) == 0U &&
            (uint64_t)width * height + 10U == size) {
            *output_size = XX_SETTLERSFT_BMP_HEADER_SIZE +
                           XX_SETTLERSFT_BMP_PALETTE_SIZE + size - 10U;
            return XX_SETTLERSFT_KIND_BITMAP;
        }
    }
    if (size >= 5U && xx_rt_memcmp(p, "FORM", 4U) == 0)
        return XX_SETTLERSFT_KIND_XMI;
    if (size == XX_SETTLERSFT_PALETTE_SIZE)
        return XX_SETTLERSFT_KIND_PALETTE;
    return XX_SETTLERSFT_KIND_BIN;
}

static void xx_settlersft_bmp_header(uint8_t *out, uint32_t width,
                                     uint32_t height, uint32_t bits,
                                     uint32_t data_size, uint32_t offset) {
    xx_mem_zero(out, XX_SETTLERSFT_BMP_HEADER_SIZE);
    out[0] = 'B'; out[1] = 'M';
    xx_data_set_u32(out + 2, 4, 0, offset + data_size, false);
    xx_data_set_u32(out + 10, 4, 0, offset, false);
    xx_data_set_u32(out + 14, 4, 0, 40U, false);
    xx_data_set_u32(out + 18, 4, 0, width, false);
    xx_data_set_u32(out + 22, 4, 0, (uint32_t)(-(int32_t)height), false);
    xx_data_set_u16(out + 26, 2, 0, 1U, false);
    xx_data_set_u16(out + 28, 2, 0, (uint16_t)bits, false);
    xx_data_set_u32(out + 34, 4, 0, data_size, false);
    if (bits == 8U) {
        xx_data_set_u32(out + 46, 4, 0, 256U, false);
        xx_data_set_u32(out + 50, 4, 0, 256U, false);
    }
}


/* --------------------------------------------------------------- parse -- */

static xx_settlersft_stream *xx_settlersft_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_settlersft_stream *stream = NULL;
    uint8_t head[XX_SETTLERSFT_HEADER_SIZE];
    uint8_t *table = NULL;
    int64_t total;
    int64_t span;
    int64_t table_size;
    uint64_t count;
    uint64_t index;
    int64_t palette_offset = -1;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Two slots are the minimum the identifying arithmetic below needs. */
    if (span < XX_SETTLERSFT_HEADER_SIZE + 2 * XX_SETTLERSFT_ENTRY_SIZE) {
        return NULL;
    }
    if (!xx_settlersft_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }

    /* The only anchor a format with no magic offers: the declared size must
     * be the real size. */
    if ((int64_t)xx_data_get_u32(head, 4, 0, false) != span) return NULL;
    count = (uint64_t)xx_data_get_u32(head + 4, 4, 0, false);
    if (count < 2U || count > XX_SETTLERSFT_MAX_ENTRIES) return NULL;
    /* Bound the table against the real file before it is allocated. */
    table_size = (int64_t)(count * (uint64_t)XX_SETTLERSFT_ENTRY_SIZE);
    if (table_size > span - XX_SETTLERSFT_HEADER_SIZE) return NULL;

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return NULL;
    if (!xx_settlersft_read_at(self,
                               self->base_address + XX_SETTLERSFT_HEADER_SIZE,
                               table, (size_t)table_size)) {
        goto fail;
    }

    /* The reference implementation's recognition predicate, spelled out: the
     * first member starts right behind the table, the second starts right
     * behind the first, and neither is empty. */
    {
        int64_t size0 = (int64_t)(int32_t)xx_data_get_u32(table, 4, 0, false);
        int64_t offset0 = (int64_t)(int32_t)xx_data_get_u32(table + 4, 4, 0, false);
        int64_t size1 = (int64_t)(int32_t)xx_data_get_u32(table + 8, 4, 0, false);
        int64_t offset1 = (int64_t)(int32_t)xx_data_get_u32(table + 12, 4, 0, false);

        if (offset0 != XX_SETTLERSFT_HEADER_SIZE + table_size || size0 <= 0 ||
            size1 <= 0 || offset1 != offset0 + size0) {
            goto fail;
        }
    }

    stream = (xx_settlersft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0U; index < count; ++index) {
        const uint8_t *entry =
            table + (size_t)(index * (uint64_t)XX_SETTLERSFT_ENTRY_SIZE);
        int64_t size = (int64_t)(int32_t)xx_data_get_u32(entry, 4, 0, false);
        int64_t offset = (int64_t)(int32_t)xx_data_get_u32(entry + 4, 4, 0, false);
        xx_settlersft_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The palette is located the way the reference implementation does
         * it: the first slot whose size is exactly 768, in use or not. */
        if (size == XX_SETTLERSFT_PALETTE_SIZE && palette_offset < 0)
            palette_offset = offset;
        if (offset == 0) continue; /* unused slot */
        /* Every extent is bounded against the real file before it is
         * recorded, so a corrupt table cannot drive a read past the end. */
        if (offset < 0 || size < 0 || size > XX_SETTLERSFT_MAX_ENTRY_SIZE ||
            offset > span || size > span - offset) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.slot_index = (uint32_t)(index + 1U);
        member.header_offset = self->base_address +
                               XX_SETTLERSFT_HEADER_SIZE +
                               (int64_t)(index * XX_SETTLERSFT_ENTRY_SIZE);
        member.header_size = XX_SETTLERSFT_ENTRY_SIZE;
        member.data_offset = self->base_address + offset;
        member.packed_size = size;
        member.unpacked_size = (uint64_t)size;
        member.method = XX_SETTLERSFT_METHOD_STORE;
        if (!xx_settlersft_add(stream, &member)) goto fail;
    }

    /* Every archive of this family carries its palette; without it the image
     * members could not be produced and the reference decoder gives up too,
     * so its absence is treated as "not this format". */
    if (stream->count == 0U || palette_offset < 0 ||
        palette_offset > span - XX_SETTLERSFT_PALETTE_SIZE ||
        !xx_settlersft_read_at(self, self->base_address + palette_offset,
                               stream->palette, sizeof(stream->palette))) goto fail;

    for (index = 0U; index < stream->count; ++index) {
        xx_settlersft_member *member = &stream->items[index];
        uint8_t *packed = NULL;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (member->packed_size != 0) {
            packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
            if (!packed || !xx_settlersft_read_at(self, member->data_offset,
                                packed, (size_t)member->packed_size)) {
                xx_mem_free(packed);
                goto fail;
            }
        }
        member->kind = xx_settlersft_classify(packed,
                          (size_t)member->packed_size, &member->unpacked_size, pd);
        xx_mem_free(packed);
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        member->method = (member->kind == XX_SETTLERSFT_KIND_MASK ||
                          member->kind == XX_SETTLERSFT_KIND_BITMAP) ?
                         XX_SETTLERSFT_METHOD_IMAGE : XX_SETTLERSFT_METHOD_STORE;
        member->name = xx_settlersft_slot_name(member->slot_index, member->kind);
        if (!member->name) goto fail;
    }

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_settlersft_stream_free(stream);
    return NULL;
}

/* Read the bounded slot and convert image records to the same BMP layout as
 * the reference implementation. */
static bool xx_settlersft_decode(Abstractformat *self,
                             const xx_settlersft_member *member,
                             const uint8_t *palette, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *output;
    size_t size;
    uint32_t width, height;
    uint32_t image_size;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->packed_size == 0) return true;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    size = (size_t)member->packed_size;
    packed = (uint8_t *)xx_mem_alloc(size);
    if (!packed) return false;
    if (!xx_settlersft_read_at(self, member->data_offset, packed, size)) {
        xx_mem_free(packed);
        return false;
    }
    if (member->kind != XX_SETTLERSFT_KIND_MASK &&
        member->kind != XX_SETTLERSFT_KIND_BITMAP) {
        *out = packed;
        *out_size = size;
        return true;
    }
    if (size < 11U || !palette || member->unpacked_size > SIZE_MAX ||
        member->unpacked_size > UINT32_MAX) {
        xx_mem_free(packed);
        return false;
    }
    width = xx_data_get_u16(packed + 2, 2, 0, false);
    height = xx_data_get_u16(packed + 4, 2, 0, false);
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!output) { xx_mem_free(packed); return false; }
    if (member->kind == XX_SETTLERSFT_KIND_MASK) {
        image_size = width * height * 4U;
        xx_settlersft_bmp_header(output, width, height, 32U, image_size,
                                 XX_SETTLERSFT_BMP_HEADER_SIZE);
        if (!xx_settlersft_rle(packed + 10, size - 10U,
                               (int16_t)xx_data_get_u16(packed, 2, 0, false),
                               width, height, palette,
                               output + XX_SETTLERSFT_BMP_HEADER_SIZE, pd)) {
            xx_mem_free(output);
            xx_mem_free(packed);
            return false;
        }
    } else {
        uint32_t i;
        uint32_t offset = XX_SETTLERSFT_BMP_HEADER_SIZE +
                          XX_SETTLERSFT_BMP_PALETTE_SIZE;
        /* The original header reports aligned stride while pixel rows are
         * copied as stored.  Preserve that byte-level behaviour. */
        image_size = ((width + 3U) & ~3U) * height;
        xx_settlersft_bmp_header(output, width, height, 8U, image_size, offset);
        for (i = 0U; i < 256U; ++i) {
            uint8_t *colour = output + XX_SETTLERSFT_BMP_HEADER_SIZE + i * 4U;
            colour[0] = palette[i * 3U + 2U];
            colour[1] = palette[i * 3U + 1U];
            colour[2] = palette[i * 3U];
            colour[3] = 0U;
        }
        xx_rt_memcpy(output + offset, packed + 10, size - 10U);
    }
    xx_mem_free(packed);
    *out = output;
    *out_size = (size_t)member->unpacked_size;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_settlersft_init(xx_settlersft *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SETTLERSFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-settlers-ft");
    xx_format_set_extension(&archive->format, "pa");
    archive->format.check_is_valid = xx_settlersft_check_is_valid;
    archive->format.handle_base_info = xx_settlersft_handle_base_info;
    archive->format.get_format_size = xx_settlersft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_settlersft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_settlersft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_settlersft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_settlersft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_settlersft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_settlersft_free_archive_records_reading;
    archive->format.destroy = xx_settlersft_vtable_destroy;
}

xx_settlersft *xx_settlersft_create(xx_io_device *device, int64_t base_address) {
    xx_settlersft *archive = (xx_settlersft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_settlersft_init(archive, device, base_address);
    return archive;
}

void xx_settlersft_destroy(xx_settlersft *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_settlersft_free(xx_settlersft *archive) {
    if (!archive) return;
    xx_settlersft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_settlersft_vtable_destroy(Abstractformat *self) {
    xx_settlersft_destroy((xx_settlersft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_settlersft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_settlersft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_settlersft_parse(self, pd);
    if (!stream) return false;
    xx_settlersft_stream_free(stream);
    return true;
}

bool xx_settlersft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_settlersft *archive = (xx_settlersft *)self;
    xx_settlersft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_settlersft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_settlersft_stream_free(stream);
    return true;
}

int64_t xx_settlersft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_settlersft_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_settlersft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_settlersft_set_record(xx_archive_record *record,
                                 const xx_settlersft_member *member) {
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

static bool xx_settlersft_copy_options(xx_list_s *target,
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

static const xx_var *xx_settlersft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_settlersft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_settlersft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_settlersft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_settlersft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_settlersft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_settlersft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_settlersft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_settlersft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_settlersft_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_settlersft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_settlersft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_settlersft_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_settlersft_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_settlersft_stream *stream;
    const xx_settlersft_member *member;
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
    stream = (xx_settlersft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_settlersft_path_safe(member->name)) return false;

    path_option =
        xx_settlersft_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_settlersft_decode(self, member, stream->palette,
                                      &plain, &plain_size, pd);
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
        !xx_settlersft_decode(self, member, stream->palette,
                              &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_settlersft_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
