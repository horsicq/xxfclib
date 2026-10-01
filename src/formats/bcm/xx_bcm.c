/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BCM, Ilya Muravyov's BWT + context-mixing compressor.  xx_bcm.h carries the
 * stream layout.
 *
 * "BCM!" (BCM 1.xx) is decoded here.  The model -- a 32-bit binary range
 * coder with 18-bit probabilities, an order-0 counter (rate 2), two order-1
 * counters keyed on the previous two symbols (rate 4, only the first one is
 * trained), and an interpolated SSE stage (rate 6) selected by a run flag --
 * and the inverse BWT follow bcm.cpp v1.30 by Ilya Muravyov
 * (https://github.com/encode84/bcm), MIT licence, Copyright (C) 2008-2018
 * Ilya Muravyov.  The code below is a C re-expression of that design, not
 * a copy of it.
 *
 * The container stores no total length and no member name, so the one member
 * is called "payload" and its size is only known after decoding.  Detection
 * decodes the first block header (length and primary index) and stops there;
 * the whole stream is decoded only on extraction, where the stored CRC-32 is
 * checked.
 *
 * The older "BCM1".."BCM9" streams (0.xx releases) use a different bitstream
 * that is not decoded: they are identified and sized and report zero
 * records, as before.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bcm/xx_bcm.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef BCM
#define XX_BCM_FILE_TYPE XX_FILE_TYPE_BCM
#else
#define XX_BCM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_BCM_MAGIC_SIZE 4U
/* Old streams: four magic bytes cannot be a compressed stream on their own
 * (XArchive uses the same floor). */
#define XX_BCM_MIN_SIZE 8
/* "BCM!": magic, then at least the 0 terminator word, the CRC word and the
 * four flush bytes -- exactly what bcm writes for an empty input. */
#define XX_BCM1_MIN_SIZE 16
/* bcm's -b switch takes the block size in MB; this is the largest block the
 * reader will rebuild (it needs five bytes of memory per block byte). */
#define XX_BCM_MAX_BLOCK ((uint32_t)256U * 1024U * 1024U)
#define XX_BCM_WINDOW 65536U
#define XX_BCM_OUT_BUFFER 65536U
#define XX_BCM_PAYLOAD_NAME "payload"

static void xx_bcm_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Input window and range decoder                                            */
/* ------------------------------------------------------------------------ */

typedef struct bcm_input_s {
    xx_io_device *device;
    int64_t next;      /**< Device offset of the next byte to buffer. */
    int64_t end;       /**< Device offset one past the stream. */
    size_t length;
    size_t offset;
    int64_t consumed;  /**< Bytes taken after the magic. */
    bool overrun;      /**< A byte past the end was needed. */
    bool io_error;
    uint8_t buffer[XX_BCM_WINDOW];
} bcm_input;

static uint32_t bcm_getc(bcm_input *in) {
    if (in->offset >= in->length) {
        int64_t left = in->end - in->next;
        size_t want, done = 0U;
        if (left <= 0) {
            /* A well-formed stream never reads past its last byte (the
             * encoder flushes exactly the four bytes the decoder primes
             * with), so running dry is corruption. */
            in->overrun = true;
            return 0xFFU;
        }
        want = left < (int64_t)XX_BCM_WINDOW ? (size_t)left
                                              : (size_t)XX_BCM_WINDOW;
        if (xx_io_seek64(in->device, in->next, SEEK_SET) != 0) {
            in->io_error = true;
            in->overrun = true;
            return 0xFFU;
        }
        while (done < want) {
            ssize_t got = xx_io_read(in->device, in->buffer + done,
                                     want - done);
            if (got <= 0 || (size_t)got > want - done) {
                in->io_error = true;
                in->overrun = true;
                return 0xFFU;
            }
            done += (size_t)got;
        }
        in->next += (int64_t)want;
        in->length = want;
        in->offset = 0U;
    }
    ++in->consumed;
    return in->buffer[in->offset++];
}

typedef struct bcm_decoder_s {
    uint32_t low;
    uint32_t high;
    uint32_t code;
    bcm_input *in;
} bcm_decoder;

static void bcm_decoder_init(bcm_decoder *rc, bcm_input *in) {
    int i;
    rc->low = 0U;
    rc->high = 0xFFFFFFFFU;
    rc->code = 0U;
    rc->in = in;
    for (i = 0; i < 4; ++i) rc->code = (rc->code << 8) | bcm_getc(in);
}

/* p is the probability of a 1 bit, 18 bits wide. */
static int bcm_decode_bit(bcm_decoder *rc, uint32_t p) {
    uint32_t mid = rc->low +
                   (uint32_t)(((uint64_t)(rc->high - rc->low) * p) >> 18);
    int bit = rc->code <= mid;
    if (bit)
        rc->high = mid;
    else
        rc->low = mid + 1U;
    while ((rc->low ^ rc->high) < (1U << 24)) {
        rc->low <<= 8;
        rc->high = (rc->high << 8) | 0xFFU;
        rc->code = (rc->code << 8) | bcm_getc(rc->in);
    }
    return bit;
}

static uint32_t bcm_decode32(bcm_decoder *rc) {
    uint32_t value = 0U;
    int i;
    for (i = 0; i < 32; ++i)
        value = (value << 1) | (uint32_t)bcm_decode_bit(rc, 1U << 17);
    return value;
}

/* ------------------------------------------------------------------------ */
/* Symbol model                                                              */
/* ------------------------------------------------------------------------ */

typedef struct bcm_model_s {
    uint16_t order0[256];
    uint16_t order1[256][256];
    uint16_t sse[2][256][17];
    uint32_t prev1;
    uint32_t prev2;
    uint32_t run;
} bcm_model;

static void bcm_model_init(bcm_model *m) {
    int i, j, k;
    for (i = 0; i < 256; ++i) m->order0[i] = 1U << 15;
    for (i = 0; i < 256; ++i)
        for (j = 0; j < 256; ++j) m->order1[i][j] = 1U << 15;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 256; ++j)
            for (k = 0; k < 17; ++k)
                m->sse[i][j][k] = (uint16_t)((k << 12) - (k == 16 ? 1 : 0));
    m->prev1 = 0U;
    m->prev2 = 0U;
    m->run = 0U;
}

