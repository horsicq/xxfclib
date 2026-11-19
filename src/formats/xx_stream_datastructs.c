/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

void xx_data_struct_state_init(xx_data_struct_state *state, Abstractformat *fmt)
{
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_state));
    state->format = fmt;
    state->has_struct = false;
    state->current_index = -1;
    state->total_structs = -1;
}

void xx_data_struct_state_cleanup(xx_data_struct_state *state)
{
    if (!state) {
        return;
    }
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_state));
    state->current_index = -1;
    state->total_structs = -1;
}

void xx_data_struct_state_free(xx_data_struct_state *state)
{
    if (!state) {
        return;
    }
    xx_data_struct_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_state *xx_format_create_data_structs_reading(Abstractformat *f, xx_pd_struct *pd)
{
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        /* Physical structures can remain readable when full format parsing fails. */
        xx_format_handle_base_info(f, pd);
    }
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (stream && stream->create_data_structs_reading) {
        return (stream->create_data_structs_reading)(f, pd);
    }
    return NULL;
}

const xx_data_struct *xx_format_get_current_data_struct(Abstractformat *f, xx_data_struct_state *state)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return NULL;
    }
    if (stream && stream->get_current_data_struct) {
        return (stream->get_current_data_struct)(f, state);
    }
    return state->has_struct ? &state->current_struct : NULL;
}

bool xx_format_data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return false;
    }
    if (stream && stream->data_struct_move_to_next) {
        return (stream->data_struct_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return;
    }
    if (stream && stream->free_data_structs_reading) {
        (stream->free_data_structs_reading)(f, state);
    } else {
        xx_data_struct_state_free(state);
    }
}

/* ========================================================================= */
/* --- Data Struct Record Lifecycle                                       --- */
/* ========================================================================= */

void xx_data_struct_record_init(xx_data_struct_record *rec)
{
    if (!rec) {
        return;
    }
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
    rec->property = XX_DATA_STRUCT_RECORD_PROPERTY_NONE;
    xx_var_init(&rec->value);
}

