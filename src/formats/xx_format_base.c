/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Generic format initialization, options and archive-record lifecycle.
 * Kept separate from content detection so native readers and the DIE
 * signature engine can share this runtime without linking every format. */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

static void xx_format_extra_parameter_free_elem(void *element);

static void xx_format_invalidate_password_state(Abstractformat *format) {
    if (!format) {
        return;
    }
    /* Header-encrypted formats must get another preparation/base-info pass
     * after their format-wide password changes. Concrete split handlers are
     * responsible for rebuilding any password-derived private view. */
    format->split_format_handled = false;
    format->base_info_handled = false;
    format->is_valid = false;
    xx_format_invalidate_memory_map(format);
}

void xx_format_init(Abstractformat *fmt, xx_io_device *dev, int64_t base_address) {
    if (!fmt) {
        return;
    }
    xx_mem_zero(fmt, sizeof(Abstractformat));
    fmt->device = dev;
    fmt->base_address = base_address;
    fmt->is_mapped = false;
    fmt->base_info_handled = false;
    fmt->is_valid = false;
    fmt->format_size = -1;
    fmt->overlay_offset = -1;
    fmt->overlay_size = 0;
    fmt->endian = XX_ENDIAN_UNKNOWN;
    fmt->file_type = XX_FILE_TYPE_UNKNOWN;
    fmt->os = XX_OS_UNKNOWN;
    fmt->format_type = XX_FORMAT_TYPE_UNKNOWN;
    fmt->arch = XX_ARCH_UNKNOWN;
    fmt->is_executable = false;
    fmt->is_archive = false;
    fmt->is_signed = false;
    fmt->is_crypted = false;
    fmt->number_of_imports = 0;
    fmt->number_of_exports = 0;
    fmt->number_of_resources = 0;
    fmt->number_of_metadata = 0;
    fmt->number_of_symbols = 0;
    fmt->number_of_archive_records = 0;
    fmt->module_address = XX_INVALID_ADDRESS;
    xx_memory_map_init(&fmt->memory_map);
    xx_list_init(&fmt->list_extra_parameters, sizeof(xx_meta),
                 xx_format_extra_parameter_free_elem);
}

void xx_format_invalidate_memory_map(Abstractformat *format) {
    bool was_handling;
    if (!format) return;
    was_handling = format->memory_map_handling;
    xx_memory_map_cleanup(&format->memory_map);
    format->memory_map_handled = false;
    /* Do not erase the recursion guard if an invalidating setter is called
     * from inside a concrete producer. */
    format->memory_map_handling = was_handling;
    format->memory_map_requested_mode = XX_MEMORY_MAP_MODE_UNKNOWN;
}

static bool xx_format_get_default_memory_map(Abstractformat *format,
                                             xx_memory_map_mode_t mode,
                                             xx_memory_map *output) {
    xx_memory_record record;
    int64_t total;
    int64_t size;
    if (!format || !format->device || !output || format->base_address < 0)
        return false;
    if (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
        mode != XX_MEMORY_MAP_MODE_REGIONS)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    output->binary_offset = format->base_address;
    output->module_address = format->module_address != XX_INVALID_ADDRESS
                                 ? format->module_address
                                 : 0U;
    output->is_image = format->is_mapped;
    output->binary_size = size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_REGIONS;
    if (size == 0) return true;
    xx_mem_zero(&record, sizeof(record));
    record.offset = format->base_address;
    record.address = output->module_address;
    record.size = size;
    record.file_part = XX_FILE_PART_REGION;
    record.file_part_number = 0;
    (void)xx_rt_snprintf(record.name, sizeof(record.name), "%s", "Binary");
    return xx_memory_map_add_record(output, &record);
}

