/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gpgsigned/xx_gpgsigned.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../../algo/deflate/xx_deflate_internal.h"

#include <limits.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_GPG_SIGNED exists. */
#ifdef GPG_SIGNED
#define XX_GPGSIGNED_FILE_TYPE XX_FILE_TYPE_GPG_SIGNED
#else
#define XX_GPGSIGNED_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The single record: binwalk's inflate extractor uses the same name. */
#define XX_GPGSIGNED_MEMBER_NAME "decompressed.bin"

/* Bytes of 0xFF served after the real data.  The library's bit reader hands
 * out zero bits once its source is exhausted instead of failing, so a stream
 * cut short could otherwise "finish" on phantom zeros.  With a non-zero pad
 * behind the data, any stream that needs a bit past the end is caught by the
 * consumed-byte count running past the available input. */
#define XX_GPGSIGNED_PAD_SIZE 16U

/* ------------------------------------------------------------------------ */
/* OpenPGP packet walker (RFC 4880 section 4.2), fed with the decompressed    */
/* bytes as they are produced.                                               */
/* ------------------------------------------------------------------------ */

enum {
    XX_GPGSIGNED_ST_TAG = 0,  /* expecting a packet tag octet */
    XX_GPGSIGNED_ST_LEN,      /* collecting length octets */
    XX_GPGSIGNED_ST_BODY,     /* skipping body bytes */
    XX_GPGSIGNED_ST_REST      /* old-format indeterminate length: the rest */
};

typedef struct xx_gpgsigned_walker_s {
    int state;
    bool new_format;
    bool partial;            /* current body chunk is a partial length */
    uint8_t len_octets[5];
    unsigned len_have;
    unsigned len_need;       /* total length octets for this header */
    uint64_t remaining;      /* body bytes left in the current chunk */
    uint64_t packets;
    uint8_t first_tag;
    bool failed;
} xx_gpgsigned_walker;

static void xx_gpgsigned_walker_init(xx_gpgsigned_walker *walker) {
    xx_mem_zero(walker, sizeof(*walker));
    walker->state = XX_GPGSIGNED_ST_TAG;
}

/* Called once every length octet of the header has been collected. */
static void xx_gpgsigned_walker_length_done(xx_gpgsigned_walker *walker) {
    const uint8_t *o = walker->len_octets;
    uint64_t length;
    walker->partial = false;
    if (walker->new_format) {
        if (o[0] < 192U) {
            length = o[0];
        } else if (o[0] < 224U) {
            length = (((uint64_t)o[0] - 192U) << 8U) + o[1] + 192U;
        } else if (o[0] == 255U) {
            length = ((uint64_t)o[1] << 24U) | ((uint64_t)o[2] << 16U) |
                     ((uint64_t)o[3] << 8U) | (uint64_t)o[4];
        } else {
            length = (uint64_t)1U << (o[0] & 0x1FU);
            walker->partial = true;
        }
    } else if (walker->len_need == 1U) {
        length = o[0];
    } else if (walker->len_need == 2U) {
        length = ((uint64_t)o[0] << 8U) | (uint64_t)o[1];
    } else {
        length = ((uint64_t)o[0] << 24U) | ((uint64_t)o[1] << 16U) |
                 ((uint64_t)o[2] << 8U) | (uint64_t)o[3];
    }
    walker->remaining = length;
    walker->state = length != 0U ? XX_GPGSIGNED_ST_BODY : XX_GPGSIGNED_ST_TAG;
}

