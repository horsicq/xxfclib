/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Direct3D shader containers ("DXBC").  The validation and the carve size
 * follow binwalk's "DirectX shader bytecode" signature: src/signatures/dxbc.rs
 * takes result.size from src/structures/dxbc.rs, which reads the 32-byte
 * header, requires the word at 0x14 to be 1, caps the chunk count at 32 and
 * looks up every chunk's FourCC through the offset table;
 * src/extractors/dxbc.rs carves total_size bytes.  binwalk's result.size is
 * total_size, and so is ours.
 *
 * binwalk only NAMES the shader model from the chunk ids ("Shader Model 4"
 * for SHDR, "Shader Model 5" for SHEX, "Unknown Shader Model" otherwise); it
 * accepts a container with neither, which is what root signatures (RTS0),
 * fx_4_0 effects (FX10), function libraries (LIBF/LIBH) and every DXIL
 * container look like.  This reader accepts them too.
 *
 * Stricter than binwalk, in ways every fxc/dxc container satisfies:
 *   - at least one chunk (binwalk accepts zero);
 *   - total_size covers the header and the offset table and fits in the file
 *     (binwalk drops a result that runs past EOF only later, in its scanner);
 *   - every chunk header and its data lie inside total_size and after the
 *     offset table (binwalk only needs four bytes somewhere in the file);
 *   - every FourCC is ASCII letters and digits.
 * Chunk alignment, ordering and contiguity are NOT required: fxc's function
 * libraries and fx_4_0 effects carry chunks of odd length.
 *
 * The container checksum is recomputed in handle_base_info and reported, never
 * enforced: binwalk ignores it, and unsigned DXIL stores sixteen zero bytes.
 *
 * Not an archive: binwalk's extractor carves the container itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dxbc/xx_dxbc.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DXBC exists in the enum. */
#ifdef DXBC
#define XX_DXBC_FILE_TYPE XX_FILE_TYPE_DXBC
#else
#define XX_DXBC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The checksum covers everything after the magic and the checksum itself. */
#define XX_DXBC_CHECKSUM_START (XX_DXBC_CHECKSUM_OFFSET + XX_DXBC_CHECKSUM_SIZE)
#define XX_DXBC_MD5_BLOCK 64U

typedef struct xx_dxbc_parsed_s {
    int64_t input_size;
    uint32_t total_size;
    uint32_t chunk_count;
    uint32_t chunk_ids[XX_DXBC_MAX_CHUNKS];
    uint32_t chunk_offsets[XX_DXBC_MAX_CHUNKS];
    uint32_t chunk_sizes[XX_DXBC_MAX_CHUNKS];
    uint8_t checksum[XX_DXBC_CHECKSUM_SIZE];
    uint32_t program_chunk_id;
    uint32_t program_version;
} xx_dxbc_parsed;

static void xx_dxbc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a shader carved out of a game
 * archive can sit past 2 GiB, and long is 32-bit on Win64. */