bool xx_format_handle_memory_map(Abstractformat *format,
                                 xx_memory_map_mode_t mode,
                                 xx_pd_struct *pd) {
    xx_memory_map result;
    bool built;
    if (!format || xx_pd_is_stopped(pd)) return false;
    if (!xx_format_handle_split_format(format, pd)) return false;
    if (format->memory_map_handled &&
        (format->memory_map_requested_mode == mode ||
         (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
          format->memory_map.mode == mode)))
        return true;
    if (format->memory_map_handling) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "Recursive memory-map construction");
        return false;
    }
    if (!format->base_info_handled && format->handle_base_info &&
        !xx_format_handle_base_info(format, pd))
        return false;
    /* A concrete base-info handler may construct the default map because it
     * needs that same map for parsing address-based tables (PE does this). */
    if (format->memory_map_handled &&
        (format->memory_map_requested_mode == mode ||
         (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
          format->memory_map.mode == mode)))
        return true;
    format->memory_map_handling = true;
    xx_memory_map_init(&result);
    built = format->get_memory_map
                ? format->get_memory_map(format, mode, &result, pd)
                : xx_format_get_default_memory_map(format, mode, &result);
    if (built && result.mode == XX_MEMORY_MAP_MODE_UNKNOWN &&
        mode != XX_MEMORY_MAP_MODE_UNKNOWN)
        result.mode = mode;
    if (built) built = xx_memory_map_finalize(&result);
    if (!built || xx_pd_is_stopped(pd)) {
        xx_memory_map_cleanup(&result);
        format->memory_map_handling = false;
        return false;
    }
    xx_memory_map_cleanup(&format->memory_map);
    format->memory_map = result;
    format->memory_map_handled = true;
    format->memory_map_handling = false;
    format->memory_map_requested_mode = mode;
    return true;
}

const xx_memory_map *xx_format_get_memory_map(Abstractformat *format,
                                               xx_memory_map_mode_t mode,
                                               xx_pd_struct *pd) {
    return xx_format_handle_memory_map(format, mode, pd)
               ? &format->memory_map
               : NULL;
}

