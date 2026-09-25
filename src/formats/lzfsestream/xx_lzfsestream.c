/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzfsestream/xx_lzfsestream.h"

#include "xxfclib/algo/lzfse/xx_lzfse.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_LZFSE exists in the enum. */
#ifdef LZFSE
#define XX_LZFSESTREAM_FILE_TYPE XX_FILE_TYPE_LZFSE
#else
#define XX_LZFSESTREAM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The single record: the decoded stream.  A literal, never file data. */
#define XX_LZFSESTREAM_PAYLOAD_NAME "payload"

/* The whole stream is read into memory and decoded in one call, so both ends
 * are capped; these match the zlib reader's ceilings. */
#define XX_LZFSESTREAM_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_LZFSESTREAM_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)
/* Every data block is at least eight bytes, so the input cap already bounds
 * the walk; this is a second, independent bound on the loop. */
#define XX_LZFSESTREAM_MAX_BLOCKS (UINT32_C(1) << 22)
/* The block walk reads the device through a window of this size, so a run of
 * tiny blocks costs memory copies rather than one device read each. */
#define XX_LZFSESTREAM_WINDOW_SIZE 65536U

/* The decoder writes into a buffer sized from the blocks' declared
 * n_raw_bytes, which a hostile header can inflate without supplying the data
 * behind it.  So a stream that declares more than this is decoded in stages:
 * the walk marks the block boundary before each point where the running
 * total first passes 16 MiB, 32 MiB, 64 MiB and so on, and every such prefix
 * must decode (with a bvx$ patched in after it) before the buffer for the
 * next one is allocated.  An output buffer is then at most 16 MiB, or less
 * than twice the output already proven plus the next block's declared size
 * (itself bounded by the walk's per-block checks). */
#define XX_LZFSESTREAM_DIRECT_OUTPUT ((uint64_t)16U * 1024U * 1024U)
/* 16 MiB doubled up to the 1 GiB cap is six steps; two spare. */
#define XX_LZFSESTREAM_MAX_CHECKPOINTS 8U

/* Block header sizes, as the reference decoder (src/algo/lzfse) skips them. */
#define XX_LZFSESTREAM_RAW_HEADER 8U
#define XX_LZFSESTREAM_LZVN_HEADER 12U
#define XX_LZFSESTREAM_V1_HEADER 772U
#define XX_LZFSESTREAM_V1_FIELDS 28U /* magic .. n_lmd_payload_bytes */
#define XX_LZFSESTREAM_V2_FIXED 32U
/* The fixed part plus 360 frequencies at their widest, 14 bits rounded up
 * to two bytes each: the largest header_size the decoder accepts. */
#define XX_LZFSESTREAM_V2_MAX_HEADER (32U + 2U * 360U)

/* An LZVN payload ends with an eight byte end-of-stream opcode. */
#define XX_LZFSESTREAM_LZVN_EOS 8U
/* No LZVN opcode yields more than 271 bytes from two (0xF0 nn, a bare match
 * of nn + 16), so a payload can never expand by more than 135.5 times. */
#define XX_LZFSESTREAM_LZVN_MAX_RATIO 136U

/* Per-block limits of an FSE (bvx1 / bvx2) block, from the reference. */
#define XX_LZFSESTREAM_FSE_MAX_LITERALS 40000U
#define XX_LZFSESTREAM_FSE_MAX_MATCHES 10000U
/* The literal buffer the decoder copies runs from, with its 64 byte tail. */
#define XX_LZFSESTREAM_FSE_LITERAL_BUFFER 40064U
/* The longest literal run one L/M/D symbol can carry: L base 60 plus 8
 * bits. */
#define XX_LZFSESTREAM_FSE_MAX_RUN 315U
/* The longest match one L/M/D symbol can carry: M base 312 plus 11 bits. */
#define XX_LZFSESTREAM_FSE_MAX_MATCH 2359U
/* An M symbol with no extra bits is a match of at most 15.  Every longer
 * match spends at least one extra bit read from the L/M/D payload, and no M
 * symbol yields more than 15 + 215 bytes per extra bit it spends (the widest,
 * 312 + 2047 for 11 bits, is 2359 <= 15 + 11 * 215).  So a block's matches
 * total at most 15 per symbol plus 215 * 8 per payload byte. */
