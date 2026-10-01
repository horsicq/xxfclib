/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Layout: VICE manual, "The CRT cartridge image format" (documentation only;
 * no VICE code is used).  See xx_crt.h.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/crt/xx_crt.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef CRT
#define XX_CRT_FILE_TYPE XX_FILE_TYPE_CRT
#else
#define XX_CRT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_CRT_HEADER_SIZE 0x40U
#define XX_CRT_CHIP_HEADER_SIZE 0x10U
/** A header length beyond this is not a cartridge header. */
#define XX_CRT_MAX_HEADER_LENGTH 0xFFFFU
/** Largest packet: a 64 KiB image plus its header plus modest padding. */
#define XX_CRT_MAX_PACKET_LENGTH (0x10000U + 0x10000U)

static const char *const xx_crt_signatures[5] = {
    "C64 CARTRIDGE   ", "C128 CARTRIDGE  ", "CBM2 CARTRIDGE  ",
    "VIC20 CARTRIDGE ", "PLUS4 CARTRIDGE "};

typedef struct xx_crt_chip_s {
    int64_t data_offset; /**< Absolute device offset of the image data. */
    uint32_t data_size;
    uint16_t chip_type;
    uint16_t bank;
    uint16_t load_address;
} xx_crt_chip;

typedef struct xx_crt_private_s {
    xx_crt_chip *chips; /**< Only chips that carry data. */
    size_t count;
    size_t capacity;
    uint64_t packets;
    int64_t end; /**< Absolute offset just past the last packet. */
    int machine;
    uint8_t header[XX_CRT_HEADER_SIZE];
    uint32_t header_length;
} xx_crt_private;

typedef struct xx_crt_stream_s {
    xx_crt_private parsed;
    size_t index;
} xx_crt_stream;

static void xx_crt_vtable_destroy(Abstractformat *self);

static uint16_t xx_crt_be16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}
static uint32_t xx_crt_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool xx_crt_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static void xx_crt_private_cleanup(xx_crt_private *parsed) {
    if (!parsed) return;
    if (parsed->chips) xx_mem_free(parsed->chips);
    xx_mem_zero(parsed, sizeof(*parsed));
}

static bool xx_crt_push(xx_crt_private *parsed, const xx_crt_chip *chip) {
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity ? parsed->capacity * 2U : 16U;
        void *next;
        if (capacity > XX_CRT_MAX_CHIPS) capacity = XX_CRT_MAX_CHIPS;
        if (capacity <= parsed->count) return false;
        next = xx_mem_realloc(parsed->chips, capacity * sizeof(*parsed->chips));
        if (!next) return false;
        parsed->chips = (xx_crt_chip *)next;
        parsed->capacity = capacity;
    }
    parsed->chips[parsed->count++] = *chip;
    return true;
}

/* Check the fixed 64-byte header.  Returns the machine index or -1. */
static int xx_crt_check_header(const uint8_t *header) {
    int machine = -1;
    int index;
    for (index = 0; index < 5; ++index) {
        if (xx_rt_memcmp(header, xx_crt_signatures[index], 16U) == 0) {
            machine = index;
            break;
        }
    }
    if (machine < 0) return -1;
    /* Versions 1.x and 2.x are defined. */
    if (header[0x14] < 1U || header[0x14] > 2U) return -1;
    return machine;
}

/* Walk the image.  With collect == false only the structure is checked and
 * nothing is allocated. */