uint64_t xx_format_offset_to_address(Abstractformat *format, int64_t offset,
                                     xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_offset_to_address(map, offset)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_address_to_offset(Abstractformat *format, uint64_t address,
                                    xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_address_to_offset(map, address) : -1;
}

uint64_t xx_format_offset_to_rel_address(Abstractformat *format,
                                          int64_t offset, xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_offset_to_relative_address(map, offset)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_rel_address_to_offset(Abstractformat *format,
                                        int64_t relative_address,
                                        xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_relative_address_to_offset(map,
                                                           relative_address)
               : -1;
}

uint64_t xx_format_rel_address_to_address(Abstractformat *format,
                                           int64_t relative_address,
                                           xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_relative_address_to_address(map,
                                                            relative_address)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_address_to_rel_address(Abstractformat *format,
                                          uint64_t address,
                                          xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_address_to_relative_address(map, address) : -1;
}

bool xx_format_handle_split_format(Abstractformat *format, xx_pd_struct *pd) {
    bool handled;
    if (!format) {
        return false;
    }
    /* Keep formats without a split handler entirely on their legacy path. */
    if (!format->handle_split_format) {
        return true;
    }
    if (xx_pd_is_stopped(pd)) {
        return false;
    }
    if (format->split_format_handling) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Recursive split-format preparation");
        return false;
    }
    if (format->split_format_handled) {
        return true;
    }
    format->split_format_handling = true;
    handled = format->handle_split_format(format, pd);
    format->split_format_handling = false;
    if (!handled || xx_pd_is_stopped(pd)) {
        return false;
    }
    format->split_format_handled = true;
    return true;
}

static void xx_format_secure_clear_parameter(xx_meta *meta) {
    xx_var *value;
    if (!meta || meta->meta_id != XX_META_ID_OPT_PASSWORD) {
        return;
    }
    value = &meta->var;
    if (!value->is_allocated) {
        return;
    }
    if (value->type == XX_VAR_TYPE_STRING && value->val.str.ptr) {
        xx_mem_zero(value->val.str.ptr, value->val.str.len);
    } else if (value->type == XX_VAR_TYPE_WSTRING && value->val.wstr.ptr) {
        xx_mem_zero(value->val.wstr.ptr,
                    value->val.wstr.len * sizeof(wchar_t));
    } else if (value->type == XX_VAR_TYPE_BYTES && value->val.bytes.data) {
        xx_mem_zero(value->val.bytes.data, value->val.bytes.size);
    }
}

static void xx_format_extra_parameter_free_elem(void *element) {
    xx_meta *meta = (xx_meta *)element;
    if (!meta) {
        return;
    }
    xx_format_secure_clear_parameter(meta);
    xx_meta_cleanup(meta);
}

static bool xx_format_copy_parameter_value(xx_var *destination,
                                           const xx_var *source) {
    if (!destination || !source) {
        return false;
    }
    switch ((xx_var_type_t)source->type) {
        case XX_VAR_TYPE_STRING_VIEW: {
            char *copy;
            if ((!source->val.str.ptr && source->val.str.len != 0U) ||
                source->val.str.len == SIZE_MAX) {
                return false;
            }
            copy = (char *)xx_mem_alloc(source->val.str.len + 1U);
            if (!copy) {
                return false;
            }
            if (source->val.str.len != 0U) {
                xx_mem_copy(copy, source->val.str.ptr, source->val.str.len);
            }
            copy[source->val.str.len] = '\0';
            return xx_var_set_str_take(destination, copy,
                                       source->val.str.len);
        }
        case XX_VAR_TYPE_WSTRING_VIEW: {
            wchar_t *copy;
            if ((!source->val.wstr.ptr && source->val.wstr.len != 0U) ||
                source->val.wstr.len >
                    (SIZE_MAX / sizeof(wchar_t)) - 1U) {
                return false;
            }
            copy = (wchar_t *)xx_mem_alloc(
                (source->val.wstr.len + 1U) * sizeof(wchar_t));
            if (!copy) {
                return false;
            }
            if (source->val.wstr.len != 0U) {
                xx_mem_copy(copy, source->val.wstr.ptr,
                            source->val.wstr.len * sizeof(wchar_t));
            }
            copy[source->val.wstr.len] = L'\0';
            return xx_var_set_wstr_take(destination, copy,
                                        source->val.wstr.len);
        }
        case XX_VAR_TYPE_BYTES_VIEW:
            return xx_var_set_bytes(destination, source->val.bytes.data,
                                    source->val.bytes.size);
        default:
            return xx_var_copy(destination, source);
    }
}

const xx_var *xx_format_find_extra_parameter(const Abstractformat *format,
                                              uint32_t meta_id) {
    size_t i;
    if (!format) {
        return NULL;
    }
    for (i = 0; i < format->list_extra_parameters.count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at(
            &format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id) {
            return &meta->var;
        }
    }
    return NULL;
}

const xx_var *xx_format_resolve_extra_parameter(
    const Abstractformat *format, const xx_list_s *operation_parameters,
    uint32_t meta_id) {
    size_t i;
    if (operation_parameters) {
        for (i = 0; i < operation_parameters->count; ++i) {
            const xx_meta *meta = (const xx_meta *)xx_list_at(
                operation_parameters, i);
            if (meta && meta->meta_id == meta_id) {
                return &meta->var;
            }
        }
    }
    return xx_format_find_extra_parameter(format, meta_id);
}

bool xx_format_set_extra_parameter(Abstractformat *format, uint32_t meta_id,
                                   const xx_var *value) {
    xx_var replacement;
    size_t i;
    if (!format) {
        return false;
    }
    if (!value) {
        (void)xx_format_remove_extra_parameter(format, meta_id);
        return true;
    }
    if (format->list_extra_parameters.elem_size == 0U &&
        !xx_list_init(&format->list_extra_parameters, sizeof(xx_meta),
                      xx_format_extra_parameter_free_elem)) {
        return false;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    xx_var_init(&replacement);
    if (!xx_format_copy_parameter_value(&replacement, value)) {
        return false;
    }
    for (i = 0; i < format->list_extra_parameters.count; ++i) {
        xx_meta *meta = (xx_meta *)xx_list_at(
            &format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id) {
            xx_format_secure_clear_parameter(meta);
            xx_var_cleanup(&meta->var);
            meta->var = replacement;
            if (meta_id == XX_META_ID_OPT_PASSWORD) {
                xx_format_invalidate_password_state(format);
            }
            return true;
        }
    }
    {
        xx_meta meta;
        xx_meta_init(&meta, meta_id);
        meta.var = replacement;
        if (!xx_list_append(&format->list_extra_parameters, &meta)) {
            xx_format_extra_parameter_free_elem(&meta);
            return false;
        }
    }
    if (meta_id == XX_META_ID_OPT_PASSWORD) {
        xx_format_invalidate_password_state(format);
    }
    return true;
}

bool xx_format_remove_extra_parameter(Abstractformat *format,
                                      uint32_t meta_id) {
    size_t i;
    bool removed = false;
    if (!format) {
        return false;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    i = format->list_extra_parameters.count;
    while (i != 0U) {
        xx_meta *meta;
        --i;
        meta = (xx_meta *)xx_list_at(&format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id &&
            xx_list_remove_at(&format->list_extra_parameters, i)) {
            removed = true;
        }
    }
    if (removed && meta_id == XX_META_ID_OPT_PASSWORD) {
        xx_format_invalidate_password_state(format);
    }
    return removed;
}

void xx_format_cleanup_extra_parameters(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    xx_list_cleanup(&format->list_extra_parameters);
    xx_format_invalidate_memory_map(format);
}

bool xx_format_set_password(Abstractformat *format,
                            const char *password_utf8) {
    xx_meta sensitive;
    xx_var value;
    bool result;
    if (!format) {
        return false;
    }
    if (!password_utf8) {
        (void)xx_format_remove_extra_parameter(format,
                                               XX_META_ID_OPT_PASSWORD);
        return true;
    }
    xx_var_init(&value);
    if (!xx_var_set_str(&value, password_utf8)) {
        return false;
    }
    result = xx_format_set_extra_parameter(format,
                                           XX_META_ID_OPT_PASSWORD, &value);
    sensitive.meta_id = XX_META_ID_OPT_PASSWORD;
    sensitive.var = value;
    xx_format_secure_clear_parameter(&sensitive);
    xx_var_cleanup(&value);
    return result;
}

const char *xx_format_get_password(const Abstractformat *format) {
    const xx_var *value = xx_format_find_extra_parameter(
        format, XX_META_ID_OPT_PASSWORD);
    return value ? xx_var_get_str(value) : NULL;
}

Abstractformat *xx_format_create(xx_io_device *dev, int64_t base_address) {
    Abstractformat *fmt = (Abstractformat *)xx_mem_alloc(sizeof(Abstractformat));
    if (!fmt) {
        return NULL;
    }
    xx_format_init(fmt, dev, base_address);
    return fmt;
}

void xx_format_free(Abstractformat *fmt) {
    if (!fmt) {
        return;
    }
    xx_format_destroy(fmt);
    xx_mem_free(fmt);
}

/* ========================================================================= */
/* --- Metadata & Archive Record Lifecycle                               --- */
/* ========================================================================= */

void xx_meta_init(xx_meta *meta, uint32_t meta_id) {
    if (!meta) {
        return;
    }
    meta->meta_id = meta_id;
    xx_var_init(&meta->var);
}

void xx_meta_cleanup(xx_meta *meta) {
    if (!meta) {
        return;
    }
    /* Password metadata also appears in per-operation option lists, not only
       in the format-wide extra-parameter list.  Clear owned password storage
       before releasing it on every cleanup path. */
    xx_format_secure_clear_parameter(meta);
    xx_var_cleanup(&meta->var);
    meta->meta_id = 0;
}

void xx_meta_free_elem(void *element) {
    if (element) {
        xx_meta_cleanup((xx_meta *)element);
    }
}

void xx_archive_record_init(xx_archive_record *rec) {
    if (!rec) {
        return;
    }
    xx_mem_zero(rec, sizeof(xx_archive_record));
    rec->header_offset = -1;
    rec->data_offset = -1;
    xx_list_init(&rec->list_meta, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_record_cleanup(xx_archive_record *rec) {
    if (!rec) {
        return;
    }
    xx_list_cleanup(&rec->list_meta);
    xx_mem_zero(rec, sizeof(xx_archive_record));
    rec->header_offset = -1;
    rec->data_offset = -1;
}

void xx_archive_record_free_elem(void *element) {
    if (element) {
        xx_archive_record_cleanup((xx_archive_record *)element);
    }
}

bool xx_archive_record_add_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var) {
    if (!rec) {
        return false;
    }
    xx_meta item;
    xx_meta_init(&item, meta_id);
    if (var) {
        if (!xx_var_copy(&item.var, var)) {
            xx_meta_cleanup(&item);
            return false;
        }
    }
    if (!xx_list_append(&rec->list_meta, &item)) {
        xx_meta_cleanup(&item);
        return false;
    }
    return true;
}

bool xx_archive_record_set_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var) {
    if (!rec) {
        return false;
    }
    size_t count = rec->list_meta.count;
    for (size_t i = 0; i < count; ++i) {
        xx_meta *m = (xx_meta *)xx_list_at(&rec->list_meta, i);
        if (m && m->meta_id == meta_id) {
            return xx_var_copy(&m->var, var);
        }
    }
    return xx_archive_record_add_meta(rec, meta_id, var);
}

bool xx_archive_record_add_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str) {
    if (!rec || !str) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_str(&v, str)) {
        return false;
    }
    bool ok = xx_archive_record_add_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_add_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
    if (!rec || !wstr) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_wstr(&v, wstr)) {
        return false;
    }
    bool ok = xx_archive_record_add_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_add_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val) {
    if (!rec) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    xx_var_set_i64(&v, val);
    return xx_archive_record_add_meta(rec, meta_id, &v);
}

bool xx_archive_record_add_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val) {
    if (!rec) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    xx_var_set_u64(&v, val);
    return xx_archive_record_add_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str) {
    if (!rec || !str) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_str(&v, str)) {
        return false;
    }
    bool ok = xx_archive_record_set_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_set_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
    if (!rec || !wstr) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_wstr(&v, wstr)) {
        return false;
    }
    bool ok = xx_archive_record_set_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_set_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_i64(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_u64(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_bool(xx_archive_record *rec, uint32_t meta_id, bool val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_bool(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

const xx_var* xx_archive_record_find_meta(const xx_archive_record *rec, uint32_t meta_id) {
    if (!rec) {
        return NULL;
    }
    size_t count = rec->list_meta.count;
    for (size_t i = 0; i < count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(&rec->list_meta, i);
        if (m && m->meta_id == meta_id) {
            return &m->var;
        }
    }
    return NULL;
}

const char* xx_archive_record_get_meta_str(const xx_archive_record *rec, uint32_t meta_id) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_str(v) : NULL;
}

const wchar_t* xx_archive_record_get_meta_wstr(const xx_archive_record *rec, uint32_t meta_id) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_wstr(v) : NULL;
}

int64_t xx_archive_record_get_meta_i64(const xx_archive_record *rec, uint32_t meta_id, int64_t default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_i64(v) : default_val;
}

uint64_t xx_archive_record_get_meta_u64(const xx_archive_record *rec, uint32_t meta_id, uint64_t default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_u64(v) : default_val;
}

bool xx_archive_record_get_meta_bool(const xx_archive_record *rec, uint32_t meta_id, bool default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_bool(v) : default_val;
}

/* ========================================================================= */
/* --- Archive Record Stream Reading Operations                          --- */
/* ========================================================================= */

void xx_archive_record_state_init(xx_archive_record_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_archive_record_state));
    state->format = fmt;
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
    xx_list_init(&state->options, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_record_state_cleanup(xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_list_cleanup(&state->options);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_archive_record_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_archive_record_state_free(xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_state_cleanup(state);
    xx_mem_free(state);
}

xx_archive_record_state *xx_format_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    /* These readers parse their index under the reading operation's memory
     * limit. Explicit INFO and detection retain their normal default budget. */
    if (!f->base_info_handled &&
        !(f->create_archive_records_reading &&
          (f->file_type == XX_FILE_TYPE_SUPERDAT ||
           f->file_type == XX_FILE_TYPE_EXCELSIOR_INSTALLER ||
           f->file_type == XX_FILE_TYPE_NETOPSYSTEMS_FEAD ||
           f->file_type == XX_FILE_TYPE_DGCA))) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_archive_records_reading) {
        return (f->create_archive_records_reading)(f, options, pd);
    }
    return NULL;
}

const xx_archive_record *xx_format_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_archive_record) {
        return (f->get_current_archive_record)(f, state);
    }
    return state->has_record ? &state->current_record : NULL;
}

