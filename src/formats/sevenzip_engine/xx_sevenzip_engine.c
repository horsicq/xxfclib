/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * The separately licensed 7-Zip implementation is accessed only through the
 * bounded framed backend. Native iterator metadata and extraction staging
 * remain library-owned, with source cursors restored on every backend exit.
 */
#include "xxfclib/formats/sevenzip_engine/xx_sevenzip_engine.h"
#include "../xx_payload_members.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define Z7E_DEFAULT_BUDGET (UINT64_C(256) * 1024U * 1024U)
/* Parent protocol buffers, up to 64 borrowed sibling descriptors/basenames,
 * and one bounded joined source path share this fixed reserve. */
#define Z7E_TRANSFER_BUDGET (UINT64_C(256) * 1024U)
#define Z7E_MIN_WORKSPACE (UINT64_C(256) * 1024U)

typedef struct z7e_format {
    Abstractformat format;
    char *handler;
    char *source_path;
    char *helper_path;
    char detected_handler[64];
    const xx_list_s *parse_options;
    xx_sevenzip_backend_status status;
    bool start_only;
} z7e_format;
typedef struct z7e_member {
    uint32_t index;
    uint64_t expected_size, packed_size, retained;
    int64_t mtime;
} z7e_member;
typedef struct z7e_listing {
    Abstractformat *format;
    pm_stream *stream;
    uint64_t used, parent_limit, max_member_size;
} z7e_listing;

static uint64_t z7e_option_bytes(const xx_list_s *options) {
    uint64_t bytes = 0;
    size_t i;
    if (!options) return 0;
    /* Allow for list growth and the owned clones retained by an iterator. */
    if (options->count > 4096U) return UINT64_MAX;
    bytes = (uint64_t)options->count * sizeof(xx_meta) * 2U;
    for (i = 0; i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i);
        uint64_t size = 0;
        if (!item) return UINT64_MAX;
        if (item->var.type == XX_VAR_TYPE_STRING || item->var.type == XX_VAR_TYPE_STRING_VIEW) {
            if (item->var.val.str.len == SIZE_MAX) return UINT64_MAX;
            size = (uint64_t)item->var.val.str.len + 1U;
        }
        else if (item->var.type == XX_VAR_TYPE_WSTRING || item->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            if (item->var.val.wstr.len > SIZE_MAX / sizeof(wchar_t) - 1U) return UINT64_MAX;
            size = ((uint64_t)item->var.val.wstr.len + 1U) * sizeof(wchar_t);
        } else if (item->var.type == XX_VAR_TYPE_BYTES || item->var.type == XX_VAR_TYPE_BYTES_VIEW) size = item->var.val.bytes.size;
        if (size > UINT64_MAX - bytes) return UINT64_MAX;
        bytes += size;
    }
    return bytes;
}

static uint64_t z7e_base_bytes(Abstractformat *f, const xx_list_s *options) {
    z7e_format *engine = (z7e_format *)f;
    uint64_t bytes = 4096U + sizeof(pm_stream) + sizeof(*engine);
    uint64_t extra = z7e_option_bytes(options);
    uint64_t defaults = z7e_option_bytes(&f->list_extra_parameters);
    if (engine->handler) bytes += xx_rt_strlen(engine->handler) + 1U;
    if (engine->source_path) bytes += xx_rt_strlen(engine->source_path) + 1U;
    if (engine->helper_path) bytes += xx_rt_strlen(engine->helper_path) + 1U;
    if (extra > UINT64_MAX - bytes) return UINT64_MAX;
    bytes += extra;
    return defaults > UINT64_MAX - bytes ? UINT64_MAX : bytes + defaults;
}

static void z7e_secret_free(void *opaque) {
    volatile char *bytes = (volatile char *)opaque;
    if (bytes) {
        size_t size = xx_rt_strlen((const char *)opaque) + 1U;
        while (size--) *bytes++ = 0;
        xx_mem_free(opaque);
    }
}

static uint64_t z7e_budget(Abstractformat *f, const xx_list_s *options) {
    const xx_var *v = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t limit = v ? xx_var_get_u64(v) : Z7E_DEFAULT_BUDGET;
    return limit < Z7E_DEFAULT_BUDGET ? limit : Z7E_DEFAULT_BUDGET;
}

