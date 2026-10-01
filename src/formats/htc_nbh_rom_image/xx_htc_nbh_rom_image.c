/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/*
 * HTC NBH ROM image reader.  Written from the layout documented in
 * xx_htc_nbh_rom_image.h, which was established by examining the behaviour of
 * NBHextract 1.0 (pof & TheBlasphemer, xda-developers) and the NBH layout used
 * by HTCFlasher's yang/nbhextract (GPL; read for understanding only, no code
 * taken).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/htc_nbh_rom_image/xx_htc_nbh_rom_image.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef HTC_NBH_ROM_IMAGE
#define XX_HTC_NBH_ROM_IMAGE_FILE_TYPE XX_FILE_TYPE_HTC_NBH_ROM_IMAGE
#else
#define XX_HTC_NBH_ROM_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_NBH_SIGNATURE_SIZE 7U
#define XX_NBH_INITIAL_SIGNATURE_SIZE 16U
#define XX_NBH_BLOCK_HEADER_SIZE 9U
#define XX_NBH_FIRST_BLOCK_OFFSET \
    (XX_NBH_SIGNATURE_SIZE + XX_NBH_INITIAL_SIGNATURE_SIZE)
/** Real files carry 128- or 256-byte RSA signatures; far above that is not
 *  a block header. */
#define XX_NBH_MAX_BLOCK_SIGNATURE 0x10000U
#define XX_NBH_FLAG_LAST 2U
#define XX_NBH_DBH_HEADER_SIZE 0x200U
#define XX_NBH_DBH_MAGIC_SIZE 0x20U
#define XX_NBH_COPY_BUFFER 65536U
#define XX_NBH_NAME_SIZE 32U

static const uint8_t xx_nbh_signature[XX_NBH_SIGNATURE_SIZE] = {
    0x52U, 0x30U, 0x30U, 0x30U, 0x46U, 0x46U, 0x0AU /* "R000FF\n" */
};

typedef struct xx_nbh_block_s {
    int64_t physical; /**< Absolute offset of the block's data. */
    int64_t logical;  /**< Offset of that data inside the DBH image. */
    int64_t length;
} xx_nbh_block;

typedef struct xx_nbh_section_s {
    char name[XX_NBH_NAME_SIZE];
    uint32_t type;
    int64_t offset; /**< Inside the DBH image. */
    int64_t length;
} xx_nbh_section;

typedef struct xx_nbh_private_s {
    xx_nbh_block *blocks;
    size_t block_count;
    size_t block_capacity;
    int64_t image_size;
    int64_t archive_end;
    bool truncated;
    xx_nbh_section sections[XX_HTC_NBH_ROM_IMAGE_MAX_SECTIONS];
    size_t section_count;
    char device[33];
    char cid[33];
    char version[17];
    char language[17];
} xx_nbh_private;

typedef struct xx_nbh_stream_s {
    xx_nbh_private parsed;
    size_t index;
} xx_nbh_stream;