void xx_data_struct_record_cleanup(xx_data_struct_record *rec)
{
    if (!rec) {
        return;
    }
    if (rec->name) {
        xx_str_wfree(rec->name);
    }
    if (rec->type) {
        xx_str_wfree(rec->type);
    }
    if (rec->raw_value) {
        xx_str_wfree(rec->raw_value);
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    if (rec->comment) {
        xx_str_wfree(rec->comment);
    }
    if (rec->description) {
        xx_str_wfree(rec->description);
    }
    xx_var_cleanup(&rec->value);
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
}

void xx_data_struct_record_free_elem(void *element)
{
    if (element) {
        xx_data_struct_record_cleanup((xx_data_struct_record *)element);
    }
}

bool xx_data_struct_record_set_name(xx_data_struct_record *rec, const wchar_t *name)
{
    if (!rec) {
        return false;
    }
    wchar_t *copy = name ? xx_str_wdup(name) : NULL;
    if (name && !copy) {
        return false;
    }
    if (rec->name) {
        xx_str_wfree(rec->name);
    }
    rec->name = copy;
    return true;
}

bool xx_data_struct_record_set_type(xx_data_struct_record *rec, const wchar_t *type)
{
    if (!rec) {
        return false;
    }
    wchar_t *copy = type ? xx_str_wdup(type) : NULL;
    if (type && !copy) {
        return false;
    }
    if (rec->type) {
        xx_str_wfree(rec->type);
    }
    rec->type = copy;
    return true;
}

bool xx_data_struct_record_set_value(xx_data_struct_record *rec, const xx_var *value)
{
    if (!rec || !value) {
        return false;
    }
    return xx_var_copy(&rec->value, value);
}

bool xx_data_struct_record_set_raw_value(xx_data_struct_record *rec, const wchar_t *raw_value)
{
    wchar_t *copy;
    if (!rec) {
        return false;
    }
    copy = raw_value ? xx_str_wdup(raw_value) : NULL;
    if (raw_value && !copy) {
        return false;
    }
    if (rec->raw_value) {
        xx_str_wfree(rec->raw_value);
    }
    rec->raw_value = copy;
    return true;
}

bool xx_data_struct_record_set_raw_uint(xx_data_struct_record *rec, uint64_t value, int64_t size)
{
    static const wchar_t digits[] = L"0123456789ABCDEF";
    wchar_t raw[19];
    size_t digit_count;
    size_t index;
    if (!rec || size <= 0 || size > 8) {
        return false;
    }
    digit_count = (size_t)size * 2U;
    raw[0] = L'0';
    raw[1] = L'x';
    for (index = 0U; index < digit_count; ++index) {
        size_t shift = (digit_count - index - 1U) * 4U;
        raw[index + 2U] = digits[(value >> shift) & UINT64_C(0x0F)];
    }
    raw[digit_count + 2U] = L'\0';
    return xx_data_struct_record_set_raw_value(rec, raw);
}

/* Bytes shown at each end of a byte string that is too long to show whole. */
#define XX_DATA_STRUCT_RAW_EDGE_BYTES 16U

static bool g_xx_data_struct_raw_bytes_full;

void xx_data_struct_set_raw_bytes_full(bool full)
{
    g_xx_data_struct_raw_bytes_full = full;
}

/* A byte string is not a number: it is shown as its bytes in file order,
 * without a 0x prefix ("2E74657874000000"), and one longer than two edges
 * keeps only its first and last bytes ("AAAA....AAAA"). */
bool xx_data_struct_record_set_raw_bytes(xx_data_struct_record *rec, xx_io_device *device, int64_t offset, int64_t size)
{
    static const wchar_t digits[] = L"0123456789ABCDEF";
    wchar_t *raw;
    size_t byte_count;
    size_t char_count;
    size_t head, tail, position = 0U;
    size_t index;
    bool full = g_xx_data_struct_raw_bytes_full;
    if (!rec || !device || offset < 0 || size < 0 || !xx_io_check(device, offset, size) ||
        (uint64_t)size > (uint64_t)(SIZE_MAX - 3U) / 2U) {
        return false;
    }
    byte_count = (size_t)size;
    head = byte_count;
    tail = 0U;
    if (!full && byte_count > 2U * XX_DATA_STRUCT_RAW_EDGE_BYTES) head = tail = XX_DATA_STRUCT_RAW_EDGE_BYTES;
    char_count = (head + tail) * 2U + (tail ? 4U : 0U) + (full ? 2U : 0U) + 1U;
    if (char_count > SIZE_MAX / sizeof(*raw)) {
        return false;
    }
    raw = (wchar_t *)xx_mem_alloc(char_count * sizeof(*raw));
    if (!raw) {
        return false;
    }
    if (full) {
        raw[position++] = L'0';
        raw[position++] = L'x';
    }
    for (index = 0U; index < head; ++index) {
        uint8_t byte = xx_io_get_u8(device, offset + (int64_t)index);
        raw[position++] = digits[byte >> 4U];
        raw[position++] = digits[byte & 0x0FU];
    }
    if (tail) {
        for (index = 0U; index < 4U; ++index) raw[position++] = L'.';
        for (index = byte_count - tail; index < byte_count; ++index) {
            uint8_t byte = xx_io_get_u8(device, offset + (int64_t)index);
            raw[position++] = digits[byte >> 4U];
            raw[position++] = digits[byte & 0x0FU];
        }
    }
    raw[position] = L'\0';
    if (rec->raw_value) {
        xx_str_wfree(rec->raw_value);
    }
    rec->raw_value = raw;
    return true;
}

static bool xx_data_struct_record_format_location(const xx_data_struct_record *rec, wchar_t display[32])
{
    static const wchar_t digits[] = L"0123456789ABCDEF";
    wchar_t reversed[16];
    uint64_t magnitude;
    bool negative = false;
    size_t width = 0U;
    size_t count = 0U;
    size_t position = 0U;

    if ((rec->property & (XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS | XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET |
                          XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)) == 0U ||
        (rec->type && xx_rt_wcschr(rec->type, L'[')))
        return false;

    /* Use the decoded value: raw bytes may contain a whole packed bitfield. */
    switch (xx_var_get_type(&rec->value)) {
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(&rec->value);
            negative = value < 0;
            /* Unsigned subtraction also handles INT64_MIN without overflow. */
            magnitude = negative ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
            break;
        }
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64:
            magnitude = xx_var_get_u64(&rec->value);
            break;
        default:
            return false;
    }

    /* Prefer the logical type width to the size of its containing bitfield. */
    if (rec->type) {
        if (xx_rt_wcscmp(rec->type, L"uint8") == 0 || xx_rt_wcscmp(rec->type, L"int8") == 0) width = 2U;
        else if (xx_rt_wcscmp(rec->type, L"uint16") == 0 || xx_rt_wcscmp(rec->type, L"int16") == 0) width = 4U;
        else if (xx_rt_wcscmp(rec->type, L"uint32") == 0 || xx_rt_wcscmp(rec->type, L"int32") == 0) width = 8U;
        else if (xx_rt_wcscmp(rec->type, L"uint64") == 0 || xx_rt_wcscmp(rec->type, L"int64") == 0) width = 16U;
    }
    if (width == 0U && rec->size > 0 && rec->size <= 8) width = (size_t)rec->size * 2U;
    do {
        reversed[count++] = digits[magnitude & UINT64_C(0xf)];
        magnitude >>= 4U;
    } while (magnitude != 0U);

    if (negative) display[position++] = L'-';
    display[position++] = L'0';
    display[position++] = L'x';
    while (width > count) {
        display[position++] = L'0';
        --width;
    }
    while (count != 0U) display[position++] = reversed[--count];
    display[position] = L'\0';
    return true;
}