#define BCM_ADAPT(counter, bit, rate) \
    do { \
        if (bit) \
            (counter) = (uint16_t)((counter) + (((counter) ^ 0xFFFFU) >> (rate))); \
        else \
            (counter) = (uint16_t)((counter) - ((counter) >> (rate))); \
    } while (0)

static uint32_t bcm_decode_symbol(bcm_model *m, bcm_decoder *rc) {
    uint16_t *first = m->order1[m->prev1];
    const uint16_t *second = m->order1[m->prev2];
    uint16_t (*sse)[17];
    uint32_t ctx = 1U;
    if (m->prev1 == m->prev2) {
        if (m->run < 3U) ++m->run;  /* only "> 2" is ever tested */
    } else {
        m->run = 0U;
    }
    sse = m->sse[m->run > 2U ? 1 : 0];
    while (ctx < 256U) {
        int p = (((int)m->order0[ctx] + (int)first[ctx]) * 7 +
                 (int)second[ctx] * 2) >> 4;
        int j = p >> 12;
        int x1 = sse[ctx][j];
        int x2 = sse[ctx][j + 1];
        /* (x2 - x1) may be negative; the reference relies on an arithmetic
         * shift here, so floor-divide explicitly. */
        int delta = (x2 - x1) * (p & 4095);
        int mixed = x1 + (delta >= 0 ? delta >> 12 : -((-delta + 4095) >> 12));
        int bit = bcm_decode_bit(rc, (uint32_t)(mixed * 3 + p));
        BCM_ADAPT(m->order0[ctx], bit, 2);
        BCM_ADAPT(first[ctx], bit, 4);
        BCM_ADAPT(sse[ctx][j], bit, 6);
        BCM_ADAPT(sse[ctx][j + 1], bit, 6);
        ctx = (ctx << 1) | (uint32_t)bit;
    }
    m->prev2 = m->prev1;
    m->prev1 = ctx & 0xFFU;
    return ctx & 0xFFU;
}

/* ------------------------------------------------------------------------ */
/* Output sink                                                               */
/* ------------------------------------------------------------------------ */

