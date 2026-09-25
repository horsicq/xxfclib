/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TI-99/4A ARC (.ARK) archives.
 *
 * An .ARK has no magic of its own. What it may have is a 0x80-byte host
 * wrapper, in one of two shapes, which is validated arithmetically:
 *
 *   TIFILES: 0x00 = 0x07, 0x01..0x07 = "TIFILES", u16 BE sector count at
 *            0x08, flags at 0x0a. The file size must be exactly
 *            count * 0x100 + 0x80, or count+1 sectors for a writer that
 *            counted the header as a sector.
 *   FIAD:    u16 LE at 0x0a is zero, 0x1c..0x7f are all zero, u16 BE sector
 *            count at 0x0e is non-zero, and the file size is exactly
 *            count * 0x100 + 0x80.
 *
 * Bit 1 (0x02) of the wrapper's flag byte says the payload is LZW
 * compressed. Without a wrapper the payload is the whole file and is stored.
 *
 *   catalogue sector, 0x100 bytes, at the START of the payload:
 *     0x00  fourteen 18-byte entries
 *     0xfc  "END!" on the last sector, four zero bytes on every other
 *
 *   catalogue entry, 18 bytes:
 *     0x00  name, 10 bytes, space or NUL padded
 *     0x0a  8 bytes of TI file attributes, of which
 *     0x0c  u16 BE length of this member in 256-byte sectors
 *
 * An all-zero entry is a free slot and consumes no data; advancing the data
 * cursor for one would shift every member behind it.
 *
 * The catalogue lives INSIDE the compressed stream, so a member of a
 * compressed archive is not a byte range of the file. Walking the catalogue
 * means expanding a PREFIX of the stream - hence the expand entry point
 * rather than the strict whole-stream decoder, because stopping at the probe
 * limit is the intended outcome there, not an error.
 *
 * Extraction prepends the 0x80-byte TIFILES header the member needs and that
 * the catalogue entry is the only source of: entry bytes 0x00..0x09 go to
 * 0x00 and entry bytes 0x0a..0x11 go to 0x0c. The two-byte gap at 0x0a is
 * deliberate - the entry's tail is written at 0x0c, not at 0x0a.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ti99arc/xx_ti99arc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ti99arc/xx_ti99arc.h"
#include "xxfclib/algo/sclsectors/xx_sclsectors.h"

#include <stdio.h>

#define XX_TI99ARC_COPY_CHUNK (64 * 1024)

typedef struct xx_ti99arc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ti99arc_member;