bool xx_data_struct_record_set_display_value(xx_data_struct_record *rec, const wchar_t *display_value)
{
    wchar_t location_display[32];
    wchar_t *copy;
    if (!rec) {
        return false;
    }
    if (display_value && xx_data_struct_record_format_location(rec, location_display)) {
        display_value = location_display;
    }
    copy = display_value ? xx_str_wdup(display_value) : NULL;
    if (display_value && !copy) {
        return false;
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    rec->display_value = copy;
    return true;
}

bool xx_data_struct_record_set_comment(xx_data_struct_record *rec, const wchar_t *comment)
{
    wchar_t *copy;
    if (!rec) {
        return false;
    }
    copy = comment ? xx_str_wdup(comment) : NULL;
    if (comment && !copy) {
        return false;
    }
    if (rec->comment) {
        xx_str_wfree(rec->comment);
    }
    rec->comment = copy;
    return true;
}

static bool xx_data_struct_record_set_size_comment(xx_data_struct_record *rec, uint64_t value)
{
    char ascii[96];
    wchar_t wide[96];
    size_t index;

    if (!xx_str_bytes_count_to_string(value, 1024U, ascii, sizeof(ascii))) {
        return false;
    }
    for (index = 0U; ascii[index] != '\0'; ++index) {
        wide[index] = (wchar_t)(unsigned char)ascii[index];
    }
    wide[index] = L'\0';
    return xx_data_struct_record_set_comment(rec, wide);
}

/* Array fields retain all their bytes instead of pretending the first four
 * bytes are the scalar value. Text arrays stop at their first NUL. */
static bool xx_data_struct_record_populate_array(xx_data_struct_record *rec, xx_io_device *device,
                                                  int64_t abs_offset, const xx_data_struct_field_desc *field)
{
    size_t size = (size_t)field->size;
    uint8_t *bytes = (uint8_t *)xx_mem_alloc(size + 1U);
    bool as_string = field->type && xx_rt_wcsncmp(field->type, L"char[", 5U) == 0;
    bool result = false;
    wchar_t *display = NULL;
    if (!bytes) return false;
    if (!xx_io_read_at(device, abs_offset, bytes, size)) goto done;
    bytes[size] = 0;
    if (!xx_data_struct_record_set_raw_bytes(rec, device, abs_offset, field->size)) goto done;
    if (as_string) {
        size_t length = 0U;
        size_t index;
        while (length < size && bytes[length] != 0U) ++length;
        if (length > SIZE_MAX / sizeof(*display) - 1U) goto done;
        display = (wchar_t *)xx_mem_alloc((length + 1U) * sizeof(*display));
        if (!display || !xx_var_set_str(&rec->value, (const char *)bytes)) goto done;
        for (index = 0U; index < length; ++index)
            display[index] = bytes[index] >= 0x20U && bytes[index] != 0x7fU ? (wchar_t)bytes[index] : L'.';
        display[length] = L'\0';
    } else if (!xx_var_set_bytes(&rec->value, bytes, size)) {
        goto done;
    }
    result = xx_data_struct_record_set_name(rec, field->name) && xx_data_struct_record_set_type(rec, field->type) &&
             xx_data_struct_record_set_display_value(rec, as_string ? display : rec->raw_value);
done:
    if (display) xx_mem_free(display);
    xx_mem_free(bytes);
    return result;
}

bool xx_data_struct_record_populate(xx_data_struct_record *rec, xx_io_device *device, int64_t parent_offset, const xx_data_struct_field_desc *field, bool is_big_endian)
{
    bool raw_as_bytes;
    bool is_signed;
    bool as_hex;
    bool is_float32, is_float64, is_floating;
    double floating_value = 0.0;
    int64_t abs_offset;
    uint64_t raw_value = 0;
    int64_t signed_value = 0;
    uint8_t bytes[8];
    char display_ascii[32];
    wchar_t display_buf[32];
    int display_length;
    size_t index;
    if (!rec) return false;
    xx_data_struct_record_init(rec);
    if (!device || !field || parent_offset < 0 || field->rel_offset < 0 || field->size < 0 ||
        field->rel_offset > INT64_MAX - parent_offset || (uint64_t)field->size >= SIZE_MAX)
        return false;
    abs_offset = parent_offset + field->rel_offset;
    if (field->size > INT64_MAX - abs_offset || !xx_io_check(device, abs_offset, field->size)) return false;

    is_float32 = field->type && (!xx_rt_wcscmp(field->type, L"float32") || !xx_rt_wcscmp(field->type, L"float"));
    is_float64 = field->type && (!xx_rt_wcscmp(field->type, L"float64") || !xx_rt_wcscmp(field->type, L"double"));
    is_floating = is_float32 || is_float64;
    if ((is_float32 && (field->size != 4 || sizeof(float) != 4)) || (is_float64 && (field->size != 8 || sizeof(double) != 8))) return false;

    rec->offset = field->rel_offset;
    rec->size = field->size;
    rec->property = field->property;
    raw_as_bytes = (field->type && (xx_rt_wcschr(field->type, L'[') != NULL || xx_rt_wcscmp(field->type, L"bytes") == 0)) || field->size > 8;
    if (raw_as_bytes) {
        if (xx_data_struct_record_populate_array(rec, device, abs_offset, field)) return true;
        goto fail;
    }
    if (field->size < 1 || field->size > 8) goto fail;
    if (!xx_io_read_at(device, abs_offset, bytes, (size_t)field->size)) goto fail;
    for (index = 0U; index < (size_t)field->size; ++index) {
        size_t shift_index = is_big_endian ? (size_t)field->size - index - 1U : index;
        raw_value |= (uint64_t)bytes[index] << (shift_index * 8U);
    }
    is_signed = field->type && (xx_rt_wcscmp(field->type, L"int8") == 0 || xx_rt_wcscmp(field->type, L"int16") == 0 ||
                               xx_rt_wcscmp(field->type, L"int24") == 0 || xx_rt_wcscmp(field->type, L"int32") == 0 ||
                               xx_rt_wcscmp(field->type, L"int40") == 0 || xx_rt_wcscmp(field->type, L"int48") == 0 ||
                               xx_rt_wcscmp(field->type, L"int56") == 0 || xx_rt_wcscmp(field->type, L"int64") == 0);
    if (is_floating) {
        if (is_float32) {
            uint32_t bits = (uint32_t)raw_value;
            float value;
            xx_rt_memcpy(&value, &bits, sizeof(value));
            xx_var_set_float(&rec->value, value);
            floating_value = value;
        } else {
            xx_rt_memcpy(&floating_value, &raw_value, sizeof(floating_value));
            xx_var_set_double(&rec->value, floating_value);
        }
    } else if (is_signed) {
        switch (field->size) {
            case 1: signed_value = (int8_t)raw_value; xx_var_set_i8(&rec->value, (int8_t)signed_value); break;
            case 2: signed_value = (int16_t)raw_value; xx_var_set_i16(&rec->value, (int16_t)signed_value); break;
            case 4: signed_value = (int32_t)raw_value; xx_var_set_i32(&rec->value, (int32_t)signed_value); break;
            default:
                signed_value = (int64_t)raw_value;
                if (field->size < 8 && (raw_value & (UINT64_C(1) << ((unsigned)field->size * 8U - 1U))) != 0U)
                    signed_value = (int64_t)(raw_value | (UINT64_MAX << ((unsigned)field->size * 8U)));
                xx_var_set_i64(&rec->value, signed_value);
                break;
        }
    } else {
        switch (field->size) {
            case 1: xx_var_set_u8(&rec->value, (uint8_t)raw_value); break;
            case 2: xx_var_set_u16(&rec->value, (uint16_t)raw_value); break;
            case 4: xx_var_set_u32(&rec->value, (uint32_t)raw_value); break;
            default: xx_var_set_u64(&rec->value, raw_value); break;
        }
    }
    as_hex = (field->property & (XX_DATA_STRUCT_RECORD_PROPERTY_ID | XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS |
                                XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE |
                                XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM)) != 0;
    if (is_floating)
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii), is_float32 ? "%.9g" : "%.17g", floating_value);
    else if (as_hex)
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii), "0x%08llX", (unsigned long long)raw_value);
    else if (is_signed)
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii), "%lld", (long long)signed_value);
    else
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii), "%llu", (unsigned long long)raw_value);
    if (display_length < 0 || (size_t)display_length >= sizeof(display_ascii)) goto fail;
    for (index = 0U; index <= (size_t)display_length; ++index)
        display_buf[index] = (wchar_t)(unsigned char)display_ascii[index];
    if (!xx_data_struct_record_set_name(rec, field->name) || !xx_data_struct_record_set_type(rec, field->type) ||
        !xx_data_struct_record_set_raw_uint(rec, raw_value, field->size) || !xx_data_struct_record_set_display_value(rec, display_buf) ||
        (!is_floating && (field->property & XX_DATA_STRUCT_RECORD_PROPERTY_SIZE) != 0 && (!is_signed || signed_value >= 0) &&
         !xx_data_struct_record_set_size_comment(rec, raw_value)))
        goto fail;
    return true;
