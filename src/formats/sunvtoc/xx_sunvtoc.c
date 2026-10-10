/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sunvtoc/xx_sunvtoc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. The SUNVTOC macro is the alias xxfc_defs.h defines next to every
 * XX_FILE_TYPE_* value, so this block heals itself the moment the enum grows
 * a SUNVTOC member; delete it then. */
#ifdef SUNVTOC
#define XX_SUNVTOC_FILE_TYPE XX_FILE_TYPE_SUNVTOC
#else
#define XX_SUNVTOC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SUNVTOC_SECTOR 512U
#define XX_SUNVTOC_MAGIC 0xDABEU
#define XX_SUNVTOC_SANITY UINT32_C(0x600DDEEE)
#define XX_SUNVTOC_SPARC_SLICES 8U
#define XX_SUNVTOC_COMMENT_SIZE 32U

typedef struct xx_sunvtoc_entry_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    uint64_t declared_size;
    uint64_t start_sector;
    uint32_t sector_count;
    uint16_t tag;
    uint16_t flag;
    uint32_t slot;
} xx_sunvtoc_entry;

typedef struct xx_sunvtoc_private_s {
    xx_sunvtoc_entry entries[XX_SUNVTOC_MAX_SLICES];
    size_t count;
    uint32_t layout;
    bool has_vtoc;
    uint32_t sectors_per_cylinder;
    int64_t label_offset;
    int64_t input_size;
    int64_t archive_end;
} xx_sunvtoc_private;

typedef struct xx_sunvtoc_archive_stream_s {
    xx_sunvtoc_private parsed;
    size_t index;
} xx_sunvtoc_archive_stream;

static void xx_sunvtoc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static bool xx_sunvtoc_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
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

/* The label is sound when the magic sits at +508 in the expected byte order
 * and the XOR of all 256 words is zero. XOR is order independent, so the
 * words are folded as they come. */
static bool xx_sunvtoc_label_ok(const uint8_t sector[XX_SUNVTOC_SECTOR], bool big_endian)
{
    uint16_t sum = 0U;
    size_t index;
    uint16_t magic = xx_data_get_u16(sector, XX_SUNVTOC_SECTOR, 508U, big_endian);
    if (magic != XX_SUNVTOC_MAGIC) return false;
    for (index = 0U; index < XX_SUNVTOC_SECTOR; index += 2U) {
        sum = (uint16_t)(sum ^ (uint16_t)((sector[index] << 8U) | sector[index + 1U]));
    }
    return sum == 0U;
}