static void xx_gpgsigned_walker_feed(xx_gpgsigned_walker *walker,
                                     const uint8_t *data, size_t size) {
    size_t pos = 0U;
    while (!walker->failed && pos < size) {
        uint8_t byte;
        switch (walker->state) {
        case XX_GPGSIGNED_ST_REST:
            return;
        case XX_GPGSIGNED_ST_BODY: {
            size_t left = size - pos;
            size_t step = (uint64_t)left > walker->remaining
                              ? (size_t)walker->remaining
                              : left;
            pos += step;
            walker->remaining -= (uint64_t)step;
            if (walker->remaining == 0U) {
                if (walker->partial) {
                    /* A partial chunk is always followed by another new
                     * format length header for the same packet. */
                    walker->state = XX_GPGSIGNED_ST_LEN;
                    walker->len_have = 0U;
                    walker->len_need = 0U;
                } else {
                    walker->state = XX_GPGSIGNED_ST_TAG;
                }
            }
            break;
        }
        case XX_GPGSIGNED_ST_TAG:
            byte = data[pos++];
            if ((byte & 0x80U) == 0U) {
                walker->failed = true;
                return;
            }
            walker->new_format = (byte & 0x40U) != 0U;
            {
                uint8_t tag = walker->new_format
                                  ? (uint8_t)(byte & 0x3FU)
                                  : (uint8_t)((byte >> 2U) & 0x0FU);
                if (tag == 0U) { /* tag 0 is reserved and never valid */
                    walker->failed = true;
                    return;
                }
                if (walker->packets == 0U) walker->first_tag = tag;
            }
            ++walker->packets;
            walker->len_have = 0U;
            walker->partial = false;
            if (walker->new_format) {
                walker->len_need = 0U; /* decided by the first octet */
                walker->state = XX_GPGSIGNED_ST_LEN;
            } else if ((byte & 0x03U) == 3U) {
                walker->state = XX_GPGSIGNED_ST_REST;
            } else {
                walker->len_need = 1U << (byte & 0x03U); /* 1, 2 or 4 */
                walker->state = XX_GPGSIGNED_ST_LEN;
            }
            break;
        case XX_GPGSIGNED_ST_LEN:
            byte = data[pos++];
            if (walker->len_have >= sizeof(walker->len_octets)) {
                walker->failed = true;
                return;
            }
            walker->len_octets[walker->len_have++] = byte;
            if (walker->new_format && walker->len_have == 1U) {
                if (byte < 192U || (byte >= 224U && byte < 255U)) {
                    walker->len_need = 1U;
                } else if (byte < 224U) {
                    walker->len_need = 2U;
                } else {
                    walker->len_need = 5U;
                }
            }
            if (walker->len_have == walker->len_need) {
                xx_gpgsigned_walker_length_done(walker);
            }
            break;
        default:
            walker->failed = true;
            return;
        }
    }
}

/* The stream must end on a packet boundary (or inside an indeterminate
 * packet, which by definition runs to the end) after at least one packet. */
static bool xx_gpgsigned_walker_complete(const xx_gpgsigned_walker *walker) {
    return !walker->failed && walker->packets != 0U &&
           (walker->state == XX_GPGSIGNED_ST_TAG ||
            walker->state == XX_GPGSIGNED_ST_REST);
}

/* ------------------------------------------------------------------------ */
/* Devices: a padded window over the source and a checking output sink.      */
/* ------------------------------------------------------------------------ */

typedef struct xx_gpgsigned_source_s {
    xx_io_device device;
    xx_io_device *base;
    int64_t pos;
    int64_t end;
    uint64_t delivered;   /* bytes handed out, pad included */
    unsigned pad_left;
    bool failed;
} xx_gpgsigned_source;

static ssize_t xx_gpgsigned_source_read(xx_io_device *device, void *buffer,
                                        size_t size) {
    xx_gpgsigned_source *source =
        device ? (xx_gpgsigned_source *)device->priv : NULL;
    uint8_t *out = (uint8_t *)buffer;
    if (!source || (!buffer && size != 0U)) return -1;
    if (size == 0U) return 0;
    if (size > (size_t)INT32_MAX) size = (size_t)INT32_MAX;
    if (source->pos < source->end) {
        int64_t left = source->end - source->pos;
        size_t want = (uint64_t)size > (uint64_t)left ? (size_t)left : size;
        ssize_t got;
        if (xx_io_seek64(source->base, source->pos, SEEK_SET) != 0) {
            source->failed = true;
            return -1;
        }
        got = xx_io_read(source->base, out, want);
        if (got <= 0 || (size_t)got > want) {
            source->failed = true;
            return -1;
        }
        source->pos += (int64_t)got;
        source->delivered += (uint64_t)got;
        return got;
    }
    if (source->pad_left != 0U) {
        size_t count = size < source->pad_left ? size : source->pad_left;
        xx_rt_memset(out, 0xFF, count);
        source->pad_left -= (unsigned)count;
        source->delivered += (uint64_t)count;
        return (ssize_t)count;
    }
    return 0;
}

static void xx_gpgsigned_source_init(xx_gpgsigned_source *source,
                                     xx_io_device *base, int64_t offset,
                                     int64_t end) {
    xx_mem_zero(source, sizeof(*source));
    source->base = base;
    source->pos = offset;
    source->end = end;
    source->pad_left = XX_GPGSIGNED_PAD_SIZE;
    source->device.read = xx_gpgsigned_source_read;
    source->device.priv = source;
}

