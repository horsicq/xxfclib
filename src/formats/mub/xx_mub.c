/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Mach-O universal ("fat") binary: a slice table in front of whole Mach-O
 * images.  Written from the layout in Apple's <mach-o/fat.h>; the accepted
 * magics, the cputype/cpusubtype sanity limits and the slice naming follow
 * 7-Zip's MubHandler.cpp (read for behaviour only, no code taken).  xx_mub.h
 * carries the field table.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mub/xx_mub.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as MUB is registered there. */
#ifdef MUB
#define XX_MUB_FILE_TYPE XX_FILE_TYPE_MUB
#else
#define XX_MUB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_MUB_MAGIC_FAT UINT32_C(0xCAFEBABE)
#define XX_MUB_MAGIC_FAT64 UINT32_C(0xCAFEBABF)
#define XX_MUB_MAGIC_LE UINT32_C(0xB9FAF10E)
#define XX_MUB_HEADER_SIZE 8U
#define XX_MUB_ARCH_SIZE 20U
#define XX_MUB_ARCH64_SIZE 32U
#define XX_MUB_TABLE_MAX (XX_MUB_HEADER_SIZE + XX_MUB_MAX_SLICES * XX_MUB_ARCH64_SIZE)

#define XX_MUB_CPU_ABI64 UINT32_C(0x01000000)
#define XX_MUB_CPU_ARCH_MASK UINT32_C(0xFF000000)
/* Flag bits a real cputype may carry: CPU_ARCH_ABI64, CPU_ARCH_ABI64_32. */
#define XX_MUB_CPU_ABI_FLAGS UINT32_C(0x03000000)
#define XX_MUB_SUB_LIB64 UINT32_C(0x80000000)
/* cpusubtype top byte: CPU_SUBTYPE_LIB64 / PTRAUTH_ABI plus the arm64e
 * pointer-authentication version nibble. */
#define XX_MUB_SUB_FLAGS UINT32_C(0x8F000000)
#define XX_MUB_SUB_I386_ALL 3U
#define XX_MUB_NAME_SIZE 40U

typedef struct xx_mub_slice_s {
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t align;
    int64_t offset;
    int64_t size;
    char name[XX_MUB_NAME_SIZE];
} xx_mub_slice;

typedef struct xx_mub_private_s {
    uint32_t magic;
    uint32_t count;
    int64_t table_end;
    int64_t archive_end;
    xx_mub_slice slices[XX_MUB_MAX_SLICES];
} xx_mub_private;

typedef struct xx_mub_archive_stream_s {
    xx_mub_private parsed;
    uint32_t index;
} xx_mub_archive_stream;

static void xx_mub_vtable_destroy(Abstractformat *self);

static bool xx_mub_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Name the slice after its CPU, matching the extension 7-Zip derives. */
static void xx_mub_make_name(xx_mub_slice *slice)
{
    const char *ext = NULL;
    size_t len;
    switch (slice->cputype) {
        case 7U: ext = "x86"; break;
        case 12U: ext = "arm"; break;
        case 14U: ext = "sparc"; break;
        case 18U: ext = "ppc"; break;
        case XX_MUB_CPU_ABI64 | 7U: ext = "x64"; break;
        case XX_MUB_CPU_ABI64 | 12U: ext = "arm64"; break;
        case XX_MUB_CPU_ABI64 | 18U: ext = "ppc64"; break;
        default: break;
    }
    if (ext) {
        (void)xx_rt_snprintf(slice->name, sizeof(slice->name), "%s", ext);
    } else {
        (void)xx_rt_snprintf(slice->name, sizeof(slice->name), "cpu%u%s", (unsigned)(slice->cputype & ~XX_MUB_CPU_ABI64),
                             (slice->cputype & XX_MUB_CPU_ABI64) ? "_64" : "");
    }
    if (slice->cpusubtype != 0U &&
        ((slice->cputype != 7U && slice->cputype != (XX_MUB_CPU_ABI64 | 7U)) || (slice->cpusubtype & ~XX_MUB_SUB_LIB64) != XX_MUB_SUB_I386_ALL)) {
        len = xx_str_len(slice->name);
        if (len < sizeof(slice->name)) {
            (void)xx_rt_snprintf(slice->name + len, sizeof(slice->name) - len, "-%u", (unsigned)slice->cpusubtype);
        }
    }
}

static bool xx_mub_name_used(const xx_mub_private *parsed, uint32_t count, const char *name)
{
    uint32_t i;
    for (i = 0U; i < count; ++i) {
        if (xx_str_cmp(parsed->slices[i].name, name) == 0) return true;
    }
    return false;
}

static void xx_mub_private_reset(xx_mub_private *parsed)
{
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->table_end = -1;
    parsed->archive_end = -1;
}