static char *xx_sunvtoc_make_name(unsigned index)
{
    static const char prefix[] = "slice";
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

/* Tag names from the Solaris VTOC, plus the Linux ids fdisk writes. */
static const char *xx_sunvtoc_tag_name(uint16_t tag)
{
    switch (tag) {
        case 0x00U: return "unassigned";
        case 0x01U: return "boot";
        case 0x02U: return "root";
        case 0x03U: return "swap";
        case 0x04U: return "usr";
        case 0x05U: return "backup";
        case 0x06U: return "stand";
        case 0x07U: return "var";
        case 0x08U: return "home";
        case 0x09U: return "alternates";
        case 0x0AU: return "cache";
        case 0x0BU: return "reserved";
        case 0x0CU: return "system";
        case 0x18U: return "BIOS_boot";
        case 0x82U: return "Linux swap";
        case 0x83U: return "Linux native";
        case 0x8EU: return "Linux LVM";
        case 0xFDU: return "Linux raid";
        default: return NULL;
    }
}

static void xx_sunvtoc_make_comment(const xx_sunvtoc_entry *entry, bool has_vtoc, char out[XX_SUNVTOC_COMMENT_SIZE])
{
    static const char hex[] = "0123456789ABCDEF";
    const char *text;
    size_t length;
    out[0] = '\0';
    if (!has_vtoc) return;
    text = xx_sunvtoc_tag_name(entry->tag);
    if (text) {
        length = xx_str_len(text);
        if (length >= XX_SUNVTOC_COMMENT_SIZE) length = XX_SUNVTOC_COMMENT_SIZE - 1U;
        xx_rt_memcpy(out, text, length);
        out[length] = '\0';
        return;
    }
    /* "tag 0xNNNN" */
    xx_rt_memcpy(out, "tag 0x", 6U);
    out[6] = hex[(entry->tag >> 12U) & 0xFU];
    out[7] = hex[(entry->tag >> 8U) & 0xFU];
    out[8] = hex[(entry->tag >> 4U) & 0xFU];
    out[9] = hex[entry->tag & 0xFU];
    out[10] = '\0';
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

static void xx_sunvtoc_private_reset(xx_sunvtoc_private *parsed)
{
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->label_offset = -1;
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static void xx_sunvtoc_private_cleanup(xx_sunvtoc_private *parsed)
{
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    xx_sunvtoc_private_reset(parsed);
}

/* Publish one slice given in 512-byte sectors from the base. A slice of no
 * sectors, one whose byte offset overflows, or one that starts at or past
 * the end of the device is skipped. Returns false only on allocation
 * failure. */
static bool xx_sunvtoc_add_slice(Abstractformat *self, xx_sunvtoc_private *parsed, uint32_t slot, uint64_t start_sector, uint32_t count, uint16_t tag, uint16_t flag)
{
    xx_sunvtoc_entry *entry;
    uint64_t start_bytes;
    uint64_t declared;
    int64_t offset;
    int64_t available;
    if (count == 0U || parsed->count >= XX_SUNVTOC_MAX_SLICES) return true;
    if (start_sector > (uint64_t)INT64_MAX / XX_SUNVTOC_SECTOR) return true;
    start_bytes = start_sector * XX_SUNVTOC_SECTOR;
    if (start_bytes > (uint64_t)(INT64_MAX - self->base_address)) return true;
    offset = self->base_address + (int64_t)start_bytes;
    if (offset >= parsed->input_size) return true;
    declared = (uint64_t)count * XX_SUNVTOC_SECTOR;
    available = parsed->input_size - offset;
    if (declared < (uint64_t)available) available = (int64_t)declared;
    entry = &parsed->entries[parsed->count];
    xx_rt_memset(entry, 0, sizeof(*entry));
    entry->name = xx_sunvtoc_make_name(slot);
    if (!entry->name) return false;
    entry->data_offset = offset;
    entry->data_size = available;
    entry->declared_size = declared;
    entry->start_sector = start_sector;
    entry->sector_count = count;
    entry->tag = tag;
    entry->flag = flag;
    entry->slot = slot;
    ++parsed->count;
    if (offset + available > parsed->archive_end) {
        parsed->archive_end = offset + available;
    }
    return true;
}

static bool xx_sunvtoc_parse_sparc(Abstractformat *self, xx_sunvtoc_private *parsed, const uint8_t *s)
{
    uint32_t heads = xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 436U, true);
    uint32_t sectors = xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 438U, true);
    uint32_t slot;
    if (heads == 0U || sectors == 0U) return false;
    parsed->layout = XX_SUNVTOC_LAYOUT_SPARC;
    parsed->sectors_per_cylinder = heads * sectors; /* <= 0xFFFE0001 */
    parsed->has_vtoc = xx_data_get_u32(s, XX_SUNVTOC_SECTOR, 188U, true) == XX_SUNVTOC_SANITY;
    for (slot = 0U; slot < XX_SUNVTOC_SPARC_SLICES; ++slot) {
        uint32_t cylinder = xx_data_get_u32(s, XX_SUNVTOC_SECTOR, 444U + slot * 8U, true);
        uint32_t count = xx_data_get_u32(s, XX_SUNVTOC_SECTOR, 448U + slot * 8U, true);
        uint16_t tag = 0U;
        uint16_t flag = 0U;
        if (parsed->has_vtoc) {
            tag = xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 142U + slot * 4U, true);
            flag = xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 144U + slot * 4U, true);
        }
        /* Both factors are below 2^32, so the product fits 64 bits. */
        if (!xx_sunvtoc_add_slice(self, parsed, slot, (uint64_t)cylinder * parsed->sectors_per_cylinder, count, tag, flag)) {
            return false;
        }
    }
    return true;
}

