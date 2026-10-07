/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/memory/xx_memory.h"

void xx_symbol_record_init(xx_symbol_record *r) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->offset = r->record_offset = -1;
    r->address = UINT64_MAX;
}
void xx_symbol_record_cleanup(xx_symbol_record *r) {
    if (!r) return;
    xx_mem_free(r->name); xx_mem_free(r->file_name);
    xx_symbol_record_init(r);
}

void xx_import_record_init(xx_import_record *r) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->offset = -1; r->address = UINT64_MAX;
}
void xx_import_record_cleanup(xx_import_record *r) {
    if (!r) return;
    xx_mem_free(r->library_name); xx_mem_free(r->name);
    xx_import_record_init(r);
}
void xx_export_record_init(xx_export_record *r) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->offset = -1; r->address = UINT64_MAX;
}
void xx_export_record_cleanup(xx_export_record *r) {
    if (!r) return;
    xx_mem_free(r->name); xx_mem_free(r->forwarder);
    xx_export_record_init(r);
}
void xx_resource_record_init(xx_resource_record *r) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->type_id = r->name_id = r->language_id = UINT32_MAX;
    r->offset = -1;
}
void xx_resource_record_cleanup(xx_resource_record *r) {
    if (!r) return;
    xx_mem_free(r->type_name); xx_mem_free(r->name); xx_mem_free(r->language_name);
    xx_resource_record_init(r);
}
void xx_metadata_record_init(xx_metadata_record *r) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    xx_var_init(&r->value); r->offset = -1;
}
void xx_metadata_record_cleanup(xx_metadata_record *r) {
    if (!r) return;
    xx_mem_free(r->key); xx_var_cleanup(&r->value);
    xx_metadata_record_init(r);
}

#define XX_STREAM_RUNTIME(kind, plural) \
void xx_##kind##_state_init(xx_##kind##_state *s, Abstractformat *f) { \
    if (!s) return; \
    xx_mem_zero(s, sizeof(*s)); \
    s->format = f; s->current_index = -1; s->total_records = -1; \
    xx_##kind##_record_init(&s->current_record); \
} \
void xx_##kind##_state_cleanup(xx_##kind##_state *s) { \
    if (!s) return; \
    xx_##kind##_record_cleanup(&s->current_record); \
    if (s->free_internal && s->internal_state) s->free_internal(s->internal_state); \
    xx_##kind##_state_init(s, NULL); \
} \
void xx_##kind##_state_free(xx_##kind##_state *s) { \
    if (!s) return; \
    xx_##kind##_state_cleanup(s); xx_mem_free(s); \
} \
xx_##kind##_state *xx_format_create_##plural##_reading(Abstractformat *f, xx_pd_struct *pd) { \
    xx_##kind##_state *state = NULL; \
    int64_t saved; \
    if (!f || !f->create_##plural##_reading || !f->get_current_##kind || \
        !f->kind##_move_to_next || !f->free_##plural##_reading) { \
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Format does not support " #plural " streaming"); \
        return NULL; \
    } \
    if (xx_pd_is_stopped(pd)) { \
        xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Stream creation cancelled"); return NULL; \
    } \
    if (!f->device || (saved = xx_io_tell(f->device)) < 0) { \
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot save stream input position"); return NULL; \
    } \
    if (xx_format_handle_base_info(f, pd)) state = f->create_##plural##_reading(f, pd); \
    if (xx_io_seek64(f->device, saved, 0) != 0) { \
        if (state) f->free_##plural##_reading(f, state); \
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot restore stream input position"); return NULL; \
    } \
    return state; \
} \
const xx_##kind##_record *xx_format_get_current_##kind(Abstractformat *f, xx_##kind##_state *s) { \
    if (!f || !s || s->format != f || !s->has_record || s->failed || !f->get_current_##kind) return NULL; \
    return f->get_current_##kind(f, s); \
} \
bool xx_format_##kind##_move_to_next(Abstractformat *f, xx_##kind##_state *s, xx_pd_struct *pd) { \
    if (!f || !s || s->format != f || !f->kind##_move_to_next) { \
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid " #kind " stream"); return false; \
    } \
    if (!s->has_record || s->failed) return false; \
    return f->kind##_move_to_next(f, s, pd); \
} \
void xx_format_free_##plural##_reading(Abstractformat *f, xx_##kind##_state *s) { \
    if (!s) return; \
    if (f && s->format == f && f->free_##plural##_reading) f->free_##plural##_reading(f, s); \
    else xx_##kind##_state_free(s); \
}

XX_STREAM_RUNTIME(import, imports)
XX_STREAM_RUNTIME(export, exports)
XX_STREAM_RUNTIME(resource, resources)
XX_STREAM_RUNTIME(metadata, metadata)
XX_STREAM_RUNTIME(symbol, symbols)
#undef XX_STREAM_RUNTIME