bool xx_format_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!state || !state->has_record) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->unpack_current_archive_record) {
        return (f->unpack_current_archive_record)(f, state, pd);
    }
    return false;
}

bool xx_format_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->archive_record_move_to_next) {
        return (f->archive_record_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_archive_records_reading) {
        (f->free_archive_records_reading)(f, state);
    } else {
        xx_archive_record_state_free(state);
    }
}


/* Archive writing and data-structure lifecycle shared by native readers. */
void xx_archive_write_state_init(xx_archive_write_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_archive_write_state));
    state->format = fmt;
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
    xx_list_init(&state->options, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_write_state_cleanup(xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_list_cleanup(&state->options);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_archive_write_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_archive_write_state_free(xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    xx_archive_write_state_cleanup(state);
    xx_mem_free(state);
}

xx_archive_write_state *xx_format_create_archive_records_writing(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    if (!f) {
        return NULL;
    }
    if (f->create_archive_records_writing) {
        return (f->create_archive_records_writing)(f, options, pd);
    }
    return NULL;
}

bool xx_format_pack_archive_record(Abstractformat *f, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->pack_archive_record) {
        return (f->pack_archive_record)(f, state, record, source_dev, pd);
    }
    return false;
}

bool xx_format_finalize_archive_records_writing(Abstractformat *f, xx_archive_write_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->finalize_archive_records_writing) {
        return (f->finalize_archive_records_writing)(f, state, pd);
    }
    return false;
}

