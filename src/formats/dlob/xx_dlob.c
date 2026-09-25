/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dlob/xx_dlob.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DLOB exists in the enum. */
#ifdef DLOB
#define XX_DLOB_FILE_TYPE XX_FILE_TYPE_DLOB
#else
#define XX_DLOB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the digest pass. */
#define XX_DLOB_STAGING_SIZE 65536U
/** The single payload record this container ever publishes. */
#define XX_DLOB_MEMBER_NAME "dlob.bin"

typedef struct xx_dlob_private_s {
    int64_t input_size;
    int64_t header_offset;  /**< The outer entity, i.e. base_address. */
    int64_t inner_offset;   /**< The payload-bearing entity's header. */
    int64_t data_offset;
    int64_t archive_end;
    uint32_t outer_meta_size;
    uint32_t inner_meta_size;
    uint32_t image_size;
    size_t count;
} xx_dlob_private;

typedef struct xx_dlob_archive_stream_s {
    xx_dlob_private parsed;
    size_t index;
} xx_dlob_archive_stream;

static void xx_dlob_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* MD5                                                                       */
/* ------------------------------------------------------------------------ */

/*
 * The inner entity carries the SEAMA MD5 over its payload and this library
 * ships no MD5, so a file-local RFC 1321 implementation lives here.  It is
 * deliberately a copy of the one in xx_seama.c rather than a shared helper:
 * the two readers are separate translation units by design and neither may
 * add a public symbol to the shared headers.
 */

typedef struct xx_dlob_md5_s {
    uint32_t state[4];
    uint64_t length; /**< Bytes absorbed so far. */
    uint8_t block[64];
    size_t used;
} xx_dlob_md5;

static const uint32_t xx_dlob_md5_k[64] = {
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

static const unsigned xx_dlob_md5_shift[64] = {
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U};

static uint32_t xx_dlob_md5_rotate(uint32_t value, unsigned bits) {
    return (uint32_t)((value << bits) | (value >> (32U - bits)));
}

static void xx_dlob_md5_compress(uint32_t state[4], const uint8_t block[64]) {
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
        mixed = mixed + a + xx_dlob_md5_k[index] + words[word];
        a = d;
        d = c;
        c = b;
        b = b + xx_dlob_md5_rotate(mixed, xx_dlob_md5_shift[index]);
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void xx_dlob_md5_init(xx_dlob_md5 *context) {
    xx_mem_zero(context, sizeof(*context));
    context->state[0] = UINT32_C(0x67452301);
    context->state[1] = UINT32_C(0xefcdab89);
    context->state[2] = UINT32_C(0x98badcfe);
    context->state[3] = UINT32_C(0x10325476);
}

static void xx_dlob_md5_update(xx_dlob_md5 *context, const uint8_t *data,
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
            xx_dlob_md5_compress(context->state, context->block);
            context->used = 0U;
        }
    }
}

static void xx_dlob_md5_final(xx_dlob_md5 *context, uint8_t digest[16]) {
    uint64_t bits = context->length * 8U;
    uint8_t pad = 0x80U;
    uint8_t tail[8];
    unsigned index;
    xx_dlob_md5_update(context, &pad, 1U);
    while (context->used != 56U) {
        uint8_t zero = 0U;
        xx_dlob_md5_update(context, &zero, 1U);
    }
    for (index = 0U; index < 8U; ++index) {
        tail[index] = (uint8_t)((bits >> (8U * index)) & 0xFFU);
    }
    xx_dlob_md5_update(context, tail, sizeof(tail));
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

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_dlob_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_dlob_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dlob_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_dlob_private_cleanup(xx_dlob_private *parsed) {
    if (!parsed) return;
    /* Nothing here owns heap memory; the cleanup exists for symmetry with the
     * other readers and to leave a failed parse in a defined state. */
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->header_offset = -1;
    parsed->inner_offset = -1;
    parsed->data_offset = -1;
    parsed->archive_end = -1;
}

/* MD5 over a device range, streamed so a large payload is never resident. */
static bool xx_dlob_digest_range(xx_io_device *device, int64_t offset,
                                 int64_t size, uint8_t digest[16],
                                 xx_pd_struct *pd) {
    uint8_t staging[XX_DLOB_STAGING_SIZE];
    xx_dlob_md5 context;
    if (!device || offset < 0 || size < 0) return false;
    xx_dlob_md5_init(&context);
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
        xx_dlob_md5_update(&context, staging, step);
        size -= (int64_t)step;
    }
    xx_dlob_md5_final(&context, digest);
    return true;
}

static bool xx_dlob_digest_is_zero(const uint8_t digest[16]) {
    unsigned index;
    for (index = 0U; index < 16U; ++index) {
        if (digest[index] != 0U) return false;
    }
    return true;
}

