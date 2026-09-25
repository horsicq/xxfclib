/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcadyan/xx_arcadyan.h"

#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_ARCADYAN exists in the enum. */
#ifdef ARCADYAN
#define XX_ARCADYAN_FILE_TYPE XX_FILE_TYPE_ARCADYAN
#else
#define XX_ARCADYAN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Name given to the reassembled stream.  Never taken from the file. */
#define XX_ARCADYAN_PAYLOAD_NAME "arcadyan.lzma"
/** Streaming buffer for the verbatim tail of the image. */
#define XX_ARCADYAN_STAGING_SIZE 65536U
/** Largest LZMA properties byte; (4 * 5 + 4) * 9 + 8 = 224. */
#define XX_ARCADYAN_MAX_PROPERTIES 224U
/** Size of the lzma_alone header: properties, dictionary, declared size. */
#define XX_ARCADYAN_LZMA_HEADER_SIZE 13U
/** Offset, in the de-obfuscated image, of the first range-coder byte. */
#define XX_ARCADYAN_RC_OFFSET                                                  \
    (XX_ARCADYAN_LZMA_OFFSET + XX_ARCADYAN_LZMA_HEADER_SIZE)
/*
 * Trial decode bounds.  The four signature bytes are the only fixed marker,
 * so - like binwalk, which dry-runs the whole LZMA decode - the reader only
 * accepts an image whose stream actually decodes.  It decodes a bounded
 * prefix: TRIAL_OUTPUT bytes of output (or the whole stream when it is
 * shorter), from at most TRIAL_INPUT bytes of input, with the dictionary
 * clamped to TRIAL_DICT.  An encoder never needs more than a sliver over one
 * input byte per output byte, so TRIAL_INPUT leaves a wide margin; the clamp
 * is exact because no match in the first TRIAL_DICT output bytes can reach
 * further back than TRIAL_DICT.  The decoder hands its output over in 64 KiB
 * flushes, and the sink stops it at the first flush that reaches
 * TRIAL_OUTPUT.
 */
#define XX_ARCADYAN_TRIAL_OUTPUT 0x10000U
#define XX_ARCADYAN_TRIAL_INPUT 0x40000U
#define XX_ARCADYAN_TRIAL_DICT 0x100000U

typedef struct xx_arcadyan_private_s {
    /* The de-obfuscated prologue, held because it is not a contiguous run of
     * device bytes and has to be produced from a scatter of four pieces. */
    uint8_t prologue[XX_ARCADYAN_PROLOGUE_SIZE];
    int64_t input_size;
    int64_t image_size;  /**< Obfuscated bytes from base_address to EOF. */
    int64_t stream_size; /**< image_size - 4, the published stream length. */
    int64_t archive_end;
    uint64_t declared_size;
    uint32_t dictionary_size;
    uint8_t properties;
} xx_arcadyan_private;

typedef struct xx_arcadyan_archive_stream_s {
    xx_arcadyan_private parsed;
    size_t index;
} xx_arcadyan_archive_stream;

