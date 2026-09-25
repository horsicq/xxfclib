/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apm/xx_apm.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. The APM macro is the alias xxfc_defs.h defines next to every
 * XX_FILE_TYPE_* value, so this block heals itself the moment the enum grows
 * an APM member; delete it then. */
#ifdef APM
#define XX_APM_FILE_TYPE XX_FILE_TYPE_APM
#else
#define XX_APM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_APM_SECTOR 512U
#define XX_APM_DDM_SIG 0x4552U  /* 'ER' */
#define XX_APM_ENTRY_SIG 0x504DU /* 'PM' */
#define XX_APM_TEXT_FIELD 32U
/* "<type> (<name>)" with both fields at their widest. */
#define XX_APM_COMMENT_SIZE (XX_APM_TEXT_FIELD * 2U + 4U)

typedef struct xx_apm_entry_s {
    char *name;     /**< Generated record name, e.g. "partition3". */
    char part_name[XX_APM_TEXT_FIELD + 1U];
    char part_type[XX_APM_TEXT_FIELD + 1U];
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    uint64_t declared_size;
    uint32_t start_block;
    uint32_t block_count;
    uint32_t status;
    uint32_t entry_index;
} xx_apm_entry;

typedef struct xx_apm_private_s {
    xx_apm_entry *entries;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    uint32_t block_size;
    uint32_t device_blocks;
    uint32_t map_step;
    uint32_t map_entries;
    uint32_t entries_read;
} xx_apm_private;

typedef struct xx_apm_archive_stream_s {
    xx_apm_private parsed;
    size_t index;
} xx_apm_archive_stream;

/* The first fields of one map entry, as the layout probe needs them. */
typedef struct xx_apm_raw_entry_s {
    uint32_t map_entries;
    uint32_t start_block;
    uint32_t block_count;
} xx_apm_raw_entry;

static void xx_apm_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Read exactly size bytes at an absolute device offset; xx_io_seek64() is
 * used because a disk image is routinely larger than 2 GiB. */
static bool xx_apm_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* base + blocks * unit, refusing anything that does not fit in int64_t. */
static bool xx_apm_block_to_offset(int64_t base_address, uint64_t blocks,
                                   uint32_t unit, int64_t *result) {
    uint64_t bytes;
    if (!result || base_address < 0 || unit == 0U) return false;
    if (blocks > (uint64_t)INT64_MAX / unit) return false;
    bytes = blocks * unit;
    if (bytes > (uint64_t)(INT64_MAX - base_address)) return false;
    *result = base_address + (int64_t)bytes;
    return true;
}

/* Build a record name of the form "partition12"; the same helper the MBR
 * and GPT readers use, hand rolled to stay CRT free. */
