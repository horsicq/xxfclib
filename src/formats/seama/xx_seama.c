/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/seama/xx_seama.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_SEAMA exists in the enum. */
#ifdef SEAMA
#define XX_SEAMA_FILE_TYPE XX_FILE_TYPE_SEAMA
#else
#define XX_SEAMA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the digest pass. */
#define XX_SEAMA_STAGING_SIZE 65536U

typedef struct xx_seama_entity_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    uint32_t header_size; /**< 12, or 28 when a digest is present. */
    uint32_t meta_size;
} xx_seama_entity;

typedef struct xx_seama_private_s {
    xx_seama_entity entities[XX_SEAMA_MAX_ENTITIES];
    size_t count;          /**< Payload entities, i.e. published records. */
    size_t entity_count;   /**< Entities walked, payload or not. */
    int64_t input_size;
    int64_t archive_end;
    uint32_t meta_size;
    uint32_t image_size;
} xx_seama_private;

typedef struct xx_seama_archive_stream_s {
    xx_seama_private parsed;
    size_t index;
} xx_seama_archive_stream;

static void xx_seama_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* MD5                                                                       */
/* ------------------------------------------------------------------------ */

/*
 * This library ships no MD5, and SEAMA's only integrity check is an MD5 over
 * the payload, so a file-local RFC 1321 implementation lives here.  It is the
 * compact table-driven form: no CRT calls, no allocation, and the message
 * words are assembled byte by byte so it is endian agnostic.
 */

typedef struct xx_seama_md5_s {
    uint32_t state[4];
    uint64_t length;  /**< Bytes absorbed so far. */
    uint8_t block[64];
    size_t used;
} xx_seama_md5;

static const uint32_t xx_seama_md5_k[64] = {
    UINT32_C(0xd76aa478), UINT32_C(0xe8c7b756), UINT32_C(0x242070db),
    UINT32_C(0xc1bdceee), UINT32_C(0xf57c0faf), UINT32_C(0x4787c62a),
    UINT32_C(0xa8304613), UINT32_C(0xfd469501), UINT32_C(0x698098d8),
    UINT32_C(0x8b44f7af), UINT32_C(0xffff5bb1), UINT32_C(0x895cd7be),
    UINT32_C(0x6b901122), UINT32_C(0xfd987193), UINT32_C(0xa679438e),
    UINT32_C(0x49b40821), UINT32_C(0xf61e2562), UINT32_C(0xc040b340),
    UINT32_C(0x265e5a51), UINT32_C(0xe9b6c7aa), UINT32_C(0xd62f105d),
    UINT32_C(0x02441453), UINT32_C(0xd8a1e681), UINT32_C(0xe7d3fbc8),
    UINT32_C(0x21e1cde6), UINT32_C(0xc33707d6), UINT32_C(0xf4d50d87),
    UINT32_C(0x455a14ed), UINT32_C(0xa9e3e905), UINT32_C(0xfcefa3f8),
    UINT32_C(0x676f02d9), UINT32_C(0x8d2a4c8a), UINT32_C(0xfffa3942),
    UINT32_C(0x8771f681), UINT32_C(0x6d9d6122), UINT32_C(0xfde5380c),
    UINT32_C(0xa4beea44), UINT32_C(0x4bdecfa9), UINT32_C(0xf6bb4b60),
    UINT32_C(0xbebfbc70), UINT32_C(0x289b7ec6), UINT32_C(0xeaa127fa),
    UINT32_C(0xd4ef3085), UINT32_C(0x04881d05), UINT32_C(0xd9d4d039),
    UINT32_C(0xe6db99e5), UINT32_C(0x1fa27cf8), UINT32_C(0xc4ac5665),
    UINT32_C(0xf4292244), UINT32_C(0x432aff97), UINT32_C(0xab9423a7),
    UINT32_C(0xfc93a039), UINT32_C(0x655b59c3), UINT32_C(0x8f0ccc92),
    UINT32_C(0xffeff47d), UINT32_C(0x85845dd1), UINT32_C(0x6fa87e4f),
    UINT32_C(0xfe2ce6e0), UINT32_C(0xa3014314), UINT32_C(0x4e0811a1),
    UINT32_C(0xf7537e82), UINT32_C(0xbd3af235), UINT32_C(0x2ad7d2bb),
    UINT32_C(0xeb86d391)};

