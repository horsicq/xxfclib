/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Partclone image reader, written from the image layout (partclone.h image
 * format 0002) and the size rules in unblob's partclone handler (MIT).  No
 * partclone code is used. */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/partclone_image/xx_partclone_image.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef PARTCLONE_IMAGE
#define XX_PARTCLONE_IMAGE_FILE_TYPE XX_FILE_TYPE_PARTCLONE_IMAGE
#else
#define XX_PARTCLONE_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PCL_HEADER_SIZE 110U
#define XX_PCL_CRC_OFFSET 106U
#define XX_PCL_BITMAP_CRC_SIZE 4U
#define XX_PCL_BM_BIT 1U

/* Real filesystems use blocks of 512 bytes to 64 KiB; partclone.dd and
 * partclone.imager may pick larger ones.  Anything above this is refused. */
#define XX_PCL_MAX_BLOCK_SIZE (UINT32_C(16) << 20)
/* CRC32 uses 4 bytes and XXH128 16; room is left for future digests. */
#define XX_PCL_MAX_CHECKSUM_SIZE 64U

#define XX_PCL_STAGING_SIZE 65536U

/* The bitmap must be physically present, so the declared output is at most
 * 8 * block_size times the input; this ceiling additionally bounds how much
 * an unattended extraction writes.  XX_META_ID_OPT_MAX_MEMBER_SIZE
 * overrides it in either direction. */
#define XX_PCL_DEFAULT_MAX_EXPANDED (UINT64_C(1) << 40)

#define XX_PCL_MEMBER_NAME "partition.img"

static const uint8_t xx_pcl_magic[16] = {
    'p', 'a', 'r', 't', 'c', 'l', 'o', 'n',
    'e', '-', 'i', 'm', 'a', 'g', 'e', 0};

typedef struct xx_pcl_private_s {
    int64_t input_size;
    int64_t bitmap_offset;
    uint64_t bitmap_size;
    int64_t data_offset;
    int64_t archive_end;
    int64_t restored_size;
    uint64_t device_size;
    uint64_t total_blocks;
    uint64_t used_blocks;
    uint32_t block_size;
    uint16_t checksum_mode;
    uint16_t checksum_size;
    uint32_t blocks_per_checksum;
    uint32_t word_bytes; /**< Bitmap word size for big-endian images. */
    bool big_endian;
    char fs_name[17];
} xx_pcl_private;

typedef struct xx_pcl_stream_s {
    xx_pcl_private parsed;
    size_t index;
} xx_pcl_stream;

static void xx_partclone_image_vtable_destroy(Abstractformat *self);

static bool xx_pcl_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_pcl_write_all(xx_io_device *device, const void *data,
                             size_t size) {
    const uint8_t *in = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t put = xx_io_write(device, in + done, size - done);
        if (put <= 0 || (size_t)put > size - done) return false;
        done += (size_t)put;
    }
    return true;
}

/* left + right into *result, all non-negative and inside int64_t. */
static bool xx_pcl_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_pcl_mul(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result) return false;
    if (left != 0U && right > (uint64_t)INT64_MAX / left) return false;
    *result = left * right;
    return true;
}

static void xx_pcl_private_reset(xx_pcl_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->restored_size = -1;
}

static uint64_t xx_pcl_max_expanded(const Abstractformat *self,
                                    const xx_list_s *options) {
    const xx_var *limit = self ? xx_format_resolve_extra_parameter(
                                     self, options,
                                     XX_META_ID_OPT_MAX_MEMBER_SIZE)
                               : NULL;
    if (!limit) return XX_PCL_DEFAULT_MAX_EXPANDED;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : XX_PCL_DEFAULT_MAX_EXPANDED;
        }
        default: return XX_PCL_DEFAULT_MAX_EXPANDED;
    }
}

/* Block number described by bit `bit` of bitmap byte `byte_index`.  The
 * writer stores the bitmap as an array of CPU words with bit n in word
 * n / W at position n % W; on a little-endian writer that is plain LSB-first
 * byte order, on a big-endian one the bytes of each word are reversed. */
