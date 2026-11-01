/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format_streams.h"
#include "xx_zip_datastructs_internal.h"



typedef struct xx_zip_ds_name_entry {
    xx_zip_data_struct_id_t id;
    const char *name;
} xx_zip_ds_name_entry;

static const xx_zip_ds_name_entry _TABLE_XX_ZIP_DataStructNames[] = {
    {XX_ZIP_DS_UNKNOWN, "UNKNOWN"},
    {XX_ZIP_DS_EXTRA_FIELD, "EXTRA_FIELD"},
    {XX_ZIP_DS_LOCAL_FILE_HEADER, "LOCAL_FILE_HEADER"},
    {XX_ZIP_DS_DATA, "DATA"},
    {XX_ZIP_DS_DATA_DESCRIPTOR, "DATA_DESCRIPTOR"},
    {XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER, "CENTRAL_DIRECTORY_HEADER"},
    {XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY, "END_OF_CENTRAL_DIRECTORY"},
    {XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY, "ZIP64_END_OF_CENTRAL_DIRECTORY"},
    {XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR, "ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR"},
};

#define _XX_ZIP_DS_NAME_COUNT (sizeof(_TABLE_XX_ZIP_DataStructNames) / sizeof(_TABLE_XX_ZIP_DataStructNames[0]))

const char *xx_zip_data_struct_id_to_string(Abstractformat *self, uint32_t id)
{
    (void)self;
    for (size_t i = 0; i < _XX_ZIP_DS_NAME_COUNT; ++i) {
        if ((uint32_t)_TABLE_XX_ZIP_DataStructNames[i].id == id) {
            return _TABLE_XX_ZIP_DataStructNames[i].name;
        }
    }
    return "UNKNOWN";
}

uint32_t xx_zip_data_struct_string_to_id(Abstractformat *self, const char *name)
{
    (void)self;
    if (!name) {
        return (uint32_t)XX_ZIP_DS_UNKNOWN;
    }
    for (size_t i = 0; i < _XX_ZIP_DS_NAME_COUNT; ++i) {
        if (xx_str_cmp(name, _TABLE_XX_ZIP_DataStructNames[i].name) == 0) {
            return (uint32_t)_TABLE_XX_ZIP_DataStructNames[i].id;
        }
    }
    return (uint32_t)XX_ZIP_DS_UNKNOWN;
}

/* ========================================================================= */
/* --- Stream Data Structs Reading (headers/tables of the ZIP format)    --- */
/* ========================================================================= */

typedef struct xx_zip_ds_stream_state {
    xx_data_struct *list;
    size_t count;
    size_t capacity;
    bool failed;
} xx_zip_ds_stream_state;

static void xx_zip_ds_stream_state_free(void *ptr)
{
    if (ptr) {
        xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)ptr;
        if (dstate->list) {
            xx_mem_free(dstate->list);
        }
        xx_mem_free(dstate);
    }
}

static void xx_zip_ds_append(xx_zip_ds_stream_state *dstate, xx_zip_data_struct_id_t id, int64_t offset, int64_t address, int64_t entry_size, int64_t total_size,
                             uint64_t count, xx_data_struct_type_t type)
{
    if (dstate->failed) return;
    if (dstate->count == dstate->capacity) {
        if (dstate->capacity > SIZE_MAX / (2U * sizeof(*dstate->list))) { dstate->failed = true; return; }
        size_t capacity = dstate->capacity ? dstate->capacity * 2U : 8U;
        void *grown = xx_mem_realloc(dstate->list, capacity * sizeof(*dstate->list));
        if (!grown) { dstate->failed = true; return; }
        dstate->list = (xx_data_struct *)grown;
        dstate->capacity = capacity;
    }
    xx_data_struct *item = &dstate->list[dstate->count++];
    item->id = (uint32_t)id;
    item->offset = offset;
    item->address = address;
    item->entry_size = entry_size;
    item->total_size = total_size;
    item->count = count;
    item->type = type;
}

/* Extra fields in both local and central headers use the same bounded TLV envelope.
 * Unknown IDs remain named generic fields; a malformed suffix stays explicitly raw. */