typedef struct bcm_output_s {
    xx_io_device *device; /**< NULL: count and checksum only. */
    uint32_t crc;
    uint64_t total;
    size_t length;
    bool failed;
    uint8_t buffer[XX_BCM_OUT_BUFFER];
} bcm_output;

static void bcm_output_flush(bcm_output *out) {
    size_t done = 0U;
    if (out->length == 0U) return;
    out->crc = xx_crc32_calc(out->crc, out->buffer, out->length);
    if (out->device) {
        while (done < out->length) {
            ssize_t put = xx_io_write(out->device, out->buffer + done,
                                      out->length - done);
            if (put <= 0 || (size_t)put > out->length - done) {
                out->failed = true;
                break;
            }
            done += (size_t)put;
        }
    }
    out->total += (uint64_t)out->length;
    out->length = 0U;
}

/* ------------------------------------------------------------------------ */
/* Stream walk                                                               */
/* ------------------------------------------------------------------------ */

static bool bcm_read_magic(Abstractformat *self, uint8_t magic[4]) {
    size_t completed = 0U;
    if (!self || !self->device || self->base_address < 0 ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0)
        return false;
    while (completed < XX_BCM_MAGIC_SIZE) {
        ssize_t received = xx_io_read(self->device, magic + completed,
                                      XX_BCM_MAGIC_SIZE - completed);
        if (received <= 0 ||
            (size_t)received > XX_BCM_MAGIC_SIZE - completed)
            return false;
        completed += (size_t)received;
    }
    return true;
}

/* Grow a byte buffer towards `need` bytes, geometrically, so the memory a
 * block costs follows the symbols actually decoded, not its declared size. */
static bool bcm_reserve(uint8_t **buffer, size_t *capacity, size_t need,
                        size_t limit) {
    size_t grown;
    uint8_t *moved;
    if (need <= *capacity) return true;
    grown = *capacity ? *capacity : (size_t)65536U;
    while (grown < need) grown = grown > limit / 2U ? limit : grown * 2U;
    if (grown > limit) grown = limit;
    if (grown < need) return false;
    moved = (uint8_t *)(*buffer ? xx_mem_realloc(*buffer, grown)
                                : xx_mem_alloc(grown));
    if (!moved) return false;
    *buffer = moved;
    *capacity = grown;
    return true;
}

/* mode 0: header probe only (first block length and primary index);
 * mode 1: full decode into `out` with CRC check. */
