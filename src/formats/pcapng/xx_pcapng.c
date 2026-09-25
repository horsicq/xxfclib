/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pcapng/xx_pcapng.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_PCAPNG exists in the enum. */
#ifdef PCAPNG
#define XX_PCAPNG_FILE_TYPE XX_FILE_TYPE_PCAPNG
#else
#define XX_PCAPNG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The size walk visits every block, and a capture is mostly small packets,
 * so the full walk reads through a window instead of seeking per field. */
#define XX_PCAPNG_WINDOW_SIZE 65536U
/* How many blocks the walk visits between two looks at the stop flag. */
#define XX_PCAPNG_STOP_POLL 4096U

typedef struct xx_pcapng_source_s {
    xx_io_device *device;
    int64_t limit;          /* device size; nothing at or past it is read */
    uint8_t *buffer;        /* NULL: every fetch is a direct read */
    size_t capacity;
    int64_t window_offset;
    size_t window_size;
} xx_pcapng_source;

typedef struct xx_pcapng_private_s {
    int64_t input_size;
    int64_t start;
    int64_t end;
    bool big_endian;
    uint16_t major_version;
    uint16_t minor_version;
    uint64_t section_length;
    uint32_t shb_size;
    uint64_t blocks;
    uint64_t sections;
    uint64_t interfaces;
    uint64_t packets;
    uint32_t link_type;
} xx_pcapng_private;

static void xx_pcapng_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64 and a
 * capture is easily larger than 2 GiB. */
