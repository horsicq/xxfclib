/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vxworks/xx_vxworks.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_VXWORKS exists in the enum. */
#ifdef VXWORKS
#define XX_VXWORKS_FILE_TYPE XX_FILE_TYPE_VXWORKS
#else
#define XX_VXWORKS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_VXWORKS_ENTRY_SIZE 16
/* A run shorter than this means nothing: three constrained words recur in
 * ordinary data. The published extractor uses the same floor. */
#define XX_VXWORKS_MIN_ENTRIES 250U
/* 4 million symbols is far beyond any real image and caps the table array at
 * 64 MiB, which bounds a hostile input that is nothing but valid entries. */
#define XX_VXWORKS_MAX_ENTRIES 4000000U
/* Entries are read in blocks rather than one seek per entry. */
#define XX_VXWORKS_BLOCK_ENTRIES 256U

typedef struct xx_vxworks_private_s {
    xx_vxworks_symbol *symbols;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t table_offset;
    int64_t table_size;
    bool is_big_endian;
} xx_vxworks_private;

static void xx_vxworks_vtable_destroy(Abstractformat *self);

/* seek64 rather than seek: a symbol table sits deep inside a firmware dump
 * and `long` is 32-bit on Win64, which would clamp the offset at 2 GiB. */
static bool xx_vxworks_read_at(xx_io_device *device, int64_t offset,
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

static bool xx_vxworks_range_within(int64_t total_size, int64_t offset,
                                    int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_vxworks_type_is_known(uint32_t type) {
    return type == XX_VXWORKS_SYMBOL_FUNCTION ||
           type == XX_VXWORKS_SYMBOL_INITIALIZED_DATA ||
           type == XX_VXWORKS_SYMBOL_UNINITIALIZED_DATA;
}

/* Decode one 16-byte entry and report whether it is a plausible symbol. */
static bool xx_vxworks_decode_entry(const uint8_t *raw, bool big_endian,
                                    xx_vxworks_symbol *output) {
    uint32_t name_pointer = xx_data_get_u32(raw, XX_VXWORKS_ENTRY_SIZE, 0U,
                                            big_endian);
    uint32_t value = xx_data_get_u32(raw, XX_VXWORKS_ENTRY_SIZE, 4U,
                                     big_endian);
    uint32_t type = xx_data_get_u32(raw, XX_VXWORKS_ENTRY_SIZE, 8U,
                                    big_endian);
    uint32_t group = xx_data_get_u32(raw, XX_VXWORKS_ENTRY_SIZE, 12U,
                                     big_endian);
    if (name_pointer == 0U || value == 0U ||
        !xx_vxworks_type_is_known(type)) {
        return false;
    }
    output->name_pointer = name_pointer;
    output->value = value;
    output->type = type;
    output->group = group;
    output->entry_offset = -1;
    return true;
}

static void xx_vxworks_private_cleanup(xx_vxworks_private *parsed) {
    if (!parsed) return;
    if (parsed->symbols) xx_mem_free(parsed->symbols);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->table_offset = -1;
    parsed->table_size = -1;
}

static bool xx_vxworks_append_symbol(xx_vxworks_private *parsed,
                                     const xx_vxworks_symbol *symbol) {
    xx_vxworks_symbol *grown;
    size_t capacity;
    if (!parsed || !symbol || parsed->count >= XX_VXWORKS_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 512U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->symbols)) return false;
        grown = (xx_vxworks_symbol *)xx_mem_realloc(
            parsed->symbols, capacity * sizeof(*parsed->symbols));
        if (!grown) return false;
        parsed->symbols = grown;
        parsed->capacity = capacity;
    }
    parsed->symbols[parsed->count++] = *symbol;
    return true;
}

/* Walk the run of entries at base_address for one byte order, stopping at the
 * first entry that does not decode. */