#define XX_LZFSESTREAM_FSE_FREE_MATCH 15U
#define XX_LZFSESTREAM_FSE_MATCH_PER_BYTE (215U * 8U)

#define XX_LZFSESTREAM_20BIT_MASK UINT64_C(0xFFFFF)

/* A block boundary inside the stream and the output produced before it. */
typedef struct xx_lzfsestream_checkpoint_s {
    uint64_t offset;
    uint64_t raw;
} xx_lzfsestream_checkpoint;

typedef struct xx_lzfsestream_scan_s {
    int64_t stream_size;
    uint64_t uncompressed_size;
    uint32_t number_of_blocks;
    uint32_t block_types;
    uint32_t number_of_checkpoints;
    xx_lzfsestream_checkpoint checkpoints[XX_LZFSESTREAM_MAX_CHECKPOINTS];
} xx_lzfsestream_scan;

/* The walk's view of the device: WINDOW_SIZE bytes starting at stream
 * offset `start`, of which `size` are valid. */
typedef struct xx_lzfsestream_window_s {
    xx_io_device *device;
    int64_t base;
    uint64_t limit;
    uint64_t start;
    size_t size;
    uint8_t *buffer;
} xx_lzfsestream_window;

static void xx_lzfsestream_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Device helpers                                                            */
/* ------------------------------------------------------------------------ */