static bool xx_crt_parse(Abstractformat *self, xx_crt_private *parsed,
                         bool collect, xx_pd_struct *pd) {
    int64_t total, at, header_end;
    uint32_t header_length;
    uint64_t packets = 0U;
    xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < 0 || self->base_address > total ||
        total - self->base_address <
            (int64_t)(XX_CRT_HEADER_SIZE + XX_CRT_CHIP_HEADER_SIZE)) {
        return false;
    }
    if (!xx_crt_read_at(self->device, self->base_address, parsed->header,
                        XX_CRT_HEADER_SIZE)) {
        return false;
    }
    parsed->machine = xx_crt_check_header(parsed->header);
    if (parsed->machine < 0) return false;
    header_length = xx_crt_be32(parsed->header + 0x10);
    parsed->header_length = header_length;
    if (header_length > XX_CRT_MAX_HEADER_LENGTH) return false;
    /* Old C64 images state 0x20 but still have the full 64-byte header. */
    if (header_length < XX_CRT_HEADER_SIZE) header_length = XX_CRT_HEADER_SIZE;
    header_end = self->base_address + (int64_t)header_length;
    if (header_end > total) return false;

    at = header_end;
    for (;;) {
        uint8_t chip_header[XX_CRT_CHIP_HEADER_SIZE];
        xx_crt_chip chip;
        uint32_t packet_length;
        if (total - at < (int64_t)XX_CRT_CHIP_HEADER_SIZE) break;
        if (packets >= XX_CRT_MAX_CHIPS) break;
        if (pd && xx_pd_is_stopped(pd)) {
            xx_crt_private_cleanup(parsed);
            return false;
        }
        if (!xx_crt_read_at(self->device, at, chip_header,
                            XX_CRT_CHIP_HEADER_SIZE)) {
            break;
        }
        if (xx_rt_memcmp(chip_header, "CHIP", 4U) != 0) break;
        packet_length = xx_crt_be32(chip_header + 4);
        chip.chip_type = xx_crt_be16(chip_header + 8);
        chip.bank = xx_crt_be16(chip_header + 10);
        chip.load_address = xx_crt_be16(chip_header + 12);
        chip.data_size = xx_crt_be16(chip_header + 14);
        chip.data_offset = at + (int64_t)XX_CRT_CHIP_HEADER_SIZE;
        if (chip.chip_type > 3U || packet_length < XX_CRT_CHIP_HEADER_SIZE ||
            packet_length > XX_CRT_MAX_PACKET_LENGTH) {
            break;
        }
        if (packet_length - XX_CRT_CHIP_HEADER_SIZE < chip.data_size) {
            /* A RAM chip only declares its size; anything else must carry
             * its image. */
            if (chip.chip_type != 1U) break;
            chip.data_size = packet_length - XX_CRT_CHIP_HEADER_SIZE;
        }
        if ((int64_t)packet_length > total - at) break;
        ++packets;
        if (collect && chip.data_size != 0U && !xx_crt_push(parsed, &chip)) {
            xx_crt_private_cleanup(parsed);
            return false;
        }
        at += (int64_t)packet_length;
    }
    /* At least one intact CHIP packet is required. */
    if (packets == 0U) {
        xx_crt_private_cleanup(parsed);
        return false;
    }
    parsed->packets = packets;
    parsed->end = at;
    return true;
}

/* ------------------------------------------------------------------------ */