typedef struct xx_ti99arc_stream_s {
    xx_ti99arc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ti99arc_stream;

static void xx_ti99arc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ti99arc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ti99arc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ti99arc_path_safe(const char *name) {
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

static void xx_ti99arc_stream_free(void *pointer) {
    xx_ti99arc_stream *stream = (xx_ti99arc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ti99arc_add(xx_ti99arc_stream *stream,
                          const xx_ti99arc_member *member) {
    xx_ti99arc_member *grown = (xx_ti99arc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_TI99ARC_WRAPPER_SIZE 0x80
#define XX_TI99ARC_SECTOR_SIZE 0x100
#define XX_TI99ARC_ENTRY_SIZE 0x12
#define XX_TI99ARC_ENTRIES_PER_SECTOR 14
#define XX_TI99ARC_TAIL_OFFSET 0xFC
#define XX_TI99ARC_MAX_SECTORS 0x400
#define XX_TI99ARC_MAX_FILE_SIZE 0x1000000
#define XX_TI99ARC_FLAG_COMPRESSED 0x02U
#define XX_TI99ARC_MAX_MEMBERS (XX_TI99ARC_MAX_SECTORS * \
                                XX_TI99ARC_ENTRIES_PER_SECTOR)
#define XX_TI99ARC_PROPS_SIZE (0x10 + XX_TI99ARC_WRAPPER_SIZE)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ti99arc_be16(const uint8_t *data);
static uint16_t xx_ti99arc_le16(const uint8_t *data);
static bool xx_ti99arc_chain(const uint8_t *catalogue, size_t catalogue_size, int64_t *out_size);
static bool xx_ti99arc_entry_free(const uint8_t *entry);
static bool xx_ti99arc_entry_name(const uint8_t *entry, char **out_name);
static bool xx_ti99arc_locate(const uint8_t *catalogue, size_t catalogue_size, int64_t entry_offset, int64_t *out_offset, uint8_t *out_entry);
static bool xx_ti99arc_is_tifiles(const uint8_t *wrapper, int64_t size);
static bool xx_ti99arc_is_fiad(const uint8_t *wrapper, int64_t size);
static xx_ti99arc_stream *xx_ti99arc_parse(Abstractformat *self, xx_pd_struct *pd);
static void xx_ti99arc_put_le32(uint8_t *data, uint32_t value);
static void xx_ti99arc_tifiles_prefix(const uint8_t *entry, uint8_t *prefix);
static bool xx_ti99arc_decode(Abstractformat *self, const xx_ti99arc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static uint16_t xx_ti99arc_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint16_t xx_ti99arc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* Walk the sector chain and return the catalogue's byte length.
 *
 * Every sector but the last ends in four zero bytes; the last ends in "END!".
 * Anything else terminates the walk with a rejection. For a format with no
 * magic at all, this chain is the entire false-positive defence: a file that
 * is not an .ARK has to produce a run of sectors whose 0xfc..0xff are zero
 * and then exactly "END!" before the sector cap. */
static bool xx_ti99arc_chain(const uint8_t *catalogue, size_t catalogue_size,
                             int64_t *out_size) {
    int64_t walk = 0;
    int32_t sectors = 0;

    *out_size = 0;
    for (;;) {
        const uint8_t *tail;

        if ((int64_t)catalogue_size - walk < XX_TI99ARC_SECTOR_SIZE) {
            return false;
        }
        tail = catalogue + walk + XX_TI99ARC_TAIL_OFFSET;
        ++sectors;
        walk += XX_TI99ARC_SECTOR_SIZE;
        if (tail[0] == 'E' && tail[1] == 'N' && tail[2] == 'D' &&
            tail[3] == '!') {
            *out_size = walk;
            return true;
        }
        if (tail[0] != 0U || tail[1] != 0U || tail[2] != 0U || tail[3] != 0U) {
            return false;
        }
        if (sectors > XX_TI99ARC_MAX_SECTORS) return false;
    }
}

/* True for an entry that is eighteen zero bytes: a free slot, which consumes
 * no data. Advancing the cursor for one shifts every member behind it. */
static bool xx_ti99arc_entry_free(const uint8_t *entry) {
    size_t index;

    for (index = 0U; index < (size_t)XX_TI99ARC_ENTRY_SIZE; ++index) {
        if (entry[index] != 0U) return false;
    }
    return true;
}

/* Names are 10 bytes, padded with spaces or NULs. TI-99 file names are upper
 * ASCII plus a few punctuation characters; the format grants no exemption for
 * high bytes, and a catalogue full of them is compressed data that happened
 * to land on a plausible chain. */
static bool xx_ti99arc_entry_name(const uint8_t *entry, char **out_name) {
    char buffer[11];
    size_t end = 10U;
    size_t index;
    char *name;

    *out_name = NULL;
    while (end > 0U && (entry[end - 1U] == 0x20U || entry[end - 1U] == 0U)) {
        --end;
    }
    if (end == 0U) return false;
    for (index = 0U; index < end; ++index) {
        uint8_t character = entry[index];
        if (character < 0x20U || character > 0x7EU) return false;
        /* The format has no directories, so a separator would turn one
         * member into a path. */
        if (character == '/' || character == '\\') return false;
        buffer[index] = (char)character;
    }
    buffer[end] = '\0';
    if (end == 1U && buffer[0] == '.') return false;
    if (end == 2U && buffer[0] == '.' && buffer[1] == '.') return false;

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

/* Re-derive a member's offset inside the expanded stream, and its catalogue
 * entry, from the entry's offset alone. The member record has no room for a
 * properties blob, so the decode of a compressed archive recovers both this
 * way rather than the parse carrying them. */
static bool xx_ti99arc_locate(const uint8_t *catalogue, size_t catalogue_size,
                              int64_t entry_offset, int64_t *out_offset,
                              uint8_t *out_entry) {
    int64_t catalogue_bytes;
    int64_t cursor;
    int64_t sector;
    int64_t sector_count;

    *out_offset = 0;
    if (entry_offset < 0) return false;
    if (!xx_ti99arc_chain(catalogue, catalogue_size, &catalogue_bytes)) {
        return false;
    }
    sector_count = catalogue_bytes / XX_TI99ARC_SECTOR_SIZE;
    cursor = catalogue_bytes;

    for (sector = 0; sector < sector_count; ++sector) {
        int32_t index;

        for (index = 0; index < XX_TI99ARC_ENTRIES_PER_SECTOR; ++index) {
            int64_t offset = sector * XX_TI99ARC_SECTOR_SIZE +
                             (int64_t)index * XX_TI99ARC_ENTRY_SIZE;
            const uint8_t *entry = catalogue + offset;
            int64_t member_size;

            if (xx_ti99arc_entry_free(entry)) continue;
            if (offset == entry_offset) {
                size_t copied;

                for (copied = 0U; copied < (size_t)XX_TI99ARC_ENTRY_SIZE;
                     ++copied) {
                    out_entry[copied] = entry[copied];
                }
                *out_offset = cursor;
                return true;
            }
            member_size = (int64_t)xx_ti99arc_be16(entry + 0x0C) *
                          XX_TI99ARC_SECTOR_SIZE;
            if (member_size > (int64_t)XX_TI99ARC_MAX_PLAIN_SIZE - cursor) {
                return false;
            }
            cursor += member_size;
        }
    }
    return false;
}

/* The TIFILES wrapper: a byte count, a name, and a size relation that has to
 * hold exactly. Two spellings of the relation exist because one writer
 * counted the 0x80-byte header as a sector. */
static bool xx_ti99arc_is_tifiles(const uint8_t *wrapper, int64_t size) {
    static const char magic[7] = {'T', 'I', 'F', 'I', 'L', 'E', 'S'};
    int64_t sectors;

    if (wrapper[0] != 0x07U) return false;
    if (xx_rt_memcmp(wrapper + 1, magic, sizeof(magic)) != 0) return false;
    sectors = (int64_t)xx_ti99arc_be16(wrapper + 8);
    return size == sectors * XX_TI99ARC_SECTOR_SIZE + XX_TI99ARC_WRAPPER_SIZE ||
           size == (sectors + 1) * XX_TI99ARC_SECTOR_SIZE;
}

/* FIAD has no magic, only a shape: a zero word at 0x0a, a hundred zero bytes
 * filling the header out to 0x80, and a non-zero sector count that accounts
 * for the file exactly. The size relation is what carries it. */
static bool xx_ti99arc_is_fiad(const uint8_t *wrapper, int64_t size) {
    int64_t sectors;
    size_t index;

    if (xx_ti99arc_le16(wrapper + 10) != 0U) return false;
    for (index = 0x1CU; index < (size_t)XX_TI99ARC_WRAPPER_SIZE; ++index) {
        if (wrapper[index] != 0U) return false;
    }
    sectors = (int64_t)xx_ti99arc_be16(wrapper + 0x0E);
    return sectors != 0 &&
           size == sectors * XX_TI99ARC_SECTOR_SIZE + XX_TI99ARC_WRAPPER_SIZE;
}

static xx_ti99arc_stream *xx_ti99arc_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_ti99arc_stream *stream = NULL;
    uint8_t wrapper[XX_TI99ARC_WRAPPER_SIZE];
    uint8_t *payload = NULL;
    uint8_t *catalogue = NULL;
    int64_t total;
    int64_t span;
    int64_t payload_offset = 0;
    int64_t payload_size;
    int64_t catalogue_bytes;
    int64_t cursor;
    int64_t sector;
    int64_t sector_count;
    size_t catalogue_size;
    uint8_t flags = 0U;
    bool compressed;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TI99ARC_SECTOR_SIZE || span > XX_TI99ARC_MAX_FILE_SIZE) {
        return NULL;
    }

    if (span >= XX_TI99ARC_WRAPPER_SIZE) {
        if (!xx_ti99arc_read_at(self, self->base_address, wrapper,
                                sizeof(wrapper))) {
            return NULL;
        }
        if (xx_ti99arc_is_tifiles(wrapper, span)) {
            flags = wrapper[0x0A];
            payload_offset = XX_TI99ARC_WRAPPER_SIZE;
        } else if (xx_ti99arc_is_fiad(wrapper, span)) {
            flags = wrapper[0x0C];
            payload_offset = XX_TI99ARC_WRAPPER_SIZE;
        }
    }
    payload_size = span - payload_offset;
    if (payload_size < XX_TI99ARC_SECTOR_SIZE) return NULL;
    compressed = (flags & XX_TI99ARC_FLAG_COMPRESSED) != 0U;

    /* The catalogue chain lives at the start of the payload, which for a
     * compressed archive means expanding it first. The probe is capped at the
     * largest catalogue the format can describe, so a file that is not this
     * format costs one bounded LZW pass and no more - and a stop at the
     * buffer limit is the intended outcome, which is why this uses the
     * expand entry point and not the strict whole-stream decoder. */
    catalogue = (uint8_t *)xx_mem_alloc((size_t)XX_TI99ARC_PROBE_SIZE);
    if (!catalogue) return NULL;
    if (compressed) {
        payload = (uint8_t *)xx_mem_alloc((size_t)payload_size);
        if (!payload ||
            !xx_ti99arc_read_at(self, self->base_address + payload_offset,
                                payload, (size_t)payload_size)) {
            goto fail;
        }
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ti99arc_expand_memory(payload, (size_t)payload_size,
                                      catalogue,
                                      (size_t)XX_TI99ARC_PROBE_SIZE,
                                      &catalogue_size, NULL)) {
            goto fail;
        }
        xx_mem_free(payload);
        payload = NULL;
    } else {
        catalogue_size = (size_t)(payload_size < (int64_t)XX_TI99ARC_PROBE_SIZE
                                      ? payload_size
                                      : (int64_t)XX_TI99ARC_PROBE_SIZE);
        if (!xx_ti99arc_read_at(self, self->base_address + payload_offset,
                                catalogue, catalogue_size)) {
            goto fail;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* Walk the chain first: the entries are only read once the chain is known
     * to terminate, because the data cursor starts behind the last sector. */
    if (!xx_ti99arc_chain(catalogue, catalogue_size, &catalogue_bytes)) {
        goto fail;
    }
    sector_count = catalogue_bytes / XX_TI99ARC_SECTOR_SIZE;

    stream = (xx_ti99arc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    cursor = catalogue_bytes;
    for (sector = 0; sector < sector_count; ++sector) {
        int32_t index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        for (index = 0; index < XX_TI99ARC_ENTRIES_PER_SECTOR; ++index) {
            xx_ti99arc_member member;
            int64_t entry_offset = sector * XX_TI99ARC_SECTOR_SIZE +
                                   (int64_t)index * XX_TI99ARC_ENTRY_SIZE;
            const uint8_t *entry = catalogue + entry_offset;
            int64_t member_size;
            char *name;

            if (xx_ti99arc_entry_free(entry)) continue;
            if (stream->count >= (size_t)XX_TI99ARC_MAX_MEMBERS) goto fail;
            member_size = (int64_t)xx_ti99arc_be16(entry + 0x0C) *
                          XX_TI99ARC_SECTOR_SIZE;

            xx_mem_zero(&member, sizeof(member));
            member.header_size = XX_TI99ARC_ENTRY_SIZE;
            member.uncompressed_size = XX_TI99ARC_WRAPPER_SIZE + member_size;
            /* The wrapper's flag byte, unchanged: the archive states its
             * compression once, not per member. */
            member.method = (uint32_t)flags;
            member.timestamp = 0U;
            member.is_folder = false;

            if (compressed) {
                /* A compressed member is a slice of the expanded stream, so
                 * the stream handed to the decoder is the WHOLE payload and
                 * data_offset points at its start. header_offset then names
                 * the entry's position inside that stream rather than a real
                 * file offset - there is no file offset to name - and the
                 * decode recovers the slice from the difference. */
                member.header_offset =
                    self->base_address + payload_offset + entry_offset;
                member.data_offset = self->base_address + payload_offset;
                member.compressed_size = payload_size;
            } else {
                member.header_offset =
                    self->base_address + payload_offset + entry_offset;
                member.data_offset =
                    self->base_address + payload_offset + cursor;
                member.compressed_size = member_size;
                /* A stored member running past EOF is a rejection. */
                if (!xx_ti99arc_range_within(
                        span, payload_offset + cursor, member_size)) {
                    goto fail;
                }
            }

            if (!xx_ti99arc_entry_name(entry, &name)) goto fail;
            member.name = name;
            if (!xx_ti99arc_path_safe(name) ||
                !xx_ti99arc_add(stream, &member)) {
                xx_str_free(name);
                goto fail;
            }

            /* The cursor is an offset into the expanded stream, so it is
             * bounded by the decoder's plaintext cap rather than by the file
             * size: a crafted catalogue must not be able to ask for an
             * unbounded buffer at extraction time. */
            if (member_size > (int64_t)XX_TI99ARC_MAX_PLAIN_SIZE - cursor) {
                goto fail;
            }
            cursor += member_size;
        }
    }

    if (stream->count == 0U) goto fail;
    xx_mem_free(catalogue);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(payload);
    xx_mem_free(catalogue);
    xx_ti99arc_stream_free(stream);
    return NULL;
}


/* A u16 BE sector count cannot describe more than 16 MiB, so a larger file is
 * not this format however well the wrapper arithmetic happens to work out. */
/* Fourteen entries per sector, 0x400 sectors: the format's own ceiling. */
/* The properties blob xx_ti99arc_decode_member() reads: four u32 LE fields
 * and then the synthesised TIFILES header. */

static void xx_ti99arc_put_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 8 >> 8) & 0xFFU);
    data[3] = (uint8_t)((value >> 8 >> 8 >> 8) & 0xFFU);
}

/* The 0x80-byte TIFILES header a member needs and the archive does not hold.
 * Entry bytes 0x00..0x09 land at 0x00 and entry bytes 0x0a..0x11 land at
 * 0x0c: the two-byte gap is the format's, not a transcription slip. */
static void xx_ti99arc_tifiles_prefix(const uint8_t *entry, uint8_t *prefix) {
    size_t index;

    for (index = 0U; index < (size_t)XX_TI99ARC_WRAPPER_SIZE; ++index) {
        prefix[index] = 0U;
    }
    for (index = 0U; index < 0x0AU; ++index) prefix[index] = entry[index];
    for (index = 0U; index < 0x08U; ++index) {
        prefix[0x0C + index] = entry[0x0A + index];
    }
}

static bool xx_ti99arc_decode(Abstractformat *self,
                              const xx_ti99arc_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t props[XX_TI99ARC_PROPS_SIZE];
    uint8_t entry[XX_TI99ARC_ENTRY_SIZE];
    uint8_t *input = NULL;
    uint8_t *catalogue = NULL;
    uint8_t *output = NULL;
    int64_t member_size;
    int64_t member_offset = 0;
    size_t expanded = 0U;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 ||
        member->uncompressed_size < XX_TI99ARC_WRAPPER_SIZE ||
        member->uncompressed_size > (int64_t)XX_TI99ARC_MAX_PLAIN_SIZE ||
        member->compressed_size > XX_TI99ARC_MAX_FILE_SIZE) {
        return false;
    }
    member_size = member->uncompressed_size - XX_TI99ARC_WRAPPER_SIZE;

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_ti99arc_read_at(self, member->data_offset, input,
                            (size_t)member->compressed_size)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) goto fail;

    /* member->method is the wrapper's flag byte, unchanged. Only bit 1 is
     * load bearing; a stored payload takes the prefix-then-copy path and a
     * compressed one goes through the LZW member decoder. */
    if ((member->method & XX_TI99ARC_FLAG_COMPRESSED) == 0U) {
        /* Stored: the catalogue entry is at a real file offset, so the
         * prefix can be rebuilt by re-reading it. */
        if (member->compressed_size != member_size) goto fail;
        if (!xx_ti99arc_read_at(self, member->header_offset, entry,
                                sizeof(entry))) {
            goto fail;
        }
        xx_ti99arc_tifiles_prefix(entry, props);
        if (!xx_sclsectors_decode_memory_ex(
                props, (size_t)XX_TI99ARC_WRAPPER_SIZE, input,
                (size_t)member->compressed_size, output,
                (size_t)member->uncompressed_size, &written)) {
            goto fail;
        }
    } else {
        /* Compressed: the member is a slice of the expanded stream and its
         * catalogue entry is inside that stream too, so both the entry and
         * the member's offset have to be recovered by walking the expanded
         * catalogue again. header_offset - data_offset is the entry's offset
         * within the payload, which is the one thing the member record has
         * room to carry. */
        catalogue = (uint8_t *)xx_mem_alloc((size_t)XX_TI99ARC_PROBE_SIZE);
        if (!catalogue) goto fail;
        if (!xx_ti99arc_expand_memory(input, (size_t)member->compressed_size,
                                      catalogue,
                                      (size_t)XX_TI99ARC_PROBE_SIZE,
                                      &expanded, NULL)) {
            goto fail;
        }
        if (!xx_ti99arc_locate(catalogue, expanded,
                               member->header_offset - member->data_offset,
                               &member_offset, entry)) {
            goto fail;
        }
        xx_mem_free(catalogue);
        catalogue = NULL;
        if (pd && xx_pd_is_stopped(pd)) goto fail;

        if (member_offset < 0 ||
            member_size > (int64_t)XX_TI99ARC_MAX_PLAIN_SIZE - member_offset) {
            goto fail;
        }
        xx_ti99arc_put_le32(props, (uint32_t)(member_offset + member_size));
        xx_ti99arc_put_le32(props + 4, (uint32_t)member_offset);
        xx_ti99arc_put_le32(props + 8, (uint32_t)member_size);
        xx_ti99arc_put_le32(props + 12, (uint32_t)XX_TI99ARC_WRAPPER_SIZE);
        xx_ti99arc_tifiles_prefix(entry, props + 0x10);
        if (!xx_ti99arc_decode_member(input, (size_t)member->compressed_size,
                                      props, sizeof(props), output,
                                      (size_t)member->uncompressed_size,
                                      &written)) {
            goto fail;
        }
    }

    /* Short output reported as success is the one failure the caller cannot
     * detect, and a compressed member that stops early looks exactly like a
     * legitimately small file. */
    if (written != (size_t)member->uncompressed_size) goto fail;
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;

fail:
    xx_mem_free(catalogue);
    xx_mem_free(output);
    xx_mem_free(input);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ti99arc_init(xx_ti99arc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_TI99ARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ti99-ark");
    xx_format_set_extension(&archive->format, "ark");
    archive->format.check_is_valid = xx_ti99arc_check_is_valid;
    archive->format.handle_base_info = xx_ti99arc_handle_base_info;
    archive->format.get_format_size = xx_ti99arc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ti99arc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ti99arc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ti99arc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ti99arc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ti99arc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ti99arc_free_archive_records_reading;
    archive->format.destroy = xx_ti99arc_vtable_destroy;
}

xx_ti99arc *xx_ti99arc_create(xx_io_device *device, int64_t base_address) {
    xx_ti99arc *archive = (xx_ti99arc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ti99arc_init(archive, device, base_address);
    return archive;
}

void xx_ti99arc_destroy(xx_ti99arc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ti99arc_free(xx_ti99arc *archive) {
    if (!archive) return;
    xx_ti99arc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ti99arc_vtable_destroy(Abstractformat *self) {
    xx_ti99arc_destroy((xx_ti99arc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ti99arc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ti99arc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ti99arc_parse(self, pd);
    if (!stream) return false;
    xx_ti99arc_stream_free(stream);
    return true;
}

bool xx_ti99arc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ti99arc *archive = (xx_ti99arc *)self;
    xx_ti99arc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ti99arc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ti99arc_stream_free(stream);
    return true;
}

int64_t xx_ti99arc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ti99arc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ti99arc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ti99arc_set_record(xx_archive_record *record,
                                 const xx_ti99arc_member *member) {
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

static bool xx_ti99arc_copy_options(xx_list_s *target,
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

static const xx_var *xx_ti99arc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ti99arc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ti99arc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ti99arc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ti99arc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ti99arc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ti99arc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ti99arc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ti99arc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ti99arc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ti99arc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ti99arc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ti99arc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ti99arc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ti99arc_stream *stream;
    const xx_ti99arc_member *member;
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
    stream = (xx_ti99arc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ti99arc_path_safe(member->name)) return false;

    path_option = xx_ti99arc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ti99arc_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ti99arc_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ti99arc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