fail:
    xx_data_struct_record_cleanup(rec);
    return false;
}

/* ========================================================================= */
/* --- Data Struct Records Stream Reading Operations                      --- */
/* ========================================================================= */

void xx_data_struct_record_state_init(xx_data_struct_record_state *state, Abstractformat *fmt, const xx_data_struct *ds)
{
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_record_state));
    state->format = fmt;
    if (ds) {
        state->parent_struct = *ds;
    }
    xx_data_struct_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
}

void xx_data_struct_record_state_cleanup(xx_data_struct_record_state *state)
{
    if (!state) {
        return;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_record_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_data_struct_record_state_free(xx_data_struct_record_state *state)
{
    if (!state) {
        return;
    }
    xx_data_struct_record_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_record_state *xx_format_create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd)
{
    if (!f || !ds) {
        return NULL;
    }
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        /* Physical structures can remain readable when full format parsing fails. */
        xx_format_handle_base_info(f, pd);
    }
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (stream && stream->create_data_struct_records_reading) {
        return (stream->create_data_struct_records_reading)(f, ds, pd);
    }
    return NULL;
}

const xx_data_struct_record *xx_format_get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return NULL;
    }
    if (stream && stream->get_current_data_struct_record) {
        return (stream->get_current_data_struct_record)(f, state);
    }
    return state->has_record ? &state->current_record : NULL;
}