static bool bcm_walk(Abstractformat *self, bcm_output *out, bool full,
                     int64_t *consumed, xx_pd_struct *pd) {
    bcm_input *in = NULL;
    bcm_model *model = NULL;
    bcm_decoder rc;
    uint8_t *block = NULL;
    size_t block_capacity = 0U;
    uint32_t *next = NULL;
    size_t next_capacity = 0U;
    int64_t total;
    bool result = false;

    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        total - self->base_address < XX_BCM1_MIN_SIZE)
        return false;
    in = (bcm_input *)xx_mem_alloc(sizeof(*in));
    if (!in) return false;
    in->device = self->device;
    in->next = self->base_address + (int64_t)XX_BCM_MAGIC_SIZE;
    in->end = total;
    in->length = 0U;
    in->offset = 0U;
    in->consumed = 0;
    in->overrun = false;
    in->io_error = false;
    bcm_decoder_init(&rc, in);

    if (!full) {
        uint32_t length = bcm_decode32(&rc);
        if (in->overrun) goto done;
        if (length == 0U) {
            /* Empty input: the terminator must be followed by the CRC of
             * nothing. */
            result = bcm_decode32(&rc) == 0U && !in->overrun;
        } else {
            uint32_t primary = bcm_decode32(&rc);
            result = !in->overrun && length <= XX_BCM_MAX_BLOCK &&
                     primary >= 1U && primary <= length;
        }
        goto done;
    }

    model = (bcm_model *)xx_mem_alloc(sizeof(*model));
    if (!model) goto done;
    bcm_model_init(model);
    for (;;) {
        uint32_t length, primary, i, p, emitted;
        uint32_t counts[257];
        if (pd && xx_pd_is_stopped(pd)) goto done;
        length = bcm_decode32(&rc);
        if (in->overrun) goto done;
        if (length == 0U) break;
        primary = bcm_decode32(&rc);
        if (in->overrun || length > XX_BCM_MAX_BLOCK || primary < 1U ||
            primary > length)
            goto done;
        for (i = 0U; i < length; ++i) {
            if ((size_t)i >= block_capacity &&
                !bcm_reserve(&block, &block_capacity, (size_t)i + 1U,
                             (size_t)length))
                goto done;
            block[i] = (uint8_t)bcm_decode_symbol(model, &rc);
            if (in->overrun) goto done;
            if ((i & 0xFFFFFU) == 0xFFFFFU && pd && xx_pd_is_stopped(pd))
                goto done;
        }
        /* Inverse BWT (the row of the whole text is `primary`, 1-based,
         * with the sentinel row removed). */
        if ((size_t)length > next_capacity) {
            uint32_t *moved = (uint32_t *)(next
                ? xx_mem_realloc(next, (size_t)length * sizeof(uint32_t))
                : xx_mem_alloc((size_t)length * sizeof(uint32_t)));
            if (!moved) goto done;
            next = moved;
            next_capacity = (size_t)length;
        }
        xx_rt_memset(counts, 0, sizeof(counts));
        for (i = 0U; i < length; ++i) ++counts[block[i] + 1U];
        for (i = 1U; i < 256U; ++i) counts[i] += counts[i - 1U];
        for (i = 0U; i < length; ++i)
            next[counts[block[i]]++] = i + (i >= primary ? 1U : 0U);
        /* `next` maps 1..length injectively into 0..length without
         * `primary`, so the chain from `primary` ends at 0 after exactly
         * `length` steps; the counter only guards against a logic error. */
        emitted = 0U;
        for (p = primary; p != 0U;) {
            if (emitted >= length) goto done;
            p = next[p - 1U];
            out->buffer[out->length++] = block[p - (p >= primary ? 1U : 0U)];
            if (out->length == XX_BCM_OUT_BUFFER) {
                bcm_output_flush(out);
                if (out->failed) goto done;
            }
            ++emitted;
        }
        if (emitted != length) goto done;
    }
    bcm_output_flush(out);
    if (out->failed) goto done;
    {
        uint32_t stored = bcm_decode32(&rc);
        if (in->overrun || stored != out->crc) goto done;
    }
    result = true;
done:
    if (result && consumed)
        *consumed = (int64_t)XX_BCM_MAGIC_SIZE + in->consumed;
    if (next) xx_mem_free(next);
    if (block) xx_mem_free(block);
    if (model) xx_mem_free(model);
    xx_mem_free(in);
    return result;
}

/* A stream is BCM when it carries a known magic and enough payload.  For
 * "BCM!" the first block header must also decode sensibly. */
static bool xx_bcm_probe(Abstractformat *self, uint8_t *signature_out) {
    uint8_t magic[XX_BCM_MAGIC_SIZE];
    int64_t total;

    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        total - self->base_address < XX_BCM_MIN_SIZE)
        return false;
    if (!bcm_read_magic(self, magic)) return false;
    if (magic[0] != 'B' || magic[1] != 'C' || magic[2] != 'M') return false;
    if (magic[3] == '!') {
        if (!bcm_walk(self, NULL, false, NULL, NULL)) return false;
    } else if (magic[3] < '1' || magic[3] > '9') {
        return false;
    }
    if (signature_out) *signature_out = magic[3];
    return true;
}

/* ------------------------------------------------------------------------ */
/* Format API                                                                */
/* ------------------------------------------------------------------------ */

void xx_bcm_init(xx_bcm *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_UNKNOWN;
    archive->format.file_type = XX_BCM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bcm");
    xx_format_set_extension(&archive->format, "bcm");
    archive->format.check_is_valid = xx_bcm_check_is_valid;
    archive->format.handle_base_info = xx_bcm_handle_base_info;
    archive->format.get_format_size = xx_bcm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bcm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bcm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bcm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bcm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bcm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bcm_free_archive_records_reading;
    archive->format.destroy = xx_bcm_vtable_destroy;
}

