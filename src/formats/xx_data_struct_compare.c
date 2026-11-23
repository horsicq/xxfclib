/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Compare the DataStructs of two files (see xx_data_struct_compare.h). */
#include "xxfclib/formats/xx_data_struct_compare.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/sort/xx_sort.h"

#include <string.h>
#include <wchar.h>

#define DSC_NONE ((size_t)-1)
#define DSC_CHUNK 65536U

/* One structure or field of one side, with its pairing key. */
typedef struct dsc_key {
    const void *name;  /* const char * (structures) or const wchar_t * (fields) */
    uint64_t occurrence;
    size_t index;      /* position in its own list */
    size_t match;      /* position of the partner in the other list, DSC_NONE */
} dsc_key;

typedef struct dsc_side {
    Abstractformat *format;
    xx_io_device *device;
    xx_data_struct *structs;
    dsc_key *keys;
    size_t count;
    size_t capacity;
} dsc_side;

typedef struct dsc_fields {
    xx_data_struct_record *records;
    dsc_key *keys;
    size_t count;
    size_t capacity;
    bool decoded;      /* false: no field decoder for this structure */
} dsc_fields;

typedef struct dsc_field_row {
    xx_data_struct_diff_kind kind;
    uint32_t what;
    size_t first;      /* DSC_NONE when absent */
    size_t second;
    uint64_t occurrence;
} dsc_field_row;

typedef struct dsc_context {
    dsc_side side[2];
    uint32_t options;
    xx_data_struct_diff_callback callback;
    void *user;
    xx_data_struct_compare_stats *stats;
    xx_pd_struct *pd;
    uint8_t *buffer[2];
    bool failed;
} dsc_context;

const char *xx_data_struct_diff_kind_to_string(xx_data_struct_diff_kind kind)
{
    switch (kind) {
    case XX_DATA_STRUCT_DIFF_SAME: return "same";
    case XX_DATA_STRUCT_DIFF_CHANGED: return "changed";
    case XX_DATA_STRUCT_DIFF_ONLY_FIRST: return "only-first";
    case XX_DATA_STRUCT_DIFF_ONLY_SECOND: return "only-second";
    default: return "unknown";
    }
}

static bool dsc_stopped(const dsc_context *ctx)
{
    return ctx->pd && xx_pd_is_stopped(ctx->pd);
}

/* ---- pairing by (name, occurrence) -------------------------------------- */

static int dsc_cmp_text(const void *a, const void *b, bool wide)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return wide ? wcscmp((const wchar_t *)a, (const wchar_t *)b)
                : strcmp((const char *)a, (const char *)b);
}