bool xx_format_data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return false;
    }
    if (stream && stream->data_struct_record_move_to_next) {
        return (stream->data_struct_record_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state)
{
    if (!f) f = state ? state->format : NULL;
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!state) {
        return;
    }
    if (stream && stream->free_data_struct_records_reading) {
        (stream->free_data_struct_records_reading)(f, state);
    } else {
        xx_data_struct_record_state_free(state);
    }
}

bool xx_data_struct_record_set_description(xx_data_struct_record *rec, const wchar_t *description)
{
    wchar_t *copy;
    if (!rec) {
        return false;
    }
    copy = description ? xx_str_wdup(description) : NULL;
    if (description && !copy) {
        return false;
    }
    if (rec->description) {
        xx_str_wfree(rec->description);
    }
    rec->description = copy;
    return true;
}

const char *xx_format_data_struct_description(Abstractformat *f, uint32_t id)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (stream && stream->data_struct_description) {
        return (stream->data_struct_description)(f, id);
    }
    return NULL;
}

xx_io_device *xx_format_get_data_struct_device(Abstractformat *f, xx_pd_struct *pd)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (!f) {
        return NULL;
    }
    if (stream && stream->data_struct_device) {
        return (stream->data_struct_device)(f, pd);
    }
    return f->device;
}