void xx_format_free_archive_records_writing(Abstractformat *f, xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_archive_records_writing) {
        (f->free_archive_records_writing)(f, state);
    } else {
        xx_archive_write_state_free(state);
    }
}

void xx_data_struct_state_init(xx_data_struct_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_state));
    state->format = fmt;
    state->has_struct = false;
    state->current_index = -1;
    state->total_structs = -1;
}

void xx_data_struct_state_cleanup(xx_data_struct_state *state) {
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

void xx_data_struct_state_free(xx_data_struct_state *state) {
    if (!state) {
        return;
    }
    xx_data_struct_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_state *xx_format_create_data_structs_reading(Abstractformat *f, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_data_structs_reading) {
        return (f->create_data_structs_reading)(f, pd);
    }
    return NULL;
}

const xx_data_struct *xx_format_get_current_data_struct(Abstractformat *f, xx_data_struct_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_data_struct) {
        return (f->get_current_data_struct)(f, state);
    }
    return state->has_struct ? &state->current_struct : NULL;
}

bool xx_format_data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->data_struct_move_to_next) {
        return (f->data_struct_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_data_structs_reading) {
        (f->free_data_structs_reading)(f, state);
    } else {
        xx_data_struct_state_free(state);
    }
}

/* ========================================================================= */
/* --- Data Struct Record Lifecycle                                       --- */
/* ========================================================================= */