static int dsc_key_cmp(const void *left, const void *right, void *context)
{
    const dsc_key *a = (const dsc_key *)left;
    const dsc_key *b = (const dsc_key *)right;
    int r = dsc_cmp_text(a->name, b->name, context != NULL);

    if (r) return r;
    if (a->occurrence != b->occurrence) return a->occurrence < b->occurrence ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sets occurrence (rank among equal names, in list order) and match. */
static bool dsc_pair(dsc_key *a, size_t na, dsc_key *b, size_t nb, bool wide)
{
    dsc_key *sa = NULL, *sb = NULL;
    void *context = wide ? (void *)1 : NULL;
    size_t i, j;

    if (na) {
        sa = (dsc_key *)xx_mem_alloc(na * sizeof(*sa));
        if (!sa) return false;
    }
    if (nb) {
        sb = (dsc_key *)xx_mem_alloc(nb * sizeof(*sb));
        if (!sb) {
            xx_mem_free(sa);
            return false;
        }
    }
    for (i = 0; i < na; ++i) { a[i].occurrence = 0; a[i].match = DSC_NONE; sa[i] = a[i]; }
    for (i = 0; i < nb; ++i) { b[i].occurrence = 0; b[i].match = DSC_NONE; sb[i] = b[i]; }
    /* Sorted by (name, index): ranks within a name follow list order. */
    if (na) xx_sort_context(sa, na, sizeof(*sa), dsc_key_cmp, context);
    if (nb) xx_sort_context(sb, nb, sizeof(*sb), dsc_key_cmp, context);
    for (i = 0; i < na; ++i) {
        sa[i].occurrence = (i && !dsc_cmp_text(sa[i].name, sa[i - 1].name, wide)) ? sa[i - 1].occurrence + 1U : 0U;
        a[sa[i].index].occurrence = sa[i].occurrence;
    }
    for (i = 0; i < nb; ++i) {
        sb[i].occurrence = (i && !dsc_cmp_text(sb[i].name, sb[i - 1].name, wide)) ? sb[i - 1].occurrence + 1U : 0U;
        b[sb[i].index].occurrence = sb[i].occurrence;
    }
    /* Both are now ordered by (name, occurrence): merge. */
    i = j = 0;
    while (i < na && j < nb) {
        int r = dsc_cmp_text(sa[i].name, sb[j].name, wide);
        if (!r) r = sa[i].occurrence < sb[j].occurrence ? -1 : (sa[i].occurrence > sb[j].occurrence ? 1 : 0);
        if (r < 0) ++i;
        else if (r > 0) ++j;
        else {
            a[sa[i].index].match = sb[j].index;
            b[sb[j].index].match = sa[i].index;
            ++i;
            ++j;
        }
    }
    xx_mem_free(sa);
    xx_mem_free(sb);
    return true;
}

/* ---- loading ------------------------------------------------------------- */

static const char *dsc_struct_name(Abstractformat *format, const xx_data_struct *ds)
{
    const char *name;

    if (ds->id == XX_DATA_STRUCT_ID_RAW_DATA) return "RAW_DATA";
    name = xx_format_data_struct_id_to_string(format, ds->id);
    return name ? name : "UNKNOWN";
}

static bool dsc_side_append(dsc_side *side, const xx_data_struct *ds)
{
    if (side->count == side->capacity) {
        size_t capacity = side->capacity ? side->capacity * 2U : 64U;
        xx_data_struct *structs = (xx_data_struct *)xx_mem_realloc(side->structs, capacity * sizeof(*structs));
        dsc_key *keys;

        if (!structs) return false;
        side->structs = structs;
        keys = (dsc_key *)xx_mem_realloc(side->keys, capacity * sizeof(*keys));
        if (!keys) return false;
        side->keys = keys;
        side->capacity = capacity;
    }
    side->structs[side->count] = *ds;
    side->keys[side->count].name = dsc_struct_name(side->format, ds);
    side->keys[side->count].index = side->count;
    side->keys[side->count].occurrence = 0;
    side->keys[side->count].match = DSC_NONE;
    ++side->count;
    return true;
}

/* A table of count equal entries (IMAGE_SECTION_HEADER x N) is compared entry
 * by entry, so each entry is decoded and paired on its own (same rule as
 * xdsc's occurrence listing). */
#define DSC_MAX_STRUCTS 4000000U

static bool dsc_is_repeated_table(const xx_data_struct *ds)
{
    uint64_t entry_size;

    if (ds->count <= 1U || ds->type == XX_DATA_STRUCT_TYPE_RAW_DATA || ds->entry_size <= 0 || ds->total_size <= 0)
        return false;
    entry_size = (uint64_t)ds->entry_size;
    return ds->count <= UINT64_MAX / entry_size && ds->count * entry_size <= (uint64_t)ds->total_size;
}

static bool dsc_side_add(dsc_side *side, const xx_data_struct *ds)
{
    uint64_t i;

    if (!dsc_is_repeated_table(ds)) return side->count < DSC_MAX_STRUCTS && dsc_side_append(side, ds);
    if (ds->count > DSC_MAX_STRUCTS - side->count) return false;
    for (i = 0; i < ds->count; ++i) {
        xx_data_struct entry = *ds;
        int64_t delta = (int64_t)(i * (uint64_t)ds->entry_size);

        if (ds->offset >= 0) entry.offset = ds->offset + delta;
        if (ds->address >= 0) entry.address = ds->address + delta;
        entry.total_size = ds->entry_size;
        entry.count = 1U;
        if (!dsc_side_append(side, &entry)) return false;
    }
    return true;
}

static bool dsc_side_load(dsc_side *side, xx_pd_struct *pd)
{
    xx_data_struct_state *state = xx_format_create_data_structs_reading(side->format, pd);
    const xx_data_struct *ds;
    bool ok = true;

    if (!state) return false;
    ds = xx_format_get_current_data_struct(side->format, state);
    if (!ds && state->total_structs > 0) ok = false;
    while (ok && ds) {
        if (!dsc_side_add(side, ds)) {
            ok = false;
            break;
        }
        if (!xx_format_data_struct_move_to_next(side->format, state, pd)) {
            if (state->current_index < 0 ||
                (state->total_structs >= 0 && state->current_index < state->total_structs - 1))
                ok = false;
            break;
        }
        ds = xx_format_get_current_data_struct(side->format, state);
        if (!ds) ok = false;
    }
    xx_format_free_data_structs_reading(side->format, state);
    if (ok && pd && xx_pd_is_stopped(pd)) ok = false;
    return ok;
}

static void dsc_fields_clear(dsc_fields *fields)
{
    size_t i;

    for (i = 0; i < fields->count; ++i) xx_data_struct_record_cleanup(&fields->records[i]);
    xx_mem_free(fields->records);
    xx_mem_free(fields->keys);
    xx_mem_zero(fields, sizeof(*fields));
}

static wchar_t *dsc_wdup(const wchar_t *text, bool *ok)
{
    wchar_t *copy;

    if (!text) return NULL;
    copy = xx_str_wdup(text);
    if (!copy) *ok = false;
    return copy;
}

static bool dsc_fields_append(dsc_fields *fields, const xx_data_struct_record *record)
{
    xx_data_struct_record *copy;
    bool ok = true;

    if (fields->count == fields->capacity) {
        size_t capacity = fields->capacity ? fields->capacity * 2U : 32U;
        xx_data_struct_record *records =
            (xx_data_struct_record *)xx_mem_realloc(fields->records, capacity * sizeof(*records));
        dsc_key *keys;

        if (!records) return false;
        fields->records = records;
        keys = (dsc_key *)xx_mem_realloc(fields->keys, capacity * sizeof(*keys));
        if (!keys) return false;
        fields->keys = keys;
        fields->capacity = capacity;
    }
    copy = &fields->records[fields->count];
    xx_data_struct_record_init(copy);
    copy->offset = record->offset;
    copy->size = record->size;
    copy->property = record->property;
    copy->name = dsc_wdup(record->name, &ok);
    copy->type = dsc_wdup(record->type, &ok);
    copy->raw_value = dsc_wdup(record->raw_value, &ok);
    copy->display_value = dsc_wdup(record->display_value, &ok);
    copy->comment = dsc_wdup(record->comment, &ok);
    copy->description = dsc_wdup(record->description, &ok);
    if (!ok) {
        xx_data_struct_record_cleanup(copy);
        return false;
    }
    fields->keys[fields->count].name = copy->name ? (const void *)copy->name : (const void *)L"";
    fields->keys[fields->count].index = fields->count;
    fields->keys[fields->count].occurrence = 0;
    fields->keys[fields->count].match = DSC_NONE;
    ++fields->count;
    return true;
}

/* Decoded fields of one STRUCT. decoded=false when the format has no decoder. */
static bool dsc_fields_load(Abstractformat *format, const xx_data_struct *ds, dsc_fields *fields,
                            xx_pd_struct *pd)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(format);
    xx_data_struct_record_state *state;
    const xx_data_struct_record *record;
    bool ok = true;

    if (ds->type == XX_DATA_STRUCT_TYPE_RAW_DATA || !stream || !stream->create_data_struct_records_reading)
        return true;
    state = xx_format_create_data_struct_records_reading(format, ds, pd);
    if (!state) return true;
    fields->decoded = true;
    record = xx_format_get_current_data_struct_record(format, state);
    if (!record && state->total_records > 0) ok = false;
    while (ok && record) {
        if (!dsc_fields_append(fields, record)) {
            ok = false;
            break;
        }
        if (!xx_format_data_struct_record_move_to_next(format, state, pd)) {
            if (state->current_index < 0 ||
                (state->total_records >= 0 && state->current_index < state->total_records - 1))
                ok = false;
            break;
        }
        record = xx_format_get_current_data_struct_record(format, state);
        if (!record) ok = false;
    }
    xx_format_free_data_struct_records_reading(format, state);
    return ok;
}

