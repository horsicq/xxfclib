/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pchrom/xx_pchrom.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_PCHROM exists in the enum. */
#ifdef PCHROM
#define XX_PCHROM_FILE_TYPE XX_FILE_TYPE_PCHROM
#else
#define XX_PCHROM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Everything the parser looks at lives in the first 0x80 bytes (map,
 * component word, sixteen region slots); 0x100 covers it with room. */
#define XX_PCHROM_HEADER_READ 0x100U
#define XX_PCHROM_FLMAP0_OFFSET 0x14U
#define XX_PCHROM_FLMAP1_OFFSET 0x18U
/* binwalk's structures/pchrom.rs accepts exactly these FLMAP0 bytes. */
#define XX_PCHROM_EXPECTED_FCBA 0x03U /* component section at 0x30 */
#define XX_PCHROM_EXPECTED_FRBA 0x04U /* region section at 0x40 */
#define XX_PCHROM_COMPONENT_OFFSET 0x30U
#define XX_PCHROM_REGION_OFFSET 0x40U
/* UEFITool: no descriptor section base above 0xE0 (address 0xE00). */
#define XX_PCHROM_MAX_SECTION_BASE 0xE0U
/* The region section must hold the five slots every descriptor version has
 * (descriptor, BIOS, ME, GbE, PDR) before the master section begins. */
#define XX_PCHROM_MIN_REGION_SLOTS 5U
#define XX_PCHROM_V1_REGION_SLOTS 7U
#define XX_PCHROM_UNIT 0x1000U
#define XX_PCHROM_FIELD_MASK 0x7FFFU

typedef struct xx_pchrom_region_s {
    uint32_t index;   /**< Region slot, 0..15. */
    uint32_t raw;     /**< The FLREG word as stored. */
    int64_t offset;   /**< Relative to base_address. */
    int64_t size;
} xx_pchrom_region;

typedef struct xx_pchrom_private_s {
    int64_t input_size;
    int64_t image_size;
    uint32_t flmap0;
    uint32_t flmap1;
    uint32_t flcomp;
    uint32_t version;
    uint32_t components;
    uint32_t mask;
    size_t count;
    xx_pchrom_region regions[XX_PCHROM_MAX_REGIONS];
} xx_pchrom_private;

typedef struct xx_pchrom_archive_stream_s {
    xx_pchrom_private parsed;
    size_t index;
} xx_pchrom_archive_stream;

/* Names follow uefi-firmware-parser's "region-<name>.fd" for the four it
 * knows (bios, me, gbe, pdr) and UEFITool's region subtype names for the
 * rest.  They are literals, never taken from the file. */
static const char *const xx_pchrom_names[XX_PCHROM_MAX_REGIONS] = {
    "region-descriptor.fd", "region-bios.fd",      "region-me.fd",
    "region-gbe.fd",        "region-pdr.fd",       "region-devexp1.fd",
    "region-bios2.fd",      "region-microcode.fd", "region-ec.fd",
    "region-devexp2.fd",    "region-ie.fd",        "region-10gbe1.fd",
    "region-10gbe2.fd",     "region-reserved1.fd", "region-reserved2.fd",
    "region-ptt.fd"};

static void xx_pchrom_vtable_destroy(Abstractformat *self);

static bool xx_pchrom_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_pchrom_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_pchrom_private_reset(xx_pchrom_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->image_size = -1;
}