typedef struct xx_gpgsigned_sink_s {
    xx_io_device device;
    xx_io_device *target;
    xx_gpgsigned_walker walker;
    uint64_t written;
    uint32_t crc32;
    bool failed;
} xx_gpgsigned_sink;

static ssize_t xx_gpgsigned_sink_write(xx_io_device *device, const void *data,
                                       size_t size) {
    xx_gpgsigned_sink *sink =
        device ? (xx_gpgsigned_sink *)device->priv : NULL;
    if (!sink || (!data && size != 0U) || size > (size_t)INT32_MAX) {
        if (sink) sink->failed = true;
        return -1;
    }
    /* The output cap is the decompression-bomb guard; the packet walker
     * stops a stream that is not OpenPGP at its first flushed buffer. */
    if ((uint64_t)size > XX_GPGSIGNED_MAX_OUTPUT - sink->written) {
        sink->failed = true;
        return -1;
    }
    xx_gpgsigned_walker_feed(&sink->walker, (const uint8_t *)data, size);
    if (sink->walker.failed) {
        sink->failed = true;
        return -1;
    }
    if (sink->target && size != 0U &&
        xx_io_write(sink->target, data, size) != (ssize_t)size) {
        sink->failed = true;
        return -1;
    }
    sink->crc32 = xx_crc32_calc(sink->crc32, data, size);
    sink->written += (uint64_t)size;
    return (ssize_t)size;
}

static int64_t xx_gpgsigned_sink_size(xx_io_device *device) {
    const xx_gpgsigned_sink *sink =
        device ? (const xx_gpgsigned_sink *)device->priv : NULL;
    return sink && sink->written <= (uint64_t)INT64_MAX
               ? (int64_t)sink->written
               : -1;
}

static void xx_gpgsigned_sink_init(xx_gpgsigned_sink *sink,
                                   xx_io_device *target) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    xx_gpgsigned_walker_init(&sink->walker);
    sink->device.write = xx_gpgsigned_sink_write;
    sink->device.total_size = xx_gpgsigned_sink_size;
    sink->device.get_total_size = xx_gpgsigned_sink_size;
    sink->device.size = xx_gpgsigned_sink_size;
    sink->device.priv = sink;
}

/* ------------------------------------------------------------------------ */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct xx_gpgsigned_result_s {
    int64_t format_size;      /* 2 + Deflate bytes through the final block */
    uint64_t uncompressed_size;
    uint64_t packet_count;
    uint32_t crc32;
    uint8_t first_tag;
} xx_gpgsigned_result;