static const unsigned xx_seama_md5_shift[64] = {
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U};

static uint32_t xx_seama_md5_rotate(uint32_t value, unsigned bits) {
    return (uint32_t)((value << bits) | (value >> (32U - bits)));
}

static void xx_seama_md5_compress(uint32_t state[4], const uint8_t block[64]) {
    uint32_t words[16];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    unsigned index;
    for (index = 0U; index < 16U; ++index) {
        words[index] = (uint32_t)block[index * 4U] |
                       ((uint32_t)block[index * 4U + 1U] << 8U) |
                       ((uint32_t)block[index * 4U + 2U] << 16U) |
                       ((uint32_t)block[index * 4U + 3U] << 24U);
    }
    for (index = 0U; index < 64U; ++index) {
        uint32_t mixed;
        unsigned word;
        if (index < 16U) {
            mixed = (b & c) | (~b & d);
            word = index;
        } else if (index < 32U) {
            mixed = (d & b) | (~d & c);
            word = (5U * index + 1U) & 15U;
        } else if (index < 48U) {
            mixed = b ^ c ^ d;
            word = (3U * index + 5U) & 15U;
        } else {
            mixed = c ^ (b | ~d);
            word = (7U * index) & 15U;
        }
        mixed = mixed + a + xx_seama_md5_k[index] + words[word];
        a = d;
        d = c;
        c = b;
        b = b + xx_seama_md5_rotate(mixed, xx_seama_md5_shift[index]);
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void xx_seama_md5_init(xx_seama_md5 *context) {
    xx_mem_zero(context, sizeof(*context));
    context->state[0] = UINT32_C(0x67452301);
    context->state[1] = UINT32_C(0xefcdab89);
    context->state[2] = UINT32_C(0x98badcfe);
    context->state[3] = UINT32_C(0x10325476);
}

static void xx_seama_md5_update(xx_seama_md5 *context, const uint8_t *data,
                                size_t size) {
    context->length += (uint64_t)size;
    while (size != 0U) {
        size_t room = sizeof(context->block) - context->used;
        size_t step = (size < room) ? size : room;
        xx_rt_memcpy(context->block + context->used, data, step);
        context->used += step;
        data += step;
        size -= step;
        if (context->used == sizeof(context->block)) {
            xx_seama_md5_compress(context->state, context->block);
            context->used = 0U;
        }
    }
}

static void xx_seama_md5_final(xx_seama_md5 *context, uint8_t digest[16]) {
    uint64_t bits = context->length * 8U;
    uint8_t pad = 0x80U;
    uint8_t tail[8];
    unsigned index;
    xx_seama_md5_update(context, &pad, 1U);
    /* The length update above also counted the padding byte; the padding is
     * driven by the buffer fill level, so that miscount is irrelevant as long
     * as the bit length captured before padding is the one appended. */
    while (context->used != 56U) {
        uint8_t zero = 0U;
        xx_seama_md5_update(context, &zero, 1U);
    }
    for (index = 0U; index < 8U; ++index) {
        tail[index] = (uint8_t)((bits >> (8U * index)) & 0xFFU);
    }
    xx_seama_md5_update(context, tail, sizeof(tail));
    for (index = 0U; index < 4U; ++index) {
        digest[index * 4U] = (uint8_t)(context->state[index] & 0xFFU);
        digest[index * 4U + 1U] =
            (uint8_t)((context->state[index] >> 8U) & 0xFFU);
        digest[index * 4U + 2U] =
            (uint8_t)((context->state[index] >> 16U) & 0xFFU);
        digest[index * 4U + 3U] =
            (uint8_t)((context->state[index] >> 24U) & 0xFFU);
    }
}

/* ------------------------------------------------------------------------ */
/* Bounds helpers                                                            */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: long is 32-bit on Win64 and a flash
 * image can legitimately sit past the 2 GiB mark inside a dump. */
static bool xx_seama_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_seama_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_seama_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_seama_private_cleanup(xx_seama_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < XX_SEAMA_MAX_ENTITIES; ++index) {
        if (parsed->entities[index].name) {
            xx_str_free(parsed->entities[index].name);
        }
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* Build "seama<index>.bin" without pulling in snprintf. */
static char *xx_seama_entity_name(size_t index) {
    char buffer[24];
    const char prefix[] = "seama";
    const char suffix[] = ".bin";
    size_t used = 0U;
    size_t position;
    for (position = 0U; position + 1U < sizeof(prefix); ++position) {
        buffer[used++] = prefix[position];
    }
    if (index == 0U) {
        buffer[used++] = '0';
    } else {
        char reversed[8];
        size_t length = 0U;
        while (index != 0U && length < sizeof(reversed)) {
            reversed[length++] = (char)('0' + (index % 10U));
            index /= 10U;
        }
        while (length != 0U) buffer[used++] = reversed[--length];
    }
    for (position = 0U; position + 1U < sizeof(suffix); ++position) {
        buffer[used++] = suffix[position];
    }
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/* MD5 over a device range, streamed so a large payload is never resident. */
static bool xx_seama_digest_range(xx_io_device *device, int64_t offset,
                                  int64_t size, uint8_t digest[16],
                                  xx_pd_struct *pd) {
    uint8_t staging[XX_SEAMA_STAGING_SIZE];
    xx_seama_md5 context;
    if (!device || offset < 0 || size < 0) return false;
    xx_seama_md5_init(&context);
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step =
            (size < (int64_t)sizeof(staging)) ? (size_t)size : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        xx_seama_md5_update(&context, staging, step);
        size -= (int64_t)step;
    }
    xx_seama_md5_final(&context, digest);
    return true;
}

static bool xx_seama_digest_is_zero(const uint8_t digest[16]) {
    unsigned index;
    for (index = 0U; index < 16U; ++index) {
        if (digest[index] != 0U) return false;
    }
    return true;
}

/*
 * Walk the entity chain.  The first entity must be well formed; a later one
 * that does not begin with the magic simply ends the chain, because a SEAMA
 * file is routinely padded out to an erase block.
 */
static bool xx_seama_parse(Abstractformat *self, xx_seama_private *parsed,
                           xx_pd_struct *pd) {
    int64_t cursor;
    bool first = true;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    cursor = self->base_address;
    parsed->archive_end = cursor;
    while (parsed->entity_count < XX_SEAMA_MAX_ENTITIES) {
        uint8_t header[XX_SEAMA_HEADER_SIZE + XX_SEAMA_DIGEST_SIZE];
        uint32_t meta_size;
        uint32_t data_size;
        uint32_t header_size;
        int64_t meta_offset;
        int64_t data_offset;
        int64_t entity_end;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_seama_range_within(parsed->input_size, cursor,
                                   XX_SEAMA_HEADER_SIZE) ||
            !xx_seama_read_at(self->device, cursor, header,
                              XX_SEAMA_HEADER_SIZE)) {
            break;
        }
        if (xx_data_get_u32(header, XX_SEAMA_HEADER_SIZE, 0U, true) !=
            XX_SEAMA_MAGIC) {
            break;
        }
        /* seama.c always writes the reserved word as zero; insisting on that
         * is what keeps a four-byte magic from matching arbitrary data. */
        if (xx_data_get_u16(header, XX_SEAMA_HEADER_SIZE, 4U, true) != 0U) {
            break;
        }
        meta_size = xx_data_get_u16(header, XX_SEAMA_HEADER_SIZE, 6U, true);
        data_size = xx_data_get_u32(header, XX_SEAMA_HEADER_SIZE, 8U, true);
        /* The digest is written only when there is a payload to digest. */
        header_size = XX_SEAMA_HEADER_SIZE +
                      (data_size != 0U ? XX_SEAMA_DIGEST_SIZE : 0U);
        if (!xx_seama_add(cursor, header_size, &meta_offset) ||
            !xx_seama_add(meta_offset, meta_size, &data_offset) ||
            !xx_seama_add(data_offset, data_size, &entity_end) ||
            entity_end > parsed->input_size) {
            break;
        }
        if (data_size != 0U) {
            uint8_t computed[16];
            if (!xx_seama_read_at(self->device, cursor, header,
                                  XX_SEAMA_HEADER_SIZE +
                                      XX_SEAMA_DIGEST_SIZE)) {
                break;
            }
            /* A zero digest is how some vendor tools mark "unchecked"; a
             * non-zero one is treated as binding, because the bootloader
             * treats it that way too. */
            if (!xx_seama_digest_is_zero(header + XX_SEAMA_HEADER_SIZE)) {
                if (!xx_seama_digest_range(self->device, data_offset,
                                           (int64_t)data_size, computed, pd) ||
                    xx_rt_memcmp(computed, header + XX_SEAMA_HEADER_SIZE,
                                 16U) != 0) {
                    break;
                }
            }
        }
        if (first) {
            parsed->meta_size = meta_size;
            first = false;
        }
        if (data_size != 0U) {
            xx_seama_entity *entity = &parsed->entities[parsed->count];
            entity->header_offset = cursor;
            entity->header_size = header_size;
            entity->meta_size = meta_size;
            entity->data_offset = data_offset;
            entity->data_size = (int64_t)data_size;
            /* The name is generated here, never read from the file, so it
             * needs no sanitising before use as a path component. */
            entity->name = xx_seama_entity_name(parsed->count);
            if (!entity->name) goto fail;
            if (parsed->count == 0U) parsed->image_size = data_size;
            ++parsed->count;
        }
        ++parsed->entity_count;
        parsed->archive_end = entity_end;
        cursor = entity_end;
        /* An entity with neither metadata nor payload carries no information
         * and advances the cursor by only twelve bytes; the entity cap would
         * stop it eventually, but there is nothing to gain by continuing. */
        if (data_size == 0U && meta_size == 0U) break;
    }
    /* A chain that never reaches a payload publishes nothing. */
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_seama_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_seama_copy_options(xx_list_s *destination,
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

static const xx_var *xx_seama_find_option(const xx_list_s *options,
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

static bool xx_seama_populate_record(xx_archive_record *record,
                                     const xx_seama_entity *entity) {
    if (!record || !entity || !entity->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entity->header_offset;
    record->header_size = (int64_t)entity->header_size;
    record->data_offset = entity->data_offset;
    record->compressed_size = entity->data_size;
    return xx_archive_record_set_original_name(record, entity->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entity->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entity->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_seama_archive_stream_free(void *pointer) {
    xx_seama_archive_stream *stream = (xx_seama_archive_stream *)pointer;
    if (!stream) return;
    xx_seama_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_seama_init(xx_seama *seama, xx_io_device *dev, int64_t base_address) {
    if (!seama) return;
    xx_mem_zero(seama, sizeof(*seama));
    xx_format_init(&seama->format, dev, base_address);
    seama->format.endian = XX_ENDIAN_BIG;
    seama->format.file_type = XX_SEAMA_FILE_TYPE;
    seama->format.format_type = XX_TYPE_ARCHIVE;
    seama->format.is_archive = true;
    xx_format_set_mime_type(&seama->format, "application/x-seama-firmware");
    xx_format_set_extension(&seama->format, "seama");
    seama->format.check_is_valid = xx_seama_check_is_valid;
    seama->format.handle_base_info = xx_seama_handle_base_info;
    seama->format.get_format_size = xx_seama_get_format_size;
    seama->format.get_number_of_archive_records =
        xx_seama_get_number_of_archive_records;
    seama->format.create_archive_records_reading =
        xx_seama_create_archive_records_reading;
    seama->format.get_current_archive_record =
        xx_seama_get_current_archive_record;
    seama->format.unpack_current_archive_record =
        xx_seama_unpack_current_archive_record;
    seama->format.archive_record_move_to_next =
        xx_seama_archive_record_move_to_next;
    seama->format.free_archive_records_reading =
        xx_seama_free_archive_records_reading;
    seama->format.destroy = xx_seama_vtable_destroy;
    seama->archive_end = -1;
}

xx_seama *xx_seama_create(xx_io_device *dev, int64_t base_address) {
    xx_seama *seama = (xx_seama *)xx_mem_alloc(sizeof(*seama));
    if (seama) xx_seama_init(seama, dev, base_address);
    return seama;
}

void xx_seama_destroy(xx_seama *seama) {
    if (!seama) return;
    if (seama->internal) {
        xx_seama_private_cleanup((xx_seama_private *)seama->internal);
        xx_mem_free(seama->internal);
        seama->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&seama->format);
}

static void xx_seama_vtable_destroy(Abstractformat *self) {
    xx_seama_destroy((xx_seama *)self);
}

void xx_seama_free(xx_seama *seama) {
    if (!seama) return;
    xx_seama_destroy(seama);
    xx_mem_free(seama);
}

bool xx_seama_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_seama_private parsed;
    bool result = xx_seama_parse(self, &parsed, pd);
    xx_seama_private_cleanup(&parsed);
    return result;
}

bool xx_seama_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_seama_private *parsed;
    xx_seama *seama = (xx_seama *)self;
    int64_t total_size;
    if (!self || !seama) return false;
    parsed = (xx_seama_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_seama_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (seama->internal) {
        xx_seama_private_cleanup((xx_seama_private *)seama->internal);
        xx_mem_free(seama->internal);
    }
    seama->internal = parsed;
    seama->number_of_records = parsed->count;
    seama->number_of_members = parsed->count;
    seama->number_of_entities = parsed->entity_count;
    seama->meta_size = parsed->meta_size;
    seama->image_size = parsed->image_size;
    seama->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_seama_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_seama_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_seama *)self)->number_of_records;
}

xx_archive_record_state *xx_seama_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_seama_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_seama_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_seama_copy_options(&state->options, options) ||
        !xx_seama_parse(self, &stream->parsed, pd)) {
        xx_seama_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_seama_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_seama_populate_record(&state->current_record,
                                 &stream->parsed.entities[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_seama_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_seama_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_seama_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_seama_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_seama_populate_record(&state->current_record,
                                  &stream->parsed.entities[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_seama_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_seama_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
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
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_seama_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_seama_get_number_of_records(const xx_seama *seama) {
    return seama ? seama->number_of_records : 0U;
}
uint64_t xx_seama_get_number_of_members(const xx_seama *seama) {
    return seama ? seama->number_of_members : 0U;
}
uint64_t xx_seama_get_number_of_entities(const xx_seama *seama) {
    return seama ? seama->number_of_entities : 0U;
}
uint32_t xx_seama_get_meta_size(const xx_seama *seama) {
    return seama ? seama->meta_size : 0U;
}
uint32_t xx_seama_get_image_size(const xx_seama *seama) {
    return seama ? seama->image_size : 0U;
}
int64_t xx_seama_get_archive_end(const xx_seama *seama) {
    return seama ? seama->archive_end : -1;
}