/*
 * binwalk's structures/pchrom.rs gates on the signature and the four FLMAP0
 * bytes, then means to take the image size from the furthest region limit.
 * Its get_pch_regions_size() is handed FCBA (3) where it expects FLMAP0, so
 * ((3 >> 16) & 0xFF) << 4 == 0 and it reads the five "region" words from
 * offsets 0..19 - the reserved vector and the signature - instead of from
 * FRBA (0x40).  It also rounds every limit up to 64 KiB and adds the 16-byte
 * signature offset on top.  With the usual 0xFF reserved vector that yields a
 * fixed 0x2000010 for every image, which its own scan loop then discards as
 * running past EOF for any image of 32 MiB or less.  Reproducing that would
 * make the reader reject every real flash dump, so the size here is what
 * binwalk intends and what UEFITool/ifdtool compute: the end of the furthest
 * used region, read from the region section the descriptor points to.
 *
 * Every check binwalk performs is kept (signature, FCBA == 3, NC <= 1, FRBA
 * == 4 with NR == 0, a non-empty region set), and on top of it: the
 * descriptor slot must start at 0, a BIOS region must exist (UEFITool refuses
 * a descriptor without one), the master section must sit after the region
 * slots, and every used region must lie inside the device.  A region that
 * runs past the end of the device fails the whole image, as binwalk and
 * UEFITool both do: that is a truncated dump or one chip of a two-chip set,
 * and the reported size could not be honoured.
 */