static bool xx_gpgsigned_read_at(xx_io_device *device, int64_t offset,
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

/* Inflate the packet body through the library's RFC 1951 engine, keeping the
 * exact number of bytes consumed (the same boundary binwalk takes from
 * flate2's total_in), and walk the output as OpenPGP packets. */
static bool xx_gpgsigned_decode(Abstractformat *self, xx_io_device *target,
                                xx_gpgsigned_result *result,
                                xx_pd_struct *pd) {
    uint8_t header[3];
    int64_t total_size;
    int64_t data_offset;
    int64_t available;
    xx_gpgsigned_source source;
    xx_gpgsigned_sink sink;
    xx_bit_reader reader;
    uint64_t unread;
    uint64_t consumed;
    bool ok;
    if (result) xx_mem_zero(result, sizeof(*result));
    if (!self || !self->device || !result || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    /* Smallest possible member: the two header bytes plus a Deflate stream
     * that yields a two-byte packet, which cannot be under four bytes. */
    if (total_size < 0 || self->base_address > total_size ||
        total_size - self->base_address < 6 ||
        self->base_address > INT64_MAX - (int64_t)XX_GPGSIGNED_HEADER_SIZE) {
        return false;
    }
    if (!xx_gpgsigned_read_at(self->device, self->base_address, header,
                              sizeof(header)) ||
        header[0] != XX_GPGSIGNED_CTB || header[1] != XX_GPGSIGNED_ALGO_ZIP ||
        (header[2] & 0x06U) == 0x06U) { /* BTYPE 3 is reserved */
        return false;
    }
    data_offset = self->base_address + (int64_t)XX_GPGSIGNED_HEADER_SIZE;
    available = total_size - data_offset;

    xx_gpgsigned_source_init(&source, self->device, data_offset, total_size);
    xx_gpgsigned_sink_init(&sink, target);
    if (!xx_br_init(&reader, &source.device, NULL, 0U, -1)) return false;
    ok = xx_deflate_decompress_stream(&reader, &sink.device, NULL, 0U, NULL,
                                      false, pd);
    if (reader.buffer_pos > reader.buffer_len || reader.bit_count < 0) {
        ok = false;
        unread = 0U;
    } else {
        unread = (uint64_t)(reader.buffer_len - reader.buffer_pos) +
                 (uint64_t)(reader.bit_count / 8);
    }
    xx_br_free(&reader);
    if (!ok || source.failed || sink.failed || unread > source.delivered) {
        return false;
    }
    consumed = source.delivered - unread;
    /* A stream that reached into the pad was cut short: reject it. */
    if (consumed == 0U || consumed > (uint64_t)available ||
        sink.written == 0U || !xx_gpgsigned_walker_complete(&sink.walker)) {
        return false;
    }
    result->format_size =
        (int64_t)XX_GPGSIGNED_HEADER_SIZE + (int64_t)consumed;
    result->uncompressed_size = sink.written;
    result->packet_count = sink.walker.packets;
    result->crc32 = sink.crc32;
    result->first_tag = sink.walker.first_tag;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static void xx_gpgsigned_vtable_destroy(Abstractformat *self);

static bool xx_gpgsigned_copy_options(xx_list_s *destination,
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

static const xx_var *xx_gpgsigned_find_option(const xx_list_s *options,
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

static bool xx_gpgsigned_populate_record(Abstractformat *self,
                                         xx_archive_record *record) {
    const xx_gpgsigned *gpg = (const xx_gpgsigned *)self;
    int64_t compressed;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= (int64_t)XX_GPGSIGNED_HEADER_SIZE) {
        return false;
    }
    compressed = self->format_size - (int64_t)XX_GPGSIGNED_HEADER_SIZE;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = XX_GPGSIGNED_HEADER_SIZE;
    record->data_offset =
        self->base_address + (int64_t)XX_GPGSIGNED_HEADER_SIZE;
    record->compressed_size = compressed;
    /* The name is a literal, never taken from the file, so it needs no
     * sanitising before it becomes a destination path component. */
    return xx_archive_record_set_original_name(record,
                                               XX_GPGSIGNED_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)compressed) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          gpg->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          XX_GPGSIGNED_ALGO_ZIP) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

static void xx_gpgsigned_reset(xx_gpgsigned *gpg) {
    gpg->uncompressed_size = 0U;
    gpg->packet_count = 0U;
    gpg->stream_end = -1;
    gpg->crc32 = 0U;
    gpg->first_packet_tag = 0U;
}

void xx_gpgsigned_init(xx_gpgsigned *gpg, xx_io_device *dev,
                       int64_t base_address) {
    if (!gpg) return;
    xx_mem_zero(gpg, sizeof(*gpg));
    xx_format_init(&gpg->format, dev, base_address);
    gpg->format.endian = XX_ENDIAN_BIG;
    gpg->format.file_type = XX_GPGSIGNED_FILE_TYPE;
    gpg->format.format_type = XX_TYPE_ARCHIVE;
    gpg->format.is_archive = true;
    xx_format_set_mime_type(&gpg->format, "application/pgp-encrypted");
    xx_format_set_extension(&gpg->format, "gpg");
    gpg->format.check_is_valid = xx_gpgsigned_check_is_valid;
    gpg->format.handle_base_info = xx_gpgsigned_handle_base_info;
    gpg->format.get_format_size = xx_gpgsigned_get_format_size;
    gpg->format.get_number_of_archive_records =
        xx_gpgsigned_get_number_of_archive_records;
    gpg->format.create_archive_records_reading =
        xx_gpgsigned_create_archive_records_reading;
    gpg->format.get_current_archive_record =
        xx_gpgsigned_get_current_archive_record;
    gpg->format.unpack_current_archive_record =
        xx_gpgsigned_unpack_current_archive_record;
    gpg->format.archive_record_move_to_next =
        xx_gpgsigned_archive_record_move_to_next;
    gpg->format.free_archive_records_reading =
        xx_gpgsigned_free_archive_records_reading;
    gpg->format.destroy = xx_gpgsigned_vtable_destroy;
    xx_gpgsigned_reset(gpg);
}

xx_gpgsigned *xx_gpgsigned_create(xx_io_device *dev, int64_t base_address) {
    xx_gpgsigned *gpg = (xx_gpgsigned *)xx_mem_alloc(sizeof(*gpg));
    if (gpg) xx_gpgsigned_init(gpg, dev, base_address);
    return gpg;
}

void xx_gpgsigned_destroy(xx_gpgsigned *gpg) {
    if (!gpg) return;
    xx_format_cleanup_extra_parameters(&gpg->format);
    xx_gpgsigned_reset(gpg);
}

static void xx_gpgsigned_vtable_destroy(Abstractformat *self) {
    xx_gpgsigned_destroy((xx_gpgsigned *)self);
}

void xx_gpgsigned_free(xx_gpgsigned *gpg) {
    if (!gpg) return;
    xx_gpgsigned_destroy(gpg);
    xx_mem_free(gpg);
}

bool xx_gpgsigned_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpgsigned_result result;
    return xx_gpgsigned_decode(self, NULL, &result, pd);
}

bool xx_gpgsigned_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpgsigned_result result;
    xx_gpgsigned *gpg = (xx_gpgsigned *)self;
    int64_t total_size;
    if (!self) return false;
    if (!xx_gpgsigned_decode(self, NULL, &result, pd)) {
        xx_gpgsigned_reset(gpg);
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    total_size = xx_io_total_size(self->device);
    gpg->uncompressed_size = result.uncompressed_size;
    gpg->packet_count = result.packet_count;
    gpg->stream_end = self->base_address + result.format_size;
    gpg->crc32 = result.crc32;
    gpg->first_packet_tag = result.first_tag;
    self->format_size = result.format_size;
    if (total_size > gpg->stream_end) {
        self->overlay_offset = gpg->stream_end;
        self->overlay_size = total_size - gpg->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->file_type = XX_GPGSIGNED_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_gpgsigned_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_gpgsigned_get_number_of_archive_records(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_gpgsigned_unpack_to_device(xx_gpgsigned *gpg,
                                   xx_io_device *destination,
                                   xx_pd_struct *pd) {
    xx_gpgsigned_result result;
    if (!gpg || !destination ||
        (!gpg->format.base_info_handled &&
         !xx_format_handle_base_info(&gpg->format, pd)) ||
        !gpg->format.is_valid ||
        !xx_gpgsigned_decode(&gpg->format, destination, &result, pd)) {
        return false;
    }
    return result.format_size == gpg->format.format_size &&
           result.uncompressed_size == gpg->uncompressed_size &&
           result.crc32 == gpg->crc32;
}

xx_archive_record_state *xx_gpgsigned_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_gpgsigned_copy_options(&state->options, options) ||
        !xx_gpgsigned_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_gpgsigned_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gpgsigned_archive_record_move_to_next(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One record only, so the first move always ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_gpgsigned_unpack_current_archive_record(Abstractformat *self,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    xx_gpgsigned *gpg = (xx_gpgsigned *)self;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    option = xx_gpgsigned_find_option(&state->options,
                                      XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: verify the record decodes as recorded. */
        xx_gpgsigned_result check;
        return xx_gpgsigned_decode(self, NULL, &check, pd) &&
               check.format_size == self->format_size &&
               check.uncompressed_size == gpg->uncompressed_size &&
               check.crc32 == gpg->crc32;
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
        destination = xx_str_concat3(base, "/", XX_GPGSIGNED_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_GPGSIGNED_MEMBER_NAME);
    }
    if (!destination || !xx_store_create_dirs_a(destination, false)) {
        goto cleanup;
    }
    {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        created = output != NULL;
        result = output && xx_gpgsigned_unpack_to_device(gpg, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_gpgsigned_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_gpgsigned_get_uncompressed_size(const xx_gpgsigned *gpg) {
    return gpg ? gpg->uncompressed_size : 0U;
}

uint64_t xx_gpgsigned_get_packet_count(const xx_gpgsigned *gpg) {
    return gpg ? gpg->packet_count : 0U;
}

int64_t xx_gpgsigned_get_stream_end(const xx_gpgsigned *gpg) {
    return gpg ? gpg->stream_end : -1;
}

uint32_t xx_gpgsigned_get_crc32(const xx_gpgsigned *gpg) {
    return gpg ? gpg->crc32 : 0U;
}
