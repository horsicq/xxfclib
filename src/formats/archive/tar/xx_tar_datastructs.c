/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format_streams.h"
#include "xx_tar_data.h"
#include "xx_tar_datastructs_internal.h"

typedef struct xx_tar_ds_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_tar_ds_stream;

typedef struct xx_tar_record_stream_s {
    const xx_data_struct_field_desc *fields;
    xx_data_struct_field_desc marker;
    size_t count;
} xx_tar_record_stream;

typedef struct xx_tar_ds_name_s {
    xx_tar_data_struct_id_t id;
    const char *name;
} xx_tar_ds_name;

static const xx_tar_ds_name xx_tar_ds_names[] = {
    {XX_TAR_DS_UNKNOWN, "UNKNOWN"}, {XX_TAR_DS_HEADER, "HEADER"}, {XX_TAR_DS_DATA, "DATA"}, {XX_TAR_DS_PADDING, "PADDING"}, {XX_TAR_DS_END_MARKERS, "END_MARKERS"}};

const char *xx_tar_data_struct_id_to_string(Abstractformat *self, uint32_t id)
{
    size_t i;
    (void)self;
    for (i = 0U; i < sizeof(xx_tar_ds_names) / sizeof(xx_tar_ds_names[0]); ++i) {
        if ((uint32_t)xx_tar_ds_names[i].id == id) return xx_tar_ds_names[i].name;
    }
    return "UNKNOWN";
}

uint32_t xx_tar_data_struct_string_to_id(Abstractformat *self, const char *name)
{
    size_t i;
    (void)self;
    if (!name) return XX_TAR_DS_UNKNOWN;
    for (i = 0U; i < sizeof(xx_tar_ds_names) / sizeof(xx_tar_ds_names[0]); ++i) {
        if (xx_str_cmp(name, xx_tar_ds_names[i].name) == 0) return (uint32_t)xx_tar_ds_names[i].id;
    }
    return XX_TAR_DS_UNKNOWN;
}

static void xx_tar_ds_stream_free(void *pointer)
{
    xx_tar_ds_stream *stream = (xx_tar_ds_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void xx_tar_set_ds(xx_data_struct *item, uint32_t id, int64_t offset, int64_t size, xx_data_struct_type_t type, bool mapped)
{
    item->id = id;
    item->offset = offset;
    item->address = mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_tar_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_private *priv;
    xx_data_struct_state *state;
    xx_tar_ds_stream *stream;
    size_t capacity;
    size_t i;
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    priv = (xx_tar_private *)((xx_tar *)self)->internal;
    if (!priv || priv->count > (SIZE_MAX - 1U) / 3U) return NULL;
    capacity = priv->count * 3U + 1U;
    if (capacity > SIZE_MAX / sizeof(xx_data_struct)) return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_ds_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_state_init(state, self);
    stream->items = (xx_data_struct *)xx_mem_alloc(capacity * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        return NULL;
    }
    for (i = 0U; i < priv->count; ++i) {
        const xx_tar_member *member = &priv->members[i];
        int64_t padding = member->padded_size - member->data_size;
        xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_HEADER, member->header_offset, XX_TAR_BLOCK_SIZE, XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        if (member->data_size != 0) {
            xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_DATA, member->data_offset, member->data_size, XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if (padding != 0) {
            xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_PADDING, member->data_offset + member->data_size, padding, XX_DATA_STRUCT_TYPE_RAW_DATA,
                          self->is_mapped);
        }
    }
    xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_END_MARKERS, priv->end_marker_offset, priv->archive_end - priv->end_marker_offset,
                  XX_DATA_STRUCT_TYPE_FOOTER, self->is_mapped);
    state->internal_state = stream;
    state->free_internal = xx_tar_ds_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_index = 0;
    state->has_struct = stream->count != 0U;
    if (state->has_struct) state->current_struct = stream->items[0];
    return state;
}

const xx_data_struct *xx_tar_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state)
{
    return self && state && state->format == self && state->has_struct ? &state->current_struct : NULL;
}

bool xx_tar_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd)
{
    xx_tar_ds_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_struct || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_ds_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = stream->items[next];
    return true;
}