/* ---- byte comparison ------------------------------------------------------ */

/* 1 equal, 0 different, -1 not comparable (location unknown / out of range). */
static int dsc_bytes_equal(dsc_context *ctx, int64_t first_offset, int64_t second_offset, int64_t size)
{
    int64_t done = 0;

    if (!ctx->side[0].device || !ctx->side[1].device || first_offset < 0 || second_offset < 0 || size < 0)
        return -1;
    if (size == 0) return 1;
    if (!xx_io_check(ctx->side[0].device, first_offset, size) ||
        !xx_io_check(ctx->side[1].device, second_offset, size))
        return -1;
    while (done < size) {
        size_t n = (size_t)(size - done > DSC_CHUNK ? DSC_CHUNK : size - done);
        if (!xx_io_read_at(ctx->side[0].device, first_offset + done, ctx->buffer[0], n) ||
            !xx_io_read_at(ctx->side[1].device, second_offset + done, ctx->buffer[1], n))
            return -1;
        if (memcmp(ctx->buffer[0], ctx->buffer[1], n) != 0) return 0;
        done += (int64_t)n;
    }
    return 1;
}

static int dsc_wtext_cmp(const wchar_t *a, const wchar_t *b)
{
    return dsc_cmp_text(a, b, true);
}

static uint32_t dsc_field_what(dsc_context *ctx, const xx_data_struct *sa, const xx_data_struct_record *a,
                               const xx_data_struct *sb, const xx_data_struct_record *b)
{
    uint32_t what = 0;
    int equal = -1;

    if (a->offset != b->offset && !(ctx->options & XX_DATA_STRUCT_COMPARE_IGNORE_LOCATION))
        what |= XX_DATA_STRUCT_DIFF_OFFSET;
    if (a->size != b->size) {
        what |= XX_DATA_STRUCT_DIFF_SIZE;
    } else if (sa->offset >= 0 && sb->offset >= 0 && a->offset >= 0 && b->offset >= 0 && a->size > 0 &&
               sa->offset <= INT64_MAX - a->offset && sb->offset <= INT64_MAX - b->offset) {
        equal = dsc_bytes_equal(ctx, sa->offset + a->offset, sb->offset + b->offset, a->size);
    }
    if (equal == 0) {
        what |= XX_DATA_STRUCT_DIFF_VALUE;
    } else if (equal < 0) {
        /* No byte location: the decoded text decides. */
        if (dsc_wtext_cmp(a->raw_value, b->raw_value) || dsc_wtext_cmp(a->display_value, b->display_value))
            what |= XX_DATA_STRUCT_DIFF_VALUE;
    }
    return what;
}