static bool xx_dxbc_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_dxbc_is_fourcc_char(uint8_t c) {
    return (c >= (uint8_t)'A' && c <= (uint8_t)'Z') ||
           (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
           (c >= (uint8_t)'0' && c <= (uint8_t)'9');
}

static void xx_dxbc_parsed_reset(xx_dxbc_parsed *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
}

/*
 * Everything binwalk's parse_dxbc_header checks, in its order, plus the
 * containment checks listed at the top of this file.  At most 1 + 1 + 32
 * reads of at most 128 bytes each, so the probe stays cheap on any input.
 */
static bool xx_dxbc_parse(Abstractformat *self, xx_dxbc_parsed *parsed,
                          xx_pd_struct *pd) {
    uint8_t header[XX_DXBC_HEADER_SIZE];
    uint8_t table[XX_DXBC_MAX_CHUNKS * 4U];
    uint8_t chunk[XX_DXBC_CHUNK_HEADER_SIZE];
    int64_t available;
    uint32_t one;
    uint32_t table_end;
    uint32_t index;

    xx_dxbc_parsed_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) goto fail;
    available = parsed->input_size - self->base_address;
    if (available < (int64_t)XX_DXBC_HEADER_SIZE ||
        !xx_dxbc_read_at(self->device, self->base_address, header,
                         sizeof(header)) ||
        xx_rt_memcmp(header, XX_DXBC_MAGIC, XX_DXBC_MAGIC_SIZE) != 0) {
        goto fail;
    }

    one = xx_data_get_u32(header, sizeof(header), XX_DXBC_ONE_OFFSET, false);
    if (one != 1U) goto fail;

    parsed->chunk_count = xx_data_get_u32(header, sizeof(header),
                                          XX_DXBC_CHUNK_COUNT_OFFSET, false);
    if (parsed->chunk_count == 0U ||
        parsed->chunk_count > XX_DXBC_MAX_CHUNKS) {
        goto fail;
    }
    /* count <= 32, so this is at most 160 and cannot overflow. */
    table_end = XX_DXBC_HEADER_SIZE + parsed->chunk_count * 4U;

    parsed->total_size = xx_data_get_u32(header, sizeof(header),
                                         XX_DXBC_TOTAL_SIZE_OFFSET, false);
    if (parsed->total_size < table_end + XX_DXBC_CHUNK_HEADER_SIZE ||
        (int64_t)parsed->total_size > available) {
        goto fail;
    }
    xx_rt_memcpy(parsed->checksum, header + XX_DXBC_CHECKSUM_OFFSET,
                 XX_DXBC_CHECKSUM_SIZE);

    if (!xx_dxbc_read_at(self->device,
                         self->base_address + (int64_t)XX_DXBC_HEADER_SIZE,
                         table, (size_t)parsed->chunk_count * 4U)) {
        goto fail;
    }

    for (index = 0U; index < parsed->chunk_count; ++index) {
        uint32_t offset = xx_data_get_u32(table, sizeof(table),
                                          (size_t)index * 4U, false);
        uint32_t size;
        uint32_t id;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* total_size >= table_end + 8 was checked above, so the
         * subtraction cannot wrap. */
        if (offset < table_end ||
            offset > parsed->total_size - XX_DXBC_CHUNK_HEADER_SIZE) {
            goto fail;
        }
        if (!xx_dxbc_read_at(self->device,
                             self->base_address + (int64_t)offset, chunk,
                             sizeof(chunk))) {
            goto fail;
        }
        if (!xx_dxbc_is_fourcc_char(chunk[0]) ||
            !xx_dxbc_is_fourcc_char(chunk[1]) ||
            !xx_dxbc_is_fourcc_char(chunk[2]) ||
            !xx_dxbc_is_fourcc_char(chunk[3])) {
            goto fail;
        }
        id = xx_data_get_u32(chunk, sizeof(chunk), 0U, false);
        size = xx_data_get_u32(chunk, sizeof(chunk), 4U, false);
        if (size > parsed->total_size - offset - XX_DXBC_CHUNK_HEADER_SIZE) {
            goto fail;
        }
        parsed->chunk_ids[index] = id;
        parsed->chunk_offsets[index] = offset;
        parsed->chunk_sizes[index] = size;
    }

    /* The first program chunk names the shader model.  SHDR, SHEX and the
     * DXIL program header all open with the same version token:
     * (type << 16) | (major << 4) | minor.  Informational only. */
    for (index = 0U; index < parsed->chunk_count; ++index) {
        uint32_t id = parsed->chunk_ids[index];
        uint8_t token[4];

        if (id != XX_DXBC_CHUNK_SHDR && id != XX_DXBC_CHUNK_SHEX &&
            id != XX_DXBC_CHUNK_DXIL) {
            continue;
        }
        parsed->program_chunk_id = id;
        if (parsed->chunk_sizes[index] >= 4U &&
            xx_dxbc_read_at(self->device,
                            self->base_address +
                                (int64_t)parsed->chunk_offsets[index] +
                                (int64_t)XX_DXBC_CHUNK_HEADER_SIZE,
                            token, sizeof(token))) {
            parsed->program_version =
                xx_data_get_u32(token, sizeof(token), 0U, false);
        }
        break;
    }
    return !(pd && xx_pd_is_stopped(pd));
fail:
    xx_dxbc_parsed_reset(parsed);
    return false;
}