static bool xx_mub_parse(Abstractformat *self, xx_mub_private *parsed, xx_pd_struct *pd)
{
    uint8_t table[XX_MUB_TABLE_MAX];
    int64_t total_size;
    int64_t avail;
    uint32_t magic;
    uint32_t count;
    uint32_t i;
    size_t rec_size;
    size_t table_size;
    bool big_endian;
    bool fat64;
    xx_mub_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < (int64_t)XX_MUB_HEADER_SIZE) {
        return false;
    }
    avail = total_size - self->base_address;
    if (!xx_mub_read_at(self->device, self->base_address, table, XX_MUB_HEADER_SIZE)) {
        return false;
    }
    magic = xx_data_get_u32(table, sizeof(table), 0U, true);
    if (magic == XX_MUB_MAGIC_FAT) {
        big_endian = true;
        fat64 = false;
    } else if (magic == XX_MUB_MAGIC_FAT64) {
        big_endian = true;
        fat64 = true;
    } else if (magic == XX_MUB_MAGIC_LE) {
        big_endian = false;
        fat64 = false;
    } else {
        return false;
    }
    count = xx_data_get_u32(table, sizeof(table), 4U, big_endian);
    if (count == 0U || count > XX_MUB_MAX_SLICES) return false;
    rec_size = fat64 ? XX_MUB_ARCH64_SIZE : XX_MUB_ARCH_SIZE;
    table_size = XX_MUB_HEADER_SIZE + (size_t)count * rec_size;
    if ((int64_t)table_size > avail || !xx_mub_read_at(self->device, self->base_address, table, table_size)) {
        return false;
    }
    parsed->magic = magic;
    parsed->table_end = (int64_t)table_size;
    parsed->archive_end = (int64_t)table_size;
    for (i = 0U; i < count; ++i) {
        size_t at = XX_MUB_HEADER_SIZE + (size_t)i * rec_size;
        xx_mub_slice *slice = &parsed->slices[i];
        uint64_t offset;
        uint64_t size;
        slice->cputype = xx_data_get_u32(table, sizeof(table), at, big_endian);
        slice->cpusubtype = xx_data_get_u32(table, sizeof(table), at + 4U, big_endian);
        if (fat64) {
            offset = xx_data_get_u64(table, sizeof(table), at + 8U, true);
            size = xx_data_get_u64(table, sizeof(table), at + 16U, true);
            slice->align = xx_data_get_u32(table, sizeof(table), at + 24U, true);
        } else {
            offset = xx_data_get_u32(table, sizeof(table), at + 8U, big_endian);
            size = xx_data_get_u32(table, sizeof(table), at + 12U, big_endian);
            slice->align = xx_data_get_u32(table, sizeof(table), at + 16U, big_endian);
        }
        /* Sanity limits after 7-Zip: a real cputype/cpusubtype is a small
         * number plus flag bits in the top byte; alignment is a shift. */
        if (slice->align > 31U || (slice->cputype & XX_MUB_CPU_ARCH_MASK & ~XX_MUB_CPU_ABI_FLAGS) != 0U ||
            (slice->cpusubtype & XX_MUB_CPU_ARCH_MASK & ~XX_MUB_SUB_FLAGS) != 0U || (slice->cputype & ~XX_MUB_CPU_ARCH_MASK) == 0U ||
            (slice->cputype & ~XX_MUB_CPU_ARCH_MASK) >= 0x100U || (slice->cpusubtype & ~XX_MUB_CPU_ARCH_MASK) >= 0x100U) {
            return false;
        }
        /* The slice must lie past the table and wholly inside the device. */
        if (offset < (uint64_t)table_size || offset > (uint64_t)avail || size > (uint64_t)avail - offset) {
            return false;
        }
        slice->offset = (int64_t)offset;
        slice->size = (int64_t)size;
        if (slice->offset + slice->size > parsed->archive_end) {
            parsed->archive_end = slice->offset + slice->size;
        }
        xx_mub_make_name(slice);
        if (xx_mub_name_used(parsed, i, slice->name)) {
            size_t len = xx_str_len(slice->name);
            if (len + 4U > sizeof(slice->name)) len = sizeof(slice->name) - 4U;
            (void)xx_rt_snprintf(slice->name + len, sizeof(slice->name) - len, "_%u", (unsigned)i);
            if (xx_mub_name_used(parsed, i, slice->name)) return false;
        }
    }
    parsed->count = count;
    return true;
}