static bool xx_zip_ds_extra_fields(Abstractformat *self, xx_zip_ds_stream_state *stream, int64_t offset,
                                   int64_t header_size, bool central, xx_pd_struct *pd)
{
    uint8_t header[46];
    int64_t fixed = central ? 46 : 30;
    if (offset < 0 || header_size < fixed || !xx_io_check(self->device, offset, header_size) ||
        !xx_io_read_at(self->device, offset, header, (size_t)fixed)) return false;
    uint16_t namesize = xx_data_get_u16(header, (size_t)fixed, central ? 28 : 26, false);
    uint16_t extra = xx_data_get_u16(header, (size_t)fixed, central ? 30 : 28, false);
    if (namesize > header_size - fixed || extra > header_size - fixed - namesize) return false;
    int64_t cursor = offset + fixed + namesize, end = cursor + extra;
    while (cursor < end) {
        uint8_t item[4];
        int64_t size = 0;
        if (xx_pd_is_stopped(pd)) return false;
        if (end - cursor >= 4) {
            if (!xx_io_read_at(self->device, cursor, item, sizeof(item))) return false;
            size = 4 + xx_data_get_u16(item, sizeof(item), 2, false);
        }
        if (size < 4 || size > end - cursor) {
            xx_zip_ds_append(stream, (xx_zip_data_struct_id_t)XX_DATA_STRUCT_ID_RAW_DATA, cursor,
                             self->is_mapped ? cursor : -1, end - cursor, end - cursor, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
            break;
        }
        xx_zip_ds_append(stream, XX_ZIP_DS_EXTRA_FIELD, cursor, self->is_mapped ? cursor : -1,
                         size, size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
        if (stream->failed) return false;
        cursor += size;
    }
    return !stream->failed;
}

xx_data_struct_state *xx_zip_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || !self->device || !xx_format_handle_split_format(self, pd)) {
        return NULL;
    }
    if (!self->base_info_handled) {
        if (!xx_format_handle_base_info(self, pd)) {
            return NULL;
        }
    }
    if (!self->is_valid) {
        return NULL;
    }

    xx_zip *zip = (xx_zip *)self;

    if (zip->number_of_records > ((SIZE_MAX / sizeof(xx_data_struct)) - 4) / 4) return NULL;
    xx_data_struct_state *state = (xx_data_struct_state *)xx_mem_alloc(sizeof(xx_data_struct_state));
    if (!state) {
        return NULL;
    }
    xx_data_struct_state_init(state, self);

    /* Each entry has a gap, LFH, payload and its own central header. */
    size_t capacity = (size_t)(4 * zip->number_of_records) + 4;
    xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)xx_mem_alloc(sizeof(xx_zip_ds_stream_state));
    if (!dstate) {
        xx_data_struct_state_free(state);
        return NULL;
    }
    dstate->count = 0;
    dstate->capacity = capacity;
    dstate->failed = false;
    dstate->list = (xx_data_struct *)xx_mem_alloc(capacity * sizeof(xx_data_struct));
    if (!dstate->list) {
        xx_mem_free(dstate);
        xx_data_struct_state_free(state);
        return NULL;
    }

    bool is_mapped = self->is_mapped;
    int64_t dev_size = xx_io_total_size(self->device);

    /* Walk the central directory to recover each entry's local header + payload location,
     * emitting LOCAL_FILE_HEADER/DATA pairs in physical order, with RAW_DATA gaps between them */
    int64_t expected_next = self->base_address;
    int64_t cd_curr_offset = zip->cd_offset;

    for (uint64_t i = 0; i < zip->number_of_records && cd_curr_offset >= 0; ++i) {
        xx_archive_record rec;
        int64_t cd_entry_size = 0;
        xx_archive_record_init(&rec);
        if (xx_pd_is_stopped(pd) || !xx_zip_parse_cd_entry(zip, self->device, dev_size, cd_curr_offset, &rec, &cd_entry_size)) {
            xx_archive_record_cleanup(&rec);
            goto data_structs_fail;
        }
        cd_curr_offset += cd_entry_size;
        (void)xx_zip_resolve_record_data_offset(zip, &rec);

        if (rec.header_offset >= 0) {
            if (rec.header_offset > expected_next) {
                int64_t gap_off = expected_next;
                int64_t gap_size = rec.header_offset - expected_next;
                int64_t gap_addr = is_mapped ? gap_off : -1;
                xx_zip_ds_append(dstate, (xx_zip_data_struct_id_t)XX_DATA_STRUCT_ID_RAW_DATA, gap_off, gap_addr, gap_size, gap_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
            }

            int64_t lfh_addr = is_mapped ? rec.header_offset : -1;
            xx_zip_ds_append(dstate, XX_ZIP_DS_LOCAL_FILE_HEADER, rec.header_offset, lfh_addr, rec.header_size, rec.header_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
            if (!xx_zip_ds_extra_fields(self, dstate, rec.header_offset, rec.header_size, false, pd)) {
                xx_archive_record_cleanup(&rec);
                goto data_structs_fail;
            }

            if (rec.data_offset >= 0) {
                int64_t data_addr = is_mapped ? rec.data_offset : -1;
                xx_zip_ds_append(dstate, XX_ZIP_DS_DATA, rec.data_offset, data_addr, rec.compressed_size, rec.compressed_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
                expected_next = rec.data_offset + rec.compressed_size;
            } else {
                expected_next = rec.header_offset + rec.header_size;
            }
        }

        xx_archive_record_cleanup(&rec);
    }

    /* Gap between the last record's payload and the start of the central directory */
    if (zip->cd_offset >= 0 && expected_next < zip->cd_offset) {
        int64_t gap_off = expected_next;
        int64_t gap_size = zip->cd_offset - expected_next;
        int64_t gap_addr = is_mapped ? gap_off : -1;
        xx_zip_ds_append(dstate, (xx_zip_data_struct_id_t)XX_DATA_STRUCT_ID_RAW_DATA, gap_off, gap_addr, gap_size, gap_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
    }

    cd_curr_offset = zip->cd_offset;
    for (uint64_t i = 0; i < zip->number_of_records && cd_curr_offset >= 0; ++i) {
        xx_archive_record rec;
        int64_t cd_entry_size = 0;
        xx_archive_record_init(&rec);
        if (xx_pd_is_stopped(pd) || !xx_zip_parse_cd_entry(zip, self->device, dev_size, cd_curr_offset, &rec, &cd_entry_size)) {
            xx_archive_record_cleanup(&rec);
            xx_zip_ds_stream_state_free(dstate);
            xx_data_struct_state_free(state);
            return NULL;
        }
        xx_archive_record_cleanup(&rec);
        xx_zip_ds_append(dstate, XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER, cd_curr_offset, is_mapped ? cd_curr_offset : -1,
                         cd_entry_size, cd_entry_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
        if (!xx_zip_ds_extra_fields(self, dstate, cd_curr_offset, cd_entry_size, true, pd)) goto data_structs_fail;
        cd_curr_offset += cd_entry_size;
    }

    if (zip->is_zip64) {
        int64_t locator_offset = zip->eocd_offset - 20;
        if (zip->eocd_offset >= 0 && locator_offset >= 0) {
            uint32_t loc_sig = xx_io_get_u32(self->device, locator_offset, false);
            if (loc_sig == XX_ZIP_ZIP64_EOCD_LOCATOR_SIGNATURE) {
                int64_t addr = is_mapped ? locator_offset : -1;
                xx_zip_ds_append(dstate, XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR, locator_offset, addr, sizeof(xx_zip_zip64_locator_t),
                                 sizeof(xx_zip_zip64_locator_t), 1, XX_DATA_STRUCT_TYPE_LOCATOR);

                int64_t zip64_eocd_offset = zip->zip64_eocd_offset;
                if (zip64_eocd_offset >= 0) {
                    int64_t addr64 = is_mapped ? zip64_eocd_offset : -1;
                    xx_zip_ds_append(dstate, XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY, zip64_eocd_offset, addr64, sizeof(xx_zip_zip64_eocd_t), zip->zip64_eocd_size, 1,
                                     XX_DATA_STRUCT_TYPE_STRUCT);
                }
            }
        }
    }

    if (zip->eocd_offset >= 0) {
        int64_t addr = is_mapped ? zip->eocd_offset : -1;
        int64_t eocd_total_size = (self->format_size >= zip->eocd_offset) ? (self->format_size - zip->eocd_offset) : (int64_t)sizeof(xx_zip_eocd_t);
        xx_zip_ds_append(dstate, XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY, zip->eocd_offset, addr, sizeof(xx_zip_eocd_t), eocd_total_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
    }

    if (dstate->failed) goto data_structs_fail;
    state->internal_state = dstate;
    state->free_internal = xx_zip_ds_stream_state_free;
    state->total_structs = (int64_t)dstate->count;
    state->current_index = -1;
    state->has_struct = false;

    if (dstate->count > 0) {
        state->current_struct = dstate->list[0];
        state->has_struct = true;
        state->current_index = 0;
    }

    return state;
data_structs_fail:
    xx_zip_ds_stream_state_free(dstate);
    xx_data_struct_state_free(state);
    return NULL;
}

const xx_data_struct *xx_zip_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state)
{
    (void)self;
    if (!state || !state->has_struct) {
        return NULL;
    }
    return &state->current_struct;
}

bool xx_zip_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd)
{
    (void)self;
    (void)pd;
    if (!state || !state->internal_state) {
        return false;
    }
    xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)state->internal_state;

    int64_t next_index = state->current_index + 1;
    if (next_index < 0 || (size_t)next_index >= dstate->count) {
        state->has_struct = false;
        return false;
    }

    state->current_struct = dstate->list[next_index];
    state->current_index = next_index;
    state->has_struct = true;
    return true;
}

void xx_zip_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state)
{
    (void)self;
    xx_data_struct_state_free(state);
}

/* ========================================================================= */
/* --- Stream Data Struct Records Reading (named fields of a STRUCT)     --- */
/* ========================================================================= */

/* Fixed-layout ZIP struct field descriptors (uses global xx_data_struct_field_desc / xx_zip_field_desc) */
static const xx_data_struct_field_desc _TABLE_XX_ZIP_Fields_LocalFileHeader[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version_needed", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"flags", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"compression_method", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"last_mod_time", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"last_mod_date", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"crc32", L"uint32", 14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM},
    {L"compressed_size", L"uint32", 18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"uncompressed_size", L"uint32", 22, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"filename_length", L"uint16", 26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"extra_field_length", L"uint16", 28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
};

static const xx_zip_field_desc _TABLE_XX_ZIP_Fields_CentralDirectoryHeader[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version_made_by", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"version_needed", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"flags", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"compression_method", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"last_mod_time", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"last_mod_date", L"uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"crc32", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM},
    {L"compressed_size", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"uncompressed_size", L"uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"filename_length", L"uint16", 28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"extra_field_length", L"uint16", 30, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"comment_length", L"uint16", 32, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"disk_number_start", L"uint16", 34, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"internal_attrs", L"uint16", 36, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"external_attrs", L"uint32", 38, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"relative_offset_local_header", L"uint32", 42, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
};

static const xx_zip_field_desc _TABLE_XX_ZIP_Fields_Eocd[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},         {L"disk_number", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"cd_start_disk", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},   {L"records_on_disk", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"total_records", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT}, {L"cd_size", L"uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"cd_offset", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},   {L"comment_length", L"uint16", 20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
};

static const xx_zip_field_desc _TABLE_XX_ZIP_Fields_Zip64Locator[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"disk_with_zip64_eocd", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"zip64_eocd_offset", L"uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
    {L"total_disks", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
};

static const xx_zip_field_desc _TABLE_XX_ZIP_Fields_Zip64Eocd[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},           {L"record_size", L"uint64", 4, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"version_made_by", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},  {L"version_needed", L"uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"disk_number", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},      {L"cd_start_disk", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"records_on_disk", L"uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT}, {L"total_records", L"uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"cd_size", L"uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},          {L"cd_offset", L"uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
};

#define _XX_ZIP_FIELDS_COUNT(table) (sizeof(table) / sizeof((table)[0]))

typedef struct xx_zip_record_stream_state {
    xx_zip_field_desc table[24];
    size_t count;
} xx_zip_record_stream_state;

static void xx_zip_record_stream_state_free(void *ptr)
{
    if (ptr) {
        xx_mem_free(ptr);
    }
}

/* Reads the field's raw value from the device and populates name/type/value/display_value */
static bool xx_zip_populate_record(xx_io_device *device, const xx_data_struct *ds, const xx_data_struct_field_desc *field, xx_data_struct_record *rec)
{
    if (!ds || field->rel_offset < 0 || field->size < 0 || ds->total_size < 0 || field->rel_offset > ds->total_size ||
        field->size > ds->total_size - field->rel_offset) return false;
    return xx_data_struct_record_populate(rec, device, ds->offset, field, false);
}

static const xx_zip_field_desc g_xx_zip_extra_fields[] = {
    {L"header_id", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"data_size", L"uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE}
};

xx_data_struct_record_state *xx_zip_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd)
{
    if (!self || !self->device || !ds || !xx_format_handle_split_format(self, pd)) {
        return NULL;
    }
    /* Only structured records have a known fixed field layout; raw data regions have none */
    if (ds->type == XX_DATA_STRUCT_TYPE_RAW_DATA) {
        return NULL;
    }

    const xx_zip_field_desc *table = NULL;
    size_t table_count = 0;

    switch ((xx_zip_data_struct_id_t)ds->id) {
        case XX_ZIP_DS_LOCAL_FILE_HEADER:
            table = _TABLE_XX_ZIP_Fields_LocalFileHeader;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XX_ZIP_Fields_LocalFileHeader);
            break;
        case XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER:
            table = _TABLE_XX_ZIP_Fields_CentralDirectoryHeader;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XX_ZIP_Fields_CentralDirectoryHeader);
            break;
        case XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY:
            table = _TABLE_XX_ZIP_Fields_Eocd;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XX_ZIP_Fields_Eocd);
            break;
        case XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR:
            table = _TABLE_XX_ZIP_Fields_Zip64Locator;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XX_ZIP_Fields_Zip64Locator);
            break;
        case XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY:
            table = _TABLE_XX_ZIP_Fields_Zip64Eocd;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XX_ZIP_Fields_Zip64Eocd);
            break;
        case XX_ZIP_DS_EXTRA_FIELD:
            table = g_xx_zip_extra_fields;
            table_count = _XX_ZIP_FIELDS_COUNT(g_xx_zip_extra_fields);
            break;
        default: return NULL;
    }

    xx_data_struct_record_state *state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(xx_data_struct_record_state));
    if (!state) {
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);

    xx_zip_record_stream_state *rstate = (xx_zip_record_stream_state *)xx_mem_alloc(sizeof(xx_zip_record_stream_state));
    if (!rstate) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    rstate->count = table_count;
    for (size_t index = 0U; index < table_count; ++index) rstate->table[index] = table[index];
    /* Variable fields are part of each header occurrence, never the next one. */
    int64_t fixed_size = table[table_count - 1U].rel_offset + table[table_count - 1U].size;
    uint16_t lengths[3] = {0, 0, 0};
    const wchar_t *names[3] = {L"filename", L"extra_field", L"comment"};
    if (ds->offset < 0 || ds->total_size < fixed_size || !xx_io_check(self->device, ds->offset, ds->total_size)) {
        xx_mem_free(rstate);
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    if (ds->id == XX_ZIP_DS_EXTRA_FIELD) {
        uint8_t length[2];
        if (!xx_io_read_at(self->device, ds->offset + 2, length, sizeof(length)) ||
            xx_data_get_u16(length, sizeof(length), 0, false) != ds->total_size - fixed_size) {
            xx_mem_free(rstate);
            xx_data_struct_record_state_free(state);
            return NULL;
        }
        if (ds->total_size > fixed_size) {
            rstate->table[rstate->count++] = (xx_zip_field_desc){L"data", L"bytes", fixed_size,
                ds->total_size - fixed_size, XX_DATA_STRUCT_RECORD_PROPERTY_NONE};
        }
    } else if (ds->id == XX_ZIP_DS_LOCAL_FILE_HEADER) {
        lengths[0] = xx_io_get_u16(self->device, ds->offset + 26, false);
        lengths[1] = xx_io_get_u16(self->device, ds->offset + 28, false);
    } else if (ds->id == XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER) {
        lengths[0] = xx_io_get_u16(self->device, ds->offset + 28, false);
        lengths[1] = xx_io_get_u16(self->device, ds->offset + 30, false);
        lengths[2] = xx_io_get_u16(self->device, ds->offset + 32, false);
    } else if (ds->id == XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY) {
        lengths[2] = xx_io_get_u16(self->device, ds->offset + 20, false);
    }
    for (size_t index = 0U; index < 3U; ++index) {
        if (lengths[index] == 0U) continue;
        if (fixed_size > ds->total_size || lengths[index] > ds->total_size - fixed_size) {
            xx_mem_free(rstate);
            xx_data_struct_record_state_free(state);
            return NULL;
        }
        xx_zip_field_desc *field = &rstate->table[rstate->count++];
        field->name = names[index];
        field->type = index == 1U ? L"uint8[]" : L"char[]";
        field->rel_offset = fixed_size;
        field->size = lengths[index];
        field->property = index == 1U ? XX_DATA_STRUCT_RECORD_PROPERTY_NONE : XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
        fixed_size += lengths[index];
    }
    if (ds->id == XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY && ds->total_size > fixed_size) {
        xx_zip_field_desc *field = &rstate->table[rstate->count++];
        field->name = L"extensible_data_sector";
        field->type = L"uint8[]";
        field->rel_offset = fixed_size;
        field->size = ds->total_size - fixed_size;
        field->property = XX_DATA_STRUCT_RECORD_PROPERTY_NONE;
    }

    state->internal_state = rstate;
    state->free_internal = xx_zip_record_stream_state_free;
    state->total_records = (int64_t)rstate->count;
    state->current_index = -1;
    state->has_record = false;

    if (rstate->count > 0) {
        if (!xx_zip_populate_record(self->device, ds, &rstate->table[0], &state->current_record)) {
            xx_data_struct_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }

    return state;
}

const xx_data_struct_record *xx_zip_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state)
{
    (void)self;
    if (!state || !state->has_record) {
        return NULL;
    }
    return &state->current_record;
}