const char *xx_format_data_struct_id_to_string(Abstractformat *f, uint32_t id)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (id == XX_DATA_STRUCT_ID_RAW_DATA) {
        return "RAW_DATA";
    }
    if (stream && stream->data_struct_id_to_string) {
        return (stream->data_struct_id_to_string)(f, id);
    }
    return "UNKNOWN";
}

uint32_t xx_format_data_struct_string_to_id(Abstractformat *f, const char *name)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(f);
    if (stream && stream->data_struct_string_to_id) {
        return (stream->data_struct_string_to_id)(f, name);
    }
    return 0;
}

static const char *xx_format_get_short_name(Abstractformat *f)
{
    return f ? xx_format_file_type_to_string(f->file_type) : "UNKNOWN";
}

wchar_t *xx_format_data_struct_to_string(Abstractformat *f, const xx_data_struct *ds)
{
    if (!ds) {
        return NULL;
    }

    const char *id_name = xx_format_data_struct_id_to_string(f, ds->id);
    const char *type_name = xx_data_struct_type_to_string(ds->type);

    char buf[256];
    /* Global ids (e.g. RAW_DATA) are format-independent, so no "<FORMAT>::" prefix is added */
    if (ds->id == XX_DATA_STRUCT_ID_RAW_DATA) {
        xx_rt_snprintf(buf, sizeof(buf), "%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s", id_name, (long long)ds->offset, (long long)ds->entry_size,
                       (long long)ds->total_size, (unsigned long long)ds->count, type_name);
    } else {
        const char *format_name = xx_format_get_short_name(f);
        xx_rt_snprintf(buf, sizeof(buf), "%s::%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s", format_name, id_name, (long long)ds->offset,
                       (long long)ds->entry_size, (long long)ds->total_size, (unsigned long long)ds->count, type_name);
    }

    return xx_str_ansi_to_unicode(buf);
}

const char *xx_data_struct_type_to_string(xx_data_struct_type_t type)
{
    switch (type) {
        case XX_DATA_STRUCT_TYPE_STRUCT: return "struct";
        case XX_DATA_STRUCT_TYPE_ENTRY: return "entry";
        case XX_DATA_STRUCT_TYPE_FOOTER: return "footer";
        case XX_DATA_STRUCT_TYPE_LOCATOR: return "locator";
        case XX_DATA_STRUCT_TYPE_RAW_DATA: return "raw_data";
        default: return "unknown";
    }
}