static bool xx_pchrom_parse(Abstractformat *self, xx_pchrom_private *parsed,
                            xx_pd_struct *pd) {
    uint8_t header[XX_PCHROM_HEADER_READ];
    static const uint8_t signature[4] = {0x5AU, 0xA5U, 0xF0U, 0x0FU};
    uint32_t fmba;
    uint32_t slots;
    uint32_t index;
    int64_t available;
    if (parsed) xx_pchrom_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    /* The descriptor region is at least 4 KiB, so anything shorter is not a
     * flash image even before the region table is consulted. */
    if (!xx_pchrom_range_within(parsed->input_size, self->base_address,
                                XX_PCHROM_DESCRIPTOR_SIZE) ||
        !xx_pchrom_read_at(self->device, self->base_address, header,
                           sizeof(header)) ||
        xx_rt_memcmp(header + XX_PCHROM_SIGNATURE_OFFSET, signature, 4U) != 0) {
        goto fail;
    }
    available = parsed->input_size - self->base_address;
    parsed->flmap0 =
        xx_data_get_u32(header, sizeof(header), XX_PCHROM_FLMAP0_OFFSET, false);
    parsed->flmap1 =
        xx_data_get_u32(header, sizeof(header), XX_PCHROM_FLMAP1_OFFSET, false);
    parsed->flcomp = xx_data_get_u32(header, sizeof(header),
                                     XX_PCHROM_COMPONENT_OFFSET, false);
    /* binwalk: flmap0_fcba == 3, flmap0_nc in {0, 1} (whole byte), and the
     * u16 flmap0_frba_nr == 4, i.e. FRBA == 4 and the NR byte == 0. */
    if (header[0x14] != XX_PCHROM_EXPECTED_FCBA || header[0x15] > 1U ||
        header[0x16] != XX_PCHROM_EXPECTED_FRBA || header[0x17] != 0U) {
        goto fail;
    }
    parsed->components = (uint32_t)header[0x15] + 1U;
    fmba = parsed->flmap1 & 0xFFU;
    if (fmba > XX_PCHROM_MAX_SECTION_BASE ||
        (fmba << 4) < XX_PCHROM_REGION_OFFSET + 4U * XX_PCHROM_MIN_REGION_SLOTS) {
        goto fail;
    }
    /* UEFITool's version test: a version 1 descriptor hardcodes the 20 MHz
     * read clock (000b) in FLCOMP bits 19:17 and defines slots 0..6 only. */
    parsed->version = ((parsed->flcomp >> 17) & 7U) == 0U ? 1U : 2U;
    slots = parsed->version == 1U ? XX_PCHROM_V1_REGION_SLOTS
                                  : XX_PCHROM_MAX_REGIONS;
    if (slots > ((fmba << 4) - XX_PCHROM_REGION_OFFSET) / 4U) {
        slots = ((fmba << 4) - XX_PCHROM_REGION_OFFSET) / 4U;
    }
    parsed->image_size = 0;
    for (index = 0U; index < slots; ++index) {
        uint32_t raw = xx_data_get_u32(header, sizeof(header),
                                       XX_PCHROM_REGION_OFFSET + 4U * index,
                                       false);
        uint32_t base = raw & XX_PCHROM_FIELD_MASK;
        uint32_t limit = (raw >> 16) & XX_PCHROM_FIELD_MASK;
        int64_t offset;
        int64_t size;
        xx_pchrom_region *region;
        if (index == 0U) {
            /* The descriptor describes itself, from flash address 0. */
            if (raw == 0xFFFFFFFFU || base != 0U) goto fail;
        } else if (raw == 0xFFFFFFFFU || limit == 0U || base > limit) {
            continue; /* unused / unprogrammed slot */
        }
        /* base, limit <= 0x7FFF, so both products stay below 2^27. */
        offset = (int64_t)base * XX_PCHROM_UNIT;
        size = ((int64_t)limit + 1 - (int64_t)base) * XX_PCHROM_UNIT;
        if (!xx_pchrom_range_within(available, offset, size)) goto fail;
        region = &parsed->regions[parsed->count++];
        region->index = index;
        region->raw = raw;
        region->offset = offset;
        region->size = size;
        parsed->mask |= 1U << index;
        if (offset + size > parsed->image_size) {
            parsed->image_size = offset + size;
        }
    }
    if ((parsed->mask & 0x2U) == 0U || parsed->image_size <= 0) goto fail;
    return true;
fail:
    xx_pchrom_private_reset(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_pchrom_copy_options(xx_list_s *destination,
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

static const xx_var *xx_pchrom_find_option(const xx_list_s *options,
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

static bool xx_pchrom_populate_record(xx_archive_record *record,
                                      const xx_pchrom_private *parsed,
                                      size_t index, int64_t base_address) {
    const xx_pchrom_region *region;
    if (!record || !parsed || index >= parsed->count ||
        index >= XX_PCHROM_MAX_REGIONS) {
        return false;
    }
    region = &parsed->regions[index];
    if (region->index >= XX_PCHROM_MAX_REGIONS) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address + region->offset;
    record->header_size = 0;
    record->data_offset = base_address + region->offset;
    record->compressed_size = region->size;
    return xx_archive_record_set_original_name(
               record, xx_pchrom_names[region->index]) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)region->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)region->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_pchrom_archive_stream_free(void *pointer) {
    xx_pchrom_archive_stream *stream = (xx_pchrom_archive_stream *)pointer;
    if (!stream) return;
    xx_pchrom_private_reset(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

const char *xx_pchrom_region_name(uint32_t index) {
    return index < XX_PCHROM_MAX_REGIONS ? xx_pchrom_names[index] : NULL;
}

void xx_pchrom_init(xx_pchrom *pchrom, xx_io_device *dev,
                    int64_t base_address) {
    if (!pchrom) return;
    xx_mem_zero(pchrom, sizeof(*pchrom));
    xx_format_init(&pchrom->format, dev, base_address);
    pchrom->format.endian = XX_ENDIAN_LITTLE;
    pchrom->format.file_type = XX_PCHROM_FILE_TYPE;
    pchrom->format.format_type = XX_TYPE_ARCHIVE;
    pchrom->format.is_archive = true;
    xx_format_set_mime_type(&pchrom->format, "application/x-intel-pch-rom");
    xx_format_set_extension(&pchrom->format, "rom");
    pchrom->format.check_is_valid = xx_pchrom_check_is_valid;
    pchrom->format.handle_base_info = xx_pchrom_handle_base_info;
    pchrom->format.get_format_size = xx_pchrom_get_format_size;
    pchrom->format.get_number_of_archive_records =
        xx_pchrom_get_number_of_archive_records;
    pchrom->format.create_archive_records_reading =
        xx_pchrom_create_archive_records_reading;
    pchrom->format.get_current_archive_record =
        xx_pchrom_get_current_archive_record;
    pchrom->format.unpack_current_archive_record =
        xx_pchrom_unpack_current_archive_record;
    pchrom->format.archive_record_move_to_next =
        xx_pchrom_archive_record_move_to_next;
    pchrom->format.free_archive_records_reading =
        xx_pchrom_free_archive_records_reading;
    pchrom->format.destroy = xx_pchrom_vtable_destroy;
    pchrom->image_end = -1;
}

xx_pchrom *xx_pchrom_create(xx_io_device *dev, int64_t base_address) {
    xx_pchrom *pchrom = (xx_pchrom *)xx_mem_alloc(sizeof(*pchrom));
    if (pchrom) xx_pchrom_init(pchrom, dev, base_address);
    return pchrom;
}

void xx_pchrom_destroy(xx_pchrom *pchrom) {
    if (!pchrom) return;
    if (pchrom->internal) {
        xx_pchrom_private_reset((xx_pchrom_private *)pchrom->internal);
        xx_mem_free(pchrom->internal);
        pchrom->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&pchrom->format);
}

static void xx_pchrom_vtable_destroy(Abstractformat *self) {
    xx_pchrom_destroy((xx_pchrom *)self);
}

void xx_pchrom_free(xx_pchrom *pchrom) {
    if (!pchrom) return;
    xx_pchrom_destroy(pchrom);
    xx_mem_free(pchrom);
}

bool xx_pchrom_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pchrom_private parsed;
    bool result = xx_pchrom_parse(self, &parsed, pd);
    xx_pchrom_private_reset(&parsed);
    return result;
}

bool xx_pchrom_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pchrom_private *parsed;
    xx_pchrom *pchrom = (xx_pchrom *)self;
    int64_t total_size;
    int64_t end;
    if (!self || !pchrom) return false;
    parsed = (xx_pchrom_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_pchrom_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (pchrom->internal) {
        xx_pchrom_private_reset((xx_pchrom_private *)pchrom->internal);
        xx_mem_free(pchrom->internal);
    }
    pchrom->internal = parsed;
    pchrom->number_of_records = parsed->count;
    pchrom->number_of_members = parsed->count;
    pchrom->flmap0 = parsed->flmap0;
    pchrom->flmap1 = parsed->flmap1;
    pchrom->flcomp = parsed->flcomp;
    pchrom->descriptor_version = parsed->version;
    pchrom->number_of_components = parsed->components;
    pchrom->region_mask = parsed->mask;
    /* parse() proved image_size <= input_size - base_address. */
    end = self->base_address + parsed->image_size;
    pchrom->image_end = end;
    self->format_size = parsed->image_size;
    total_size = xx_io_total_size(self->device);
    if (total_size > end) {
        self->overlay_offset = end;
        self->overlay_size = total_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_pchrom_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_pchrom_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_pchrom *)self)->number_of_records;
}

xx_archive_record_state *xx_pchrom_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_pchrom_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_pchrom_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_pchrom_copy_options(&state->options, options) ||
        !xx_pchrom_parse(self, &stream->parsed, pd)) {
        xx_pchrom_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_pchrom_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_pchrom_populate_record(&state->current_record, &stream->parsed, 0U,
                                  self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_pchrom_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pchrom_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_pchrom_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pchrom_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index < stream->parsed.count &&
        xx_pchrom_populate_record(&state->current_record, &stream->parsed,
                                  stream->index, self->base_address)) {
        state->current_index = (int64_t)stream->index;
        return true;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_pchrom_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    int64_t total;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    total = xx_io_total_size(self->device);
    if (!xx_pchrom_range_within(total, record->data_offset,
                                record->compressed_size)) {
        return false;
    }
    option = xx_pchrom_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true;
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
    if (!result) xx_rt_remove(destination);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_pchrom_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_pchrom_get_number_of_records(const xx_pchrom *pchrom) {
    return pchrom ? pchrom->number_of_records : 0U;
}
uint64_t xx_pchrom_get_number_of_members(const xx_pchrom *pchrom) {
    return pchrom ? pchrom->number_of_members : 0U;
}
uint32_t xx_pchrom_get_descriptor_version(const xx_pchrom *pchrom) {
    return pchrom ? pchrom->descriptor_version : 0U;
}
uint32_t xx_pchrom_get_region_mask(const xx_pchrom *pchrom) {
    return pchrom ? pchrom->region_mask : 0U;
}
int64_t xx_pchrom_get_image_end(const xx_pchrom *pchrom) {
    return pchrom ? pchrom->image_end : -1;
}