static bool xx_vxworks_scan(Abstractformat *self, xx_vxworks_private *parsed,
                            bool big_endian, xx_pd_struct *pd) {
    uint8_t block[XX_VXWORKS_ENTRY_SIZE * XX_VXWORKS_BLOCK_ENTRIES];
    int64_t offset = self->base_address;
    bool ended = false;
    parsed->is_big_endian = big_endian;
    while (!ended) {
        int64_t remaining;
        size_t want;
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (parsed->count >= XX_VXWORKS_MAX_ENTRIES) break;
        remaining = parsed->input_size - offset;
        if (remaining < XX_VXWORKS_ENTRY_SIZE) break;
        want = (size_t)(remaining / XX_VXWORKS_ENTRY_SIZE);
        if (want > XX_VXWORKS_BLOCK_ENTRIES) want = XX_VXWORKS_BLOCK_ENTRIES;
        if (!xx_vxworks_read_at(self->device, offset, block,
                                want * XX_VXWORKS_ENTRY_SIZE)) {
            break;
        }
        for (index = 0U; index < want; ++index) {
            xx_vxworks_symbol symbol;
            if (!xx_vxworks_decode_entry(block + index * XX_VXWORKS_ENTRY_SIZE,
                                         big_endian, &symbol)) {
                ended = true;
                break;
            }
            symbol.entry_offset = offset + (int64_t)index *
                                               XX_VXWORKS_ENTRY_SIZE;
            if (!xx_vxworks_append_symbol(parsed, &symbol)) return false;
        }
        offset += (int64_t)want * XX_VXWORKS_ENTRY_SIZE;
    }
    if (parsed->count < XX_VXWORKS_MIN_ENTRIES) return false;
    parsed->table_offset = self->base_address;
    parsed->table_size = (int64_t)parsed->count * XX_VXWORKS_ENTRY_SIZE;
    return true;
}

static bool xx_vxworks_parse(Abstractformat *self, xx_vxworks_private *parsed,
                             xx_pd_struct *pd) {
    uint8_t probe[XX_VXWORKS_ENTRY_SIZE];
    int64_t total_size;
    bool big_endian;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->table_offset = -1;
        parsed->table_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_vxworks_range_within(total_size, self->base_address,
                                 XX_VXWORKS_ENTRY_SIZE) ||
        !xx_vxworks_read_at(self->device, self->base_address, probe,
                            sizeof(probe))) {
        return false;
    }
    /* The type word is 0x00000500 class, so its second byte is zero in a big
     * endian image and non-zero in a little endian one. That single byte
     * decides the order for the whole table. */
    big_endian = (probe[9] == 0U);
    parsed->input_size = total_size;
    if (!xx_vxworks_scan(self, parsed, big_endian, pd)) {
        xx_vxworks_private_cleanup(parsed);
        return false;
    }
    return true;
}

void xx_vxworks_init(xx_vxworks *vxworks, xx_io_device *dev,
                     int64_t base_address) {
    if (!vxworks) return;
    xx_mem_zero(vxworks, sizeof(*vxworks));
    xx_format_init(&vxworks->format, dev, base_address);
    vxworks->format.endian = XX_ENDIAN_UNKNOWN;
    vxworks->format.file_type = XX_VXWORKS_FILE_TYPE;
    vxworks->format.format_type = XX_TYPE_FIRMWARE;
    /* Not an archive: there is nothing here to unpack. */
    vxworks->format.is_archive = false;
    xx_format_set_mime_type(&vxworks->format,
                            "application/x-vxworks-symbol-table");
    xx_format_set_extension(&vxworks->format, "bin");
    vxworks->format.check_is_valid = xx_vxworks_check_is_valid;
    vxworks->format.handle_base_info = xx_vxworks_handle_base_info;
    vxworks->format.get_format_size = xx_vxworks_get_format_size;
    vxworks->format.get_number_of_metadata = xx_vxworks_get_number_of_metadata;
    vxworks->format.destroy = xx_vxworks_vtable_destroy;
    vxworks->table_offset = -1;
    vxworks->table_size = -1;
}