static uint32_t dsc_struct_what(dsc_context *ctx, const xx_data_struct *a, const xx_data_struct *b, bool compare_bytes)
{
    uint32_t what = 0;

    if (!(ctx->options & XX_DATA_STRUCT_COMPARE_IGNORE_LOCATION)) {
        if (a->offset != b->offset) what |= XX_DATA_STRUCT_DIFF_OFFSET;
        if (a->address != b->address) what |= XX_DATA_STRUCT_DIFF_ADDRESS;
    }
    if (a->entry_size != b->entry_size || a->total_size != b->total_size) what |= XX_DATA_STRUCT_DIFF_SIZE;
    if (a->count != b->count) what |= XX_DATA_STRUCT_DIFF_COUNT;
    if (compare_bytes) {
        if (a->total_size != b->total_size) {
            what |= XX_DATA_STRUCT_DIFF_VALUE;
        } else if (dsc_bytes_equal(ctx, a->offset, b->offset, a->total_size) == 0) {
            what |= XX_DATA_STRUCT_DIFF_VALUE;
        }
    }
    return what;
}

/* ---- emission ------------------------------------------------------------- */

static bool dsc_emit(dsc_context *ctx, xx_data_struct_diff *diff, bool field)
{
    uint64_t *counter = field ? ctx->stats->fields : ctx->stats->structs;

    if ((unsigned)diff->kind < 4U) ++counter[diff->kind];
    if (diff->kind == XX_DATA_STRUCT_DIFF_SAME && !(ctx->options & XX_DATA_STRUCT_COMPARE_SAME)) return true;
    if (!ctx->callback) return true;
    return ctx->callback(ctx->user, diff);
}