bool xx_zip_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    if (!self || !self->device || !state || state->format != self || !state->has_record || !state->internal_state || xx_pd_is_stopped(pd)) {
        if (state) state->has_record = false;
        return false;
    }
    xx_zip_record_stream_state *rstate = (xx_zip_record_stream_state *)state->internal_state;

    int64_t next_index = state->current_index + 1;
    if (next_index < 0 || (size_t)next_index >= rstate->count) {
        state->has_record = false;
        return false;
    }

    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_zip_populate_record(self->device, &state->parent_struct, &rstate->table[next_index], &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next_index;
    state->has_record = true;

    return true;
}

void xx_zip_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state)
{
    (void)self;
    xx_data_struct_record_state_free(state);
}


const StreamDataStructs *xx_zip_get_stream_datastructs(void)
{
    static const StreamDataStructs stream = {
        .data_struct_id_to_string = xx_zip_data_struct_id_to_string,
        .data_struct_string_to_id = xx_zip_data_struct_string_to_id,
        .create_data_structs_reading = xx_zip_create_data_structs_reading,
        .get_current_data_struct = xx_zip_get_current_data_struct,
        .data_struct_move_to_next = xx_zip_data_struct_move_to_next,
        .free_data_structs_reading = xx_zip_free_data_structs_reading,
        .create_data_struct_records_reading = xx_zip_create_data_struct_records_reading,
        .get_current_data_struct_record = xx_zip_get_current_data_struct_record,
        .data_struct_record_move_to_next = xx_zip_data_struct_record_move_to_next,
        .free_data_struct_records_reading = xx_zip_free_data_struct_records_reading,
    };
    return &stream;
}