void xx_data_struct_record_init(xx_data_struct_record *rec) {
    if (!rec) {
        return;
    }
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
    rec->property = XX_DATA_STRUCT_RECORD_PROPERTY_NONE;
    xx_var_init(&rec->value);
}

void xx_data_struct_record_cleanup(xx_data_struct_record *rec) {
    if (!rec) {
        return;
    }
    if (rec->name) {
        xx_str_wfree(rec->name);
    }
    if (rec->type) {
        xx_str_wfree(rec->type);
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    xx_var_cleanup(&rec->value);
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
}

void xx_data_struct_record_free_elem(void *element) {
    if (element) {
        xx_data_struct_record_cleanup((xx_data_struct_record *)element);
    }
}

bool xx_data_struct_record_set_name(xx_data_struct_record *rec, const wchar_t *name) {
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

bool xx_data_struct_record_set_type(xx_data_struct_record *rec, const wchar_t *type) {
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

bool xx_data_struct_record_set_value(xx_data_struct_record *rec, const xx_var *value) {
    if (!rec || !value) {
        return false;
    }
    return xx_var_copy(&rec->value, value);
}

bool xx_data_struct_record_set_display_value(xx_data_struct_record *rec, const wchar_t *display_value) {
    if (!rec) {
        return false;
    }
    wchar_t *copy = display_value ? xx_str_wdup(display_value) : NULL;
    if (display_value && !copy) {
        return false;
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    rec->display_value = copy;
    return true;
}

bool xx_data_struct_record_populate(xx_data_struct_record *rec, xx_io_device *device,
                                   int64_t parent_offset, const xx_data_struct_field_desc *field,
                                   bool is_big_endian) {
    if (!rec || !device || !field) {
        return false;
    }
    xx_data_struct_record_init(rec);

    rec->offset = field->rel_offset;
    rec->size = field->size;
    rec->property = field->property;

    int64_t abs_offset = parent_offset + field->rel_offset;
    uint64_t raw_value = 0;
    if (field->size == 1) {
        raw_value = xx_io_get_u8(device, abs_offset);
    } else if (field->size == 2) {
        raw_value = xx_io_get_u16(device, abs_offset, is_big_endian);
    } else if (field->size == 8) {
        raw_value = xx_io_get_u64(device, abs_offset, is_big_endian);
    } else {
        raw_value = xx_io_get_u32(device, abs_offset, is_big_endian);
    }

    xx_var_set_u64(&rec->value, raw_value);

    char display_ascii[32];
    wchar_t display_buf[32];
    int display_length;
    size_t display_index;
    bool as_hex = (field->property & (XX_DATA_STRUCT_RECORD_PROPERTY_ID | XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)) != 0;
    if (as_hex) {
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii),
                                        "0x%08llX",
                                        (unsigned long long)raw_value);
    } else {
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii),
                                        "%llu",
                                        (unsigned long long)raw_value);
    }
    if (display_length < 0 ||
        (size_t)display_length >= sizeof(display_ascii)) {
        return false;
    }
    for (display_index = 0U; display_index <= (size_t)display_length;
         ++display_index) {
        display_buf[display_index] =
            (wchar_t)(unsigned char)display_ascii[display_index];
    }

    xx_data_struct_record_set_name(rec, field->name);
    xx_data_struct_record_set_type(rec, field->type);
    xx_data_struct_record_set_display_value(rec, display_buf);

    return true;
}

/* ========================================================================= */
/* --- Data Struct Records Stream Reading Operations                      --- */
/* ========================================================================= */

void xx_data_struct_record_state_init(xx_data_struct_record_state *state, Abstractformat *fmt, const xx_data_struct *ds) {
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

void xx_data_struct_record_state_cleanup(xx_data_struct_record_state *state) {
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

void xx_data_struct_record_state_free(xx_data_struct_record_state *state) {
    if (!state) {
        return;
    }
    xx_data_struct_record_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_record_state *xx_format_create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd) {
    if (!f || !ds) {
        return NULL;
    }
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_data_struct_records_reading) {
        return (f->create_data_struct_records_reading)(f, ds, pd);
    }
    return NULL;
}

const xx_data_struct_record *xx_format_get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_data_struct_record) {
        return (f->get_current_data_struct_record)(f, state);
    }
    return state->has_record ? &state->current_record : NULL;
}

bool xx_format_data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->data_struct_record_move_to_next) {
        return (f->data_struct_record_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_data_struct_records_reading) {
        (f->free_data_struct_records_reading)(f, state);
    } else {
        xx_data_struct_record_state_free(state);
    }
}