static bool z7e_options(Abstractformat *f, const xx_list_s *options,
                         uint64_t retained, xx_pd_struct *pd,
                         xx_sevenzip_backend_options *backend) {
    uint64_t limit = z7e_budget(f, options);
    const xx_var *v;
    xx_mem_zero(backend, sizeof(*backend));
    backend->max_member_size = UINT64_MAX;
    backend->pd = pd; backend->status = &((z7e_format *)f)->status;
    backend->source_path = ((z7e_format *)f)->source_path;
    backend->helper_path = ((z7e_format *)f)->helper_path;
    backend->start_only = ((z7e_format *)f)->start_only;
    backend->detected_handler = ((z7e_format *)f)->detected_handler;
    backend->detected_handler_capacity = sizeof(((z7e_format *)f)->detected_handler);
    if (xx_pd_is_stopped(pd)) return false;
    if (retained > UINT64_MAX - Z7E_TRANSFER_BUDGET || limit <= retained + Z7E_TRANSFER_BUDGET ||
        limit - retained - Z7E_TRANSFER_BUDGET < Z7E_MIN_WORKSPACE) {
        ((z7e_format *)f)->status = XX_SEVENZIP_BACKEND_LIMIT;
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "7-Zip operation memory limit is too small");
        return false;
    }
    backend->memory_limit = limit - retained - Z7E_TRANSFER_BUDGET;
    v = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v) backend->max_member_size = xx_var_get_u64(v);
    v = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_PASSWORD);
    if (v) {
        char *password;
        size_t size;
        if (v->type != XX_VAR_TYPE_STRING && v->type != XX_VAR_TYPE_STRING_VIEW) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "7-Zip password must be UTF-8 text");
            return false;
        }
        size = v->val.str.len;
        if (size > 65536U || (!v->val.str.ptr && size) ||
            backend->memory_limit < Z7E_MIN_WORKSPACE + size + 1U) return false;
        /* A public string view has an exact extent and need not be terminated.
         * The transport accepts a C string, so own a bounded operation copy. */
        password = (char *)xx_mem_alloc(size + 1U);
        if (!password) return false;
        if (size) xx_rt_memcpy(password, v->val.str.ptr, size);
        password[size] = 0;
        if (xx_rt_strlen(password) != size) { z7e_secret_free(password); return false; }
        backend->password = password;
        backend->memory_limit -= size + 1U;
    }
    return true;
}

static void z7e_error(Abstractformat *f, xx_pd_struct *pd) {
    xx_sevenzip_backend_status status = ((z7e_format *)f)->status;
    const char *message = "7-Zip could not completely read this archive";
    if (status == XX_SEVENZIP_BACKEND_PASSWORD) { f->is_crypted = true; message = "7-Zip password is required or incorrect"; }
    else if (status == XX_SEVENZIP_BACKEND_UNAVAILABLE) message = "Bundled 7-Zip helper is unavailable";
    else if (status == XX_SEVENZIP_BACKEND_LIMIT) message = "7-Zip operation resource limit exceeded";
    else if (status == XX_SEVENZIP_BACKEND_UNSUPPORTED) message = "7-Zip does not support this archive operation";
    else if (status == XX_SEVENZIP_BACKEND_TIMEOUT) message = "7-Zip operation timed out";
    if (pd && !pd->last_error) xx_pd_set_error(pd, XXFC_ERR_GENERIC, message);
}

static bool z7e_reserved(const char *component, size_t size) {
    char base[16];
    size_t i, length = 0;
    while (length < size && component[length] != '.') ++length;
    while (length && component[length - 1U] == ' ') --length;
    if (length >= sizeof(base)) return false;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)component[i];
        base[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    base[length] = 0;
    if (!xx_rt_strcmp(base, "CON") || !xx_rt_strcmp(base, "PRN") ||
        !xx_rt_strcmp(base, "AUX") || !xx_rt_strcmp(base, "NUL") ||
        !xx_rt_strcmp(base, "CONIN$") || !xx_rt_strcmp(base, "CONOUT$")) return true;
    if (length >= 4U && ((!xx_rt_memcmp(base, "COM", 3U)) || (!xx_rt_memcmp(base, "LPT", 3U)))) {
        if (length == 4U && base[3] >= '1' && base[3] <= '9') return true;
        /* Windows also treats ISO-8859-1 superscript 1, 2 and 3 as digits in
         * device names (UTF-8 C2 B9/B2/B3), including with an extension. */
        if (length == 5U && (unsigned char)base[3] == 0xc2U &&
            ((unsigned char)base[4] == 0xb9U || (unsigned char)base[4] == 0xb2U ||
             (unsigned char)base[4] == 0xb3U)) return true;
    }
    return false;
}