static char *xx_apm_make_name(unsigned index) {
    static const char prefix[] = "partition";
    char digits[16];
    char buffer[sizeof(prefix) + sizeof(digits)];
    size_t used = sizeof(prefix) - 1U;
    size_t count = 0U;
    xx_rt_memcpy(buffer, prefix, used);
    do {
        digits[count++] = (char)('0' + (index % 10U));
        index /= 10U;
    } while (index != 0U && count < sizeof(digits));
    while (count != 0U) buffer[used++] = digits[--count];
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/* Copy a NUL-padded 32-byte text field. The map stores Mac OS Roman; only
 * printable ASCII is kept verbatim, anything else becomes '?', so the text is
 * always safe to show. It never becomes a path. */
static void xx_apm_copy_text(const uint8_t *raw,
                             char out[XX_APM_TEXT_FIELD + 1U]) {
    size_t index;
    size_t end = 0U;
    for (index = 0U; index < XX_APM_TEXT_FIELD; ++index) {
        uint8_t ch = raw[index];
        if (ch == 0U) break;
        out[index] = (ch >= 0x20U && ch < 0x7FU) ? (char)ch : '?';
        if (ch != 0x20U) end = index + 1U;
    }
    out[end] = '\0'; /* Trailing blanks carry nothing. */
}

static bool xx_apm_is_block_size(uint32_t value) {
    return value == 512U || value == 1024U || value == 2048U || value == 4096U;
}

/* Read the map entry at offset and check the parts every entry must have:
 * the 'PM' signature, the zero pad word, and a map size that is at least 1
 * and within the cap. */
static bool xx_apm_read_entry(xx_io_device *device, int64_t offset,
                              int64_t total_size,
                              uint8_t buffer[XX_APM_SECTOR],
                              xx_apm_raw_entry *raw) {
    if (offset < 0 || offset > total_size ||
        total_size - offset < (int64_t)XX_APM_SECTOR) {
        return false;
    }
    if (!xx_apm_read_at(device, offset, buffer, XX_APM_SECTOR)) return false;
    if (xx_data_get_u16(buffer, XX_APM_SECTOR, 0U, true) != XX_APM_ENTRY_SIG ||
        xx_data_get_u16(buffer, XX_APM_SECTOR, 2U, true) != 0U) {
        return false;
    }
    raw->map_entries = xx_data_get_u32(buffer, XX_APM_SECTOR, 4U, true);
    raw->start_block = xx_data_get_u32(buffer, XX_APM_SECTOR, 8U, true);
    raw->block_count = xx_data_get_u32(buffer, XX_APM_SECTOR, 12U, true);
    return raw->map_entries != 0U &&
           raw->map_entries <= XX_APM_MAX_MAP_ENTRIES;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

static void xx_apm_private_cleanup(xx_apm_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_apm_append_entry(xx_apm_private *parsed, xx_apm_entry *entry) {
    xx_apm_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_APM_MAX_MAP_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 16U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_apm_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_rt_memset(entry, 0, sizeof(*entry));
    return true;
}

/* Pick the map step: 512 when the block at byte 512 is an entry for the map
 * itself (or the device block is 512 anyway), the declared block size when
 * a map entry sits there, and 512 again as the last resort for a map at 512
 * whose first entry describes something else. */
static bool xx_apm_choose_step(Abstractformat *self, int64_t total_size,
                               uint32_t block_size, uint32_t *step,
                               xx_apm_raw_entry *first) {
    uint8_t buffer[XX_APM_SECTOR];
    xx_apm_raw_entry narrow;
    xx_apm_raw_entry wide;
    bool narrow_ok;
    int64_t offset;
    narrow_ok = xx_apm_read_entry(self->device,
                                  self->base_address + XX_APM_SECTOR,
                                  total_size, buffer, &narrow);
    if (narrow_ok && (block_size == XX_APM_SECTOR || narrow.start_block == 1U)) {
        *step = XX_APM_SECTOR;
        *first = narrow;
        return true;
    }
    if (block_size != XX_APM_SECTOR &&
        xx_apm_block_to_offset(self->base_address, 1U, block_size, &offset) &&
        xx_apm_read_entry(self->device, offset, total_size, buffer, &wide)) {
        *step = block_size;
        *first = wide;
        return true;
    }
    if (narrow_ok) {
        *step = XX_APM_SECTOR;
        *first = narrow;
        return true;
    }
    return false;
}

/* Decode one map entry already known to carry the signature and publish it
 * when it has a payload on this device. */
static bool xx_apm_collect_entry(Abstractformat *self, xx_apm_private *parsed,
                                 const uint8_t *buffer, int64_t entry_offset,
                                 uint32_t entry_index) {
    xx_apm_entry entry;
    int64_t offset;
    int64_t available;
    uint64_t declared;
    xx_rt_memset(&entry, 0, sizeof(entry));
    entry.start_block = xx_data_get_u32(buffer, XX_APM_SECTOR, 8U, true);
    entry.block_count = xx_data_get_u32(buffer, XX_APM_SECTOR, 12U, true);
    entry.status = xx_data_get_u32(buffer, XX_APM_SECTOR, 88U, true);
    if (entry.block_count == 0U) return true; /* Nothing to carry. */
    /* A range that wraps the 32-bit block space is nonsense; the entry is
     * dropped rather than failing the whole map. */
    if ((uint64_t)entry.start_block + entry.block_count > 0x100000000ULL) {
        return true;
    }
    if (!xx_apm_block_to_offset(self->base_address, entry.start_block,
                                parsed->map_step, &offset)) {
        return true;
    }
    if (offset >= parsed->input_size) return true; /* Past the device. */
    declared = (uint64_t)entry.block_count * parsed->map_step;
    available = parsed->input_size - offset;
    if (declared < (uint64_t)available) available = (int64_t)declared;
    xx_apm_copy_text(buffer + 16, entry.part_name);
    xx_apm_copy_text(buffer + 48, entry.part_type);
    entry.entry_index = entry_index;
    entry.header_offset = entry_offset;
    entry.data_offset = offset;
    entry.data_size = available;
    entry.declared_size = declared;
    entry.name = xx_apm_make_name(entry_index);
    if (!entry.name || !xx_apm_append_entry(parsed, &entry)) {
        if (entry.name) xx_str_free(entry.name);
        return false;
    }
    if (offset + available > parsed->archive_end) {
        parsed->archive_end = offset + available;
    }
    return true;
}

static bool xx_apm_parse(Abstractformat *self, xx_apm_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t buffer[XX_APM_SECTOR];
    xx_apm_raw_entry first;
    int64_t total_size;
    int64_t device_end;
    uint32_t index;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_rt_memset(parsed, 0, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    /* Block 0 and at least one map entry. */
    if (total_size <= self->base_address ||
        total_size - self->base_address < (int64_t)(XX_APM_SECTOR * 2U)) {
        return false;
    }
    if (!xx_apm_read_at(self->device, self->base_address, buffer,
                        XX_APM_SECTOR)) {
        return false;
    }
    if (xx_data_get_u16(buffer, XX_APM_SECTOR, 0U, true) != XX_APM_DDM_SIG) {
        return false;
    }
    parsed->block_size = xx_data_get_u16(buffer, XX_APM_SECTOR, 2U, true);
    if (!xx_apm_is_block_size(parsed->block_size)) return false;
    parsed->device_blocks = xx_data_get_u32(buffer, XX_APM_SECTOR, 4U, true);
    if (!xx_apm_choose_step(self, total_size, parsed->block_size,
                            &parsed->map_step, &first)) {
        goto fail;
    }
    parsed->map_entries = first.map_entries;
    parsed->input_size = total_size;
    /* The walk stops at the first block that is not a map entry or that the
     * device does not hold: a truncated dump keeps its leading partitions. */
    for (index = 1U; index <= parsed->map_entries; ++index) {
        xx_apm_raw_entry raw;
        int64_t entry_offset;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_apm_block_to_offset(self->base_address, index,
                                    parsed->map_step, &entry_offset) ||
            !xx_apm_read_entry(self->device, entry_offset, total_size, buffer,
                               &raw)) {
            break;
        }
        parsed->entries_read = index;
        if (entry_offset + (int64_t)XX_APM_SECTOR > parsed->archive_end) {
            parsed->archive_end = entry_offset + (int64_t)XX_APM_SECTOR;
        }
        if (!xx_apm_collect_entry(self, parsed, buffer, entry_offset, index)) {
            goto fail;
        }
    }
    if (parsed->entries_read == 0U || parsed->count == 0U) goto fail;
    /* sbBlkCount gives the device size: a disk whose map leaves its tail
     * unaccounted for still ends there, as far as this device holds it. */
    if (parsed->device_blocks != 0U &&
        xx_apm_block_to_offset(self->base_address, parsed->device_blocks,
                               parsed->block_size, &device_end)) {
        if (device_end > total_size) device_end = total_size;
        if (device_end > parsed->archive_end) parsed->archive_end = device_end;
    }
    return true;
fail:
    xx_apm_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_apm_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_apm_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

/* "<type> (<name>)", or whichever of the two is set. */
static void xx_apm_make_comment(const xx_apm_entry *entry,
                                char out[XX_APM_COMMENT_SIZE]) {
    size_t type_length = xx_str_len(entry->part_type);
    size_t name_length = xx_str_len(entry->part_name);
    size_t used = type_length;
    xx_rt_memcpy(out, entry->part_type, type_length);
    if (name_length != 0U) {
        if (type_length != 0U) {
            out[used++] = ' ';
            out[used++] = '(';
        }
        xx_rt_memcpy(out + used, entry->part_name, name_length);
        used += name_length;
        if (type_length != 0U) out[used++] = ')';
    }
    out[used] = '\0';
}

/* The payload is carried verbatim, so both sizes are the bytes present. */
static bool xx_apm_populate_record(xx_archive_record *record,
                                   const xx_apm_entry *entry) {
    char comment[XX_APM_COMMENT_SIZE];
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_apm_make_comment(entry, comment);
    record->header_offset = entry->header_offset;
    record->header_size = (int64_t)XX_APM_SECTOR;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          entry->status) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          comment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_apm_archive_stream_free(void *pointer) {
    xx_apm_archive_stream *stream = (xx_apm_archive_stream *)pointer;
    if (!stream) return;
    xx_apm_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Record names are generated here, never taken from the map, so this only
 * has to refuse the impossible. */
static bool xx_apm_safe_name(const char *name) {
    size_t index;
    if (!name || !name[0]) return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.')) {
            return false;
        }
    }
    return name[0] != '.';
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xx_apm_init(xx_apm *apm, xx_io_device *dev, int64_t base_address) {
    if (!apm) return;
    xx_rt_memset(apm, 0, sizeof(*apm));
    xx_format_init(&apm->format, dev, base_address);
    apm->format.endian = XX_ENDIAN_BIG;
    apm->format.file_type = XX_APM_FILE_TYPE;
    apm->format.format_type = XX_TYPE_ARCHIVE;
    apm->format.is_archive = true;
    xx_format_set_mime_type(&apm->format, "application/x-apple-partition-map");
    xx_format_set_extension(&apm->format, "img");
    apm->format.check_is_valid = xx_apm_check_is_valid;
    apm->format.handle_base_info = xx_apm_handle_base_info;
    apm->format.get_format_size = xx_apm_get_format_size;
    apm->format.get_number_of_archive_records =
        xx_apm_get_number_of_archive_records;
    apm->format.create_archive_records_reading =
        xx_apm_create_archive_records_reading;
    apm->format.get_current_archive_record = xx_apm_get_current_archive_record;
    apm->format.unpack_current_archive_record =
        xx_apm_unpack_current_archive_record;
    apm->format.archive_record_move_to_next = xx_apm_archive_record_move_to_next;
    apm->format.free_archive_records_reading =
        xx_apm_free_archive_records_reading;
    apm->format.destroy = xx_apm_vtable_destroy;
    apm->archive_end = -1;
}

xx_apm *xx_apm_create(xx_io_device *dev, int64_t base_address) {
    xx_apm *apm = (xx_apm *)xx_mem_alloc(sizeof(*apm));
    if (apm) xx_apm_init(apm, dev, base_address);
    return apm;
}

void xx_apm_destroy(xx_apm *apm) {
    if (!apm) return;
    if (apm->internal) {
        xx_apm_private_cleanup((xx_apm_private *)apm->internal);
        xx_mem_free(apm->internal);
        apm->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&apm->format);
}

static void xx_apm_vtable_destroy(Abstractformat *self) {
    xx_apm_destroy((xx_apm *)self);
}

void xx_apm_free(xx_apm *apm) {
    if (!apm) return;
    xx_apm_destroy(apm);
    xx_mem_free(apm);
}

bool xx_apm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_apm_private parsed;
    bool result = xx_apm_parse(self, &parsed, pd);
    xx_apm_private_cleanup(&parsed);
    return result;
}

bool xx_apm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_apm_private *parsed;
    xx_apm *apm = (xx_apm *)self;
    int64_t total_size;
    if (!self || !apm) return false;
    parsed = (xx_apm_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_apm_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (apm->internal) {
        xx_apm_private_cleanup((xx_apm_private *)apm->internal);
        xx_mem_free(apm->internal);
    }
    apm->internal = parsed;
    apm->number_of_records = parsed->count;
    apm->number_of_members = parsed->count;
    apm->block_size = parsed->block_size;
    apm->device_blocks = parsed->device_blocks;
    apm->map_step = parsed->map_step;
    apm->map_entries = parsed->map_entries;
    apm->entries_read = parsed->entries_read;
    apm->archive_end = parsed->archive_end;
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

int64_t xx_apm_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_apm_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_apm *)self)->number_of_records;
}

xx_archive_record_state *xx_apm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_apm_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_apm_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_apm_copy_options(&state->options, options) ||
        !xx_apm_parse(self, &stream->parsed, pd)) {
        xx_apm_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_apm_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_apm_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_apm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_apm_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_apm_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_apm_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_apm_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_apm_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    size_t base_length;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_apm_safe_name(name)) return false;
    option = xx_apm_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    base_length = xx_str_len(base);
    if (base_length != 0U && base[base_length - 1U] != '/' &&
        base[base_length - 1U] != '\\') {
        char *with_slash = xx_str_concat(base, "/");
        if (!with_slash) goto cleanup;
        destination = xx_str_concat(with_slash, name);
        xx_str_free(with_slash);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_apm_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_apm_get_number_of_records(const xx_apm *apm) {
    return apm ? apm->number_of_records : 0U;
}
uint64_t xx_apm_get_number_of_members(const xx_apm *apm) {
    return apm ? apm->number_of_members : 0U;
}
uint32_t xx_apm_get_block_size(const xx_apm *apm) {
    return apm ? apm->block_size : 0U;
}
uint32_t xx_apm_get_map_step(const xx_apm *apm) {
    return apm ? apm->map_step : 0U;
}
int64_t xx_apm_get_archive_end(const xx_apm *apm) {
    return apm ? apm->archive_end : -1;
}

bool xx_apm_get_partition_info(const xx_apm *apm, uint64_t index,
                               xx_apm_partition_info *info) {
    const xx_apm_private *parsed;
    const xx_apm_entry *entry;
    if (!apm || !info || !apm->internal) return false;
    parsed = (const xx_apm_private *)apm->internal;
    if (index >= parsed->count) return false;
    entry = &parsed->entries[index];
    info->offset = entry->data_offset;
    info->size = entry->data_size;
    info->declared_size = entry->declared_size;
    info->start_block = entry->start_block;
    info->block_count = entry->block_count;
    info->status = entry->status;
    info->entry_index = entry->entry_index;
    info->partition_name = entry->part_name;
    info->partition_type = entry->part_type;
    info->name = entry->name;
    return true;
}
