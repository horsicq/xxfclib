/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_STREAM_RUNTIME_INTERNAL_H
#define XX_STREAM_RUNTIME_INTERNAL_H

#define XX_STREAM_RUNTIME(kind, plural, TYPE, GETTER)                                                                                                                 \
    void xx_##kind##_state_init(xx_##kind##_state *s, Abstractformat *f)                                                                                              \
    {                                                                                                                                                                 \
        if (!s) return;                                                                                                                                               \
        xx_mem_zero(s, sizeof(*s));                                                                                                                                   \
        s->format = f;                                                                                                                                                \
        s->current_index = -1;                                                                                                                                        \
        s->total_records = -1;                                                                                                                                        \
        xx_##kind##_record_init(&s->current_record);                                                                                                                  \
    }                                                                                                                                                                 \
    void xx_##kind##_state_cleanup(xx_##kind##_state *s)                                                                                                              \
    {                                                                                                                                                                 \
        if (!s) return;                                                                                                                                               \
        xx_##kind##_record_cleanup(&s->current_record);                                                                                                               \
        if (s->free_internal && s->internal_state) s->free_internal(s->internal_state);                                                                               \
        xx_##kind##_state_init(s, NULL);                                                                                                                              \
    }                                                                                                                                                                 \
    void xx_##kind##_state_free(xx_##kind##_state *s)                                                                                                                 \
    {                                                                                                                                                                 \
        if (!s) return;                                                                                                                                               \
        xx_##kind##_state_cleanup(s);                                                                                                                                 \
        xx_mem_free(s);                                                                                                                                               \
    }                                                                                                                                                                 \
    xx_##kind##_state *xx_format_create_##plural##_reading(Abstractformat *f, xx_pd_struct *pd)                                                                       \
    {                                                                                                                                                                 \
        const TYPE *stream = GETTER(f);                                                                                                                               \
        xx_##kind##_state *state = NULL;                                                                                                                              \
        int64_t saved;                                                                                                                                                \
        if (!f || !stream || !stream->create_##plural##_reading || !stream->get_current_##kind || !stream->kind##_move_to_next || !stream->free_##plural##_reading) { \
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Format does not support " #plural " streaming");                                                               \
            return NULL;                                                                                                                                              \
        }                                                                                                                                                             \
        if (xx_pd_is_stopped(pd)) {                                                                                                                                   \
            xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Stream creation cancelled");                                                                                       \
            return NULL;                                                                                                                                              \
        }                                                                                                                                                             \
        if (!f->device || (saved = xx_io_tell(f->device)) < 0) {                                                                                                      \
            xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot save stream input position");                                                                                    \
            return NULL;                                                                                                                                              \
        }                                                                                                                                                             \
        if (xx_format_handle_base_info(f, pd)) state = stream->create_##plural##_reading(f, pd);                                                                      \
        if (xx_io_seek64(f->device, saved, 0) != 0) {                                                                                                                 \
            if (state) stream->free_##plural##_reading(f, state);                                                                                                     \
            xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot restore stream input position");                                                                                 \
            return NULL;                                                                                                                                              \
        }                                                                                                                                                             \
        return state;                                                                                                                                                 \
    }                                                                                                                                                                 \
    const xx_##kind##_record *xx_format_get_current_##kind(Abstractformat *f, xx_##kind##_state *s)                                                                   \
    {                                                                                                                                                                 \
        const TYPE *stream = GETTER(f);                                                                                                                               \
        if (!f || !stream || !s || s->format != f || !s->has_record || s->failed || !stream->get_current_##kind) return NULL;                                         \
        return stream->get_current_##kind(f, s);                                                                                                                      \
    }                                                                                                                                                                 \
    bool xx_format_##kind##_move_to_next(Abstractformat *f, xx_##kind##_state *s, xx_pd_struct *pd)                                                                   \
    {                                                                                                                                                                 \
        const TYPE *stream = GETTER(f);                                                                                                                               \
        if (!f || !stream || !s || s->format != f || !stream->kind##_move_to_next) {                                                                                  \
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid " #kind " stream");                                                                                    \
            return false;                                                                                                                                             \
        }                                                                                                                                                             \
        if (!s->has_record || s->failed) return false;                                                                                                                \
        return stream->kind##_move_to_next(f, s, pd);                                                                                                                 \
    }                                                                                                                                                                 \
    void xx_format_free_##plural##_reading(Abstractformat *f, xx_##kind##_state *s)                                                                                   \
    {                                                                                                                                                                 \
        const TYPE *stream = GETTER(f);                                                                                                                               \
        if (!s) return;                                                                                                                                               \
        if (f && stream && s->format == f && stream->free_##plural##_reading) stream->free_##plural##_reading(f, s);                                                  \
        else xx_##kind##_state_free(s);                                                                                                                               \
    }

#endif