/* Validate a path only when extracting. Metadata and RAM-only testing also
 * support names legal on another filesystem but unrepresentable on Windows. */
static char *z7e_path(const char *source, uint32_t index) {
    char *path;
    size_t i, start = 0, size;
    if (!source || !*source) {
        char fallback[40];
        xx_rt_snprintf(fallback, sizeof(fallback), "data-%u.bin", index);
        return xx_str_dup(fallback);
    }
    size = xx_rt_strlen(source);
    if (size > 16384U) return NULL;
    path = xx_str_dup(source);
    if (!path) return NULL;
    for (i = 0; i <= size; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c == '\\') c = (unsigned char)(path[i] = '/');
        if (c && (c < 32 || c == 127 || c == ':' || c == '<' || c == '>' ||
                  c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (!c || c == '/') {
            size_t component = i - start;
            if (!component || (component == 1 && path[start] == '.') ||
                (component == 2 && path[start] == '.' && path[start + 1] == '.') ||
                path[i - 1] == '.' || path[i - 1] == ' ' || z7e_reserved(path + start, component)) goto bad;
            start = i + 1;
        }
    }
    return path;
bad:
    xx_str_free(path); return NULL;
}

static char *z7e_name(const char *source, uint32_t index) {
    char *name;
    size_t i;
    if (!source || !*source) return z7e_path(source, index);
    if (xx_rt_strlen(source) > 65536U) return NULL;
    name = xx_str_dup(source);
    if (name) for (i = 0; name[i]; ++i) if (name[i] == '\\') name[i] = '/';
    return name;
}

static bool z7e_entry(void *opaque, const xx_sevenzip_backend_entry *entry) {
    z7e_listing *listing = (z7e_listing *)opaque;
    pm_stream *s = listing->stream;
    pm_member *m;
    z7e_member *context;
    char *name;
    uint64_t growth = s->count == s->capacity ? (s->capacity ? s->capacity : 8U) * sizeof(pm_member) : 0;
    uint64_t bytes;
    if (!entry || (entry->directory && entry->path &&
        (!entry->path[0] || !xx_rt_strcmp(entry->path, "/")))) return entry != NULL;
    if (!entry->directory && entry->size != UINT64_MAX && entry->size > listing->max_member_size) return false;
    if (s->count >= 1000000U || !(name = z7e_name(entry->path, entry->index))) return false;
    /* The current public record owns an additional name and scalar metadata;
     * conservatively reserve that name for each retained listing member. */
    bytes = growth + sizeof(*context) + 2U * (uint64_t)xx_rt_strlen(name) + 256U;
    if (listing->used > listing->parent_limit || bytes > listing->parent_limit - listing->used) {
        xx_str_free(name); return false;
    }
    context = (z7e_member *)xx_mem_calloc(1, sizeof(*context));
    if (!context) { xx_str_free(name); return false; }
    if (!pm_add(listing->format, s, "file", 0, 0)) {
        xx_mem_free(context); xx_str_free(name); return false;
    }
    context->index = entry->index; context->expected_size = entry->directory ? 0U : entry->size;
    context->packed_size = entry->packed_size; context->mtime = entry->mtime;
    m = s->items + s->count - 1U;
    m->context = context; m->free_context = xx_mem_free; m->display_name = name;
    m->size = entry->directory ? 0 : (int64_t)entry->size;
    m->packed_size = (int64_t)entry->packed_size;
    m->offset = -1; m->directory = entry->directory; m->source_encrypted = entry->encrypted;
    listing->format->is_crypted = listing->format->is_crypted || entry->encrypted;
    listing->used += bytes;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *stream, xx_pd_struct *pd) {
    z7e_format *engine = (z7e_format *)f;
    z7e_listing listing;
    xx_sevenzip_backend_options options;
    uint64_t budget = z7e_budget(f, engine->parse_options);
    size_t i;
    int64_t length = pm_available(f);
    if (length < 0 || !z7e_options(f, engine->parse_options, z7e_base_bytes(f, engine->parse_options), pd, &options)) return false;
    xx_mem_zero(&listing, sizeof(listing));
    listing.format = f; listing.stream = stream; listing.used = z7e_base_bytes(f, engine->parse_options);
    if (options.password) listing.used += xx_rt_strlen(options.password) + 1U;
    /* Reserve the transport and half the remainder for the helper before
     * retaining any callback names, entry array or per-member context. */
    {
        uint64_t half = (budget - Z7E_TRANSFER_BUDGET) / 2U;
        if (options.memory_limit > half) options.memory_limit = half;
    }
    if (options.memory_limit < Z7E_MIN_WORKSPACE) { z7e_secret_free((void *)options.password); return false; }
    listing.parent_limit = budget - Z7E_TRANSFER_BUDGET - options.memory_limit;
    listing.max_member_size = options.max_member_size;
    if (!xx_sevenzip_backend_list(f->device, f->base_address, length, engine->handler,
        &options, z7e_entry, &listing)) {
        z7e_secret_free((void *)options.password); z7e_error(f, pd); return false;
    }
    z7e_secret_free((void *)options.password);
    stream->size = length;
    for (i = 0; i < stream->count; ++i) ((z7e_member *)stream->items[i].context)->retained = listing.used;
    return true;
}

static bool z7e_record(xx_archive_record_state *state) {
    pm_stream *s = (pm_stream *)state->internal_state;
    z7e_member *context;
    if (!pm_record(state)) return false;
    context = (z7e_member *)s->items[s->index].context;
    if (!xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_UNCOMPRESSED_SIZE, context->expected_size) ||
        !xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_COMPRESSED_SIZE, context->packed_size)) return false;
    if (context->mtime && context->mtime >= -INT64_C(11644473600) &&
        context->mtime <= (int64_t)(UINT64_MAX / UINT64_C(10000000)) - INT64_C(11644473600))
        return xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_TIMESTAMP,
            (uint64_t)(context->mtime + INT64_C(11644473600)) * UINT64_C(10000000));
    return true;
}