static bool xx_sunvtoc_parse_x86(Abstractformat *self, xx_sunvtoc_private *parsed, const uint8_t *s)
{
    uint32_t nparts;
    uint32_t limit;
    uint32_t slot;
    if (xx_data_get_u32(s, XX_SUNVTOC_SECTOR, 12U, false) != XX_SUNVTOC_SANITY || xx_data_get_u32(s, XX_SUNVTOC_SECTOR, 16U, false) != 1U ||
        xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 28U, false) != XX_SUNVTOC_SECTOR) {
        return false;
    }
    nparts = xx_data_get_u16(s, XX_SUNVTOC_SECTOR, 30U, false);
    if (nparts > XX_SUNVTOC_MAX_SLICES) return false;
    limit = nparts > 8U ? XX_SUNVTOC_MAX_SLICES : 8U;
    parsed->layout = XX_SUNVTOC_LAYOUT_X86;
    parsed->has_vtoc = true;
    for (slot = 0U; slot < limit; ++slot) {
        size_t at = 72U + (size_t)slot * 12U;
        if (!xx_sunvtoc_add_slice(self, parsed, slot, xx_data_get_u32(s, XX_SUNVTOC_SECTOR, at + 4U, false), xx_data_get_u32(s, XX_SUNVTOC_SECTOR, at + 8U, false),
                                  xx_data_get_u16(s, XX_SUNVTOC_SECTOR, at, false), xx_data_get_u16(s, XX_SUNVTOC_SECTOR, at + 2U, false))) {
            return false;
        }
    }
    return true;
}

/* Reads at most two sectors: the SPARC label in sector 0, else the x86
 * VTOC in sector 1. A cheap byte test on the magic comes first, so garbage
 * is refused after one 512-byte read. */