static void xx_arcadyan_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_arcadyan_read_at(xx_io_device *device, int64_t offset,
                                void *data, size_t size) {
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_arcadyan_range_within(int64_t total_size, int64_t offset,
                                     int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

void xx_arcadyan_deobfuscate_prologue(void *data, size_t size) {
    uint8_t *bytes = (uint8_t *)data;
    uint8_t block[XX_ARCADYAN_BLOCK_SIZE];
    size_t index;
    if (!bytes || size < XX_ARCADYAN_PROLOGUE_SIZE) return;
    /* Step 1: exchange the two 32-byte blocks at 0x04 and 0x68. */
    xx_rt_memcpy(block, bytes + XX_ARCADYAN_LZMA_OFFSET,
                 XX_ARCADYAN_BLOCK_SIZE);
    xx_rt_memcpy(bytes + XX_ARCADYAN_LZMA_OFFSET,
                 bytes + XX_ARCADYAN_MAGIC_OFFSET, XX_ARCADYAN_BLOCK_SIZE);
    xx_rt_memcpy(bytes + XX_ARCADYAN_MAGIC_OFFSET, block,
                 XX_ARCADYAN_BLOCK_SIZE);
    /* Step 2: swap the nibbles of every byte now sitting at 0x04. */
    for (index = 0U; index < XX_ARCADYAN_BLOCK_SIZE; ++index) {
        uint8_t value = bytes[XX_ARCADYAN_LZMA_OFFSET + index];
        bytes[XX_ARCADYAN_LZMA_OFFSET + index] =
            (uint8_t)(((value & 0x0FU) << 4) | ((value & 0xF0U) >> 4));
    }
    /* Step 3: swap those same bytes in adjacent pairs. */
    for (index = 0U; index + 1U < XX_ARCADYAN_BLOCK_SIZE; index += 2U) {
        uint8_t first = bytes[XX_ARCADYAN_LZMA_OFFSET + index];
        bytes[XX_ARCADYAN_LZMA_OFFSET + index] =
            bytes[XX_ARCADYAN_LZMA_OFFSET + index + 1U];
        bytes[XX_ARCADYAN_LZMA_OFFSET + index + 1U] = first;
    }
}

typedef struct xx_arcadyan_trial_sink_s {
    uint64_t written;
    bool reached; /**< TRIAL_OUTPUT bytes decoded cleanly; decode stopped. */
} xx_arcadyan_trial_sink;

/* Counts the decoder's output and stops it once enough has been seen.  The
 * refusal is what ends the decode; `reached` tells the caller that it was the
 * sink, not the stream, that said no. */
static ssize_t xx_arcadyan_trial_write(xx_io_device *device, const void *data,
                                       size_t size) {
    xx_arcadyan_trial_sink *sink =
        device ? (xx_arcadyan_trial_sink *)device->priv : NULL;
    if (!sink || (!data && size != 0U)) return -1;
    if ((uint64_t)size >= (uint64_t)XX_ARCADYAN_TRIAL_OUTPUT - sink->written) {
        sink->reached = true;
        return -1;
    }
    sink->written += (uint64_t)size;
    return (ssize_t)size;
}

/*
 * Decode the start of the stream.  Accepts when the decoder produced
 * TRIAL_OUTPUT bytes without error, or ended the stream cleanly before that:
 * at the declared size, or - size unknown - at the end marker.
 */
static bool xx_arcadyan_trial_decode(xx_io_device *device, int64_t base,
                                     const xx_arcadyan_private *parsed,
                                     xx_pd_struct *pd) {
    const size_t in_prologue =
        XX_ARCADYAN_PROLOGUE_SIZE - XX_ARCADYAN_RC_OFFSET;
    uint8_t properties[XX_LZMA_PROPS_SIZE];
    xx_arcadyan_trial_sink counter;
    xx_io_device sink;
    uint8_t *input;
    int64_t available;
    int64_t uncompressed;
    size_t input_size;
    uint32_t dictionary;
    bool decoded;
    bool ok = false;
    if (!device || !parsed || base < 0 ||
        parsed->image_size <= (int64_t)XX_ARCADYAN_PROLOGUE_SIZE) {
        return false;
    }
    available = parsed->image_size - (int64_t)XX_ARCADYAN_RC_OFFSET;
    input_size = available < (int64_t)XX_ARCADYAN_TRIAL_INPUT
                     ? (size_t)available
                     : (size_t)XX_ARCADYAN_TRIAL_INPUT;
    if (input_size <= in_prologue) return false;
    input = (uint8_t *)xx_mem_alloc(input_size);
    if (!input) return false;
    xx_rt_memcpy(input, parsed->prologue + XX_ARCADYAN_RC_OFFSET, in_prologue);
    if (!xx_arcadyan_read_at(device, base + (int64_t)XX_ARCADYAN_PROLOGUE_SIZE,
                             input + in_prologue, input_size - in_prologue)) {
        goto done;
    }
    dictionary = parsed->dictionary_size < XX_ARCADYAN_TRIAL_DICT
                     ? parsed->dictionary_size
                     : XX_ARCADYAN_TRIAL_DICT;
    properties[0] = parsed->properties;
    properties[1] = (uint8_t)dictionary;
    properties[2] = (uint8_t)(dictionary >> 8U);
    properties[3] = (uint8_t)(dictionary >> 16U);
    properties[4] = (uint8_t)(dictionary >> 24U);
    /* declared_size was bounded to XX_ARCADYAN_MAX_DECODED by the caller. */
    uncompressed = parsed->declared_size == UINT64_MAX
                       ? -1
                       : (int64_t)parsed->declared_size;
    xx_mem_zero(&counter, sizeof(counter));
    xx_mem_zero(&sink, sizeof(sink));
    sink.write = xx_arcadyan_trial_write;
    sink.priv = &counter;
    decoded = xx_lzma_unpack_memory_to_device(input, input_size, properties,
                                              XX_LZMA_PROPS_SIZE, uncompressed,
                                              &sink, pd);
    if (counter.reached) {
        ok = !(pd && xx_pd_is_stopped(pd));
    } else if (decoded) {
        ok = counter.written != 0U &&
             (uncompressed < 0 || counter.written == (uint64_t)uncompressed);
    }
done:
    xx_mem_free(input);
    return ok;
}

static void xx_arcadyan_private_cleanup(xx_arcadyan_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->declared_size = UINT64_MAX;
}

static bool xx_arcadyan_parse(Abstractformat *self, xx_arcadyan_private *parsed,
                              xx_pd_struct *pd) {
    static const uint8_t magic[XX_ARCADYAN_MAGIC_SIZE] = {0x00U, 0xD5U, 0x08U,
                                                          0x00U};
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->declared_size = UINT64_MAX;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < 0 || self->base_address > parsed->input_size) {
        goto fail;
    }
    /*
     * There is no length field, so the image runs to the end of the device.
     * binwalk bounds that span and so does this reader: the signature is only
     * four bytes at a fixed offset, and without a size bound any file that
     * happens to carry them would be claimed as Arcadyan firmware.
     */
    parsed->image_size = parsed->input_size - self->base_address;
    if (parsed->image_size <= (int64_t)XX_ARCADYAN_MIN_SIZE ||
        parsed->image_size > (int64_t)XX_ARCADYAN_MAX_SIZE) {
        goto fail;
    }
    if (!xx_arcadyan_range_within(parsed->input_size, self->base_address,
                                  (int64_t)XX_ARCADYAN_PROLOGUE_SIZE) ||
        !xx_arcadyan_read_at(self->device, self->base_address, parsed->prologue,
                             XX_ARCADYAN_PROLOGUE_SIZE) ||
        xx_rt_memcmp(parsed->prologue + XX_ARCADYAN_MAGIC_OFFSET, magic,
                     XX_ARCADYAN_MAGIC_SIZE) != 0) {
        goto fail;
    }
    xx_arcadyan_deobfuscate_prologue(parsed->prologue,
                                     XX_ARCADYAN_PROLOGUE_SIZE);

    /*
     * What the shuffle uncovers is an lzma_alone header: one properties byte,
     * a 32-bit dictionary size and a 64-bit uncompressed size, all little
     * endian.  The signature fixes the properties byte (0x5D) and the low
     * three dictionary bytes (00 00 80); the dictionary's top byte and the
     * declared uncompressed size are free.  A header claiming gigabytes of
     * output is an expansion bomb and is rejected here, at parse time, not
     * later.
     */
    parsed->properties = parsed->prologue[XX_ARCADYAN_LZMA_OFFSET];
    parsed->dictionary_size = xx_data_get_u32(
        parsed->prologue, XX_ARCADYAN_PROLOGUE_SIZE,
        XX_ARCADYAN_LZMA_OFFSET + 1U, false);
    parsed->declared_size = xx_data_get_u64(
        parsed->prologue, XX_ARCADYAN_PROLOGUE_SIZE,
        XX_ARCADYAN_LZMA_OFFSET + 5U, false);
    if (parsed->properties > XX_ARCADYAN_MAX_PROPERTIES ||
        parsed->dictionary_size < 0x1000U ||
        parsed->dictionary_size > 0x40000000U) {
        goto fail;
    }
    /* UINT64_MAX is lzma_alone's "size unknown"; any other value has to be a
     * plausible output length. */
    if (parsed->declared_size != UINT64_MAX &&
        (parsed->declared_size == 0U ||
         parsed->declared_size > XX_ARCADYAN_MAX_DECODED)) {
        goto fail;
    }
    /* An LZMA range coder always opens with a zero byte. */
    if (parsed->prologue[XX_ARCADYAN_RC_OFFSET] != 0U) goto fail;

    /* The published stream is the de-obfuscated image minus its first four
     * bytes, which are not part of the LZMA data. */
    parsed->stream_size = parsed->image_size - (int64_t)XX_ARCADYAN_LZMA_OFFSET;
    parsed->archive_end = self->base_address + parsed->image_size;
    if (!xx_arcadyan_trial_decode(self->device, self->base_address, parsed,
                                  pd)) {
        goto fail;
    }
    return true;
fail:
    xx_arcadyan_private_cleanup(parsed);
    return false;
}

/*
 * Write the de-obfuscated LZMA stream.
 *
 * The first 0x84 bytes come out of the prologue held in the parse result -
 * they are a reordering of device bytes, not a run of them - and the tail
 * from 0x88 on is copied straight through a staging buffer.
 */
static bool xx_arcadyan_write_stream(xx_io_device *device, int64_t base,
                                     const xx_arcadyan_private *parsed,
                                     const char *destination,
                                     xx_pd_struct *pd) {
    uint8_t staging[XX_ARCADYAN_STAGING_SIZE];
    xx_io_device *output;
    int64_t remaining;
    size_t head = XX_ARCADYAN_PROLOGUE_SIZE - XX_ARCADYAN_LZMA_OFFSET;
    bool ok = true;
    bool created = false;
    if (!device || !parsed || !destination || base < 0) return false;
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    if (!output) return false;
    if (xx_io_write(output, parsed->prologue + XX_ARCADYAN_LZMA_OFFSET, head) !=
        (ssize_t)head) {
        ok = false;
    }
    remaining = parsed->image_size - (int64_t)XX_ARCADYAN_PROLOGUE_SIZE;
    if (ok && remaining > 0 &&
        xx_io_seek64(device, base + (int64_t)XX_ARCADYAN_PROLOGUE_SIZE,
                     SEEK_SET) != 0) {
        ok = false;
    }
    while (ok && remaining > 0) {
        size_t step = (remaining < (int64_t)sizeof(staging))
                          ? (size_t)remaining
                          : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) {
                ok = false;
                break;
            }
            done += (size_t)got;
        }
        if (!ok) break;
        if (xx_io_write(output, staging, step) != (ssize_t)step) ok = false;
        remaining -= (int64_t)step;
    }
    xx_io_close(output);
    if (!ok && created) xx_rt_remove(destination);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_arcadyan_copy_options(xx_list_s *destination,
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

static const xx_var *xx_arcadyan_find_option(const xx_list_s *options,
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

static bool xx_arcadyan_populate_record(xx_archive_record *record,
                                        const xx_arcadyan_private *parsed,
                                        int64_t base) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* There is no header to point at; the four dropped bytes are the closest
     * thing the format has to one. */
    record->header_offset = base;
    record->header_size = (int64_t)XX_ARCADYAN_LZMA_OFFSET;
    record->data_offset = base + (int64_t)XX_ARCADYAN_LZMA_OFFSET;
    record->compressed_size = parsed->stream_size;
    /*
     * The record carries the LZMA-alone stream itself, which this reader does
     * not decompress - it is stored as far as this reader is concerned, so
     * the two sizes agree.  The LZMA header's own declared output size is
     * reported through xx_arcadyan::declared_size instead, where it is
     * clearly a claim from the file rather than a measurement.
     */
    return xx_archive_record_set_original_name(record,
                                               XX_ARCADYAN_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_arcadyan_archive_stream_free(void *pointer) {
    xx_arcadyan_archive_stream *stream = (xx_arcadyan_archive_stream *)pointer;
    if (!stream) return;
    xx_arcadyan_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_arcadyan_init(xx_arcadyan *arc, xx_io_device *dev,
                      int64_t base_address) {
    if (!arc) return;
    xx_mem_zero(arc, sizeof(*arc));
    xx_format_init(&arc->format, dev, base_address);
    /* The only multi-byte fields are the LZMA header's, which are little. */
    arc->format.endian = XX_ENDIAN_LITTLE;
    arc->format.file_type = XX_ARCADYAN_FILE_TYPE;
    arc->format.format_type = XX_TYPE_ARCHIVE;
    arc->format.is_archive = true;
    xx_format_set_mime_type(&arc->format, "application/x-arcadyan-firmware");
    xx_format_set_extension(&arc->format, "bin");
    arc->format.check_is_valid = xx_arcadyan_check_is_valid;
    arc->format.handle_base_info = xx_arcadyan_handle_base_info;
    arc->format.get_format_size = xx_arcadyan_get_format_size;
    arc->format.get_number_of_archive_records =
        xx_arcadyan_get_number_of_archive_records;
    arc->format.create_archive_records_reading =
        xx_arcadyan_create_archive_records_reading;
    arc->format.get_current_archive_record =
        xx_arcadyan_get_current_archive_record;
    arc->format.unpack_current_archive_record =
        xx_arcadyan_unpack_current_archive_record;
    arc->format.archive_record_move_to_next =
        xx_arcadyan_archive_record_move_to_next;
    arc->format.free_archive_records_reading =
        xx_arcadyan_free_archive_records_reading;
    arc->format.destroy = xx_arcadyan_vtable_destroy;
    arc->declared_size = UINT64_MAX;
    arc->archive_end = -1;
}

xx_arcadyan *xx_arcadyan_create(xx_io_device *dev, int64_t base_address) {
    xx_arcadyan *arc = (xx_arcadyan *)xx_mem_alloc(sizeof(*arc));
    if (arc) xx_arcadyan_init(arc, dev, base_address);
    return arc;
}

void xx_arcadyan_destroy(xx_arcadyan *arc) {
    if (!arc) return;
    if (arc->internal) {
        xx_arcadyan_private_cleanup((xx_arcadyan_private *)arc->internal);
        xx_mem_free(arc->internal);
        arc->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&arc->format);
}

static void xx_arcadyan_vtable_destroy(Abstractformat *self) {
    xx_arcadyan_destroy((xx_arcadyan *)self);
}

void xx_arcadyan_free(xx_arcadyan *arc) {
    if (!arc) return;
    xx_arcadyan_destroy(arc);
    xx_mem_free(arc);
}

bool xx_arcadyan_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_arcadyan_private parsed;
    bool result = xx_arcadyan_parse(self, &parsed, pd);
    xx_arcadyan_private_cleanup(&parsed);
    return result;
}

bool xx_arcadyan_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_arcadyan_private *parsed;
    xx_arcadyan *arc = (xx_arcadyan *)self;
    if (!self || !arc) return false;
    parsed = (xx_arcadyan_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_arcadyan_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (arc->internal) {
        xx_arcadyan_private_cleanup((xx_arcadyan_private *)arc->internal);
        xx_mem_free(arc->internal);
    }
    arc->internal = parsed;
    arc->number_of_records = 1U;
    arc->number_of_members = 1U;
    arc->declared_size = parsed->declared_size;
    arc->dictionary_size = parsed->dictionary_size;
    arc->properties = parsed->properties;
    arc->stream_size = parsed->stream_size;
    arc->archive_end = parsed->archive_end;
    self->format_size = parsed->image_size;
    /* The image is defined as running to the end of the device, so there is
     * never an overlay. */
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_arcadyan_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_arcadyan_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_arcadyan *)self)->number_of_records;
}

xx_archive_record_state *xx_arcadyan_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_arcadyan_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_arcadyan_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_arcadyan_copy_options(&state->options, options) ||
        !xx_arcadyan_parse(self, &stream->parsed, pd)) {
        xx_arcadyan_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_arcadyan_archive_stream_free;
    state->total_records = 1;
    if (xx_arcadyan_populate_record(&state->current_record, &stream->parsed,
                                    self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_arcadyan_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arcadyan_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_arcadyan_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One image, one stream: the first move always ends the walk. */
    stream = (xx_arcadyan_archive_stream *)state->internal_state;
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_arcadyan_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    const xx_arcadyan_archive_stream *stream;
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (const xx_arcadyan_archive_stream *)state->internal_state;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option =
        xx_arcadyan_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the image's span is addressable. */
        int64_t total = xx_io_total_size(self->device);
        return self->base_address >= 0 && stream->parsed.image_size >= 0 &&
               self->base_address <= total &&
               stream->parsed.image_size <= total - self->base_address;
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
    /* Not a straight copy: the prologue has to be reassembled first. */
    result = xx_arcadyan_write_stream(self->device, self->base_address,
                                      &stream->parsed, destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_arcadyan_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_arcadyan_get_number_of_records(const xx_arcadyan *arc) {
    return arc ? arc->number_of_records : 0U;
}
uint64_t xx_arcadyan_get_number_of_members(const xx_arcadyan *arc) {
    return arc ? arc->number_of_members : 0U;
}
int64_t xx_arcadyan_get_stream_size(const xx_arcadyan *arc) {
    return arc ? arc->stream_size : -1;
}
int64_t xx_arcadyan_get_archive_end(const xx_arcadyan *arc) {
    return arc ? arc->archive_end : -1;
}