static xx_archive_record_state *z7e_create(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    z7e_format *engine = (z7e_format *)f;
    xx_archive_record_state *state;
    pm_stream *stream;
    size_t i;
    const xx_list_s *previous = engine->parse_options;
    uint64_t bytes = z7e_base_bytes(f, options);
    if (bytes > z7e_budget(f, options) || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, f);
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        bool copied = false;
        if (!item) goto failed;
        xx_meta_init(&copy, item->meta_id);
        if (item->var.type == XX_VAR_TYPE_STRING || item->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t size = item->var.val.str.len;
            char *text = (char *)xx_mem_alloc(size + 1U);
            if (text && (!size || item->var.val.str.ptr)) {
                if (size) xx_rt_memcpy(text, item->var.val.str.ptr, size);
                text[size] = 0;
                copied = xx_rt_strlen(text) == size && xx_var_set_str_take(&copy.var, text, size);
            }
            if (!copied) xx_mem_free(text);
        } else if (item->var.type == XX_VAR_TYPE_WSTRING || item->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t size = item->var.val.wstr.len;
            size_t j;
            wchar_t *text = (wchar_t *)xx_mem_alloc((size + 1U) * sizeof(wchar_t));
            if (text && (!size || item->var.val.wstr.ptr)) {
                if (size) xx_rt_memcpy(text, item->var.val.wstr.ptr, size * sizeof(wchar_t));
                text[size] = 0;
                for (j = 0; j < size && text[j]; ++j) {}
                copied = j == size && xx_var_set_wstr_take(&copy.var, text, size);
            }
            if (!copied) xx_mem_free(text);
        } else if (item->var.type == XX_VAR_TYPE_BYTES_VIEW)
            copied = xx_var_set_bytes(&copy.var, item->var.val.bytes.data, item->var.val.bytes.size);
        else copied = xx_var_copy(&copy.var, &item->var);
        if (copied && item->meta_id == XX_META_ID_OPT_PASSWORD && copy.var.type == XX_VAR_TYPE_STRING)
            copy.var.free_fn = z7e_secret_free;
        if (!copied || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto failed;
        }
    }
    engine->parse_options = &state->options;
    stream = pm_open(f, pd);
    engine->parse_options = previous;
    if (!stream) goto failed;
    state->internal_state = stream; state->free_internal = pm_free_stream;
    state->total_records = (int64_t)stream->count;
    state->has_record = stream->count != 0;
    if (state->has_record && !z7e_record(state)) goto failed;
    return state;