/*
 * The DXBC checksum.  It is MD5's compression function, but the message is
 * not padded the MD5 way.  With P = bytes 0x14 .. total_size, n = |P|,
 * bits = 8n (32-bit), last = n mod 64 and full = n - last, the blocks are:
 *
 *   P[0 .. full)                                   as ordinary MD5 blocks, then
 *   last >= 56:  P[full ..] 0x80 0...               (one block)
 *                bits 0... ((bits >> 2) | 1)        (one block)
 *   last <  56:  bits P[full ..] 0x80 0... ((bits >> 2) | 1)   (one block)
 *
 * and the digest is the four state words, little endian, with no final
 * padding pass.  Checked against every fxc and dxc sample in the test set,
 * for tail lengths both above and below 56.
 */
static xx_dxbc_checksum_state_t xx_dxbc_compute_checksum_state(
    xx_io_device *device, int64_t base_address, const xx_dxbc_parsed *parsed,
    xx_pd_struct *pd) {
    static const uint8_t zero[XX_DXBC_CHECKSUM_SIZE] = {0};
    xx_hash_context context;
    uint8_t buffer[4096];
    uint8_t block[2U * XX_DXBC_MD5_BLOCK];
    uint8_t digest[XX_DXBC_CHECKSUM_SIZE];
    uint32_t payload;
    uint32_t last;
    uint32_t full;
    uint32_t done;
    uint32_t bits;
    uint32_t tail_word;
    size_t block_size;
    int64_t start;
    unsigned word;

    if (!device || !parsed || base_address < 0) {
        return XX_DXBC_CHECKSUM_NOT_CHECKED;
    }
    if (xx_rt_memcmp(parsed->checksum, zero, sizeof(zero)) == 0) {
        return XX_DXBC_CHECKSUM_ZERO;
    }
    if (parsed->total_size > XX_DXBC_CHECKSUM_MAX_SIZE ||
        parsed->total_size < XX_DXBC_CHECKSUM_START) {
        return XX_DXBC_CHECKSUM_NOT_CHECKED;
    }
    payload = parsed->total_size - XX_DXBC_CHECKSUM_START;
    last = payload % XX_DXBC_MD5_BLOCK;
    full = payload - last;
    /* payload <= 64 MiB, so 8 * payload fits in 32 bits. */
    bits = payload * 8U;
    tail_word = (bits >> 2U) | 1U;
    start = base_address + (int64_t)XX_DXBC_CHECKSUM_START;

    if (!xx_hash_init(&context, XX_HASH_MD5)) {
        return XX_DXBC_CHECKSUM_NOT_CHECKED;
    }
    for (done = 0U; done < full;) {
        uint32_t step = full - done;
        if (step > (uint32_t)sizeof(buffer)) step = (uint32_t)sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_dxbc_read_at(device, start + (int64_t)done, buffer, step)) {
            return XX_DXBC_CHECKSUM_NOT_CHECKED;
        }
        xx_hash_update(&context, buffer, step);
        done += step;
    }

    xx_mem_zero(block, sizeof(block));
    if (last >= 56U) {
        if (!xx_dxbc_read_at(device, start + (int64_t)full, block, last)) {
            return XX_DXBC_CHECKSUM_NOT_CHECKED;
        }
        block[last] = 0x80U;
        xx_data_set_u32(block, sizeof(block), XX_DXBC_MD5_BLOCK, bits, false);
        xx_data_set_u32(block, sizeof(block), 2U * XX_DXBC_MD5_BLOCK - 4U,
                        tail_word, false);
        block_size = 2U * XX_DXBC_MD5_BLOCK;
    } else {
        xx_data_set_u32(block, sizeof(block), 0U, bits, false);
        if (last != 0U &&
            !xx_dxbc_read_at(device, start + (int64_t)full, block + 4U,
                             last)) {
            return XX_DXBC_CHECKSUM_NOT_CHECKED;
        }
        block[4U + last] = 0x80U;
        xx_data_set_u32(block, sizeof(block), XX_DXBC_MD5_BLOCK - 4U,
                        tail_word, false);
        block_size = XX_DXBC_MD5_BLOCK;
    }
    xx_hash_update(&context, block, block_size);
    /* Every byte fed was part of a whole block, so the state is final. */
    if (context.buffered != 0U) return XX_DXBC_CHECKSUM_NOT_CHECKED;
    for (word = 0U; word < 4U; ++word) {
        xx_data_set_u32(digest, sizeof(digest), (size_t)word * 4U,
                        context.state[word], false);
    }
    return xx_rt_memcmp(digest, parsed->checksum, sizeof(digest)) == 0
               ? XX_DXBC_CHECKSUM_VALID
               : XX_DXBC_CHECKSUM_MISMATCH;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_dxbc_init(xx_dxbc *dxbc, xx_io_device *dev, int64_t base_address) {
    if (!dxbc) return;
    xx_mem_zero(dxbc, sizeof(*dxbc));
    xx_format_init(&dxbc->format, dev, base_address);
    dxbc->format.endian = XX_ENDIAN_LITTLE;
    dxbc->format.file_type = XX_DXBC_FILE_TYPE;
    /* Compiled GPU code has no format-type kind of its own; it is not a
     * host executable, an archive or a package, so it stays UNKNOWN. */
    dxbc->format.format_type = XX_TYPE_UNKNOWN;
    dxbc->format.is_archive = false;
    xx_format_set_mime_type(&dxbc->format, "application/x-dxbc");
    /* fxc's and Visual Studio's name for a compiled shader object. */
    xx_format_set_extension(&dxbc->format, "cso");
    dxbc->format.check_is_valid = xx_dxbc_check_is_valid;
    dxbc->format.handle_base_info = xx_dxbc_handle_base_info;
    dxbc->format.get_format_size = xx_dxbc_get_format_size;
    dxbc->format.destroy = xx_dxbc_vtable_destroy;
    dxbc->checksum_state = XX_DXBC_CHECKSUM_NOT_CHECKED;
}