static bool dsc_field_rows_push(dsc_field_row **rows, size_t *count, size_t *capacity, const dsc_field_row *row)
{
    if (*count == *capacity) {
        size_t n = *capacity ? *capacity * 2U : 32U;
        dsc_field_row *grown = (dsc_field_row *)xx_mem_realloc(*rows, n * sizeof(**rows));
        if (!grown) return false;
        *rows = grown;
        *capacity = n;
    }
    (*rows)[(*count)++] = *row;
    return true;
}

/* Field rows of a pair in merged order (first's order, second-only inserted). */
static bool dsc_field_rows(dsc_context *ctx, const xx_data_struct *sa, dsc_fields *fa,
                           const xx_data_struct *sb, dsc_fields *fb,
                           dsc_field_row **rows, size_t *count)
{
    size_t capacity = 0, i, j = 0;

    *rows = NULL;
    *count = 0;
    if (!dsc_pair(fa->keys, fa->count, fb->keys, fb->count, true)) return false;
    for (i = 0; i < fa->count; ++i) {
        dsc_field_row row;
        size_t m = fa->keys[i].match;

        if (m != DSC_NONE) {
            for (; j < m; ++j) {
                if (fb->keys[j].match == DSC_NONE) {
                    dsc_field_row only = {XX_DATA_STRUCT_DIFF_ONLY_SECOND, 0, DSC_NONE, j, fb->keys[j].occurrence};
                    if (!dsc_field_rows_push(rows, count, &capacity, &only)) return false;
                }
            }
            if (j <= m) j = m + 1U;
            row.kind = XX_DATA_STRUCT_DIFF_SAME;
            row.what = dsc_field_what(ctx, sa, &fa->records[i], sb, &fb->records[m]);
            if (row.what) row.kind = XX_DATA_STRUCT_DIFF_CHANGED;
            row.first = i;
            row.second = m;
        } else {
            row.kind = XX_DATA_STRUCT_DIFF_ONLY_FIRST;
            row.what = 0;
            row.first = i;
            row.second = DSC_NONE;
        }
        row.occurrence = fa->keys[i].occurrence;
        if (!dsc_field_rows_push(rows, count, &capacity, &row)) return false;
    }
    for (; j < fb->count; ++j) {
        if (fb->keys[j].match == DSC_NONE) {
            dsc_field_row only = {XX_DATA_STRUCT_DIFF_ONLY_SECOND, 0, DSC_NONE, j, fb->keys[j].occurrence};
            if (!dsc_field_rows_push(rows, count, &capacity, &only)) return false;
        }
    }
    return true;
}

static bool dsc_emit_pair(dsc_context *ctx, size_t ia, size_t ib)
{
    const xx_data_struct *sa = &ctx->side[0].structs[ia];
    const xx_data_struct *sb = &ctx->side[1].structs[ib];
    dsc_fields fa, fb;
    dsc_field_row *rows = NULL;
    size_t row_count = 0, r;
    xx_data_struct_diff diff;
    bool want_fields = !(ctx->options & XX_DATA_STRUCT_COMPARE_NO_FIELDS);
    bool ok = true;

    xx_mem_zero(&fa, sizeof(fa));
    xx_mem_zero(&fb, sizeof(fb));
    if (want_fields) {
        if (!dsc_fields_load(ctx->side[0].format, sa, &fa, ctx->pd) ||
            !dsc_fields_load(ctx->side[1].format, sb, &fb, ctx->pd)) {
            ok = false;
            goto done;
        }
        if (fa.decoded || fb.decoded) {
            if (!dsc_field_rows(ctx, sa, &fa, sb, &fb, &rows, &row_count)) {
                ok = false;
                goto done;
            }
        }
    }
    xx_mem_zero(&diff, sizeof(diff));
    diff.struct_name = (const char *)ctx->side[0].keys[ia].name;
    diff.occurrence = ctx->side[0].keys[ia].occurrence;
    diff.first = sa;
    diff.second = sb;
    /* Structures with decoded fields are judged by their fields; the rest
     * (raw data, entries, structures without a decoder) by their bytes. */
    diff.what = dsc_struct_what(ctx, sa, sb, !(fa.decoded || fb.decoded));
    for (r = 0; r < row_count; ++r) {
        if (rows[r].kind != XX_DATA_STRUCT_DIFF_SAME) {
            diff.what |= XX_DATA_STRUCT_DIFF_FIELDS;
            break;
        }
    }
    diff.kind = diff.what ? XX_DATA_STRUCT_DIFF_CHANGED : XX_DATA_STRUCT_DIFF_SAME;
    if (!dsc_emit(ctx, &diff, false)) {
        ok = false;
        goto done;
    }
    for (r = 0; r < row_count; ++r) {
        diff.kind = rows[r].kind;
        diff.what = rows[r].what;
        diff.first_field = rows[r].first != DSC_NONE ? &fa.records[rows[r].first] : NULL;
        diff.second_field = rows[r].second != DSC_NONE ? &fb.records[rows[r].second] : NULL;
        diff.field_occurrence = rows[r].occurrence;
        if (!dsc_emit(ctx, &diff, true)) {
            ok = false;
            goto done;
        }
    }
done:
    xx_mem_free(rows);
    dsc_fields_clear(&fa);
    dsc_fields_clear(&fb);
    return ok;
}