failed:
    engine->parse_options = previous;
    xx_archive_record_state_free(state);
    return NULL;
}

static bool z7e_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    return pm_next(f, state, pd) && z7e_record(state);
}

static bool z7e_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    pm_stream *s;
    pm_member *member;
    z7e_member *context;
    xx_sevenzip_backend_options options;
    const xx_var *value;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL, *safe_name = NULL;
    xx_io_device *output = NULL;
    bool okay = false, overwrite = false;
    if (!f || !state || state->format != f || !state->has_record || xx_pd_is_stopped(pd)) return false;
    s = (pm_stream *)state->internal_state; member = s->items + s->index;
    context = (z7e_member *)member->context;
    if (!z7e_options(f, &state->options, context->retained, pd, &options)) return false;
    if (context->expected_size != UINT64_MAX && context->expected_size > options.max_member_size) {
        z7e_secret_free((void *)options.password); return false;
    }
    value = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (value) {
        if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(value);
        else if (value->type == XX_VAR_TYPE_WSTRING || value->type == XX_VAR_TYPE_WSTRING_VIEW)
            base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(value));
        if (!base) goto done;
        safe_name = z7e_path(member->display_name, context->index);
        if (!safe_name) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Archive member name is not a safe relative output path");
            goto done;
        }
        path = base[0] ? xx_str_concat3(base, "/", safe_name) : xx_str_dup(safe_name);
        if (!path) goto done;
        value = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
        overwrite = value && xx_var_get_bool(value);
        if (!member->directory) {
            if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
            output = pm_stage(path, &stage);
            if (!output) goto done;
        }
    }
    /* Even directories and empty files reach the backend, which verifies the
     * member index and source state. TEST uses a NULL destination throughout. */
    okay = xx_sevenzip_backend_read(f->device, f->base_address, pm_available(f),
        ((z7e_format *)f)->handler, context->index, context->expected_size, output, &options);
    if (!okay) z7e_error(f, pd);
    if (okay && member->directory && path) okay = xx_store_create_dirs_a(path, true);
done:
    if (output && xx_io_close(output) != 0) okay = false;
    if (okay && stage) okay = !xx_pd_is_stopped(pd) && xx_io_file_replace_a(stage, path, overwrite);
    if (stage) { if (!okay) (void)xx_io_file_remove_a(stage); xx_str_free(stage); }
    z7e_secret_free((void *)options.password);
    xx_str_free(path); xx_str_free(owned); xx_str_free(safe_name);
    return okay;
}

static bool z7e_valid(Abstractformat *f, xx_pd_struct *pd) {
    if (pm_valid(f, pd)) return true;
    /* A locked encrypted header is a valid archive requiring credentials.
     * Keep the format selectable so a caller can set a password and retry. */
    return ((z7e_format *)f)->status == XX_SEVENZIP_BACKEND_PASSWORD;
}

static bool z7e_handle(Abstractformat *f, xx_pd_struct *pd) {
    if (pm_handle(f, pd)) return true;
    if (((z7e_format *)f)->status != XX_SEVENZIP_BACKEND_PASSWORD) return false;
    f->is_valid = true; f->is_crypted = true; f->base_info_handled = true;
    f->format_size = pm_available(f); f->number_of_archive_records = 0;
    return true;
}