static bool xx_lzfsestream_read_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_lzfsestream_write_all(xx_io_device *device, const void *data,
                                     size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy `size` bytes at stream offset `offset` out of the window, refilling
 * it from the device first when they are not all inside.  Never reads at or
 * past window->limit. */
static bool xx_lzfsestream_window_get(xx_lzfsestream_window *window,
                                      uint64_t offset, uint8_t *out,
                                      size_t size) {
    size_t index;
    uint64_t skip;
    if (!window || !window->buffer || !out || size == 0U ||
        size > XX_LZFSESTREAM_WINDOW_SIZE || size > window->limit ||
        offset > window->limit - size) {
        return false;
    }
    if (offset < window->start || offset - window->start > window->size ||
        size > window->size - (size_t)(offset - window->start)) {
        uint64_t want = window->limit - offset;
        if (want > XX_LZFSESTREAM_WINDOW_SIZE) want = XX_LZFSESTREAM_WINDOW_SIZE;
        window->start = offset;
        window->size = 0U;
        if (!xx_lzfsestream_read_at(window->device,
                                    window->base + (int64_t)offset,
                                    window->buffer, (size_t)want)) {
            return false;
        }
        window->size = (size_t)want;
    }
    skip = offset - window->start;
    for (index = 0U; index < size; ++index) {
        out[index] = window->buffer[(size_t)skip + index];
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Block walk                                                                */
/* ------------------------------------------------------------------------ */

/*
 * binwalk's lzfse_parser, made strict.  binwalk walks block headers until a
 * bvx$ marker and fails on anything else; this does the same over at most
 * XX_LZFSESTREAM_MAX_INPUT bytes of the device, and additionally refuses
 * every header the decoder would refuse (so the walk and the decode agree on
 * block boundaries), every payload that does not fit in the device, and any
 * declared n_raw_bytes no payload of that size could decode to.  It also
 * marks the checkpoints the staged decode verifies.
 */
static bool xx_lzfsestream_walk_window(xx_lzfsestream_window *window,
                                       xx_lzfsestream_scan *scan,
                                       xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    uint64_t raw_total = 0U;
    uint64_t threshold = XX_LZFSESTREAM_DIRECT_OUTPUT;
    uint32_t blocks = 0U;
    uint32_t types = 0U;
    uint32_t checkpoints = 0U;

    for (;;) {
        uint8_t header[XX_LZFSESTREAM_V2_FIXED];
        uint32_t magic;
        uint64_t remaining;
        uint64_t raw;
        uint64_t block_size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (cursor > window->limit) return false;
        remaining = window->limit - cursor;
        if (remaining < 4U ||
            !xx_lzfsestream_window_get(window, cursor, header, 4U)) {
            return false;
        }
        magic = xx_data_get_u32(header, 4U, 0U, false);

        if (magic == XX_LZFSE_MAGIC_ENDOFSTREAM) {
            /* binwalk's magics are the four data blocks only, so a stream
             * that is nothing but the marker is not one. */
            if (blocks == 0U) return false;
            cursor += 4U;
            break;
        }
        if (blocks >= XX_LZFSESTREAM_MAX_BLOCKS) return false;

        if (magic == XX_LZFSE_MAGIC_UNCOMPRESSED) {
            if (remaining < XX_LZFSESTREAM_RAW_HEADER ||
                !xx_lzfsestream_window_get(window, cursor, header,
                                           XX_LZFSESTREAM_RAW_HEADER)) {
                return false;
            }
            raw = xx_data_get_u32(header, XX_LZFSESTREAM_RAW_HEADER, 4U, false);
            if (raw > remaining - XX_LZFSESTREAM_RAW_HEADER) return false;
            block_size = XX_LZFSESTREAM_RAW_HEADER + raw;
            types |= XX_LZFSESTREAM_BLOCK_RAW;
        } else if (magic == XX_LZFSE_MAGIC_COMPRESSEDLZVN) {
            uint64_t payload;
            if (remaining < XX_LZFSESTREAM_LZVN_HEADER ||
                !xx_lzfsestream_window_get(window, cursor, header,
                                           XX_LZFSESTREAM_LZVN_HEADER)) {
                return false;
            }
            raw = xx_data_get_u32(header, XX_LZFSESTREAM_LZVN_HEADER, 4U, false);
            payload = xx_data_get_u32(header, XX_LZFSESTREAM_LZVN_HEADER, 8U,
                                      false);
            if (payload < XX_LZFSESTREAM_LZVN_EOS ||
                payload > remaining - XX_LZFSESTREAM_LZVN_HEADER ||
                raw > payload * XX_LZFSESTREAM_LZVN_MAX_RATIO) {
                return false;
            }
            block_size = XX_LZFSESTREAM_LZVN_HEADER + payload;
            types |= XX_LZFSESTREAM_BLOCK_LZVN;
        } else if (magic == XX_LZFSE_MAGIC_COMPRESSEDV1 ||
                   magic == XX_LZFSE_MAGIC_COMPRESSEDV2) {
            uint64_t header_size;
            uint64_t literals;
            uint64_t matches;
            uint64_t literal_payload;
            uint64_t lmd_payload;
            uint64_t run_limit;
            uint64_t match_limit;
            uint64_t bit_limit;
            if (magic == XX_LZFSE_MAGIC_COMPRESSEDV1) {
                /* The v1 header is the reference's C struct copied whole:
                 * the scalars first, then 360 u16 frequencies and two bytes
                 * of alignment padding. */
                header_size = XX_LZFSESTREAM_V1_HEADER;
                if (remaining < header_size ||
                    !xx_lzfsestream_window_get(window, cursor, header,
                                               XX_LZFSESTREAM_V1_FIELDS)) {
                    return false;
                }
                raw = xx_data_get_u32(header, sizeof(header), 4U, false);
                literals = xx_data_get_u32(header, sizeof(header), 12U, false);
                matches = xx_data_get_u32(header, sizeof(header), 16U, false);
                literal_payload =
                    xx_data_get_u32(header, sizeof(header), 20U, false);
                lmd_payload = xx_data_get_u32(header, sizeof(header), 24U, false);
                types |= XX_LZFSESTREAM_BLOCK_V1;
            } else {
                uint64_t v0;
                uint64_t v1;
                uint64_t v2;
                if (remaining < XX_LZFSESTREAM_V2_FIXED ||
                    !xx_lzfsestream_window_get(window, cursor, header,
                                               XX_LZFSESTREAM_V2_FIXED)) {
                    return false;
                }
                raw = xx_data_get_u32(header, sizeof(header), 4U, false);
                v0 = xx_data_get_u64(header, sizeof(header), 8U, false);
                v1 = xx_data_get_u64(header, sizeof(header), 16U, false);
                v2 = xx_data_get_u64(header, sizeof(header), 24U, false);
                header_size = v2 & UINT64_C(0xFFFFFFFF);
                literals = v0 & XX_LZFSESTREAM_20BIT_MASK;
                literal_payload = (v0 >> 20) & XX_LZFSESTREAM_20BIT_MASK;
                matches = (v0 >> 40) & XX_LZFSESTREAM_20BIT_MASK;
                lmd_payload = (v1 >> 40) & XX_LZFSESTREAM_20BIT_MASK;
                if (header_size < XX_LZFSESTREAM_V2_FIXED ||
                    header_size > XX_LZFSESTREAM_V2_MAX_HEADER ||
                    header_size > remaining) {
                    return false;
                }
                types |= XX_LZFSESTREAM_BLOCK_V2;
            }
            if (literals > XX_LZFSESTREAM_FSE_MAX_LITERALS ||
                matches > XX_LZFSESTREAM_FSE_MAX_MATCHES) {
                return false;
            }
            /* Both payloads are at most 2^32 - 1 here, so the sum cannot
             * wrap a 64-bit value. */
            if (literal_payload + lmd_payload > remaining - header_size) {
                return false;
            }
            /* Every output byte of an FSE block comes from one of its
             * n_matches L/M/D symbols: a literal run, bounded by the
             * decoder's literal buffer, then a match, bounded both per
             * symbol and by the bits the payload holds.  A header that
             * claims more cannot decode.  With matches <= 10000 and
             * lmd_payload < 2^32 nothing here wraps. */
            run_limit = matches * XX_LZFSESTREAM_FSE_MAX_RUN;
            match_limit = matches * XX_LZFSESTREAM_FSE_MAX_MATCH;
            bit_limit = matches * XX_LZFSESTREAM_FSE_FREE_MATCH +
                        lmd_payload * XX_LZFSESTREAM_FSE_MATCH_PER_BYTE;
            if (run_limit > XX_LZFSESTREAM_FSE_LITERAL_BUFFER) {
                run_limit = XX_LZFSESTREAM_FSE_LITERAL_BUFFER;
            }
            if (match_limit > bit_limit) match_limit = bit_limit;
            if (raw > run_limit + match_limit) return false;
            block_size = header_size + literal_payload + lmd_payload;
        } else {
            return false; /* not a block magic */
        }

        if (raw > XX_LZFSESTREAM_MAX_OUTPUT - raw_total) return false;
        /* raw_total never exceeds threshold, so this cannot wrap. */
        if (raw > threshold - raw_total) {
            if (raw_total != 0U) {
                if (checkpoints >= XX_LZFSESTREAM_MAX_CHECKPOINTS) return false;
                scan->checkpoints[checkpoints].offset = cursor;
                scan->checkpoints[checkpoints].raw = raw_total;
                ++checkpoints;
            }
            /* Stops at 1 GiB at the latest, since raw_total + raw is
             * within the output cap. */
            while (threshold < raw_total + raw) threshold *= 2U;
        }
        raw_total += raw;
        cursor += block_size;
        ++blocks;
    }

    scan->stream_size = (int64_t)cursor;
    scan->uncompressed_size = raw_total;
    scan->number_of_blocks = blocks;
    scan->block_types = types;
    scan->number_of_checkpoints = checkpoints;
    return true;
}

static bool xx_lzfsestream_walk(Abstractformat *self, xx_lzfsestream_scan *scan,
                                xx_pd_struct *pd) {
    xx_lzfsestream_window window;
    int64_t total_size;
    bool result;
    if (!self || !self->device || !scan || self->base_address < 0) {
        return false;
    }
    xx_mem_zero(scan, sizeof(*scan));
    scan->stream_size = -1;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < (int64_t)XX_LZFSESTREAM_MIN_SIZE) {
        return false;
    }
    xx_mem_zero(&window, sizeof(window));
    window.device = self->device;
    window.base = self->base_address;
    window.limit = (uint64_t)(total_size - self->base_address);
    if (window.limit > XX_LZFSESTREAM_MAX_INPUT) {
        window.limit = XX_LZFSESTREAM_MAX_INPUT;
    }
    window.buffer = (uint8_t *)xx_mem_alloc(XX_LZFSESTREAM_WINDOW_SIZE);
    if (!window.buffer) return false;
    result = xx_lzfsestream_walk_window(&window, scan, pd);
    xx_mem_free(window.buffer);
    if (!result) scan->stream_size = -1;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Decode                                                                    */
/* ------------------------------------------------------------------------ */

/* Decode input[0, end) followed by a bvx$ patched in over the next four
 * bytes, which must yield exactly `raw` bytes.  The caller guarantees that
 * end + 4 is within the input; the four bytes are restored afterwards. */
static bool xx_lzfsestream_verify_prefix(uint8_t *input, size_t end,
                                         size_t raw) {
    uint8_t saved[4];
    uint8_t *output;
    size_t written = 0U;
    bool decoded;
    output = (uint8_t *)xx_mem_alloc(raw != 0U ? raw : 1U);
    if (!output) return false;
    xx_rt_memcpy(saved, input + end, sizeof(saved));
    input[end] = (uint8_t)(XX_LZFSE_MAGIC_ENDOFSTREAM & 0xFFU);
    input[end + 1U] = (uint8_t)((XX_LZFSE_MAGIC_ENDOFSTREAM >> 8) & 0xFFU);
    input[end + 2U] = (uint8_t)((XX_LZFSE_MAGIC_ENDOFSTREAM >> 16) & 0xFFU);
    input[end + 3U] = (uint8_t)((XX_LZFSE_MAGIC_ENDOFSTREAM >> 24) & 0xFFU);
    decoded = xx_lzfse_decompress_memory(input, end + 4U, output, raw,
                                         &written) &&
              written == raw;
    xx_rt_memcpy(input + end, saved, sizeof(saved));
    xx_mem_free(output);
    return decoded;
}

/* Walk, then decode every block with src/algo/lzfse into a buffer of exactly
 * the declared size, after first proving each checkpointed prefix (see
 * XX_LZFSESTREAM_DIRECT_OUTPUT).  The decoder itself insists that each
 * compressed block yields exactly its n_raw_bytes and that the stream
 * reaches bvx$; the total is checked again here.  When destination is set
 * the output is written to it. */
static bool xx_lzfsestream_decode(Abstractformat *self,
                                  xx_lzfsestream_scan *scan,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size;
    size_t output_size;
    size_t written = 0U;
    uint32_t index;
    bool result = false;
    if (!xx_lzfsestream_walk(self, scan, pd)) return false;
    if (scan->stream_size < (int64_t)XX_LZFSESTREAM_MIN_SIZE ||
        (uint64_t)scan->stream_size > XX_LZFSESTREAM_MAX_INPUT ||
        (uint64_t)scan->stream_size > (uint64_t)SIZE_MAX ||
        scan->uncompressed_size > XX_LZFSESTREAM_MAX_OUTPUT ||
        scan->uncompressed_size > (uint64_t)SIZE_MAX ||
        scan->number_of_checkpoints > XX_LZFSESTREAM_MAX_CHECKPOINTS) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    input_size = (size_t)scan->stream_size;
    output_size = (size_t)scan->uncompressed_size;
    input = (uint8_t *)xx_mem_alloc(input_size);
    if (!input ||
        !xx_lzfsestream_read_at(self->device, self->base_address, input,
                                input_size)) {
        goto cleanup;
    }
    for (index = 0U; index < scan->number_of_checkpoints; ++index) {
        const xx_lzfsestream_checkpoint *checkpoint =
            &scan->checkpoints[index];
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        /* A checkpoint is where a data block starts, so the stream's own
         * bvx$ lies at least four bytes beyond it. */
        if (checkpoint->offset > (uint64_t)(input_size - 4U) ||
            checkpoint->raw > scan->uncompressed_size ||
            !xx_lzfsestream_verify_prefix(input, (size_t)checkpoint->offset,
                                          (size_t)checkpoint->raw)) {
            goto cleanup;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;
    /* An empty stream still gets a buffer, so the decoder is never handed a
     * NULL destination. */
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!output ||
        !xx_lzfse_decompress_memory(input, input_size, output, output_size,
                                    &written) ||
        written != output_size) {
        goto cleanup;
    }
    if (destination &&
        !xx_lzfsestream_write_all(destination, output, output_size, pd)) {
        goto cleanup;
    }
    result = true;
cleanup:
    if (output) xx_mem_free(output);
    if (input) xx_mem_free(input);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_lzfsestream_copy_options(xx_list_s *destination,
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

static const xx_var *xx_lzfsestream_find_option(const xx_list_s *options,
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

static bool xx_lzfsestream_populate_record(Abstractformat *self,
                                           xx_archive_record *record) {
    const xx_lzfsestream *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < (int64_t)XX_LZFSESTREAM_MIN_SIZE) {
        return false;
    }
    archive = (const xx_lzfsestream *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* The stream has no container header of its own: the record's data
     * starts at the first block and runs to the end of bvx$. */
    record->header_offset = self->base_address;
    record->header_size = 0;
    record->data_offset = self->base_address;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_original_name(record,
                                               XX_LZFSESTREAM_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_lzfsestream_init(xx_lzfsestream *archive, xx_io_device *device,
                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LZFSESTREAM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzfse");
    xx_format_set_extension(&archive->format, "lzfse");
    archive->format.check_is_valid = xx_lzfsestream_check_is_valid;
    archive->format.handle_base_info = xx_lzfsestream_handle_base_info;
    archive->format.get_format_size = xx_lzfsestream_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzfsestream_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzfsestream_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzfsestream_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzfsestream_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzfsestream_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzfsestream_free_archive_records_reading;
    archive->format.destroy = xx_lzfsestream_vtable_destroy;
    archive->stream_end = -1;
}

xx_lzfsestream *xx_lzfsestream_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_lzfsestream *archive =
        (xx_lzfsestream *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lzfsestream_init(archive, device, base_address);
    return archive;
}

void xx_lzfsestream_destroy(xx_lzfsestream *archive) {
    if (!archive) return;
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->number_of_blocks = 0U;
    archive->block_types = 0U;
}

static void xx_lzfsestream_vtable_destroy(Abstractformat *self) {
    xx_lzfsestream_destroy((xx_lzfsestream *)self);
}

void xx_lzfsestream_free(xx_lzfsestream *archive) {
    if (!archive) return;
    xx_lzfsestream_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lzfsestream_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzfsestream_scan scan;
    return xx_lzfsestream_decode(self, &scan, NULL, pd);
}

bool xx_lzfsestream_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzfsestream_scan scan;
    xx_lzfsestream *archive;
    int64_t total_size;
    if (!self) return false;
    archive = (xx_lzfsestream *)self;
    if (!xx_lzfsestream_decode(self, &scan, NULL, pd)) {
        archive->uncompressed_size = 0U;
        archive->stream_end = -1;
        archive->number_of_blocks = 0U;
        archive->block_types = 0U;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    total_size = xx_io_total_size(self->device);
    archive->uncompressed_size = scan.uncompressed_size;
    archive->stream_end = self->base_address + scan.stream_size;
    archive->number_of_blocks = scan.number_of_blocks;
    archive->block_types = scan.block_types;
    self->format_size = scan.stream_size;
    self->number_of_archive_records = 1U;
    if (archive->stream_end < total_size) {
        self->overlay_offset = archive->stream_end;
        self->overlay_size = total_size - archive->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_LZFSESTREAM_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lzfsestream_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_lzfsestream_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_lzfsestream_unpack_to_device(xx_lzfsestream *archive,
                                     xx_io_device *destination,
                                     xx_pd_struct *pd) {
    xx_lzfsestream_scan scan;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_lzfsestream_decode(&archive->format, &scan, destination, pd)) {
        return false;
    }
    /* The device may have changed between base info and now; the bytes
     * written are only the record if the stream is still the one sized. */
    return scan.stream_size == archive->format.format_size &&
           scan.uncompressed_size == archive->uncompressed_size;
}

xx_archive_record_state *xx_lzfsestream_create_archive_records_reading(
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
    if (!xx_lzfsestream_copy_options(&state->options, options) ||
        !xx_lzfsestream_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_lzfsestream_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lzfsestream_archive_record_move_to_next(Abstractformat *self,
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

bool xx_lzfsestream_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_lzfsestream *archive = (xx_lzfsestream *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value =
        xx_lzfsestream_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_lzfsestream_scan scan;
        return xx_lzfsestream_decode(self, &scan, NULL, pd) &&
               scan.stream_size == self->format_size &&
               scan.uncompressed_size == archive->uncompressed_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    /* The member name is the fixed literal above, never read from the file,
     * so it needs no sanitising before it becomes a path component. */
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path =
            xx_str_concat3(base_path, "/", XX_LZFSESTREAM_PAYLOAD_NAME);
    } else {
        destination_path =
            xx_str_concat(base_path, XX_LZFSESTREAM_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_lzfsestream_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_lzfsestream_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_lzfsestream_get_uncompressed_size(const xx_lzfsestream *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_lzfsestream_get_stream_end(const xx_lzfsestream *archive) {
    return archive ? archive->stream_end : -1;
}

uint32_t xx_lzfsestream_get_number_of_blocks(const xx_lzfsestream *archive) {
    return archive ? archive->number_of_blocks : 0U;
}

uint32_t xx_lzfsestream_get_block_types(const xx_lzfsestream *archive) {
    return archive ? archive->block_types : 0U;
}