static bool xx_dlob_parse(Abstractformat *self, xx_dlob_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t outer[XX_DLOB_HEADER_SIZE];
    uint8_t inner[XX_DLOB_HEADER_SIZE + XX_DLOB_DIGEST_SIZE];
    uint32_t outer_size;
    uint32_t inner_size;
    int64_t outer_meta;
    int64_t inner_meta;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->header_offset = -1;
        parsed->inner_offset = -1;
        parsed->data_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->header_offset = self->base_address;

    /* Outer entity: magic, zero reserved, a metadata block, and NO payload.
     * The zero size is the whole point - it is what distinguishes this shape
     * from a plain SEAMA image, and it is why no digest follows. */
    if (!xx_dlob_range_within(parsed->input_size, self->base_address,
                              XX_DLOB_HEADER_SIZE) ||
        !xx_dlob_read_at(self->device, self->base_address, outer,
                         sizeof(outer)) ||
        xx_data_get_u32(outer, sizeof(outer), 0U, true) != XX_DLOB_MAGIC ||
        xx_data_get_u16(outer, sizeof(outer), 4U, true) != 0U) {
        goto fail;
    }
    parsed->outer_meta_size = xx_data_get_u16(outer, sizeof(outer), 6U, true);
    outer_size = xx_data_get_u32(outer, sizeof(outer), 8U, true);
    if (outer_size != 0U || parsed->outer_meta_size == 0U) goto fail;
    if (!xx_dlob_add(self->base_address, XX_DLOB_HEADER_SIZE, &outer_meta) ||
        !xx_dlob_add(outer_meta, parsed->outer_meta_size,
                     &parsed->inner_offset) ||
        parsed->inner_offset > parsed->input_size) {
        goto fail;
    }

    /* Inner entity: same layout, but it carries the firmware and therefore
     * also the sixteen digest bytes. */
    if (!xx_dlob_range_within(parsed->input_size, parsed->inner_offset,
                              XX_DLOB_HEADER_SIZE + XX_DLOB_DIGEST_SIZE) ||
        !xx_dlob_read_at(self->device, parsed->inner_offset, inner,
                         sizeof(inner)) ||
        xx_data_get_u32(inner, sizeof(inner), 0U, true) != XX_DLOB_MAGIC ||
        xx_data_get_u16(inner, sizeof(inner), 4U, true) != 0U) {
        goto fail;
    }
    parsed->inner_meta_size = xx_data_get_u16(inner, sizeof(inner), 6U, true);
    inner_size = xx_data_get_u32(inner, sizeof(inner), 8U, true);
    if (inner_size == 0U) goto fail;
    parsed->image_size = inner_size;
    if (!xx_dlob_add(parsed->inner_offset,
                     XX_DLOB_HEADER_SIZE + XX_DLOB_DIGEST_SIZE, &inner_meta) ||
        !xx_dlob_add(inner_meta, parsed->inner_meta_size,
                     &parsed->data_offset) ||
        !xx_dlob_range_within(parsed->input_size, parsed->data_offset,
                              (int64_t)inner_size) ||
        !xx_dlob_add(parsed->data_offset, inner_size, &parsed->archive_end)) {
        goto fail;
    }

    /* A zero digest is how some vendor tools mark "unchecked"; a non-zero one
     * is binding, because the bootloader treats it that way. */
    if (!xx_dlob_digest_is_zero(inner + XX_DLOB_HEADER_SIZE)) {
        uint8_t computed[16];
        if (!xx_dlob_digest_range(self->device, parsed->data_offset,
                                  (int64_t)inner_size, computed, pd) ||
            xx_rt_memcmp(computed, inner + XX_DLOB_HEADER_SIZE, 16U) != 0) {
            goto fail;
        }
    }
    parsed->count = 1U;
    return true;
fail:
    xx_dlob_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dlob_copy_options(xx_list_s *destination,
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

static const xx_var *xx_dlob_find_option(const xx_list_s *options,
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

static bool xx_dlob_populate_record(xx_archive_record *record,
                                    const xx_dlob_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* Everything from the outer magic to the first payload byte is header as
     * far as a caller is concerned: two entity headers and two metadata
     * blocks. */
    record->header_offset = parsed->header_offset;
    record->header_size = parsed->data_offset - parsed->header_offset;
    record->data_offset = parsed->data_offset;
    record->compressed_size = (int64_t)parsed->image_size;
    /* The name is a literal chosen here, never taken from the file, so it
     * needs no sanitising before use as a destination path component. */
    return xx_archive_record_set_original_name(record, XX_DLOB_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          parsed->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_dlob_archive_stream_free(void *pointer) {
    xx_dlob_archive_stream *stream = (xx_dlob_archive_stream *)pointer;
    if (!stream) return;
    xx_dlob_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dlob_init(xx_dlob *dlob, xx_io_device *dev, int64_t base_address) {
    if (!dlob) return;
    xx_mem_zero(dlob, sizeof(*dlob));
    xx_format_init(&dlob->format, dev, base_address);
    dlob->format.endian = XX_ENDIAN_BIG;
    dlob->format.file_type = XX_DLOB_FILE_TYPE;
    dlob->format.format_type = XX_TYPE_ARCHIVE;
    dlob->format.is_archive = true;
    xx_format_set_mime_type(&dlob->format, "application/x-dlob-firmware");
    xx_format_set_extension(&dlob->format, "bin");
    dlob->format.check_is_valid = xx_dlob_check_is_valid;
    dlob->format.handle_base_info = xx_dlob_handle_base_info;
    dlob->format.get_format_size = xx_dlob_get_format_size;
    dlob->format.get_number_of_archive_records =
        xx_dlob_get_number_of_archive_records;
    dlob->format.create_archive_records_reading =
        xx_dlob_create_archive_records_reading;
    dlob->format.get_current_archive_record = xx_dlob_get_current_archive_record;
    dlob->format.unpack_current_archive_record =
        xx_dlob_unpack_current_archive_record;
    dlob->format.archive_record_move_to_next =
        xx_dlob_archive_record_move_to_next;
    dlob->format.free_archive_records_reading =
        xx_dlob_free_archive_records_reading;
    dlob->format.destroy = xx_dlob_vtable_destroy;
    dlob->data_offset = -1;
    dlob->archive_end = -1;
}

xx_dlob *xx_dlob_create(xx_io_device *dev, int64_t base_address) {
    xx_dlob *dlob = (xx_dlob *)xx_mem_alloc(sizeof(*dlob));
    if (dlob) xx_dlob_init(dlob, dev, base_address);
    return dlob;
}

void xx_dlob_destroy(xx_dlob *dlob) {
    if (!dlob) return;
    if (dlob->internal) {
        xx_dlob_private_cleanup((xx_dlob_private *)dlob->internal);
        xx_mem_free(dlob->internal);
        dlob->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dlob->format);
}

static void xx_dlob_vtable_destroy(Abstractformat *self) {
    xx_dlob_destroy((xx_dlob *)self);
}

void xx_dlob_free(xx_dlob *dlob) {
    if (!dlob) return;
    xx_dlob_destroy(dlob);
    xx_mem_free(dlob);
}

bool xx_dlob_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlob_private parsed;
    bool result = xx_dlob_parse(self, &parsed, pd);
    xx_dlob_private_cleanup(&parsed);
    return result;
}

bool xx_dlob_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlob_private *parsed;
    xx_dlob *dlob = (xx_dlob *)self;
    int64_t total_size;
    if (!self || !dlob) return false;
    parsed = (xx_dlob_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dlob_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dlob->internal) {
        xx_dlob_private_cleanup((xx_dlob_private *)dlob->internal);
        xx_mem_free(dlob->internal);
    }
    dlob->internal = parsed;
    dlob->number_of_records = parsed->count;
    dlob->number_of_members = parsed->count;
    dlob->outer_meta_size = parsed->outer_meta_size;
    dlob->inner_meta_size = parsed->inner_meta_size;
    dlob->image_size = parsed->image_size;
    dlob->data_offset = parsed->data_offset;
    dlob->archive_end = parsed->archive_end;
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

int64_t xx_dlob_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dlob_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dlob *)self)->number_of_records;
}

xx_archive_record_state *xx_dlob_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dlob_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dlob_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dlob_copy_options(&state->options, options) ||
        !xx_dlob_parse(self, &stream->parsed, pd)) {
        xx_dlob_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dlob_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_dlob_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dlob_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dlob_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_dlob_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dlob_archive_stream *)state->internal_state;
    ++stream->index;
    /* One payload only, so the first move always ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_dlob_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_dlob_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_dlob_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dlob_get_number_of_records(const xx_dlob *dlob) {
    return dlob ? dlob->number_of_records : 0U;
}
uint64_t xx_dlob_get_number_of_members(const xx_dlob *dlob) {
    return dlob ? dlob->number_of_members : 0U;
}
uint32_t xx_dlob_get_image_size(const xx_dlob *dlob) {
    return dlob ? dlob->image_size : 0U;
}
int64_t xx_dlob_get_archive_end(const xx_dlob *dlob) {
    return dlob ? dlob->archive_end : -1;
}