void xx_tar_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state)
{
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_tar_header_fields[] = {{L"name", L"char[100]", 0, 100, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"mode", L"char[8]", 100, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
                                                                 {L"uid", L"char[8]", 108, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"gid", L"char[8]", 116, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"size", L"char[12]", 124, 12, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
                                                                 {L"mtime", L"char[12]", 136, 12, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
                                                                 {L"checksum", L"char[8]", 148, 8, XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM},
                                                                 {L"typeflag", L"char", 156, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"linkname", L"char[100]", 157, 100, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"magic", L"char[6]", 257, 6, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"version", L"char[2]", 263, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"uname", L"char[32]", 265, 32, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"gname", L"char[32]", 297, 32, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"devmajor", L"char[8]", 329, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"devminor", L"char[8]", 337, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"prefix", L"char[155]", 345, 155, XX_DATA_STRUCT_RECORD_PROPERTY_STRING}};

static void xx_tar_record_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

static bool xx_tar_populate_field(xx_io_device *device, const xx_data_struct *parent, const xx_data_struct_field_desc *field, xx_data_struct_record *record)
{
    char *text;
    wchar_t *display;
    size_t length;
    int64_t total_size;
    if (device && parent && field && record && field->type && xx_rt_wcscmp(field->type, L"uint8[]") == 0)
        return xx_data_struct_record_populate(record, device, parent->offset, field, false);
    if (!device || !parent || !field || !record || field->size < 0 || (uint64_t)field->size > (uint64_t)(SIZE_MAX - 1U)) {
        return false;
    }
    total_size = xx_io_total_size(device);
    if (parent->offset < 0 || field->rel_offset < 0 || parent->offset > INT64_MAX - field->rel_offset || parent->offset + field->rel_offset > total_size ||
        field->size > total_size - (parent->offset + field->rel_offset)) {
        return false;
    }
    length = (size_t)field->size;
    text = (char *)xx_mem_alloc(length + 1U);
    if (!text || !xx_tar_read_exact_at(device, parent->offset + field->rel_offset, text, length)) {
        if (text) xx_mem_free(text);
        return false;
    }
    while (length > 0U && (text[length - 1U] == ' ' || text[length - 1U] == '\0')) {
        --length;
    }
    text[length] = '\0';
    display = xx_str_utf8_to_unicode(text);
    if (!display) {
        xx_mem_free(text);
        return false;
    }
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (!xx_var_set_str(&record->value, text) || !xx_data_struct_record_set_name(record, field->name) || !xx_data_struct_record_set_type(record, field->type) ||
        !xx_data_struct_record_set_raw_bytes(record, device, parent->offset + field->rel_offset, field->size) ||
        !xx_data_struct_record_set_display_value(record, display)) {
        xx_str_wfree(display);
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    xx_str_wfree(display);
    xx_mem_free(text);
    return true;
}

xx_data_struct_record_state *xx_tar_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd)
{
    xx_data_struct_record_state *state;
    xx_tar_record_stream *stream;
    size_t count;
    if (!self || !self->device || !ds || xx_pd_is_stopped(pd) ||
        (ds->id != XX_TAR_DS_HEADER && ds->id != XX_TAR_DS_END_MARKERS) || ds->offset < 0 || ds->total_size <= 0 ||
        !xx_io_check(self->device, ds->offset, ds->total_size) || (ds->id == XX_TAR_DS_HEADER && ds->total_size < XX_TAR_BLOCK_SIZE)) {
        return NULL;
    }
    count = ds->id == XX_TAR_DS_HEADER ? sizeof(xx_tar_header_fields) / sizeof(xx_tar_header_fields[0]) : 1U;
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);
    stream->fields = xx_tar_header_fields;
    if (ds->id == XX_TAR_DS_END_MARKERS) {
        stream->marker.name = L"zero_blocks";
        stream->marker.type = L"uint8[]";
        stream->marker.rel_offset = 0;
        stream->marker.size = ds->total_size;
        stream->marker.property = XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED;
        stream->fields = &stream->marker;
    }
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_tar_record_stream_free;
    state->total_records = (int64_t)count;
    if (!xx_tar_populate_field(self->device, ds, &stream->fields[0], &state->current_record)) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_data_struct_record *xx_tar_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_tar_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    xx_tar_record_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_tar_populate_field(self->device, &state->parent_struct, &stream->fields[next], &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

void xx_tar_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state)
{
    (void)self;
    xx_data_struct_record_state_free(state);
}


const StreamDataStructs *xx_tar_get_stream_datastructs(void)
{
    static const StreamDataStructs stream = {
        .data_struct_id_to_string = xx_tar_data_struct_id_to_string,
        .data_struct_string_to_id = xx_tar_data_struct_string_to_id,
        .create_data_structs_reading = xx_tar_create_data_structs_reading,
        .get_current_data_struct = xx_tar_get_current_data_struct,
        .data_struct_move_to_next = xx_tar_data_struct_move_to_next,
        .free_data_structs_reading = xx_tar_free_data_structs_reading,
        .create_data_struct_records_reading = xx_tar_create_data_struct_records_reading,
        .get_current_data_struct_record = xx_tar_get_current_data_struct_record,
        .data_struct_record_move_to_next = xx_tar_data_struct_record_move_to_next,
        .free_data_struct_records_reading = xx_tar_free_data_struct_records_reading,
    };
    return &stream;
}