static bool dsc_emit_single(dsc_context *ctx, int which, size_t index)
{
    dsc_side *side = &ctx->side[which];
    xx_data_struct_diff diff;

    xx_mem_zero(&diff, sizeof(diff));
    diff.kind = which == 0 ? XX_DATA_STRUCT_DIFF_ONLY_FIRST : XX_DATA_STRUCT_DIFF_ONLY_SECOND;
    diff.struct_name = (const char *)side->keys[index].name;
    diff.occurrence = side->keys[index].occurrence;
    if (which == 0) diff.first = &side->structs[index];
    else diff.second = &side->structs[index];
    return dsc_emit(ctx, &diff, false);
}

bool xx_format_compare_data_structs(Abstractformat *first, Abstractformat *second, uint32_t options,
                                    xx_data_struct_diff_callback callback, void *user,
                                    xx_data_struct_compare_stats *stats, xx_pd_struct *pd)
{
    dsc_context ctx;
    xx_data_struct_compare_stats local_stats;
    size_t i, j = 0;
    bool ok = false;
    int s;

    if (!first || !second) return false;
    xx_mem_zero(&ctx, sizeof(ctx));
    xx_mem_zero(&local_stats, sizeof(local_stats));
    ctx.options = options;
    ctx.callback = callback;
    ctx.user = user;
    ctx.stats = stats ? stats : &local_stats;
    xx_mem_zero(ctx.stats, sizeof(*ctx.stats));
    ctx.pd = pd;
    ctx.side[0].format = first;
    ctx.side[1].format = second;
    for (s = 0; s < 2; ++s) {
        ctx.side[s].device = xx_format_get_data_struct_device(ctx.side[s].format, pd);
        ctx.buffer[s] = (uint8_t *)xx_mem_alloc(DSC_CHUNK);
        if (!ctx.buffer[s]) goto cleanup;
        if (!dsc_side_load(&ctx.side[s], pd)) goto cleanup;
    }
    if (!dsc_pair(ctx.side[0].keys, ctx.side[0].count, ctx.side[1].keys, ctx.side[1].count, false))
        goto cleanup;
    for (i = 0; i < ctx.side[0].count; ++i) {
        size_t m = ctx.side[0].keys[i].match;

        if (dsc_stopped(&ctx)) goto cleanup;
        if (m != DSC_NONE) {
            for (; j < m; ++j) {
                if (ctx.side[1].keys[j].match == DSC_NONE && !dsc_emit_single(&ctx, 1, j)) goto cleanup;
            }
            if (j <= m) j = m + 1U;
            if (!dsc_emit_pair(&ctx, i, m)) goto cleanup;
        } else if (!dsc_emit_single(&ctx, 0, i)) {
            goto cleanup;
        }
    }
    for (; j < ctx.side[1].count; ++j) {
        if (ctx.side[1].keys[j].match == DSC_NONE && !dsc_emit_single(&ctx, 1, j)) goto cleanup;
    }
    ok = !dsc_stopped(&ctx);

cleanup:
    for (s = 0; s < 2; ++s) {
        xx_mem_free(ctx.side[s].structs);
        xx_mem_free(ctx.side[s].keys);
        xx_mem_free(ctx.buffer[s]);
    }
    return ok;
}