xx_vxworks *xx_vxworks_create(xx_io_device *dev, int64_t base_address) {
    xx_vxworks *vxworks = (xx_vxworks *)xx_mem_alloc(sizeof(*vxworks));
    if (vxworks) xx_vxworks_init(vxworks, dev, base_address);
    return vxworks;
}

void xx_vxworks_destroy(xx_vxworks *vxworks) {
    if (!vxworks) return;
    if (vxworks->internal) {
        xx_vxworks_private_cleanup((xx_vxworks_private *)vxworks->internal);
        xx_mem_free(vxworks->internal);
        vxworks->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&vxworks->format);
}

static void xx_vxworks_vtable_destroy(Abstractformat *self) {
    xx_vxworks_destroy((xx_vxworks *)self);
}

void xx_vxworks_free(xx_vxworks *vxworks) {
    if (!vxworks) return;
    xx_vxworks_destroy(vxworks);
    xx_mem_free(vxworks);
}

bool xx_vxworks_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_vxworks_private parsed;
    bool result = xx_vxworks_parse(self, &parsed, pd);
    xx_vxworks_private_cleanup(&parsed);
    return result;
}

bool xx_vxworks_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_vxworks_private *parsed;
    xx_vxworks *vxworks = (xx_vxworks *)self;
    int64_t total_size;
    int64_t table_end;
    if (!self || !vxworks) return false;
    parsed = (xx_vxworks_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_vxworks_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (vxworks->internal) {
        xx_vxworks_private_cleanup((xx_vxworks_private *)vxworks->internal);
        xx_mem_free(vxworks->internal);
    }
    vxworks->internal = parsed;
    vxworks->number_of_symbols = parsed->count;
    vxworks->table_offset = parsed->table_offset;
    vxworks->table_size = parsed->table_size;
    vxworks->is_big_endian = parsed->is_big_endian;
    self->endian = parsed->is_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->table_size;
    table_end = parsed->table_offset + parsed->table_size;
    total_size = xx_io_total_size(self->device);
    /* Whatever follows the table is the rest of the firmware image, not an
     * overlay of this structure, so no overlay is reported. */
    (void)table_end;
    (void)total_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_metadata = parsed->count;
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_vxworks_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_vxworks_get_number_of_metadata(Abstractformat *self,
                                           xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_vxworks *)self)->number_of_symbols;
}

uint64_t xx_vxworks_get_number_of_symbols(const xx_vxworks *vxworks) {
    return vxworks ? vxworks->number_of_symbols : 0U;
}
int64_t xx_vxworks_get_table_offset(const xx_vxworks *vxworks) {
    return vxworks ? vxworks->table_offset : -1;
}
int64_t xx_vxworks_get_table_size(const xx_vxworks *vxworks) {
    return vxworks ? vxworks->table_size : -1;
}
bool xx_vxworks_get_is_big_endian(const xx_vxworks *vxworks) {
    return vxworks ? vxworks->is_big_endian : false;
}

bool xx_vxworks_get_symbol(const xx_vxworks *vxworks, uint64_t index,
                           xx_vxworks_symbol *output) {
    const xx_vxworks_private *parsed;
    if (!vxworks || !output || !vxworks->internal) return false;
    parsed = (const xx_vxworks_private *)vxworks->internal;
    if (index >= (uint64_t)parsed->count) return false;
    *output = parsed->symbols[(size_t)index];
    return true;
}

const char *xx_vxworks_symbol_type_name(uint32_t type) {
    switch (type) {
        case XX_VXWORKS_SYMBOL_FUNCTION: return "function";
        case XX_VXWORKS_SYMBOL_INITIALIZED_DATA: return "initialized data";
        case XX_VXWORKS_SYMBOL_UNINITIALIZED_DATA: return "uninitialized data";
        default: return NULL;
    }
}