static bool xx_sunvtoc_parse(Abstractformat *self, xx_sunvtoc_private *parsed, xx_pd_struct *pd)
{
    uint8_t sector[XX_SUNVTOC_SECTOR];
    int64_t total_size;
    bool ok = false;
    if (parsed) xx_sunvtoc_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size <= self->base_address || total_size - self->base_address < (int64_t)XX_SUNVTOC_SECTOR) {
        return false;
    }
    parsed->input_size = total_size;
    if (!xx_sunvtoc_read_at(self->device, self->base_address, sector, XX_SUNVTOC_SECTOR)) {
        return false;
    }
    if (sector[508] == 0xDAU && sector[509] == 0xBEU && xx_sunvtoc_label_ok(sector, true)) {
        parsed->label_offset = self->base_address;
        ok = xx_sunvtoc_parse_sparc(self, parsed, sector);
    }
    if (!ok && parsed->count == 0U && total_size - self->base_address >= (int64_t)(XX_SUNVTOC_SECTOR * 2U)) {
        xx_sunvtoc_private_cleanup(parsed);
        parsed->input_size = total_size;
        if (xx_sunvtoc_read_at(self->device, self->base_address + XX_SUNVTOC_SECTOR, sector, XX_SUNVTOC_SECTOR) && sector[508] == 0xBEU && sector[509] == 0xDAU &&
            xx_sunvtoc_label_ok(sector, false)) {
            parsed->label_offset = self->base_address + XX_SUNVTOC_SECTOR;
            ok = xx_sunvtoc_parse_x86(self, parsed, sector);
        }
    }
    if (!ok || parsed->count == 0U) {
        xx_sunvtoc_private_cleanup(parsed);
        return false;
    }
    if (parsed->label_offset + (int64_t)XX_SUNVTOC_SECTOR > parsed->archive_end) {
        parsed->archive_end = parsed->label_offset + (int64_t)XX_SUNVTOC_SECTOR;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_sunvtoc_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *xx_sunvtoc_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_sunvtoc_populate_record(xx_archive_record *record, const xx_sunvtoc_entry *entry, bool has_vtoc, int64_t label_offset)
{
    char comment[XX_SUNVTOC_COMMENT_SIZE];
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_sunvtoc_make_comment(entry, has_vtoc, comment);
    record->header_offset = label_offset;
    record->header_size = (int64_t)XX_SUNVTOC_SECTOR;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, entry->flag) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, comment) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_sunvtoc_archive_stream_free(void *pointer)
{
    xx_sunvtoc_archive_stream *stream = (xx_sunvtoc_archive_stream *)pointer;
    if (!stream) return;
    xx_sunvtoc_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Record names are generated ("slice<n>"), never read from the label; this
 * only refuses the impossible. */
static bool xx_sunvtoc_safe_name(const char *name)
{
    size_t index;
    if (!name || !name[0]) return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xx_sunvtoc_init(xx_sunvtoc *sunvtoc, xx_io_device *dev, int64_t base_address)
{
    if (!sunvtoc) return;
    xx_rt_memset(sunvtoc, 0, sizeof(*sunvtoc));
    xx_format_init(&sunvtoc->format, dev, base_address);
    sunvtoc->format.endian = XX_ENDIAN_BIG;
    sunvtoc->format.file_type = XX_SUNVTOC_FILE_TYPE;
    sunvtoc->format.format_type = XX_TYPE_ARCHIVE;
    sunvtoc->format.is_archive = true;
    xx_format_set_mime_type(&sunvtoc->format, "application/x-raw-disk-image");
    xx_format_set_extension(&sunvtoc->format, "img");
    sunvtoc->format.check_is_valid = xx_sunvtoc_check_is_valid;
    sunvtoc->format.handle_base_info = xx_sunvtoc_handle_base_info;
    sunvtoc->format.get_format_size = xx_sunvtoc_get_format_size;
    sunvtoc->format.get_number_of_archive_records = xx_sunvtoc_get_number_of_archive_records;
    sunvtoc->format.create_archive_records_reading = xx_sunvtoc_create_archive_records_reading;
    sunvtoc->format.get_current_archive_record = xx_sunvtoc_get_current_archive_record;
    sunvtoc->format.unpack_current_archive_record = xx_sunvtoc_unpack_current_archive_record;
    sunvtoc->format.archive_record_move_to_next = xx_sunvtoc_archive_record_move_to_next;
    sunvtoc->format.free_archive_records_reading = xx_sunvtoc_free_archive_records_reading;
    sunvtoc->format.destroy = xx_sunvtoc_vtable_destroy;
    sunvtoc->archive_end = -1;
}

xx_sunvtoc *xx_sunvtoc_create(xx_io_device *dev, int64_t base_address)
{
    xx_sunvtoc *sunvtoc = (xx_sunvtoc *)xx_mem_alloc(sizeof(*sunvtoc));
    if (sunvtoc) xx_sunvtoc_init(sunvtoc, dev, base_address);
    return sunvtoc;
}

void xx_sunvtoc_destroy(xx_sunvtoc *sunvtoc)
{
    if (!sunvtoc) return;
    if (sunvtoc->internal) {
        xx_sunvtoc_private_cleanup((xx_sunvtoc_private *)sunvtoc->internal);
        xx_mem_free(sunvtoc->internal);
        sunvtoc->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&sunvtoc->format);
}

static void xx_sunvtoc_vtable_destroy(Abstractformat *self)
{
    xx_sunvtoc_destroy((xx_sunvtoc *)self);
}

void xx_sunvtoc_free(xx_sunvtoc *sunvtoc)
{
    if (!sunvtoc) return;
    xx_sunvtoc_destroy(sunvtoc);
    xx_mem_free(sunvtoc);
}

bool xx_sunvtoc_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_sunvtoc_private parsed;
    bool result = xx_sunvtoc_parse(self, &parsed, pd);
    xx_sunvtoc_private_cleanup(&parsed);
    return result;
}

bool xx_sunvtoc_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_sunvtoc_private *parsed;
    xx_sunvtoc *sunvtoc = (xx_sunvtoc *)self;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_sunvtoc_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_sunvtoc_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (sunvtoc->internal) {
        xx_sunvtoc_private_cleanup((xx_sunvtoc_private *)sunvtoc->internal);
        xx_mem_free(sunvtoc->internal);
    }
    sunvtoc->internal = parsed;
    sunvtoc->number_of_records = parsed->count;
    sunvtoc->number_of_members = parsed->count;
    sunvtoc->layout = parsed->layout;
    sunvtoc->has_vtoc = parsed->has_vtoc;
    sunvtoc->sectors_per_cylinder = parsed->sectors_per_cylinder;
    sunvtoc->archive_end = parsed->archive_end;
    self->endian = parsed->layout == XX_SUNVTOC_LAYOUT_X86 ? XX_ENDIAN_LITTLE : XX_ENDIAN_BIG;
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

int64_t xx_sunvtoc_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_sunvtoc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_sunvtoc *)self)->number_of_records;
}