static void xx_nbh_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool xx_nbh_read_at(xx_io_device *device, int64_t offset, void *data,
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

static void xx_nbh_private_cleanup(xx_nbh_private *parsed) {
    if (!parsed) return;
    if (parsed->blocks) xx_mem_free(parsed->blocks);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->archive_end = -1;
}

static bool xx_nbh_push_block(xx_nbh_private *parsed, int64_t physical,
                              int64_t length) {
    xx_nbh_block *block;
    if (length <= 0) return true; /* nothing to map */
    if (parsed->block_count >= XX_HTC_NBH_ROM_IMAGE_MAX_BLOCKS) return false;
    if (parsed->block_count == parsed->block_capacity) {
        size_t capacity =
            parsed->block_capacity ? parsed->block_capacity * 2U : 64U;
        xx_nbh_block *grown;
        if (capacity > XX_HTC_NBH_ROM_IMAGE_MAX_BLOCKS) {
            capacity = XX_HTC_NBH_ROM_IMAGE_MAX_BLOCKS;
        }
        grown = (xx_nbh_block *)xx_mem_realloc(parsed->blocks,
                                               capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->blocks = grown;
        parsed->block_capacity = capacity;
    }
    if (parsed->image_size > INT64_MAX - length) return false;
    block = &parsed->blocks[parsed->block_count++];
    block->physical = physical;
    block->logical = parsed->image_size;
    block->length = length;
    parsed->image_size += length;
    return true;
}

/* Index of the block holding DBH offset @p logical (< image_size). */
static size_t xx_nbh_find_block(const xx_nbh_private *parsed, int64_t logical) {
    size_t low = 0U;
    size_t high = parsed->block_count;
    while (high - low > 1U) {
        size_t middle = low + (high - low) / 2U;
        if (parsed->blocks[middle].logical <= logical) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

/* Read DBH bytes [logical, logical + size) across block boundaries. */
static bool xx_nbh_read_logical(xx_io_device *device,
                                const xx_nbh_private *parsed, int64_t logical,
                                uint8_t *data, size_t size) {
    size_t index;
    if (logical < 0 || parsed->block_count == 0U ||
        (uint64_t)size > (uint64_t)parsed->image_size ||
        logical > parsed->image_size - (int64_t)size) {
        return false;
    }
    if (size == 0U) return true;
    index = xx_nbh_find_block(parsed, logical);
    while (size != 0U) {
        const xx_nbh_block *block;
        int64_t within;
        int64_t available;
        size_t step;
        if (index >= parsed->block_count) return false;
        block = &parsed->blocks[index];
        within = logical - block->logical;
        if (within < 0 || within >= block->length) return false;
        available = block->length - within;
        step = ((uint64_t)available < (uint64_t)size) ? (size_t)available
                                                        : size;
        if (!xx_nbh_read_at(device, block->physical + within, data, step)) {
            return false;
        }
        data += step;
        size -= step;
        logical += (int64_t)step;
        ++index;
    }
    return true;
}

static void xx_nbh_copy_text(char *destination, size_t capacity,
                             const uint8_t *source, size_t length) {
    size_t index;
    if (capacity == 0U) return;
    for (index = 0U; index < length && index + 1U < capacity; ++index) {
        uint8_t byte = source[index];
        if (byte == 0U) break;
        destination[index] = (byte >= 0x20U && byte < 0x7FU) ? (char)byte : '?';
    }
    destination[index] = '\0';
}

static const char *xx_nbh_type_name(uint32_t type) {
    switch (type) {
        case 0x100U: return "IPL";
        case 0x101U: return "G3IPL";
        case 0x102U: return "G4IPL";
        case 0x200U: return "SPL";
        case 0x201U: return "G3SPL";
        case 0x202U: return "G4SPL";
        case 0x300U: return "GSM";
        case 0x400U: return "OS";
        case 0x600U: return "MainSplash";
        case 0x601U: return "SubSplash";
        case 0x700U:
        case 0x900U: return "ExtROM";
        default: return "Unknown";
    }
}

/* "%02d_%s.nb" without the CRT. */
static void xx_nbh_make_name(char *out, unsigned index, const char *type) {
    size_t at = 0U;
    out[at++] = (char)('0' + (index / 10U) % 10U);
    out[at++] = (char)('0' + index % 10U);
    out[at++] = '_';
    while (*type && at + 4U < XX_NBH_NAME_SIZE) out[at++] = *type++;
    out[at++] = '.';
    out[at++] = 'n';
    out[at++] = 'b';
    out[at] = '\0';
}

/* ------------------------------------------------------------------------ */
/* Parse                                                                     */
/* ------------------------------------------------------------------------ */

static bool xx_nbh_walk_blocks(Abstractformat *self, xx_nbh_private *parsed,
                               int64_t total, xx_pd_struct *pd) {
    int64_t position = self->base_address + (int64_t)XX_NBH_FIRST_BLOCK_OFFSET;
    size_t headers = 0U;
    for (;;) {
        uint8_t header[XX_NBH_BLOCK_HEADER_SIZE];
        uint32_t data_length, signature_length;
        uint8_t flag;
        int64_t data_start, available, signature_end;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (headers >= XX_HTC_NBH_ROM_IMAGE_MAX_BLOCKS) {
            parsed->truncated = true;
            break;
        }
        if (position > total - (int64_t)XX_NBH_BLOCK_HEADER_SIZE ||
            !xx_nbh_read_at(self->device, position, header, sizeof(header))) {
            parsed->truncated = true;
            break;
        }
        data_length = xx_data_get_u32(header, sizeof(header), 0U, false);
        signature_length = xx_data_get_u32(header, sizeof(header), 4U, false);
        flag = header[8];
        /* Only flags 1 (more) and 2 (last) are ever written; 0 is tolerated.
         * Anything else, or an absurd signature, is not a block header: the
         * first one makes the file invalid, a later one ends the stream. */
        if (flag > XX_NBH_FLAG_LAST ||
            signature_length > XX_NBH_MAX_BLOCK_SIGNATURE) {
            if (headers == 0U) return false;
            parsed->truncated = true;
            break;
        }
        ++headers;
        data_start = position + (int64_t)XX_NBH_BLOCK_HEADER_SIZE;
        available = total - data_start;
        if ((int64_t)data_length > available) {
            /* Truncated inside the data: keep what is there. */
            if (!xx_nbh_push_block(parsed, data_start, available)) return false;
            parsed->archive_end = total;
            parsed->truncated = true;
            break;
        }
        if (!xx_nbh_push_block(parsed, data_start, (int64_t)data_length)) {
            return false;
        }
        signature_end =
            data_start + (int64_t)data_length + (int64_t)signature_length;
        if (signature_end > total) {
            parsed->archive_end = total;
            parsed->truncated = true;
            break;
        }
        position = signature_end;
        parsed->archive_end = position;
        if (flag == XX_NBH_FLAG_LAST) break;
    }
    return parsed->archive_end > 0;
}

static bool xx_nbh_parse(Abstractformat *self, xx_nbh_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t signature[XX_NBH_SIGNATURE_SIZE];
    uint8_t header[XX_NBH_DBH_HEADER_SIZE];
    int64_t total;
    unsigned slot;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->archive_end = -1;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < 0 || self->base_address > total ||
        total - self->base_address <
            (int64_t)(XX_NBH_FIRST_BLOCK_OFFSET + XX_NBH_BLOCK_HEADER_SIZE +
                      XX_NBH_DBH_HEADER_SIZE)) {
        return false;
    }
    if (!xx_nbh_read_at(self->device, self->base_address, signature,
                        sizeof(signature)) ||
        xx_rt_memcmp(signature, xx_nbh_signature, sizeof(signature)) != 0) {
        return false;
    }
    if (!xx_nbh_walk_blocks(self, parsed, total, pd)) goto fail;
    if (parsed->image_size < (int64_t)XX_NBH_DBH_HEADER_SIZE ||
        !xx_nbh_read_logical(self->device, parsed, 0, header,
                             sizeof(header))) {
        goto fail;
    }
    /* "HTCIMAGE", one character per little-endian 32-bit word. */
    {
        static const char magic[] = "HTCIMAGE";
        for (slot = 0U; slot < 8U; ++slot) {
            if (xx_data_get_u32(header, sizeof(header), slot * 4U, false) !=
                (uint32_t)(uint8_t)magic[slot]) {
                goto fail;
            }
        }
    }
    xx_nbh_copy_text(parsed->device, sizeof(parsed->device), header + 0x20U,
                     32U);
    xx_nbh_copy_text(parsed->cid, sizeof(parsed->cid), header + 0x1C0U, 32U);
    xx_nbh_copy_text(parsed->version, sizeof(parsed->version), header + 0x1E0U,
                     16U);
    xx_nbh_copy_text(parsed->language, sizeof(parsed->language),
                     header + 0x1F0U, 16U);
    for (slot = 0U; slot < XX_HTC_NBH_ROM_IMAGE_MAX_SECTIONS; ++slot) {
        uint32_t type =
            xx_data_get_u32(header, sizeof(header), 0x40U + slot * 4U, false);
        uint32_t offset =
            xx_data_get_u32(header, sizeof(header), 0xC0U + slot * 4U, false);
        uint32_t length =
            xx_data_get_u32(header, sizeof(header), 0x140U + slot * 4U, false);
        xx_nbh_section *section;
        if (type == 0U) continue;
        /* A section must lie wholly inside the reassembled image. */
        if ((int64_t)offset > parsed->image_size ||
            (int64_t)length > parsed->image_size - (int64_t)offset) {
            continue;
        }
        section = &parsed->sections[parsed->section_count++];
        section->type = type;
        section->offset = (int64_t)offset;
        section->length = (int64_t)length;
        xx_nbh_make_name(section->name, slot, xx_nbh_type_name(type));
    }
    return true;
fail:
    xx_nbh_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Records                                                                   */
/* ------------------------------------------------------------------------ */

static bool xx_nbh_copy_options(xx_list_s *destination,
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

static const xx_var *xx_nbh_find_option(const xx_list_s *options,
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

static bool xx_nbh_populate_record(Abstractformat *self,
                                   xx_archive_record *record,
                                   const xx_nbh_private *parsed,
                                   const xx_nbh_section *section) {
    int64_t physical = -1;
    if (!record || !parsed || !section) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (section->length > 0 && section->offset < parsed->image_size) {
        const xx_nbh_block *block =
            &parsed->blocks[xx_nbh_find_block(parsed, section->offset)];
        physical = block->physical + (section->offset - block->logical);
    }
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_NBH_FIRST_BLOCK_OFFSET;
    /* The first byte's file position; the rest may continue past block
     * headers and signatures, so the span is not contiguous in the file. */
    record->data_offset = physical;
    record->compressed_size = section->length;
    return xx_archive_record_set_original_name(record, section->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)section->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)section->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_nbh_stream_free(void *pointer) {
    xx_nbh_stream *stream = (xx_nbh_stream *)pointer;
    if (!stream) return;
    xx_nbh_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

static bool xx_nbh_write_section(xx_io_device *source,
                                 const xx_nbh_private *parsed,
                                 const xx_nbh_section *section,
                                 xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool result = true;
    if (section->length == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(XX_NBH_COPY_BUFFER);
    if (!buffer) return false;
    while (done < section->length) {
        int64_t remaining = section->length - done;
        size_t step = remaining < (int64_t)XX_NBH_COPY_BUFFER
                          ? (size_t)remaining
                          : (size_t)XX_NBH_COPY_BUFFER;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_nbh_read_logical(source, parsed, section->offset + done,
                                 buffer, step) ||
            xx_io_write(destination, buffer, step) != (ssize_t)step) {
            result = false;
            break;
        }
        done += (int64_t)step;
    }
    xx_mem_free(buffer);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_htc_nbh_rom_image_init(xx_htc_nbh_rom_image *archive,
                               xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HTC_NBH_ROM_IMAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-htc-nbh");
    xx_format_set_extension(&archive->format, "nbh");
    archive->format.check_is_valid = xx_htc_nbh_rom_image_check_is_valid;
    archive->format.handle_base_info = xx_htc_nbh_rom_image_handle_base_info;
    archive->format.get_format_size = xx_htc_nbh_rom_image_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_htc_nbh_rom_image_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_htc_nbh_rom_image_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_htc_nbh_rom_image_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_htc_nbh_rom_image_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_htc_nbh_rom_image_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_htc_nbh_rom_image_free_archive_records_reading;
    archive->format.destroy = xx_nbh_vtable_destroy;
    archive->archive_end = -1;
}

xx_htc_nbh_rom_image *xx_htc_nbh_rom_image_create(xx_io_device *device,
                                                  int64_t base_address) {
    xx_htc_nbh_rom_image *archive =
        (xx_htc_nbh_rom_image *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_htc_nbh_rom_image_init(archive, device, base_address);
    return archive;
}

void xx_htc_nbh_rom_image_destroy(xx_htc_nbh_rom_image *archive) {
    if (!archive) return;
    if (archive->internal) {
        xx_nbh_private_cleanup((xx_nbh_private *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_nbh_vtable_destroy(Abstractformat *self) {
    xx_htc_nbh_rom_image_destroy((xx_htc_nbh_rom_image *)self);
}

void xx_htc_nbh_rom_image_free(xx_htc_nbh_rom_image *archive) {
    if (!archive) return;
    xx_htc_nbh_rom_image_destroy(archive);
    xx_mem_free(archive);
}

bool xx_htc_nbh_rom_image_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_nbh_private parsed;
    bool result = xx_nbh_parse(self, &parsed, pd);
    xx_nbh_private_cleanup(&parsed);
    return result;
}

bool xx_htc_nbh_rom_image_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_htc_nbh_rom_image *archive = (xx_htc_nbh_rom_image *)self;
    xx_nbh_private *parsed;
    int64_t total;
    if (!self) return false;
    parsed = (xx_nbh_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_nbh_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (archive->internal) {
        xx_nbh_private_cleanup((xx_nbh_private *)archive->internal);
        xx_mem_free(archive->internal);
    }
    archive->internal = parsed;
    archive->number_of_records = parsed->section_count;
    archive->number_of_blocks = parsed->block_count;
    archive->image_size = parsed->image_size;
    archive->archive_end = parsed->archive_end;
    archive->truncated = parsed->truncated;
    xx_rt_memcpy(archive->device, parsed->device, sizeof(archive->device));
    xx_rt_memcpy(archive->cid, parsed->cid, sizeof(archive->cid));
    xx_rt_memcpy(archive->version, parsed->version, sizeof(archive->version));
    xx_rt_memcpy(archive->language, parsed->language,
                 sizeof(archive->language));
    if (parsed->version[0]) xx_format_set_version(self, parsed->version);
    self->format_size = parsed->archive_end - self->base_address;
    total = xx_io_total_size(self->device);
    if (total > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->section_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_htc_nbh_rom_image_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_htc_nbh_rom_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_htc_nbh_rom_image *)self)->number_of_records;
}

xx_archive_record_state *xx_htc_nbh_rom_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_nbh_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_nbh_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_nbh_copy_options(&state->options, options) ||
        !xx_nbh_parse(self, &stream->parsed, pd)) {
        xx_nbh_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_nbh_stream_free;
    state->total_records = (int64_t)stream->parsed.section_count;
    if (stream->parsed.section_count != 0U &&
        xx_nbh_populate_record(self, &state->current_record, &stream->parsed,
                               &stream->parsed.sections[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_htc_nbh_rom_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_htc_nbh_rom_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_nbh_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_nbh_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.section_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_nbh_populate_record(self, &state->current_record, &stream->parsed,
                                &stream->parsed.sections[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_htc_nbh_rom_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_nbh_stream *stream;
    const xx_nbh_section *section;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool created = false;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_nbh_stream *)state->internal_state;
    if (stream->index >= stream->parsed.section_count) return false;
    section = &stream->parsed.sections[stream->index];
    option = xx_nbh_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the span is addressable. */
        return section->offset >= 0 && section->length >= 0 &&
               section->offset <= stream->parsed.image_size &&
               section->length <= stream->parsed.image_size - section->offset;
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
    /* The name is built here from the slot index and a fixed type table, so
     * it is always a plain, unique file name. */
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", section->name);
    } else {
        destination = xx_str_concat(base, section->name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    output = xx_io_file_open(destination, "wb");
    if (!output) goto cleanup;
    created = true;
    result = xx_nbh_write_section(self->device, &stream->parsed, section,
                                  output, pd);
    if (xx_io_close(output) != 0) result = false;
cleanup:
    if (!result && destination && created) xx_rt_remove(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_htc_nbh_rom_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