static bool xx_pcapng_read_at(xx_io_device *device, int64_t offset, void *data,
                              size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Copy `size` (1..8) bytes at absolute `offset`, which must lie wholly
 * inside the device. */
static bool xx_pcapng_fetch(xx_pcapng_source *source, int64_t offset,
                            void *out, size_t size) {
    int64_t available;
    size_t want;
    if (!source || !out || size == 0U || size > 8U || offset < 0 ||
        source->limit < (int64_t)size ||
        offset > source->limit - (int64_t)size) {
        return false;
    }
    if (!source->buffer || source->capacity < size) {
        return xx_pcapng_read_at(source->device, offset, out, size);
    }
    if (source->window_size < size || offset < source->window_offset ||
        offset - source->window_offset >
            (int64_t)(source->window_size - size)) {
        available = source->limit - offset;
        want = source->capacity;
        if (available < (int64_t)want) want = (size_t)available;
        source->window_size = 0U;
        if (!xx_pcapng_read_at(source->device, offset, source->buffer, want)) {
            return false;
        }
        source->window_offset = offset;
        source->window_size = want;
    }
    xx_rt_memcpy(out, source->buffer + (size_t)(offset - source->window_offset),
                 size);
    return true;
}

/*
 * One generic block, exactly as binwalk's structures/pcap.rs
 * parse_pcapng_block() accepts it:
 *   - eight header bytes are present;
 *   - bit 31 of the block type is clear;
 *   - the u32 at (offset + length - 4) equals the length, and lies inside
 *     the data.
 * binwalk computes `length - 4` in wrapping usize arithmetic, so a length
 * below 4 yields an empty slice lookup and fails; it is refused here
 * explicitly.  Lengths 4..11 and lengths that are not a multiple of four
 * are NOT refused, because binwalk does not refuse them and the walk must
 * stop where binwalk's stops.  Every accepted length is >= 4, so the walk
 * always advances.
 */
static bool xx_pcapng_read_block(xx_pcapng_source *source, int64_t offset,
                                 bool big_endian, uint32_t *type,
                                 uint32_t *length) {
    uint8_t header[XX_PCAPNG_BLOCK_HEADER_SIZE];
    uint8_t trailer[XX_PCAPNG_BLOCK_TRAILER_SIZE];
    uint32_t block_type;
    uint32_t block_length;
    if (!source || offset < 0 || offset > source->limit ||
        source->limit - offset < (int64_t)XX_PCAPNG_BLOCK_HEADER_SIZE ||
        !xx_pcapng_fetch(source, offset, header, sizeof(header))) {
        return false;
    }
    block_type = xx_data_get_u32(header, sizeof(header), 0U, big_endian);
    block_length = xx_data_get_u32(header, sizeof(header), 4U, big_endian);
    if ((block_type & XX_PCAPNG_BLOCK_TYPE_RESERVED_MASK) != 0U ||
        block_length < XX_PCAPNG_BLOCK_TRAILER_SIZE ||
        (int64_t)block_length > source->limit - offset ||
        !xx_pcapng_fetch(source,
                         offset + (int64_t)block_length -
                             (int64_t)XX_PCAPNG_BLOCK_TRAILER_SIZE,
                         trailer, sizeof(trailer)) ||
        xx_data_get_u32(trailer, sizeof(trailer), 0U, big_endian) !=
            block_length) {
        return false;
    }
    if (type) *type = block_type;
    if (length) *length = block_length;
    return true;
}

static void xx_pcapng_private_reset(xx_pcapng_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->start = -1;
    parsed->end = -1;
    parsed->link_type = XX_PCAPNG_LINK_TYPE_NONE;
}

/*
 * The leading Section Header Block, then the block right after it.
 *
 * binwalk (structures/pcap.rs parse_pcapng_section_block) wants:
 *   - the 0x0A0D0D0A type;
 *   - the byte-order magic in one of its two spellings, which fixes the
 *     byte order for everything that follows;
 *   - the SHB to pass parse_pcapng_block() in that byte order.
 * and extractors/pcap.rs pcapng_carver() then wants at least one more
 * valid block (MIN_BLOCK_COUNT = 2), starting strictly before the end of
 * the data.
 *
 * On top of that, three deliberate tightenings for the leading SHB only,
 * none of which a conforming writer can violate:
 *   - block length >= 28, the smallest SHB the specification allows (binwalk
 *     would take e.g. a length of 8, whose trailer is the length field
 *     itself, and then walk on from inside the SHB);
 *   - block length a multiple of 4, as the specification requires of every
 *     block;
 *   - major version 1, the only one defined; readers are told not to
 *     interpret a section with any other major version.
 * The walk after the SHB is binwalk's, unmodified, so on every file both
 * accept, both measure the same length.
 */
static bool xx_pcapng_parse_head(xx_pcapng_source *source, int64_t start,
                                 xx_pcapng_private *parsed) {
    uint8_t shb[XX_PCAPNG_SHB_FIXED_SIZE];
    uint32_t magic;
    uint32_t type = 0U;
    uint32_t length = 0U;
    uint32_t second_type = 0U;
    uint32_t second_length = 0U;
    int64_t second;
    bool big_endian;
    if (!source || !parsed || start < 0 || source->limit < start ||
        source->limit - start < (int64_t)XX_PCAPNG_SHB_MIN_SIZE) {
        return false;
    }
    /* The fixed part goes through the same fetch path in 8-byte pieces, so
     * the window (when there is one) is filled by the first of them. */
    if (!xx_pcapng_fetch(source, start, shb, 8U) ||
        !xx_pcapng_fetch(source, start + 8, shb + 8, 8U) ||
        !xx_pcapng_fetch(source, start + 16, shb + 16, 8U)) {
        return false;
    }
    if (xx_data_get_u32(shb, sizeof(shb), 0U, false) != XX_PCAPNG_SHB_TYPE) {
        return false;
    }
    magic = xx_data_get_u32(shb, sizeof(shb), 8U, false);
    if (magic == XX_PCAPNG_BYTE_ORDER_MAGIC) {
        big_endian = false;
    } else if (magic == XX_PCAPNG_BYTE_ORDER_MAGIC_SWAPPED) {
        big_endian = true;
    } else {
        return false;
    }
    parsed->major_version = xx_data_get_u16(shb, sizeof(shb), 12U, big_endian);
    parsed->minor_version = xx_data_get_u16(shb, sizeof(shb), 14U, big_endian);
    parsed->section_length =
        xx_data_get_u64(shb, sizeof(shb), 16U, big_endian);
    if (parsed->major_version != XX_PCAPNG_MAJOR_VERSION) return false;
    if (!xx_pcapng_read_block(source, start, big_endian, &type, &length) ||
        type != XX_PCAPNG_SHB_TYPE || length < XX_PCAPNG_SHB_MIN_SIZE ||
        (length & 3U) != 0U) {
        return false;
    }
    /* binwalk's is_offset_safe(): the next block starts before the end. */
    second = start + (int64_t)length;
    if (second >= source->limit ||
        !xx_pcapng_read_block(source, second, big_endian, &second_type,
                              &second_length)) {
        return false;
    }
    (void)second_type;
    (void)second_length;
    parsed->big_endian = big_endian;
    parsed->shb_size = length;
    return true;
}

/* Walk every block after the SHB, in the first section's byte order, until
 * one fails parse_pcapng_block() or the data ends -- pcapng_carver()'s loop.
 * A later SHB written in the same byte order is just another block to it;
 * one written in the other byte order fails (its length reads byte-swapped)
 * and ends the capture there, as it does for binwalk. */
static bool xx_pcapng_walk(xx_pcapng_source *source, xx_pcapng_private *parsed,
                           xx_pd_struct *pd) {
    int64_t position;
    uint32_t type;
    uint32_t length;
    uint8_t link[2];
    if (!source || !parsed || parsed->shb_size == 0U) return false;
    position = parsed->start + (int64_t)parsed->shb_size;
    parsed->blocks = 1U;
    parsed->sections = 1U;
    while (position < source->limit) {
        if ((parsed->blocks % XX_PCAPNG_STOP_POLL) == 0U && pd &&
            xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_pcapng_read_block(source, position, parsed->big_endian, &type,
                                  &length)) {
            break;
        }
        ++parsed->blocks;
        switch (type) {
        case XX_PCAPNG_SHB_TYPE:
            ++parsed->sections;
            break;
        case XX_PCAPNG_BLOCK_IDB:
            ++parsed->interfaces;
            /* type, length, link type, reserved, snap length, trailer */
            if (parsed->link_type == XX_PCAPNG_LINK_TYPE_NONE &&
                length >= 20U &&
                xx_pcapng_fetch(source, position + 8, link, sizeof(link))) {
                parsed->link_type = xx_data_get_u16(link, sizeof(link), 0U,
                                                    parsed->big_endian);
            }
            break;
        case XX_PCAPNG_BLOCK_PB:
        case XX_PCAPNG_BLOCK_SPB:
        case XX_PCAPNG_BLOCK_EPB:
            ++parsed->packets;
            break;
        default:
            break;
        }
        /* read_block() proved 4 <= length <= limit - position. */
        position += (int64_t)length;
    }
    if (parsed->blocks < 2U) return false;
    parsed->end = position;
    return true;
}

static bool xx_pcapng_parse(Abstractformat *self, xx_pcapng_private *parsed,
                            bool full, xx_pd_struct *pd) {
    xx_pcapng_source source;
    bool result = false;
    xx_pcapng_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_mem_zero(&source, sizeof(source));
    source.device = self->device;
    source.limit = xx_io_total_size(self->device);
    parsed->input_size = source.limit;
    parsed->start = self->base_address;
    if (source.limit < 0 || self->base_address > source.limit) goto done;
    if (full) {
        /* Optional: without the window every fetch is a direct read. */
        source.buffer = (uint8_t *)xx_mem_alloc(XX_PCAPNG_WINDOW_SIZE);
        source.capacity = source.buffer ? XX_PCAPNG_WINDOW_SIZE : 0U;
    }
    if (!xx_pcapng_parse_head(&source, self->base_address, parsed)) goto done;
    if (full && !xx_pcapng_walk(&source, parsed, pd)) goto done;
    result = true;
done:
    if (source.buffer) xx_mem_free(source.buffer);
    if (!result) xx_pcapng_private_reset(parsed);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

static void xx_pcapng_clear_fields(xx_pcapng *pcapng) {
    pcapng->big_endian = false;
    pcapng->major_version = 0U;
    pcapng->minor_version = 0U;
    pcapng->section_length = 0U;
    pcapng->shb_size = 0U;
    pcapng->number_of_blocks = 0U;
    pcapng->number_of_sections = 0U;
    pcapng->number_of_interfaces = 0U;
    pcapng->number_of_packets = 0U;
    pcapng->link_type = XX_PCAPNG_LINK_TYPE_NONE;
    pcapng->capture_end = -1;
}

void xx_pcapng_init(xx_pcapng *pcapng, xx_io_device *dev,
                    int64_t base_address) {
    if (!pcapng) return;
    xx_mem_zero(pcapng, sizeof(*pcapng));
    xx_format_init(&pcapng->format, dev, base_address);
    pcapng->format.endian = XX_ENDIAN_LITTLE;
    pcapng->format.file_type = XX_PCAPNG_FILE_TYPE;
    pcapng->format.format_type = XX_TYPE_RAW;
    pcapng->format.is_archive = false;
    xx_format_set_mime_type(&pcapng->format, "application/x-pcapng");
    xx_format_set_extension(&pcapng->format, "pcapng");
    pcapng->format.check_is_valid = xx_pcapng_check_is_valid;
    pcapng->format.handle_base_info = xx_pcapng_handle_base_info;
    pcapng->format.get_format_size = xx_pcapng_get_format_size;
    pcapng->format.destroy = xx_pcapng_vtable_destroy;
    xx_pcapng_clear_fields(pcapng);
}

xx_pcapng *xx_pcapng_create(xx_io_device *dev, int64_t base_address) {
    xx_pcapng *pcapng = (xx_pcapng *)xx_mem_alloc(sizeof(*pcapng));
    if (pcapng) xx_pcapng_init(pcapng, dev, base_address);
    return pcapng;
}

void xx_pcapng_destroy(xx_pcapng *pcapng) {
    if (!pcapng) return;
    /* Nothing here owns heap memory beyond the base structure's extras; the
     * walk's read window lives only for the duration of one parse. */
    xx_format_cleanup_extra_parameters(&pcapng->format);
}

static void xx_pcapng_vtable_destroy(Abstractformat *self) {
    xx_pcapng_destroy((xx_pcapng *)self);
}

void xx_pcapng_free(xx_pcapng *pcapng) {
    if (!pcapng) return;
    xx_pcapng_destroy(pcapng);
    xx_mem_free(pcapng);
}

/* Bounded: the SHB and the one block after it, at most six small reads.
 * This is exactly binwalk's acceptance test (a valid SHB plus a second
 * valid block); the full walk only decides the length. */
bool xx_pcapng_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcapng_private parsed;
    return xx_pcapng_parse(self, &parsed, false, pd);
}

bool xx_pcapng_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcapng_private parsed;
    xx_pcapng *pcapng = (xx_pcapng *)self;
    if (!self) return false;
    if (!xx_pcapng_parse(self, &parsed, true, pd)) {
        xx_pcapng_clear_fields(pcapng);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    pcapng->big_endian = parsed.big_endian;
    pcapng->major_version = parsed.major_version;
    pcapng->minor_version = parsed.minor_version;
    pcapng->section_length = parsed.section_length;
    pcapng->shb_size = parsed.shb_size;
    pcapng->number_of_blocks = parsed.blocks;
    pcapng->number_of_sections = parsed.sections;
    pcapng->number_of_interfaces = parsed.interfaces;
    pcapng->number_of_packets = parsed.packets;
    pcapng->link_type = parsed.link_type;
    pcapng->capture_end = parsed.end;
    self->endian = parsed.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    /* binwalk's carve length: from the SHB to the end of the last block the
     * walk accepted.  Whatever follows is overlay. */
    self->format_size = parsed.end - self->base_address;
    if (parsed.input_size > parsed.end) {
        self->overlay_offset = parsed.end;
        self->overlay_size = parsed.input_size - parsed.end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_pcapng_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

bool xx_pcapng_is_big_endian(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->big_endian : false;
}
uint16_t xx_pcapng_get_major_version(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->major_version : 0U;
}
uint16_t xx_pcapng_get_minor_version(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->minor_version : 0U;
}
uint64_t xx_pcapng_get_number_of_blocks(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->number_of_blocks : 0U;
}
uint64_t xx_pcapng_get_number_of_sections(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->number_of_sections : 0U;
}
uint64_t xx_pcapng_get_number_of_interfaces(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->number_of_interfaces : 0U;
}
uint64_t xx_pcapng_get_number_of_packets(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->number_of_packets : 0U;
}
uint32_t xx_pcapng_get_link_type(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->link_type : XX_PCAPNG_LINK_TYPE_NONE;
}
int64_t xx_pcapng_get_capture_end(const xx_pcapng *pcapng) {
    return pcapng ? pcapng->capture_end : -1;
}