xx_archive_record_state *xx_sunvtoc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_sunvtoc_archive_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_sunvtoc_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_sunvtoc_copy_options(&state->options, options) || !xx_sunvtoc_parse(self, &stream->parsed, pd)) {
        xx_sunvtoc_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_sunvtoc_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_sunvtoc_populate_record(&state->current_record, &stream->parsed.entries[0], stream->parsed.has_vtoc, stream->parsed.label_offset)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_sunvtoc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_sunvtoc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_sunvtoc_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_sunvtoc_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_sunvtoc_populate_record(&state->current_record, &stream->parsed.entries[stream->index], stream->parsed.has_vtoc, stream->parsed.label_offset)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_sunvtoc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    size_t base_length;
    bool result = false;
    if (!self || !self->device || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_sunvtoc_safe_name(name)) return false;
    option = xx_sunvtoc_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    base_length = xx_str_len(base);
    if (base_length != 0U && base[base_length - 1U] != '/' && base[base_length - 1U] != '\\') {
        char *with_slash = xx_str_concat(base, "/");
        if (!with_slash) goto cleanup;
        destination = xx_str_concat(with_slash, name);
        xx_str_free(with_slash);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    /* The store helper deletes its own output on failure. */
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device, record->data_offset, record->compressed_size, destination, pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_sunvtoc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_sunvtoc_get_number_of_records(const xx_sunvtoc *sunvtoc)
{
    return sunvtoc ? sunvtoc->number_of_records : 0U;
}
uint32_t xx_sunvtoc_get_layout(const xx_sunvtoc *sunvtoc)
{
    return sunvtoc ? sunvtoc->layout : XX_SUNVTOC_LAYOUT_NONE;
}
int64_t xx_sunvtoc_get_archive_end(const xx_sunvtoc *sunvtoc)
{
    return sunvtoc ? sunvtoc->archive_end : -1;
}

bool xx_sunvtoc_get_slice_info(const xx_sunvtoc *sunvtoc, uint64_t index, xx_sunvtoc_slice_info *info)
{
    const xx_sunvtoc_private *parsed;
    const xx_sunvtoc_entry *entry;
    if (!sunvtoc || !info || !sunvtoc->internal) return false;
    parsed = (const xx_sunvtoc_private *)sunvtoc->internal;
    if (index >= parsed->count) return false;
    entry = &parsed->entries[index];
    info->offset = entry->data_offset;
    info->size = entry->data_size;
    info->declared_size = entry->declared_size;
    info->start_sector = entry->start_sector;
    info->sector_count = entry->sector_count;
    info->tag = entry->tag;
    info->flag = entry->flag;
    info->slot = entry->slot;
    info->name = entry->name;
    return true;
}