xx_bcm *xx_bcm_create(xx_io_device *device, int64_t base_address) {
    xx_bcm *archive = (xx_bcm *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_bcm_init(archive, device, base_address);
    return archive;
}

void xx_bcm_destroy(xx_bcm *archive) {
    if (!archive) return;
    /* NOT xx_format_destroy: that dispatches through format.destroy, which is
     * this function, and the pair recurses until the stack is gone. The base
     * teardown is the close hook plus the extra-parameter list. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->version = 0U;
}

void xx_bcm_free(xx_bcm *archive) {
    if (!archive) return;
    xx_bcm_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bcm_vtable_destroy(Abstractformat *self) {
    xx_bcm_destroy((xx_bcm *)self);
}

bool xx_bcm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_bcm_probe(self, NULL);
}

bool xx_bcm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bcm *archive = (xx_bcm *)self;
    uint8_t signature = 0U;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_bcm_probe(self, &signature);
    if (!self->is_valid) {
        self->format_size = 0;
        archive->number_of_records = 0U;
        self->number_of_archive_records = 0U;
        return false;
    }
    archive->signature = signature;
    archive->version = signature == '!' ? 0U : (uint8_t)(signature - '0');
    archive->number_of_records = signature == '!' ? 1U : 0U;
    self->number_of_archive_records = archive->number_of_records;
    /* The bitstream carries no length; its end is only known after a full
     * decode, so the stream is taken to run to the end of the device. */
    total = xx_io_total_size(self->device);
    self->format_size = total - self->base_address;
    return true;
}

int64_t xx_bcm_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bcm_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_bcm *)self)->number_of_records : 0U;
}

uint8_t xx_bcm_get_version(const xx_bcm *archive) {
    return archive ? archive->version : 0U;
}

bool xx_bcm_unpack_to_device(xx_bcm *archive, xx_io_device *destination,
                             uint64_t *out_size, int64_t *consumed,
                             xx_pd_struct *pd) {
    bcm_output *out;
    uint8_t signature = 0U;
    bool result;
    if (!archive || !xx_bcm_probe(&archive->format, &signature) ||
        signature != '!')
        return false;
    out = (bcm_output *)xx_mem_alloc(sizeof(*out));
    if (!out) return false;
    out->device = destination;
    out->crc = 0U;
    out->total = 0U;
    out->length = 0U;
    out->failed = false;
    result = bcm_walk(&archive->format, out, true, consumed, pd);
    if (result && out_size) *out_size = out->total;
    xx_mem_free(out);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Record reading                                                            */
/* ------------------------------------------------------------------------ */

typedef struct bcm_stream_s {
    size_t index;
    size_t count;
} bcm_stream;

static void bcm_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool bcm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *bcm_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bcm_set_record(xx_archive_record *record, Abstractformat *self) {
    int64_t packed = self->format_size - (int64_t)XX_BCM_MAGIC_SIZE;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_BCM_MAGIC_SIZE;
    record->data_offset = self->base_address + (int64_t)XX_BCM_MAGIC_SIZE;
    record->compressed_size = packed;
    /* The uncompressed size is not stored; 0 means unknown. */
    return xx_archive_record_set_original_name(record, XX_BCM_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

xx_archive_record_state *xx_bcm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    bcm_stream *stream;
    xx_archive_record_state *state;
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid || ((xx_bcm *)self)->number_of_records == 0U)
        return NULL;
    stream = (bcm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = bcm_stream_free;
    state->total_records = 1U;
    if (!bcm_copy_options(&state->options, options) ||
        !bcm_set_record(&state->current_record, self)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bcm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_bcm_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    bcm_stream *stream;
    (void)pd;
    if (!self || !state || state->format != self ||
        !(stream = (bcm_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_bcm_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    bcm_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (bcm_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = bcm_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the stream decodes and its CRC matches. */
        return xx_bcm_unpack_to_device((xx_bcm *)self, NULL, NULL, NULL, pd);
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
               ? xx_str_concat3(base, "/", XX_BCM_PAYLOAD_NAME)
               : xx_str_concat(base, XX_BCM_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = xx_bcm_unpack_to_device((xx_bcm *)self, destination, NULL,
                                         NULL, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bcm_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