static bool xx_mub_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_mub_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_mub_populate_record(xx_archive_record *record, const xx_mub_private *parsed, int64_t base_address, uint32_t index)
{
    const xx_mub_slice *slice;
    if (!record || !parsed || index >= parsed->count) return false;
    slice = &parsed->slices[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address;
    record->header_size = parsed->table_end;
    record->data_offset = base_address + slice->offset;
    record->compressed_size = slice->size;
    return xx_archive_record_set_original_name(record, slice->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)slice->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)slice->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_mub_archive_stream_free(void *pointer)
{
    xx_mub_archive_stream *stream = (xx_mub_archive_stream *)pointer;
    if (!stream) return;
    xx_mub_private_reset(&stream->parsed);
    xx_mem_free(stream);
}

void xx_mub_init(xx_mub *mub, xx_io_device *dev, int64_t base_address)
{
    if (!mub) return;
    xx_mem_zero(mub, sizeof(*mub));
    xx_format_init(&mub->format, dev, base_address);
    mub->format.endian = XX_ENDIAN_BIG;
    mub->format.file_type = XX_MUB_FILE_TYPE;
    mub->format.format_type = XX_TYPE_ARCHIVE;
    mub->format.is_archive = true;
    xx_format_set_mime_type(&mub->format, "application/x-mach-binary");
    xx_format_set_extension(&mub->format, "macho");
    mub->format.check_is_valid = xx_mub_check_is_valid;
    mub->format.handle_base_info = xx_mub_handle_base_info;
    mub->format.get_format_size = xx_mub_get_format_size;
    mub->format.get_number_of_archive_records = xx_mub_get_number_of_archive_records;
    mub->format.create_archive_records_reading = xx_mub_create_archive_records_reading;
    mub->format.get_current_archive_record = xx_mub_get_current_archive_record;
    mub->format.unpack_current_archive_record = xx_mub_unpack_current_archive_record;
    mub->format.archive_record_move_to_next = xx_mub_archive_record_move_to_next;
    mub->format.free_archive_records_reading = xx_mub_free_archive_records_reading;
    mub->format.destroy = xx_mub_vtable_destroy;
    mub->archive_end = -1;
}

xx_mub *xx_mub_create(xx_io_device *dev, int64_t base_address)
{
    xx_mub *mub = (xx_mub *)xx_mem_alloc(sizeof(*mub));
    if (mub) xx_mub_init(mub, dev, base_address);
    return mub;
}

void xx_mub_destroy(xx_mub *mub)
{
    if (!mub) return;
    if (mub->internal) {
        xx_mem_free(mub->internal);
        mub->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&mub->format);
}

static void xx_mub_vtable_destroy(Abstractformat *self)
{
    xx_mub_destroy((xx_mub *)self);
}

void xx_mub_free(xx_mub *mub)
{
    if (!mub) return;
    xx_mub_destroy(mub);
    xx_mem_free(mub);
}

bool xx_mub_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_mub_private *parsed;
    bool result;
    /* The slice table is ~700 bytes; keep it off the detector's stack. */
    parsed = (xx_mub_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed) return false;
    result = xx_mub_parse(self, parsed, pd);
    xx_mem_free(parsed);
    return result;
}

bool xx_mub_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_mub_private *parsed;
    xx_mub *mub = (xx_mub *)self;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_mub_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_mub_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (mub->internal) xx_mem_free(mub->internal);
    mub->internal = parsed;
    mub->number_of_records = parsed->count;
    mub->magic = parsed->magic;
    mub->is_fat64 = parsed->magic == XX_MUB_MAGIC_FAT64;
    mub->archive_end = self->base_address + parsed->archive_end;
    self->endian = parsed->magic == XX_MUB_MAGIC_LE ? XX_ENDIAN_LITTLE : XX_ENDIAN_BIG;
    self->format_size = parsed->archive_end;
    total_size = xx_io_total_size(self->device);
    if (total_size > mub->archive_end) {
        self->overlay_offset = mub->archive_end;
        self->overlay_size = total_size - mub->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_mub_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_mub_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_mub *)self)->number_of_records;
}

xx_archive_record_state *xx_mub_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_mub_archive_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_mub_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_mub_copy_options(&state->options, options) || !xx_mub_parse(self, &stream->parsed, pd)) {
        xx_mub_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_mub_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count > 0U && xx_mub_populate_record(&state->current_record, &stream->parsed, self->base_address, 0U)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_mub_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_mub_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_mub_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_mub_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index < stream->parsed.count && xx_mub_populate_record(&state->current_record, &stream->parsed, self->base_address, stream->index)) {
        state->current_index = (int64_t)stream->index;
        return true;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_mub_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    /* Names are built from numbers and fixed words, never from the file. */
    if (!name || !name[0]) return false;
    option = xx_mub_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 && record->data_offset <= total && record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (xx_store_create_dirs_a(destination, false)) {
        /* The helper deletes its own output on failure. */
        result = xx_store_unpack_device_to_file(self->device, record->data_offset, record->compressed_size, destination, pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_mub_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_mub_get_number_of_records(const xx_mub *mub)
{
    return mub ? mub->number_of_records : 0U;
}

int64_t xx_mub_get_archive_end(const xx_mub *mub)
{
    return mub ? mub->archive_end : -1;
}