Abstractformat *xx_sevenzip_engine_create(xx_io_device *device, int64_t base, const char *handler) {
    z7e_format *engine;
    if (!device || base < 0 || (handler && xx_rt_strlen(handler) > 63U)) return NULL;
    engine = (z7e_format *)xx_mem_calloc(1, sizeof(*engine));
    if (!engine) return NULL;
    if (handler && !(engine->handler = xx_str_dup(handler))) { xx_mem_free(engine); return NULL; }
    pm_init(&engine->format, device, base, XX_FILE_TYPE_UNKNOWN, handler ? handler : "");
    engine->format.check_is_valid = z7e_valid;
    engine->format.handle_base_info = z7e_handle;
    engine->format.create_archive_records_reading = z7e_create;
    engine->format.archive_record_move_to_next = z7e_next;
    engine->format.unpack_current_archive_record = z7e_unpack;
    return &engine->format;
}

void xx_sevenzip_engine_free(Abstractformat *format) {
    z7e_format *engine = (z7e_format *)format;
    if (!engine) return;
    xx_format_cleanup_extra_parameters(format);
    xx_str_free(engine->handler); xx_str_free(engine->source_path); xx_str_free(engine->helper_path); xx_mem_free(engine);
}

Abstractformat *xx_sevenzip_engine_create_helper(xx_io_device *device,int64_t base,
    const char *handler,const char *filename,xx_file_type_t type,const char *extension) {
#ifdef _WIN32
    wchar_t path[32768];DWORD n;wchar_t *leaf;Abstractformat *f;z7e_format *engine;
    if(!filename || !filename[0] || xx_rt_strchr(filename,'/') || xx_rt_strchr(filename,'\\') || xx_rt_strchr(filename,':') ||
       !xx_rt_strcmp(filename,".") || !xx_rt_strcmp(filename,".."))return NULL;
    n=GetModuleFileNameW(NULL,path,32768);if(!n || n>=32768)return NULL;
    while(n && path[n-1]!=L'\\' && path[n-1]!=L'/')--n;
    leaf=xx_str_utf8_to_unicode(filename);if(!n || !leaf || n+xx_str_wlen(leaf)>=32768) { xx_str_wfree(leaf);return NULL; }
    xx_rt_memcpy(path+n,leaf,(xx_str_wlen(leaf)+1U)*sizeof(*leaf));xx_str_wfree(leaf);
    f=xx_sevenzip_engine_create(device,base,handler);if(!f)return NULL;engine=(z7e_format *)f;
    engine->helper_path=xx_str_unicode_to_utf8(path);if(!engine->helper_path) { xx_sevenzip_engine_free(f);return NULL; }
    f->file_type=type;xx_format_set_extension(f,extension);return f;
#else
    (void)device;(void)base;(void)handler;(void)filename;(void)type;(void)extension;return NULL;
#endif
}

xx_sevenzip_backend_status xx_sevenzip_engine_get_status(const Abstractformat *format) {
    return format ? ((const z7e_format *)format)->status : XX_SEVENZIP_BACKEND_FORMAT;
}

const char *xx_sevenzip_engine_get_handler(const Abstractformat *format) {
    const z7e_format *engine = (const z7e_format *)format;
    if (!engine) return NULL;
    return engine->detected_handler[0] ? engine->detected_handler : engine->handler;
}

bool xx_sevenzip_engine_set_source_path(Abstractformat *format, const char *source_path) {
    z7e_format *engine = (z7e_format *)format;
    char *copy;
    if (!engine) return false;
    copy = source_path ? xx_str_dup(source_path) : NULL;
    if (source_path && !copy) return false;
    xx_str_free(engine->source_path); engine->source_path = copy;
    return true;
}

bool xx_sevenzip_engine_set_start_only(Abstractformat *format, bool start_only) {
    z7e_format *engine = (z7e_format *)format;
    if (!engine) return false;
    if (engine->start_only == start_only) return true;
    engine->start_only = start_only;
    engine->detected_handler[0] = 0;
    engine->status = XX_SEVENZIP_BACKEND_FORMAT;
    format->split_format_handled = false;
    format->base_info_handled = false;
    format->is_valid = false;
    format->is_crypted = false;
    format->format_size = -1;
    format->number_of_archive_records = 0;
    xx_format_invalidate_memory_map(format);
    return true;
}