static uint64_t xx_pcl_block_of(const xx_pcl_private *parsed,
                                uint64_t byte_index, unsigned bit) {
    if (!parsed->big_endian) return byte_index * 8U + bit;
    {
        uint64_t word = byte_index / parsed->word_bytes;
        uint64_t in_word = byte_index % parsed->word_bytes;
        return word * parsed->word_bytes * 8U +
               (parsed->word_bytes - 1U - in_word) * 8U + bit;
    }
}

/* Byte index and bit holding block n (inverse of xx_pcl_block_of). */
static uint64_t xx_pcl_byte_of(const xx_pcl_private *parsed, uint64_t block,
                               unsigned *bit) {
    *bit = (unsigned)(block & 7U);
    if (!parsed->big_endian) return block / 8U;
    {
        uint64_t word_bits = (uint64_t)parsed->word_bytes * 8U;
        uint64_t word = block / word_bits;
        uint64_t in_word = (block % word_bits) / 8U;
        return word * parsed->word_bytes + (parsed->word_bytes - 1U - in_word);
    }
}

static unsigned xx_pcl_popcount8(uint8_t value) {
    unsigned count = 0U;
    while (value) {
        value &= (uint8_t)(value - 1U);
        ++count;
    }
    return count;
}

static bool xx_pcl_parse_header(Abstractformat *self, xx_pcl_private *parsed) {
    uint8_t header[XX_PCL_HEADER_SIZE];
    bool be;
    uint32_t stored_crc;
    uint32_t computed_crc;
    uint16_t cpu_bits;
    uint64_t checksum_count = 0U;
    uint64_t data_bytes;
    uint64_t checksum_bytes = 0U;
    int64_t offset;

    if (!xx_pcl_read_at(self->device, self->base_address, header,
                        sizeof(header)) ||
        xx_rt_memcmp(header, xx_pcl_magic, sizeof(xx_pcl_magic)) != 0 ||
        xx_rt_memcmp(header + 30U, "0002", 4U) != 0) {
        return false;
    }
    if (header[34] == 0xDEU && header[35] == 0xC0U) {
        be = false;
    } else if (header[34] == 0xC0U && header[35] == 0xDEU) {
        be = true;
    } else {
        return false;
    }
    /* partclone keeps the raw CRC register: seed 0xFFFFFFFF, no final XOR,
     * which is the complement of the standard CRC-32. */
    stored_crc = xx_data_get_u32(header, sizeof(header), XX_PCL_CRC_OFFSET, be);
    computed_crc = ~xx_crc32_calc(0U, header, XX_PCL_CRC_OFFSET);
    if (stored_crc != computed_crc) return false;

    parsed->big_endian = be;
    xx_rt_memcpy(parsed->fs_name, header + 36U, 16U);
    parsed->fs_name[16] = '\0';
    parsed->device_size = xx_data_get_u64(header, sizeof(header), 52U, be);
    parsed->total_blocks = xx_data_get_u64(header, sizeof(header), 60U, be);
    parsed->used_blocks = xx_data_get_u64(header, sizeof(header), 76U, be);
    parsed->block_size = xx_data_get_u32(header, sizeof(header), 84U, be);
    cpu_bits = xx_data_get_u16(header, sizeof(header), 94U, be);
    parsed->checksum_mode = xx_data_get_u16(header, sizeof(header), 96U, be);
    parsed->checksum_size = xx_data_get_u16(header, sizeof(header), 98U, be);
    parsed->blocks_per_checksum =
        xx_data_get_u32(header, sizeof(header), 100U, be);

    if (header[105] != XX_PCL_BM_BIT) return false;
    if (parsed->block_size == 0U || parsed->block_size > XX_PCL_MAX_BLOCK_SIZE) {
        return false;
    }
    if (parsed->total_blocks == 0U ||
        parsed->used_blocks > parsed->total_blocks ||
        parsed->device_size > (uint64_t)INT64_MAX) {
        return false;
    }
    if (parsed->checksum_size > XX_PCL_MAX_CHECKSUM_SIZE) return false;
    if (be) {
        /* The byte order of each bitmap word depends on the word size. */
        if (cpu_bits != 32U && cpu_bits != 64U) return false;
        parsed->word_bytes = cpu_bits / 8U;
    } else {
        parsed->word_bytes = 1U;
    }

    /* Bitmap: ceil(total / 8) bytes and its CRC. */
    parsed->bitmap_size = parsed->total_blocks / 8U +
                          ((parsed->total_blocks & 7U) ? 1U : 0U);
    if (!xx_pcl_add(self->base_address, XX_PCL_HEADER_SIZE,
                    &parsed->bitmap_offset) ||
        !xx_pcl_add(parsed->bitmap_offset, parsed->bitmap_size, &offset) ||
        !xx_pcl_add(offset, XX_PCL_BITMAP_CRC_SIZE, &parsed->data_offset) ||
        parsed->data_offset > parsed->input_size) {
        return false;
    }

    /* Data: used blocks plus one checksum per full or final partial group. */
    if (parsed->blocks_per_checksum != 0U && parsed->checksum_size != 0U) {
        checksum_count = parsed->used_blocks / parsed->blocks_per_checksum +
                         ((parsed->used_blocks % parsed->blocks_per_checksum)
                              ? 1U
                              : 0U);
    }
    if (!xx_pcl_mul(parsed->used_blocks, parsed->block_size, &data_bytes) ||
        !xx_pcl_mul(checksum_count, parsed->checksum_size, &checksum_bytes) ||
        !xx_pcl_add(parsed->data_offset, data_bytes, &offset) ||
        !xx_pcl_add(offset, checksum_bytes, &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        return false;
    }
    return true;
}

/* One pass over the bitmap: verify its CRC, check that it marks exactly
 * used_blocks blocks, and find the restored image size. */
static bool xx_pcl_scan_bitmap(Abstractformat *self, xx_pcl_private *parsed,
                               xx_pd_struct *pd) {
    uint8_t *buffer;
    uint8_t crc_bytes[4];
    uint32_t crc = 0U;
    uint64_t done = 0U;
    uint64_t count = 0U;
    uint64_t last_used = 0U;
    bool any_used = false;
    bool last_block_used = false;
    uint64_t full_size;
    uint64_t restored;
    bool result = false;

    buffer = (uint8_t *)xx_mem_alloc(XX_PCL_STAGING_SIZE);
    if (!buffer) return false;
    if (xx_io_seek64(self->device, parsed->bitmap_offset, SEEK_SET) != 0) {
        goto done;
    }
    while (done < parsed->bitmap_size) {
        uint64_t left = parsed->bitmap_size - done;
        size_t step = left < XX_PCL_STAGING_SIZE ? (size_t)left
                                                 : XX_PCL_STAGING_SIZE;
        size_t got = 0U;
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        while (got < step) {
            ssize_t n = xx_io_read(self->device, buffer + got, step - got);
            if (n <= 0 || (size_t)n > step - got) goto done;
            got += (size_t)n;
        }
        crc = xx_crc32_calc(crc, buffer, step);
        for (index = 0U; index < step; ++index) {
            uint8_t value = buffer[index];
            unsigned bit;
            if (!value) continue;
            if (!parsed->big_endian &&
                (done + index + 1U) * 8U <= parsed->total_blocks) {
                /* Whole byte inside the image: the common fast path. */
                uint64_t top = (done + index) * 8U;
                unsigned high = 7U;
                count += xx_pcl_popcount8(value);
                while (!(value & (1U << high))) --high;
                top += high;
                if (!any_used || top > last_used) last_used = top;
                any_used = true;
                continue;
            }
            for (bit = 0U; bit < 8U; ++bit) {
                uint64_t block;
                if (!(value & (1U << bit))) continue;
                block = xx_pcl_block_of(parsed, done + index, bit);
                /* Padding bits past the last block carry no meaning. */
                if (block >= parsed->total_blocks) continue;
                ++count;
                if (!any_used || block > last_used) last_used = block;
                any_used = true;
            }
        }
        done += step;
    }
    if (!xx_pcl_read_at(self->device, parsed->bitmap_offset +
                                          (int64_t)parsed->bitmap_size,
                        crc_bytes, sizeof(crc_bytes)) ||
        xx_data_get_u32(crc_bytes, sizeof(crc_bytes), 0U, parsed->big_endian) !=
            ~crc) {
        goto done;
    }
    if (count != parsed->used_blocks) goto done;

    /* Restore writes each used block at block * block_size, and truncates
     * the output to device_size when the last block is unused. */
    last_block_used = any_used && last_used == parsed->total_blocks - 1U;
    if (!xx_pcl_mul(parsed->total_blocks, parsed->block_size, &full_size)) {
        goto done;
    }
    if (last_block_used) {
        restored = full_size;
    } else {
        restored = parsed->device_size;
        if (any_used) {
            uint64_t used_end = (last_used + 1U) * parsed->block_size;
            if (used_end > restored) restored = used_end;
        }
    }
    if (restored > (uint64_t)INT64_MAX ||
        restored > xx_pcl_max_expanded(self, NULL)) {
        goto done;
    }
    parsed->restored_size = (int64_t)restored;
    result = true;
done:
    xx_mem_free(buffer);
    return result;
}

static bool xx_pcl_parse(Abstractformat *self, xx_pcl_private *parsed,
                         xx_pd_struct *pd) {
    xx_pcl_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < 0 ||
        parsed->input_size - self->base_address < (int64_t)XX_PCL_HEADER_SIZE ||
        !xx_pcl_parse_header(self, parsed) ||
        !xx_pcl_scan_bitmap(self, parsed, pd)) {
        xx_pcl_private_reset(parsed);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Restore                                                                   */
/* ------------------------------------------------------------------------ */

static bool xx_pcl_write_zeros(xx_io_device *destination, const uint8_t *zeros,
                               uint64_t size, xx_pd_struct *pd) {
    while (size > 0U) {
        size_t step = size < XX_PCL_STAGING_SIZE ? (size_t)size
                                                 : XX_PCL_STAGING_SIZE;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_pcl_write_all(destination, zeros, step)) {
            return false;
        }
        size -= step;
    }
    return true;
}

static bool xx_pcl_restore(Abstractformat *self, const xx_pcl_private *parsed,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *bitmap = NULL;
    uint8_t *staging = NULL;
    uint8_t *zeros = NULL;
    int64_t data_pos;
    uint64_t chunk_start = 0U;   /* first bitmap byte in `bitmap` */
    uint64_t chunk_size = 0U;
    uint64_t block;
    uint64_t written = 0U;       /* bytes emitted to destination */
    uint64_t pending_zero = 0U;  /* unused-block bytes not yet emitted */
    uint64_t used_seen = 0U;
    uint32_t in_group = 0U;
    uint64_t limit;
    bool result = false;

    if (!self || !self->device || !parsed || !destination ||
        parsed->restored_size < 0) {
        return false;
    }
    limit = (uint64_t)parsed->restored_size;
    bitmap = (uint8_t *)xx_mem_alloc(XX_PCL_STAGING_SIZE);
    staging = (uint8_t *)xx_mem_alloc(XX_PCL_STAGING_SIZE);
    zeros = (uint8_t *)xx_mem_calloc(1U, XX_PCL_STAGING_SIZE);
    if (!bitmap || !staging || !zeros) goto done;
    data_pos = parsed->data_offset;

    for (block = 0U; block < parsed->total_blocks; ++block) {
        unsigned bit;
        uint64_t byte_index = xx_pcl_byte_of(parsed, block, &bit);
        uint32_t remaining;
        if (byte_index >= parsed->bitmap_size) {
            /* Only reachable for a big-endian partial last word, whose
             * missing bytes cannot hold a used block (the scan counted
             * exactly used_blocks bits that were present). */
            pending_zero += parsed->block_size;
            continue;
        }
        if (byte_index < chunk_start || byte_index >= chunk_start + chunk_size) {
            /* Chunks start on a multiple of 8 bytes, so a big-endian word
             * never straddles two of them. */
            uint64_t left;
            chunk_start = byte_index - (byte_index % XX_PCL_STAGING_SIZE);
            left = parsed->bitmap_size - chunk_start;
            chunk_size = left < XX_PCL_STAGING_SIZE ? left : XX_PCL_STAGING_SIZE;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_pcl_read_at(self->device,
                                parsed->bitmap_offset + (int64_t)chunk_start,
                                bitmap, (size_t)chunk_size)) {
                goto done;
            }
        }
        if (!(bitmap[byte_index - chunk_start] & (1U << bit))) {
            pending_zero += parsed->block_size;
            continue;
        }
        if (++used_seen > parsed->used_blocks) goto done;
        /* A used block: emit the zero run in front of it, then copy it. */
        if (!xx_pcl_write_zeros(destination, zeros, pending_zero, pd)) goto done;
        written += pending_zero;
        pending_zero = 0U;
        if (xx_io_seek64(self->device, data_pos, SEEK_SET) != 0) goto done;
        remaining = parsed->block_size;
        while (remaining > 0U) {
            size_t step = remaining < XX_PCL_STAGING_SIZE ? remaining
                                                          : XX_PCL_STAGING_SIZE;
            size_t got = 0U;
            while (got < step) {
                ssize_t n = xx_io_read(self->device, staging + got, step - got);
                if (n <= 0 || (size_t)n > step - got) goto done;
                got += (size_t)n;
            }
            if (!xx_pcl_write_all(destination, staging, step)) goto done;
            remaining -= (uint32_t)step;
        }
        written += parsed->block_size;
        data_pos += (int64_t)parsed->block_size;
        if (parsed->blocks_per_checksum != 0U && parsed->checksum_size != 0U &&
            ++in_group == parsed->blocks_per_checksum) {
            /* The checksum trails its group; it is skipped, not verified. */
            data_pos += (int64_t)parsed->checksum_size;
            in_group = 0U;
        }
        if (data_pos > parsed->archive_end || written > limit) goto done;
    }
    if (used_seen != parsed->used_blocks) goto done;
    /* Trailing unused blocks: pad (or cut) to the restored size. */
    if (written > limit) goto done;
    if (!xx_pcl_write_zeros(destination, zeros, limit - written, pd)) goto done;
    result = true;
done:
    if (bitmap) xx_mem_free(bitmap);
    if (staging) xx_mem_free(staging);
    if (zeros) xx_mem_free(zeros);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_pcl_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_pcl_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_pcl_populate_record(xx_archive_record *record,
                                   const xx_pcl_private *parsed,
                                   int64_t base_address) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address;
    record->header_size = XX_PCL_HEADER_SIZE;
    record->data_offset = parsed->data_offset;
    record->compressed_size = parsed->archive_end - parsed->data_offset;
    return xx_archive_record_set_original_name(record, XX_PCL_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->restored_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_pcl_stream_free(void *pointer) {
    xx_pcl_stream *stream = (xx_pcl_stream *)pointer;
    if (!stream) return;
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_partclone_image_init(xx_partclone_image *image, xx_io_device *dev,
                             int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_PARTCLONE_IMAGE_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-partclone-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_partclone_image_check_is_valid;
    image->format.handle_base_info = xx_partclone_image_handle_base_info;
    image->format.get_format_size = xx_partclone_image_get_format_size;
    image->format.get_number_of_archive_records =
        xx_partclone_image_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_partclone_image_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_partclone_image_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_partclone_image_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_partclone_image_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_partclone_image_free_archive_records_reading;
    image->format.destroy = xx_partclone_image_vtable_destroy;
    image->restored_size = -1;
    image->archive_end = -1;
}

xx_partclone_image *xx_partclone_image_create(xx_io_device *dev,
                                              int64_t base_address) {
    xx_partclone_image *image =
        (xx_partclone_image *)xx_mem_alloc(sizeof(*image));
    if (image) xx_partclone_image_init(image, dev, base_address);
    return image;
}

void xx_partclone_image_destroy(xx_partclone_image *image) {
    if (!image) return;
    if (image->internal) {
        xx_mem_free(image->internal);
        image->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_partclone_image_vtable_destroy(Abstractformat *self) {
    xx_partclone_image_destroy((xx_partclone_image *)self);
}

void xx_partclone_image_free(xx_partclone_image *image) {
    if (!image) return;
    xx_partclone_image_destroy(image);
    xx_mem_free(image);
}

bool xx_partclone_image_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcl_private parsed;
    return xx_pcl_parse(self, &parsed, pd);
}

bool xx_partclone_image_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_partclone_image *image = (xx_partclone_image *)self;
    xx_pcl_private *parsed;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_pcl_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_pcl_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (image->internal) xx_mem_free(image->internal);
    image->internal = parsed;
    image->number_of_records = 1U;
    image->total_blocks = parsed->total_blocks;
    image->used_blocks = parsed->used_blocks;
    image->device_size = parsed->device_size;
    image->block_size = parsed->block_size;
    image->checksum_mode = parsed->checksum_mode;
    image->checksum_size = parsed->checksum_size;
    image->blocks_per_checksum = parsed->blocks_per_checksum;
    image->big_endian = parsed->big_endian;
    xx_rt_memcpy(image->fs_name, parsed->fs_name, sizeof(image->fs_name));
    image->restored_size = parsed->restored_size;
    image->archive_end = parsed->archive_end;
    self->endian = parsed->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_partclone_image_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_partclone_image_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_partclone_image *)self)->number_of_records;
}

bool xx_partclone_image_unpack_to_device(xx_partclone_image *image,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd) {
    xx_pcl_private parsed;
    if (!image || !destination) return false;
    return xx_pcl_parse(&image->format, &parsed, pd) &&
           xx_pcl_restore(&image->format, &parsed, destination, pd);
}

xx_archive_record_state *xx_partclone_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_pcl_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_pcl_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_pcl_copy_options(&state->options, options) ||
        !xx_pcl_parse(self, &stream->parsed, pd)) {
        xx_pcl_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_pcl_stream_free;
    state->total_records = 1;
    if (xx_pcl_populate_record(&state->current_record, &stream->parsed,
                               self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_partclone_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_partclone_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_pcl_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pcl_stream *)state->internal_state;
    ++stream->index;
    /* Exactly one member, so the first move ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_partclone_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_pcl_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    size_t base_length;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pcl_stream *)state->internal_state;
    if (stream->index != 0U) return false;
    /* A stricter ceiling supplied with this read session applies here. */
    if (stream->parsed.restored_size < 0 ||
        (uint64_t)stream->parsed.restored_size >
            xx_pcl_max_expanded(self, &state->options)) {
        return false;
    }
    option = xx_pcl_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        return stream->parsed.archive_end >= 0 &&
               stream->parsed.archive_end <= stream->parsed.input_size;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    base_length = xx_str_len(base);
    if (base_length && base[base_length - 1U] != '/' &&
        base[base_length - 1U] != '\\') {
        destination_path = xx_str_concat3(base, "/", XX_PCL_MEMBER_NAME);
    } else {
        destination_path = xx_str_concat(base, XX_PCL_MEMBER_NAME);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    created = true;
    result = xx_pcl_restore(self, &stream->parsed, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_partclone_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_partclone_image_get_restored_size(const xx_partclone_image *image) {
    return image ? image->restored_size : -1;
}

int64_t xx_partclone_image_get_archive_end(const xx_partclone_image *image) {
    return image ? image->archive_end : -1;
}