static void xx_crt_stream_free(void *pointer) {
    xx_crt_stream *stream = (xx_crt_stream *)pointer;
    if (!stream) return;
    xx_crt_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

static bool xx_crt_copy_options(xx_list_s *destination,
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

static const xx_var *xx_crt_find_option(const xx_list_s *options,
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

static bool xx_crt_populate_record(xx_archive_record *record,
                                   const xx_crt_chip *chip, size_t index) {
    static const char *const kinds[4] = {"", "_ram", "_flash", "_eeprom"};
    char name[64];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* Only numbers from the file go into the name, so it is always safe; the
     * running index keeps repeated bank/address pairs distinct. */
    (void)xx_rt_snprintf(name, sizeof(name), "chip%04u_bank%u_%04x%s.bin",
                         (unsigned)index, (unsigned)chip->bank,
                         (unsigned)chip->load_address,
                         kinds[chip->chip_type & 3U]);
    record->header_offset = chip->data_offset - (int64_t)XX_CRT_CHIP_HEADER_SIZE;
    record->header_size = (int64_t)XX_CRT_CHIP_HEADER_SIZE;
    record->data_offset = chip->data_offset;
    record->compressed_size = (int64_t)chip->data_size;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          chip->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          chip->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ------------------------------------------------------------------------ */

void xx_crt_init(xx_crt *crt, xx_io_device *dev, int64_t base_address) {
    if (!crt) return;
    xx_mem_zero(crt, sizeof(*crt));
    xx_format_init(&crt->format, dev, base_address);
    crt->format.endian = XX_ENDIAN_BIG;
    crt->format.file_type = XX_CRT_FILE_TYPE;
    crt->format.format_type = XX_TYPE_ARCHIVE;
    crt->format.is_archive = true;
    xx_format_set_mime_type(&crt->format, "application/x-commodore-crt");
    xx_format_set_extension(&crt->format, "crt");
    crt->format.check_is_valid = xx_crt_check_is_valid;
    crt->format.handle_base_info = xx_crt_handle_base_info;
    crt->format.get_format_size = xx_crt_get_format_size;
    crt->format.get_number_of_archive_records =
        xx_crt_get_number_of_archive_records;
    crt->format.create_archive_records_reading =
        xx_crt_create_archive_records_reading;
    crt->format.get_current_archive_record = xx_crt_get_current_archive_record;
    crt->format.unpack_current_archive_record =
        xx_crt_unpack_current_archive_record;
    crt->format.archive_record_move_to_next =
        xx_crt_archive_record_move_to_next;
    crt->format.free_archive_records_reading =
        xx_crt_free_archive_records_reading;
    crt->format.destroy = xx_crt_vtable_destroy;
    crt->machine = -1;
}

xx_crt *xx_crt_create(xx_io_device *dev, int64_t base_address) {
    xx_crt *crt = (xx_crt *)xx_mem_alloc(sizeof(*crt));
    if (crt) xx_crt_init(crt, dev, base_address);
    return crt;
}

void xx_crt_destroy(xx_crt *crt) {
    if (!crt) return;
    xx_format_cleanup_extra_parameters(&crt->format);
}

static void xx_crt_vtable_destroy(Abstractformat *self) {
    xx_crt_destroy((xx_crt *)self);
}

void xx_crt_free(xx_crt *crt) {
    if (!crt) return;
    xx_crt_destroy(crt);
    xx_mem_free(crt);
}

bool xx_crt_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_crt_private parsed;
    bool result = xx_crt_parse(self, &parsed, false, pd);
    xx_crt_private_cleanup(&parsed);
    return result;
}

bool xx_crt_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_crt *crt = (xx_crt *)self;
    xx_crt_private parsed;
    int64_t total;
    size_t index;
    uint64_t records = 0U;
    if (!self) return false;
    if (!xx_crt_parse(self, &parsed, true, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    records = parsed.count;
    crt->machine = parsed.machine;
    crt->version_major = parsed.header[0x14];
    crt->version_minor = parsed.header[0x15];
    crt->hardware_type = xx_crt_be16(parsed.header + 0x16);
    crt->exrom = parsed.header[0x18];
    crt->game = parsed.header[0x19];
    crt->subtype = parsed.header[0x1A];
    crt->header_length = parsed.header_length;
    for (index = 0U; index < 32U; ++index) {
        uint8_t c = parsed.header[0x20 + index];
        if (c == 0U) break;
        crt->name[index] = (c >= 0x20U && c < 0x7FU) ? (char)c : '?';
    }
    crt->name[index] = '\0';
    crt->number_of_chips = parsed.packets;
    crt->number_of_records = records;
    self->format_size = parsed.end - self->base_address;
    total = xx_io_total_size(self->device);
    if (total > parsed.end) {
        self->overlay_offset = parsed.end;
        self->overlay_size = total - parsed.end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = records;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_crt_private_cleanup(&parsed);
    return true;
}

int64_t xx_crt_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_crt_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_crt *)self)->number_of_records;
}

xx_archive_record_state *xx_crt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_crt_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_crt_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_crt_copy_options(&state->options, options) ||
        !xx_crt_parse(self, &stream->parsed, true, pd)) {
        xx_crt_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_crt_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_crt_populate_record(&state->current_record,
                               &stream->parsed.chips[0], 0U)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_crt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_crt_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_crt_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_crt_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_crt_populate_record(&state->current_record,
                                &stream->parsed.chips[stream->index],
                                stream->index)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_crt_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_crt_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    /* The helper deletes its own output on failure. */
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_crt_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
