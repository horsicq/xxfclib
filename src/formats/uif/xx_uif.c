/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MagicISO UIF: a disc image cut into runs of sectors, each stored, zero or
 * packed (zlib for versions <= 2, LZMA with an optional x86 filter for
 * version 3 and later), described by a "blhr" block table that a 64-byte
 * "bbis" trailer at the end of the file points to.  xx_uif.h carries the
 * field table.
 *
 * Written from the format's structure as observed in uif2iso 0.1.7c's
 * behaviour on generated images (the tool, GPL, was used as a black-box
 * oracle only; no code was taken from it).  The x86 branch converter is the
 * public-domain LZMA SDK algorithm (Bra86.c, Igor Pavlov), re-expressed here.
 *
 * The image is one member, image.<ext> by the output format; image type 9
 * adds the descriptor file(s) stored in "blss".  Blocks must be listed in
 * ascending, non-overlapping order inside the declared sector count; a
 * sector range no block covers reads as zeros, as uif2iso leaves it.  The
 * last sector is cut to the trailer's "last sector bytes" when non-zero.
 * Encrypted images (DES fixed key, password "bsdr", MagicISO's private
 * cipher) are listed but not extracted.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uif/xx_uif.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef UIF
#define XX_UIF_FILE_TYPE XX_FILE_TYPE_UIF
#else
#define XX_UIF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define UIF_TRAILER 64U
#define UIF_SECTION_HEADER 16U
#define UIF_ENTRY 24U
#define UIF_MAX_SECTOR_SIZE 0x10000U
/* 2 Mi blocks: a 48 MiB table, far above a dual-layer DVD cut in 32 KiB. */
#define UIF_MAX_ENTRIES (2U * 1024U * 1024U)
#define UIF_MAX_BLOCK (64U * 1024U * 1024U)
#define UIF_MAX_BLOB (64U * 1024U * 1024U)
#define UIF_LZMA_HEADER 14U

#define UIF_TYPE_STORED 1U
#define UIF_TYPE_ZERO 3U
#define UIF_TYPE_PACKED 5U

#define UIF_MAX_MEMBERS 3U

typedef struct uif_member_s {
    const char *name;
    bool is_image;
    uint32_t blob_offset; /**< Slice of the blss data for descriptors. */
    uint32_t blob_size;
} uif_member;

typedef struct uif_context_s {
    int64_t base;
    int64_t input_size;
    uint16_t version;
    uint16_t image_type;
    uint32_t sectors;
    uint32_t sector_size;
    uint32_t lastdiff;
    uint64_t blhr_offset;
    uint32_t entry_count;
    bool encrypted;
    uint8_t *table;          /**< entry_count * 24 bytes, when kept. */
    uint8_t *blob;           /**< blss descriptor data, when kept. */
    uint32_t blob_size;
    uint32_t output_format;
    uint64_t image_size;
    uint64_t packed_total;
    uint32_t max_bytes;
    uint32_t max_zsize;
    uint32_t member_count;
    uif_member members[UIF_MAX_MEMBERS];
} uif_context;

typedef struct uif_stream_s {
    uif_context context;
    size_t index;
} uif_stream;

