/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the "TX" help-text overlay the Sydex CopyQM family of
 * DOS tools appends to its own executable image.  The program only DISPLAYS
 * these screens, so the overlay is a data container, not a self-extracting
 * archive, and it is NOT a CopyQM disk image behind an executable prefix.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/copyqmexe/xx_copyqmexe.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef COPYQMEXE
#define XX_COPYQMEXE_FILE_TYPE XX_FILE_TYPE_COPYQMEXE
#else
#define XX_COPYQMEXE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define COPYQMEXE_MAX_MEMBERS 65536U
#define COPYQMEXE_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct copyqmexe_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} copyqmexe_member;

typedef struct copyqmexe_stream_s {
    copyqmexe_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} copyqmexe_stream;

static uint16_t copyqmexe_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t copyqmexe_le32(const uint8_t *b) {
    return (uint32_t)copyqmexe_le16(b) | ((uint32_t)copyqmexe_le16(b + 2U) << 16U);
}

static bool copyqmexe_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *copyqmexe_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void copyqmexe_stream_free(void *opaque) {
    copyqmexe_stream *stream = (copyqmexe_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool copyqmexe_add_member(copyqmexe_stream *stream, const copyqmexe_member *member) {
    copyqmexe_member *grown;
    if (!stream || !member || stream->count >= COPYQMEXE_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (copyqmexe_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define COPYQMEXE_MAX_OVERLAY (64 * 1024 * 1024)
#define COPYQMEXE_MAX_SCREENS 1000U
#define COPYQMEXE_MAX_LINES 100000U

typedef struct copyqmexe_layout_s {
    uint8_t *overlay;
    int64_t overlay_size;
    int64_t image_end;
    int64_t node_table;
    int64_t directory;
    int64_t data_start;
    int64_t data_size;
    int64_t stride;
    uint32_t node_count;
    uint32_t screen_count;
} copyqmexe_layout;

static void copyqmexe_layout_free(copyqmexe_layout *layout) {
    if (layout->overlay) xx_mem_free(layout->overlay);
    layout->overlay = NULL;
}

static uint32_t copyqmexe_row_lines(const copyqmexe_layout *layout,
                                    uint32_t screen) {
    const int64_t row = layout->directory + (int64_t)screen * layout->stride;
    return copyqmexe_le16(layout->overlay + row + (layout->stride == 8 ? 2 : 0));
}

static uint32_t copyqmexe_row_start(const copyqmexe_layout *layout,
                                    uint32_t screen) {
    const int64_t row = layout->directory + (int64_t)screen * layout->stride;
    return copyqmexe_le32(layout->overlay + row + (layout->stride == 8 ? 4 : 2));
}

/* The Sydex tools (COPYQM, TELEDISK, CQMENU, FORMATQM and their installers)
 * append their help text to the DOS image as a "TX" overlay: a Huffman node
 * table, a directory of screens, and a body of variable length line records.
 * This is a data overlay the program only DISPLAYS - it unpacks nothing onto
 * the filesystem - so it is a container, not a self-extracting archive.
 *
 * Note that this is NOT a CopyQM .CQM disk image behind an executable prefix;
 * it shares only the vendor. */
static bool copyqmexe_open(Abstractformat *format, copyqmexe_layout *layout) {
    uint8_t dos[0x40];
    uint8_t *overlay = NULL;
    int64_t total, size, image_end, overlay_size, node_table, directory;
    uint32_t last_page, pages, node_count, screen_count, body_size, index;
    int64_t strides[2];
    int32_t variant, valid_count = 0;
    xx_mem_zero(layout, sizeof(*layout));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 0x40 ||
        !copyqmexe_read_at(format->device, format->base_address, dos,
                           sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    last_page = copyqmexe_le16(dos + 2U);
    pages = copyqmexe_le16(dos + 4U);
    if (pages == 0U || last_page > 511U) return false;
    image_end = (int64_t)(pages - 1U) * 512 +
                (int64_t)(last_page != 0U ? last_page : 512U);
    if (image_end < 0x40 || image_end > size || size - image_end < 22 ||
        size - image_end > COPYQMEXE_MAX_OVERLAY)
        return false;
    overlay_size = size - image_end;
    overlay = (uint8_t *)xx_mem_alloc((size_t)overlay_size);
    if (!overlay) return false;
    if (!copyqmexe_read_at(format->device, format->base_address + image_end,
                           overlay, (size_t)overlay_size) ||
        overlay[0] != 'T' || overlay[1] != 'X')
        goto fail;
    node_count = copyqmexe_le16(overlay + 2U);
    screen_count = copyqmexe_le16(overlay + 4U);
    body_size = copyqmexe_le32(overlay + 6U);
    if (node_count < 3U || (node_count & 1U) == 0U || node_count > 255U ||
        screen_count == 0U || screen_count > COPYQMEXE_MAX_SCREENS ||
        (int64_t)body_size != overlay_size - 10 ||
        copyqmexe_le16(overlay + 10U) != node_count)
        goto fail;
    node_table = 12;
    directory = node_table + (int64_t)node_count * 2;
    /* Two directory layouts exist: the newer one prepends a word to each
     * record, so the stride is 8 with the line count at +2 and the start at
     * +4; the older one is 6 with them at +0 and +2.  Probe both and require
     * exactly one to validate. */
    strides[0] = 8;
    strides[1] = 6;
    for (variant = 0; variant < 2; ++variant) {
        const int64_t stride = strides[variant];
        const int64_t start = directory + (int64_t)screen_count * stride;
        int64_t body;
        uint32_t previous = 0U;
        bool ok = true;
        if (start < directory || start >= overlay_size) continue;
        body = overlay_size - start;
        for (index = 0U; ok && index < screen_count; ++index) {
            const int64_t row = directory + (int64_t)index * stride;
            uint32_t lines, value;
            if (row + stride > overlay_size) {
                ok = false;
                break;
            }
            lines = copyqmexe_le16(overlay + row + (stride == 8 ? 2 : 0));
            value = copyqmexe_le32(overlay + row + (stride == 8 ? 4 : 2));
            if (lines == 0U || lines > COPYQMEXE_MAX_LINES ||
                (int64_t)value >= body || (index > 0U && value <= previous))
                ok = false;
            previous = value;
        }
        if (ok) {
            ++valid_count;
            layout->stride = stride;
            layout->data_start = start;
            layout->data_size = body;
        }
    }
    if (valid_count != 1) goto fail;
    /* A leaf node has a zero first byte; an internal node holds two one-based
     * child indices.  Reject bad indices and a leaf root before decoding. */
    for (index = 0U; index < node_count; ++index) {
        const uint8_t a = overlay[node_table + (int64_t)index * 2];
        const uint8_t b = overlay[node_table + (int64_t)index * 2 + 1];
        if (a != 0U && (a > node_count || b == 0U || b > node_count)) goto fail;
    }
    if (overlay[node_table + (int64_t)(node_count - 1U) * 2] == 0U) goto fail;
    layout->overlay = overlay;
    layout->overlay_size = overlay_size;
    layout->image_end = image_end;
    layout->node_table = node_table;
    layout->directory = directory;
    layout->node_count = node_count;
    layout->screen_count = screen_count;
    /* Newer tools put a six-byte screen geometry prefix (the same word three
     * times) in front of the body; older ones start the first line at once. */
    if (copyqmexe_row_start(layout, 0U) != 0U) {
        uint16_t geometry;
        if (copyqmexe_row_start(layout, 0U) != 6U || layout->data_size < 6)
            goto fail;
        geometry = copyqmexe_le16(overlay + layout->data_start);
        if (geometry == 0U ||
            copyqmexe_le16(overlay + layout->data_start + 2) != geometry ||
            copyqmexe_le16(overlay + layout->data_start + 4) != geometry) {
            layout->overlay = NULL;
            goto fail;
        }
    }
    return true;
fail:
    if (overlay) xx_mem_free(overlay);
    layout->overlay = NULL;
    return false;
}

/* Expand one screen.  `output` is NULL while the screens are being measured;
 * the walk is identical either way, so a screen can never be sized with one
 * decode and written with another. */
static bool copyqmexe_screen(const copyqmexe_layout *layout, uint32_t screen,
                             uint8_t *output, size_t output_size,
                             size_t *produced) {
    const int64_t start = (int64_t)copyqmexe_row_start(layout, screen);
    const int64_t end = (screen + 1U < layout->screen_count)
                            ? (int64_t)copyqmexe_row_start(layout, screen + 1U)
                            : layout->data_size;
    const uint32_t lines = copyqmexe_row_lines(layout, screen);
    int64_t cursor = start;
    size_t total = 0U;
    uint32_t line;
    if (start < 0 || end <= start || end > layout->data_size) return false;
    for (line = 0U; line < lines; ++line) {
        int64_t bit_data;
        int32_t bit_count, bit_position = 0;
        uint32_t record_size;
        bool terminated = false;
        bool indent_pending = false;
        if (cursor >= end) return false;
        record_size = layout->overlay[layout->data_start + cursor];
        if (record_size < 2U || (int64_t)record_size > end - cursor)
            return false;
        bit_data = layout->data_start + cursor + 1;
        bit_count = (int32_t)(record_size - 1U) * 8;
        while (!terminated) {
            uint32_t node = layout->node_count;
            uint32_t depth = 0U;
            for (;;) {
                int64_t node_offset;
                uint8_t a, b, packed;
                bool bit;
                if (node == 0U || node > layout->node_count ||
                    ++depth > layout->node_count)
                    return false;
                node_offset = layout->node_table + (int64_t)(node - 1U) * 2;
                a = layout->overlay[node_offset];
                b = layout->overlay[node_offset + 1];
                if (a == 0U) {
                    const uint8_t symbol = b;
                    size_t add = 0U;
                    uint8_t fill = 0U;
                    if (indent_pending) {
                        if (symbol < 0x20U) return false;
                        add = (size_t)(symbol - 0x20U);
                        fill = ' ';
                        indent_pending = false;
                    } else if (symbol == 0x09U) {
                        indent_pending = true;
                    } else if (symbol == 0x02U) {
                        /* Display attribute toggle: deliberately dropped. */
                    } else if (symbol == 0x0aU) {
                        add = 1U;
                        fill = '\n';
                        terminated = true;
                    } else if (symbol < 0x20U && symbol != 0x0cU) {
                        return false;
                    } else {
                        add = 1U;
                        fill = symbol;
                    }
                    if (add != 0U) {
                        if (add > COPYQMEXE_MAX_OUTPUT - total) return false;
                        if (output) {
                            size_t at;
                            if (total + add > output_size) return false;
                            for (at = 0U; at < add; ++at) output[total + at] = fill;
                        }
                        total += add;
                    }
                    break;
                }
                if (bit_position >= bit_count) return false;
                packed = layout->overlay[bit_data + (bit_position >> 3)];
                bit = (packed & (0x80U >> (bit_position & 7))) != 0U;
                ++bit_position;
                node = bit ? b : a;
            }
        }
        if (indent_pending) return false;
        cursor += (int64_t)record_size;
    }
    if (cursor != end) return false;
    if (produced) *produced = total;
    return true;
}

static bool copyqmexe_parse(Abstractformat *format, copyqmexe_stream **result) {
    copyqmexe_layout layout;
    copyqmexe_stream *stream;
    uint32_t screen;
    if (!result || !copyqmexe_open(format, &layout)) return false;
    stream = (copyqmexe_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        copyqmexe_layout_free(&layout);
        return false;
    }
    for (screen = 0U; screen < layout.screen_count; ++screen) {
        copyqmexe_member member;
        size_t produced = 0U;
        int64_t start = (int64_t)copyqmexe_row_start(&layout, screen);
        int64_t end = (screen + 1U < layout.screen_count)
                          ? (int64_t)copyqmexe_row_start(&layout, screen + 1U)
                          : layout.data_size;
        if (!copyqmexe_screen(&layout, screen, NULL, 0U, &produced)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = copyqmexe_make_name("screen", (int)(screen + 1U), -1,
                                          ".txt");
        member.header_offset = format->base_address + layout.image_end;
        member.header_size = 12;
        member.data_offset = format->base_address + layout.image_end +
                             layout.data_start + start;
        member.packed_size = end - start;
        member.unpacked_size = (uint64_t)produced;
        member.method = screen;
        member.decode = true;
        if (!member.name || !copyqmexe_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
    }
    copyqmexe_layout_free(&layout);
    stream->archive_size = layout.image_end + layout.overlay_size;
    *result = stream;
    return true;
fail:
    copyqmexe_layout_free(&layout);
    copyqmexe_stream_free(stream);
    return false;
}

static bool copyqmexe_decode(Abstractformat *format,
                             const copyqmexe_member *member, uint8_t **plain,
                             size_t *plain_size) {
    copyqmexe_layout layout;
    uint8_t *output;
    size_t produced = 0U;
    if (member->unpacked_size > COPYQMEXE_MAX_OUTPUT ||
        !copyqmexe_open(format, &layout))
        return false;
    if (member->method >= layout.screen_count) {
        copyqmexe_layout_free(&layout);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc(member->unpacked_size != 0U
                                         ? (size_t)member->unpacked_size : 1U);
    if (!output ||
        !copyqmexe_screen(&layout, member->method, output,
                          (size_t)member->unpacked_size, &produced) ||
        produced != (size_t)member->unpacked_size) {
        if (output) xx_mem_free(output);
        copyqmexe_layout_free(&layout);
        return false;
    }
    copyqmexe_layout_free(&layout);
    *plain = output;
    *plain_size = produced;
    return true;
}

static bool copyqmexe_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *copyqmexe_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool copyqmexe_set_record(xx_archive_record *record,
                           const copyqmexe_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool copyqmexe_extract(Abstractformat *format, const copyqmexe_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return copyqmexe_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > COPYQMEXE_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !copyqmexe_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_copyqmexe_init(xx_copyqmexe *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_COPYQMEXE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_copyqmexe_check_is_valid;
    archive->format.handle_base_info = xx_copyqmexe_handle_base_info;
    archive->format.get_format_size = xx_copyqmexe_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_copyqmexe_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_copyqmexe_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_copyqmexe_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_copyqmexe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_copyqmexe_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_copyqmexe_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_copyqmexe *xx_copyqmexe_create(xx_io_device *device, int64_t base_address) {
    xx_copyqmexe *archive = (xx_copyqmexe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_copyqmexe_init(archive, device, base_address);
    return archive;
}

void xx_copyqmexe_destroy(xx_copyqmexe *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_copyqmexe_free(xx_copyqmexe *archive) {
    if (!archive) return;
    xx_copyqmexe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_copyqmexe_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    copyqmexe_stream *stream;
    (void)pd;
    if (!copyqmexe_parse(format, &stream)) return false;
    copyqmexe_stream_free(stream);
    return true;
}

bool xx_copyqmexe_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    copyqmexe_stream *stream;
    xx_copyqmexe *archive;
    (void)pd;
    if (!format || !copyqmexe_parse(format, &stream)) return false;
    archive = (xx_copyqmexe *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    copyqmexe_stream_free(stream);
    return true;
}

int64_t xx_copyqmexe_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copyqmexe_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_copyqmexe_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copyqmexe_handle_base_info(format, pd))
               ? ((xx_copyqmexe *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_copyqmexe_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    copyqmexe_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!copyqmexe_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        copyqmexe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = copyqmexe_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!copyqmexe_copy_options(&state->options, options) ||
        !copyqmexe_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_copyqmexe_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_copyqmexe_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    copyqmexe_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (copyqmexe_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = copyqmexe_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_copyqmexe_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    copyqmexe_stream *stream;
    copyqmexe_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (copyqmexe_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!copyqmexe_extract(format, member, &plain, &plain_size)) goto done;
    path_option = copyqmexe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
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

void xx_copyqmexe_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