xx_dxbc *xx_dxbc_create(xx_io_device *dev, int64_t base_address) {
    xx_dxbc *dxbc = (xx_dxbc *)xx_mem_alloc(sizeof(*dxbc));

    if (dxbc) xx_dxbc_init(dxbc, dev, base_address);
    return dxbc;
}

void xx_dxbc_destroy(xx_dxbc *dxbc) {
    if (!dxbc) return;
    xx_format_cleanup_extra_parameters(&dxbc->format);
}

static void xx_dxbc_vtable_destroy(Abstractformat *self) {
    xx_dxbc_destroy((xx_dxbc *)self);
}

void xx_dxbc_free(xx_dxbc *dxbc) {
    if (!dxbc) return;
    xx_dxbc_destroy(dxbc);
    xx_mem_free(dxbc);
}

/* -------------------------------------------------------------- format -- */

bool xx_dxbc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dxbc_parsed parsed;

    return xx_dxbc_parse(self, &parsed, pd);
}

bool xx_dxbc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dxbc *dxbc = (xx_dxbc *)self;
    xx_dxbc_parsed parsed;
    xx_dxbc_checksum_state_t checksum_state;
    int64_t end;

    if (!self || !dxbc) return false;
    if (!xx_dxbc_parse(self, &parsed, pd)) {
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    checksum_state = xx_dxbc_compute_checksum_state(
        self->device, self->base_address, &parsed, pd);
    if (pd && xx_pd_is_stopped(pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }

    dxbc->total_size = parsed.total_size;
    dxbc->chunk_count = parsed.chunk_count;
    xx_rt_memcpy(dxbc->chunk_ids, parsed.chunk_ids, sizeof(dxbc->chunk_ids));
    xx_rt_memcpy(dxbc->chunk_offsets, parsed.chunk_offsets,
                 sizeof(dxbc->chunk_offsets));
    xx_rt_memcpy(dxbc->chunk_sizes, parsed.chunk_sizes,
                 sizeof(dxbc->chunk_sizes));
    xx_rt_memcpy(dxbc->checksum, parsed.checksum, sizeof(dxbc->checksum));
    dxbc->checksum_state = checksum_state;
    dxbc->program_chunk_id = parsed.program_chunk_id;
    dxbc->program_version = parsed.program_version;

    /* binwalk's carve length.  parse() guaranteed base + total <= input. */
    end = self->base_address + (int64_t)parsed.total_size;
    self->format_size = (int64_t)parsed.total_size;
    if (end < parsed.input_size) {
        self->overlay_offset = end;
        self->overlay_size = parsed.input_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_DXBC_FILE_TYPE;
    self->number_of_archive_records = 0U;
    self->is_archive = false;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dxbc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_dxbc_get_total_size(const xx_dxbc *dxbc) {
    return dxbc ? dxbc->total_size : 0U;
}

uint32_t xx_dxbc_get_chunk_count(const xx_dxbc *dxbc) {
    return dxbc ? dxbc->chunk_count : 0U;
}

uint32_t xx_dxbc_get_chunk_id(const xx_dxbc *dxbc, uint32_t index) {
    return dxbc && index < dxbc->chunk_count && index < XX_DXBC_MAX_CHUNKS
               ? dxbc->chunk_ids[index]
               : 0U;
}

uint32_t xx_dxbc_get_chunk_offset(const xx_dxbc *dxbc, uint32_t index) {
    return dxbc && index < dxbc->chunk_count && index < XX_DXBC_MAX_CHUNKS
               ? dxbc->chunk_offsets[index]
               : 0U;
}

uint32_t xx_dxbc_get_chunk_size(const xx_dxbc *dxbc, uint32_t index) {
    return dxbc && index < dxbc->chunk_count && index < XX_DXBC_MAX_CHUNKS
               ? dxbc->chunk_sizes[index]
               : 0U;
}

bool xx_dxbc_has_chunk(const xx_dxbc *dxbc, uint32_t chunk_id) {
    uint32_t index;

    if (!dxbc) return false;
    for (index = 0U;
         index < dxbc->chunk_count && index < XX_DXBC_MAX_CHUNKS; ++index) {
        if (dxbc->chunk_ids[index] == chunk_id) return true;
    }
    return false;
}

xx_dxbc_checksum_state_t xx_dxbc_get_checksum_state(const xx_dxbc *dxbc) {
    return dxbc ? dxbc->checksum_state : XX_DXBC_CHECKSUM_NOT_CHECKED;
}

bool xx_dxbc_get_checksum(const xx_dxbc *dxbc, void *out, size_t out_size) {
    if (!dxbc || !out || out_size < XX_DXBC_CHECKSUM_SIZE) return false;
    xx_rt_memcpy(out, dxbc->checksum, XX_DXBC_CHECKSUM_SIZE);
    return true;
}

uint32_t xx_dxbc_get_program_chunk_id(const xx_dxbc *dxbc) {
    return dxbc ? dxbc->program_chunk_id : 0U;
}

xx_dxbc_program_type_t xx_dxbc_get_program_type(const xx_dxbc *dxbc) {
    /* A zero token means the program chunk was too short to carry one. */
    if (!dxbc || dxbc->program_chunk_id == 0U || dxbc->program_version == 0U) {
        return XX_DXBC_PROGRAM_NONE;
    }
    return (xx_dxbc_program_type_t)(dxbc->program_version >> 16U);
}

uint32_t xx_dxbc_get_shader_model_major(const xx_dxbc *dxbc) {
    return dxbc ? (dxbc->program_version >> 4U) & 0x0FU : 0U;
}

uint32_t xx_dxbc_get_shader_model_minor(const xx_dxbc *dxbc) {
    return dxbc ? dxbc->program_version & 0x0FU : 0U;
}