static bool uif_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U, capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    if (capacity == 0U) capacity = 65536U;
    while (done < size) {
        size_t request = size - done;
        if (request > capacity) request = capacity;
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static void uif_context_release(uif_context *context) {
    if (context->table) xx_mem_free(context->table);
    if (context->blob) xx_mem_free(context->blob);
    context->table = NULL;
    context->blob = NULL;
}

/* x86 branch converter, decoding direction, over one block (start ip 0,
 * fresh state), as the LZMA SDK's public-domain Bra86.c defines it. */
static void uif_x86_decode(uint8_t *data, size_t size) {
    static const uint8_t allowed[8] = {1, 1, 1, 0, 1, 0, 0, 0};
    static const uint8_t bit_number[8] = {0, 1, 2, 2, 3, 3, 3, 3};
    size_t position = 0U, previous = (size_t)0 - 1U;
    uint32_t mask = 0U;
    const uint32_t ip = 5U;
    if (size < 5U) return;
    for (;;) {
        size_t limit = size - 4U;
        while (position < limit && (data[position] & 0xFEU) != 0xE8U)
            ++position;
        if (position >= limit) break;
        {
            size_t distance = position - previous;
            if (distance > 3U) {
                mask = 0U;
            } else {
                mask = (mask << (distance - 1U)) & 7U;
                if (mask != 0U) {
                    uint8_t b = data[position + 4U - bit_number[mask]];
                    if (!allowed[mask] || b == 0U || b == 0xFFU) {
                        previous = position;
                        mask = ((mask << 1U) & 7U) | 1U;
                        ++position;
                        continue;
                    }
                }
            }
        }
        previous = position;
        if (data[position + 4U] == 0U || data[position + 4U] == 0xFFU) {
            uint32_t source = ((uint32_t)data[position + 4U] << 24U) |
                              ((uint32_t)data[position + 3U] << 16U) |
                              ((uint32_t)data[position + 2U] << 8U) |
                              (uint32_t)data[position + 1U];
            uint32_t target;
            for (;;) {
                uint32_t index;
                uint8_t b;
                target = source - (ip + (uint32_t)position);
                if (mask == 0U) break;
                index = (uint32_t)bit_number[mask] * 8U;
                b = (uint8_t)(target >> (24U - index));
                if (!(b == 0U || b == 0xFFU)) break;
                source = target ^ ((UINT32_C(1) << (32U - index)) - 1U);
            }
            data[position + 4U] = (uint8_t)(~(((target >> 24U) & 1U) - 1U));
            data[position + 3U] = (uint8_t)(target >> 16U);
            data[position + 2U] = (uint8_t)(target >> 8U);
            data[position + 1U] = (uint8_t)target;
            position += 5U;
        } else {
            mask = ((mask << 1U) & 7U) | 1U;
            ++position;
        }
    }
}

/* Unpack one packed run into `output` (zeroed past what the stream gives).
 * `exact` demands the stream fill the output. */
static bool uif_unpack_memory(uint16_t version, const uint8_t *input,
                              size_t input_size, uint8_t *output,
                              size_t output_size, bool exact) {
    size_t written = 0U;
    if (output_size == 0U) return input_size == 0U;
    if (version <= 2U) {
        /* The whole stream is stored, so its Adler-32 is checked as zlib's
         * inflate (and so uif2iso) does. */
        if (!xx_zlib_stream_header_is_valid(input, input_size) ||
            !xx_zlib_stream_decode_memory(input, input_size, output,
                                          output_size, &written) ||
            written > output_size ||
            !xx_zlib_stream_trailer_matches(input, input_size, output, written))
            return false;
    } else {
        uint8_t props[XX_LZMA_PROPS_SIZE];
        uint32_t dictionary;
        if (input_size <= UIF_LZMA_HEADER || input[0] > 1U || input[1] >= 225U)
            return false;
        xx_rt_memcpy(props, input + 1U, sizeof(props));
        /* The whole run is in memory, so no match reaches further back than
         * its size: a larger dictionary is never needed. */
        dictionary = xx_data_get_u32(props + 1U, 4, 0, false);
        if (dictionary > output_size) {
            uint32_t clamp = output_size < 4096U ? 4096U : (uint32_t)output_size;
            props[1] = (uint8_t)clamp;
            props[2] = (uint8_t)(clamp >> 8U);
            props[3] = (uint8_t)(clamp >> 16U);
            props[4] = (uint8_t)(clamp >> 24U);
        }
        if (!xx_lzma_decompress_memory(input + UIF_LZMA_HEADER,
                                       input_size - UIF_LZMA_HEADER, props,
                                       sizeof(props), (int64_t)output_size,
                                       output, output_size, &written))
            return false;
        if (written > output_size) return false;
        if (input[0] != 0U) uif_x86_decode(output, written);
    }
    if (written > output_size || (exact && written != output_size))
        return false;
    if (written < output_size)
        xx_rt_memset(output + written, 0, output_size - written);
    return true;
}

/* A section header ("blhr", "blms", "blss") and the bytes it stores:
 * size - `size_base` packed bytes, or `plain` bytes when not packed. */
static bool uif_section_stored(const uint8_t *header, uint32_t size_base,
                               uint64_t plain, uint64_t *stored,
                               bool *packed) {
    uint32_t size = xx_data_get_u32(header + 4U, 4, 0, false);
    *packed = xx_data_get_u32(header + 8U, 4, 0, false) != 0U;
    if (*packed) {
        if (size < size_base) return false;
        *stored = (uint64_t)size - size_base;
    } else {
        *stored = plain;
    }
    return true;
}

/* Read `stored` bytes at `offset` and produce `plain` bytes into a fresh
 * buffer (NULL on failure). */
static uint8_t *uif_load_section(const uif_context *context,
                                 xx_io_device *device, uint64_t offset,
                                 uint64_t stored, bool packed, uint64_t plain) {
    uint8_t *raw, *out;
    uint64_t limit = (uint64_t)context->input_size - UIF_TRAILER;
    if (plain == 0U || plain > UIF_MAX_BLOB || stored > UIF_MAX_BLOB ||
        offset > limit || stored > limit - offset)
        return NULL;
    if (!packed && stored != plain) return NULL;
    raw = (uint8_t *)xx_mem_alloc(stored ? (size_t)stored : 1U);
    if (!raw) return NULL;
    if (!uif_read_at(device, context->base + (int64_t)offset, raw,
                     (size_t)stored)) {
        xx_mem_free(raw);
        return NULL;
    }
    if (!packed) return raw;
    out = (uint8_t *)xx_mem_alloc((size_t)plain);
    if (!out || !uif_unpack_memory(context->version, raw, (size_t)stored, out,
                                   (size_t)plain, true)) {
        if (out) xx_mem_free(out);
        xx_mem_free(raw);
        return NULL;
    }
    xx_mem_free(raw);
    return out;
}

static const char *uif_image_name(uint32_t output_format) {
    switch (output_format) {
    case 0U: return "image.iso";
    case 1U: return "image.bin";
    case 2U: return "image.mdf";
    case 3U: return "image.img";
    case 4U: return "image.nrg";
    default: return "image.dat";
    }
}

/* Image type 9: "blms" (skipped) then "blss" with the output format and the
 * descriptor file(s).  Anything unreadable leaves the plain image alone, as
 * uif2iso falls back to an ISO when a section signature is wrong. */
static void uif_parse_raw_sections(uif_context *context, xx_io_device *device,
                                   uint64_t position, bool keep) {
    uint8_t header[UIF_SECTION_HEADER + 4U];
    uint64_t limit = (uint64_t)context->input_size - UIF_TRAILER;
    uint64_t stored;
    bool packed;
    uint32_t plain;
    if (position > limit || limit - position < UIF_SECTION_HEADER ||
        !uif_read_at(device, context->base + (int64_t)position, header,
                     UIF_SECTION_HEADER) ||
        xx_rt_memcmp(header, "blms", 4U) != 0 ||
        !uif_section_stored(header, 8U, xx_data_get_u32(header + 12U, 4, 0, false), &stored,
                            &packed))
        return;
    position += UIF_SECTION_HEADER;
    if (stored > limit - position) return;
    position += stored;
    if (limit - position < sizeof(header) ||
        !uif_read_at(device, context->base + (int64_t)position, header,
                     sizeof(header)))
        return;
    /* uif2iso takes the format word even when the signature is off. */
    context->output_format = xx_data_get_u32(header + UIF_SECTION_HEADER, 4, 0, false);
    if (xx_rt_memcmp(header, "blss", 4U) != 0) return;
    plain = xx_data_get_u32(header + 12U, 4, 0, false);
    if (plain == 0U || context->output_format == 0U ||
        context->output_format == 4U || context->output_format > 4U ||
        !uif_section_stored(header, 12U, plain, &stored, &packed))
        return;
    position += sizeof(header);
    {
        uint8_t *blob = uif_load_section(context, device, position, stored,
                                         packed, plain);
        if (!blob) return;
        if (context->output_format == 3U) {
            uint32_t ccd, sub;
            if (plain < 8U) {
                xx_mem_free(blob);
                return;
            }
            ccd = xx_data_get_u32(blob, 4, 0, false);
            sub = xx_data_get_u32(blob + 4U, 4, 0, false);
            if (ccd > plain - 8U || sub > plain - 8U - ccd) {
                xx_mem_free(blob);
                return;
            }
            context->members[1].name = "image.ccd";
            context->members[1].blob_offset = 8U;
            context->members[1].blob_size = ccd;
            context->members[2].name = "image.sub";
            context->members[2].blob_offset = 8U + ccd;
            context->members[2].blob_size = sub;
            context->member_count = 3U;
        } else {
            context->members[1].name =
                context->output_format == 1U ? "image.cue" : "image.mds";
            context->members[1].blob_offset = 0U;
            context->members[1].blob_size = plain;
            context->member_count = 2U;
        }
        context->blob_size = plain;
        if (keep)
            context->blob = blob;
        else
            xx_mem_free(blob);
    }
}

/* Walk the table: ascending, non-overlapping runs inside the image, packed
 * sizes that suit their types, data inside the file. */
static bool uif_walk(uif_context *context, xx_pd_struct *pd) {
    uint64_t next_sector = 0U, limit = (uint64_t)context->input_size - UIF_TRAILER;
    uint32_t index;
    uint32_t max_count = UIF_MAX_BLOCK / context->sector_size;
    context->packed_total = 0U;
    context->max_bytes = 0U;
    context->max_zsize = 0U;
    for (index = 0U; index < context->entry_count; ++index) {
        const uint8_t *entry = context->table + (size_t)index * UIF_ENTRY;
        uint64_t offset = xx_data_get_u64(entry, 8, 0, false);
        uint32_t zsize = xx_data_get_u32(entry + 8U, 4, 0, false);
        uint32_t sector = xx_data_get_u32(entry + 12U, 4, 0, false);
        uint32_t count = xx_data_get_u32(entry + 16U, 4, 0, false);
        uint32_t type = xx_data_get_u32(entry + 20U, 4, 0, false);
        uint32_t bytes;
        if ((index & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (count == 0U || count > max_count || sector < next_sector ||
            (uint64_t)sector + count > context->sectors)
            return false;
        bytes = count * context->sector_size;
        switch (type) {
        case UIF_TYPE_STORED:
            if (zsize > bytes) return false;
            break;
        case UIF_TYPE_ZERO:
            break;
        case UIF_TYPE_PACKED:
            if (zsize < 3U ||
                (uint64_t)zsize > (uint64_t)bytes + bytes / 8U + 1024U)
                return false;
            break;
        default:
            return false;
        }
        if (zsize != 0U && (offset > limit || zsize > limit - offset))
            return false;
        if (type != UIF_TYPE_ZERO) {
            context->packed_total += zsize;
            if (zsize > context->max_zsize) context->max_zsize = zsize;
        }
        if (bytes > context->max_bytes) context->max_bytes = bytes;
        next_sector = (uint64_t)sector + count;
    }
    context->image_size = next_sector * context->sector_size;
    if (context->lastdiff != 0U && next_sector >= context->sectors)
        context->image_size -= context->sector_size - context->lastdiff;
    return true;
}

static bool uif_parse(Abstractformat *format, uif_context *out, bool keep,
                      xx_pd_struct *pd) {
    uint8_t trailer[UIF_TRAILER], header[UIF_SECTION_HEADER];
    uif_context context;
    int64_t total;
    uint64_t limit, stored, plain;
    bool packed;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&context, sizeof(context));
    context.base = format->base_address;
    context.input_size = total - format->base_address;
    if (context.input_size < (int64_t)(UIF_TRAILER + UIF_SECTION_HEADER) ||
        !uif_read_at(format->device,
                     context.base + context.input_size - UIF_TRAILER, trailer,
                     UIF_TRAILER) ||
        xx_rt_memcmp(trailer, "bbis", 4U) != 0)
        return false;
    context.version = xx_data_get_u16(trailer + 8U, 2, 0, false);
    context.image_type = xx_data_get_u16(trailer + 10U, 2, 0, false);
    context.sectors = xx_data_get_u32(trailer + 16U, 4, 0, false);
    context.sector_size = xx_data_get_u32(trailer + 20U, 4, 0, false);
    context.lastdiff = xx_data_get_u32(trailer + 24U, 4, 0, false);
    context.blhr_offset = xx_data_get_u64(trailer + 28U, 8, 0, false);
    limit = (uint64_t)context.input_size - UIF_TRAILER;
    if (context.sectors == 0U || context.sector_size == 0U ||
        context.sector_size > UIF_MAX_SECTOR_SIZE ||
        context.lastdiff >= context.sector_size ||
        context.blhr_offset > limit ||
        limit - context.blhr_offset < UIF_SECTION_HEADER ||
        !uif_read_at(format->device, context.base + (int64_t)context.blhr_offset,
                     header, UIF_SECTION_HEADER))
        return false;
    if (xx_rt_memcmp(header, "bsdr", 4U) == 0) {
        context.encrypted = true; /* password: the table cannot be read */
    } else if (xx_rt_memcmp(header, "blhr", 4U) != 0) {
        return false;
    }
    /* Versions up to 1 ignore the fixed-key byte; MagicISO's own cipher is
     * selected whatever the version. */
    if (trailer[0x39] == 2U ||
        (context.version > 1U && trailer[0x38] != 0U && trailer[0x38] <= 16U))
        context.encrypted = true;
    context.members[0].name = uif_image_name(0U);
    context.members[0].is_image = true;
    context.member_count = 1U;
    if (context.encrypted) {
        context.image_size = (uint64_t)context.sectors * context.sector_size;
        *out = context;
        return true;
    }
    context.entry_count = xx_data_get_u32(header + 12U, 4, 0, false);
    if (context.entry_count == 0U || context.entry_count > UIF_MAX_ENTRIES)
        return false;
    plain = (uint64_t)context.entry_count * UIF_ENTRY;
    if (!uif_section_stored(header, 8U, plain, &stored, &packed)) return false;
    context.table = uif_load_section(&context, format->device,
                                     context.blhr_offset + UIF_SECTION_HEADER,
                                     stored, packed, plain);
    if (!context.table) return false;
    if (!uif_walk(&context, pd)) {
        uif_context_release(&context);
        return false;
    }
    if (context.image_type == 9U) {
        uif_parse_raw_sections(&context, format->device,
                               context.blhr_offset + UIF_SECTION_HEADER + stored,
                               keep);
        context.members[0].name = uif_image_name(context.output_format);
    }
    if (!keep) uif_context_release(&context);
    *out = context;
    return true;
}

static bool uif_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *uif_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool uif_set_record(xx_archive_record *record,
                           const uif_context *context, size_t index) {
    const uif_member *member = &context->members[index];
    uint64_t packed = member->is_image ? context->packed_total
                                       : member->blob_size;
    uint64_t unpacked = member->is_image ? context->image_size
                                         : member->blob_size;
    uint32_t method = 0U;
    if (member->is_image) method = context->version <= 2U ? 8U : 14U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        context->base + (int64_t)context->input_size - UIF_TRAILER;
    record->header_size = UIF_TRAILER;
    record->data_offset = context->base;
    record->compressed_size = (int64_t)packed;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           context->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool uif_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool uif_write_zeros(xx_io_device *device, uint8_t *scratch,
                            size_t scratch_size, uint64_t amount,
                            xx_pd_struct *pd) {
    if (!device) return true;
    xx_rt_memset(scratch, 0, scratch_size);
    while (amount != 0U) {
        size_t part = amount < scratch_size ? (size_t)amount : scratch_size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!uif_write_all(device, scratch, part)) return false;
        amount -= part;
    }
    return true;
}

/* Rebuild the image run by run (NULL destination: decode and discard). */
static bool uif_rebuild(Abstractformat *format, const uif_context *context,
                        xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *plain = NULL, *packed = NULL;
    uint64_t written = 0U;
    uint32_t index;
    bool result = false;
    if (context->encrypted || !context->table) return false;
    plain = (uint8_t *)xx_mem_alloc(context->max_bytes ? context->max_bytes : 1U);
    packed = (uint8_t *)xx_mem_alloc(context->max_zsize ? context->max_zsize : 1U);
    if (!plain || !packed) goto done;
    for (index = 0U; index < context->entry_count; ++index) {
        const uint8_t *entry = context->table + (size_t)index * UIF_ENTRY;
        uint64_t offset = xx_data_get_u64(entry, 8, 0, false);
        uint32_t zsize = xx_data_get_u32(entry + 8U, 4, 0, false);
        uint32_t sector = xx_data_get_u32(entry + 12U, 4, 0, false);
        uint32_t count = xx_data_get_u32(entry + 16U, 4, 0, false);
        uint32_t type = xx_data_get_u32(entry + 20U, 4, 0, false);
        uint32_t bytes = count * context->sector_size;
        uint64_t start = (uint64_t)sector * context->sector_size;
        uint64_t keep = bytes;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (start < written || bytes > context->max_bytes ||
            zsize > context->max_zsize)
            goto done;
        if (start > written) {
            /* A hole no run covers. */
            if (!uif_write_zeros(destination, plain, context->max_bytes,
                                 start - written, pd))
                goto done;
            written = start;
        }
        if (type == UIF_TYPE_ZERO) {
            xx_rt_memset(plain, 0, bytes);
        } else {
            if (zsize != 0U &&
                !uif_read_at(format->device, context->base + (int64_t)offset,
                             packed, zsize))
                goto done;
            if (type == UIF_TYPE_STORED) {
                if (zsize > bytes) goto done;
                if (zsize) xx_rt_memcpy(plain, packed, zsize);
                xx_rt_memset(plain + zsize, 0, bytes - zsize);
            } else if (!uif_unpack_memory(context->version, packed, zsize,
                                          plain, bytes, false)) {
                goto done;
            }
        }
        if (start + keep > context->image_size) keep = context->image_size - start;
        if (!uif_write_all(destination, plain, (size_t)keep)) goto done;
        written += keep;
    }
    result = written == context->image_size;
done:
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    return result;
}

static bool uif_emit(Abstractformat *format, const uif_context *context,
                     size_t index, xx_io_device *destination,
                     xx_pd_struct *pd) {
    const uif_member *member = &context->members[index];
    if (member->is_image)
        return uif_rebuild(format, context, destination, pd);
    if (!context->blob ||
        (uint64_t)member->blob_offset + member->blob_size > context->blob_size)
        return false;
    return uif_write_all(destination, context->blob + member->blob_offset,
                         member->blob_size);
}

static void uif_stream_free(void *opaque) {
    uif_stream *stream = (uif_stream *)opaque;
    if (!stream) return;
    uif_context_release(&stream->context);
    xx_mem_free(stream);
}

void xx_uif_init(xx_uif *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_UIF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-uif");
    xx_format_set_extension(&archive->format, "uif");
    archive->format.check_is_valid = xx_uif_check_is_valid;
    archive->format.handle_base_info = xx_uif_handle_base_info;
    archive->format.get_format_size = xx_uif_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_uif_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_uif_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_uif_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_uif_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_uif_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_uif_free_archive_records_reading;
}

xx_uif *xx_uif_create(xx_io_device *device, int64_t base_address) {
    xx_uif *archive = (xx_uif *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_uif_init(archive, device, base_address);
    return archive;
}

void xx_uif_destroy(xx_uif *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_uif_free(xx_uif *archive) {
    if (!archive) return;
    xx_uif_destroy(archive);
    xx_mem_free(archive);
}

bool xx_uif_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uif_context context;
    return uif_parse(format, &context, false, pd);
}

bool xx_uif_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    uif_context context;
    xx_uif *archive;
    if (!format || !uif_parse(format, &context, false, pd)) return false;
    archive = (xx_uif *)format;
    archive->number_of_records = context.member_count;
    archive->image_size = context.image_size;
    archive->sectors = context.sectors;
    archive->sector_size = context.sector_size;
    archive->block_count = context.entry_count;
    archive->version = context.version;
    archive->image_type = context.image_type;
    archive->output_format = context.output_format;
    archive->encrypted = context.encrypted;
    format->number_of_archive_records = context.member_count;
    format->format_size = context.input_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_uif_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_uif_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_uif_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_uif_handle_base_info(format, pd))
               ? ((xx_uif *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_uif_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    uif_stream *stream;
    xx_archive_record_state *state;
    stream = (uif_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!uif_parse(format, &stream->context, true, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        uif_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = uif_stream_free;
    state->total_records = stream->context.member_count;
    if (!uif_copy_options(&state->options, options) ||
        !uif_set_record(&state->current_record, &stream->context, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_uif_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_uif_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    uif_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (uif_stream *)state->internal_state) ||
        stream->index + 1U >= stream->context.member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!uif_set_record(&state->current_record, &stream->context,
                        stream->index)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_uif_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    uif_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    const char *name;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (uif_stream *)state->internal_state) ||
        stream->index >= stream->context.member_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->context.encrypted) return false; /* do not create a file */
    path_option = uif_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return uif_emit(format, &stream->context, stream->index, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    name = stream->context.members[stream->index].name;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = uif_emit(format, &stream->context, stream->index, destination,
                          pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_uif_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
