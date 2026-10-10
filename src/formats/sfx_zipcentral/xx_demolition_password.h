/* SPDX-License-Identifier: MIT
 * Private static Demolition Core ZIP credential locator. Never executes code.
 * Producer imports and bounded x86 string arguments identify candidates;
 * the ZIP reader must authenticate each member before publishing one.
 */
#ifndef XX_DEMOLITION_PASSWORD_H
#define XX_DEMOLITION_PASSWORD_H
#include "../common/xx_carrier_helpers.h"
#define DEMOLITION_CANDIDATES 8U
#define DEMOLITION_MEMBER_LIMIT UINT64_C(67108864)
typedef struct demolition_state {
    void *zip_state;
    void (*zip_free)(void *);
    char candidates[DEMOLITION_CANDIDATES][65];
    unsigned count;
    int preferred;
    uint64_t remaining_work;
} demolition_state;
typedef struct demolition_section {
    uint32_t rva, raw, size, virtual_size, flags;
    bool rdata;
} demolition_section;
typedef struct demolition_image {
    const uint8_t *bytes;
    size_t size;
    uint32_t base;
    demolition_section sections[32];
    unsigned count;
    uint32_t assign[8], construct[8], copy[8];
    unsigned assigns, constructs, copies;
} demolition_image;
static bool demolition_span(size_t limit, uint64_t at, uint64_t size) { return at <= limit && size <= limit - at; }
static const uint8_t *demolition_rva(const demolition_image *image, uint32_t rva, size_t size) {
    unsigned i;
    for (i = 0; i < image->count; ++i) {
        const demolition_section *s = &image->sections[i];
        if (rva >= s->rva && (uint64_t)rva - s->rva <= s->size && size <= s->size - ((uint64_t)rva - s->rva))
            return image->bytes + s->raw + (rva - s->rva);
    }
    return NULL;
}
static bool demolition_string(const demolition_image *image, uint32_t rva, char *out, size_t capacity) {
    size_t i;
    for (i = 0; i < capacity; ++i) {
        const uint8_t *p;
        if ((uint64_t)rva + i > UINT32_MAX || !(p = demolition_rva(image, rva + (uint32_t)i, 1)))
            return false;
        out[i] = (char)*p;
        if (!*p)
            return true;
        if (*p < 0x20U || *p > 0x7eU)
            return false;
    }
    return false;
}
static bool demolition_iat(uint32_t *slots, unsigned *count, uint32_t va) {
    if (*count >= 8U)
        return false;
    slots[(*count)++] = va;
    return true;
}
static bool demolition_slot(const uint32_t *slots, unsigned count, uint32_t va) {
    unsigned i;
    for (i = 0; i < count; ++i)
        if (slots[i] == va)
            return true;
    return false;
}
static bool demolition_imports(demolition_image *image, uint32_t rva, uint32_t bytes, xx_pd_struct *pd) {
    static const char open_name[] =
        "?OpenForRead@ZipUtils@Core@Demolition@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAPAVZipFile@123@@Z";
    static const char delete_name[] = "?DeleteCache@ZipFile@ZipUtils@Core@Demolition@@QAEXXZ";
    static const char destructor[] = "??1ZipFile@ZipUtils@Core@Demolition@@QAE@XZ";
    static const char assign[] = "??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z";
    static const char construct[] = "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBD@Z";
    static const char copy[] = "??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@ABV01@@Z";
    bool found_open = false, found_delete = false, found_destructor = false;
    unsigned d, total = 0;
    if (!rva || bytes < 20U || bytes > 1280U)
        return false;
    for (d = 0; d < 64U && (uint64_t)(d + 1U) * 20U <= bytes; ++d) {
        const uint8_t *descriptor;
        uint32_t source, target;
        char dll[65];
        bool core, stl, ended = false;
        unsigned j;
        if (carrier_stop(pd) || (uint64_t)rva + d * 20U > UINT32_MAX ||
            !(descriptor = demolition_rva(image, rva + d * 20U, 20)))
            return false;
        if (!xx_data_get_u32(descriptor, 4, 0, false) && !xx_data_get_u32(descriptor + 4, 4, 0, false) &&
            !xx_data_get_u32(descriptor + 8, 4, 0, false) && !xx_data_get_u32(descriptor + 12, 4, 0, false) &&
            !xx_data_get_u32(descriptor + 16, 4, 0, false))
            return found_open && found_delete && found_destructor;
        if (!demolition_string(image, xx_data_get_u32(descriptor + 12, 4, 0, false), dll, sizeof(dll)))
            return false;
        core = !xx_str_icmp(dll, "Core.dll");
        stl = !xx_str_icmp(dll, "MSVCP71.dll") || !xx_str_icmp(dll, "MSVCP90.dll");
        source = xx_data_get_u32(descriptor, 4, 0, false);
        target = xx_data_get_u32(descriptor + 16, 4, 0, false);
        if (!source)
            source = target;
        if (!source || !target)
            return false;
        for (j = 0; j < 4096U && total < 4096U; ++j, ++total) {
            const uint8_t *thunk;
            uint32_t name_rva, va;
            char name[257];
            if (carrier_stop(pd) || (uint64_t)source + j * 4U > UINT32_MAX || (uint64_t)target + j * 4U > UINT32_MAX ||
                !(thunk = demolition_rva(image, source + j * 4U, 4)) || !demolition_rva(image, target + j * 4U, 4))
                return false;
            name_rva = xx_data_get_u32(thunk, 4, 0, false);
            if (!name_rva) {
                ended = true;
                break;
            }
            if (name_rva & UINT32_C(0x80000000))
                continue;
            if (name_rva > UINT32_MAX - 2U || !demolition_rva(image, name_rva, 2) ||
                !demolition_string(image, name_rva + 2U, name, sizeof(name)))
                return false;
            if (core) {
                found_open |= !xx_rt_strcmp(name, open_name);
                found_delete |= !xx_rt_strcmp(name, delete_name);
                found_destructor |= !xx_rt_strcmp(name, destructor);
            }
            if (!stl)
                continue;
            if ((uint64_t)image->base + target + j * 4U > UINT32_MAX)
                return false;
            va = image->base + target + j * 4U;
            if (!xx_rt_strcmp(name, assign) && !demolition_iat(image->assign, &image->assigns, va))
                return false;
            if (!xx_rt_strcmp(name, construct) && !demolition_iat(image->construct, &image->constructs, va))
                return false;
            if (!xx_rt_strcmp(name, copy) && !demolition_iat(image->copy, &image->copies, va))
                return false;
        }
        if (!ended)
            return false;
    }
    return false;
}
static bool demolition_executable(const demolition_image *image, uint32_t rva, size_t length) {
    unsigned i;
    for (i = 0; i < image->count; ++i) {
        const demolition_section *s = &image->sections[i];
        if ((s->flags & UINT32_C(0xe0000000)) == UINT32_C(0x60000000) && rva >= s->rva &&
            (uint64_t)rva - s->rva <= s->size && length <= s->size - ((uint64_t)rva - s->rva))
            return true;
    }
    return false;
}
static bool demolition_argument(const demolition_image *image, const demolition_section *section, size_t at,
                                size_t length) {
    const uint8_t *b = image->bytes + section->raw;
    size_t i, stop;
    if (!demolition_span(section->size, at, 16))
        return false;
    /* ZipFile::password at +0x20, imported assignment from const char*. */
    if (!xx_rt_memcmp(b + at + 5, "\x83\xc1\x20\xff\x15", 5) &&
        demolition_slot(image->assign, image->assigns, xx_data_get_u32(b + at + 10, 4, 0, false)))
        return true;
    /* Stack string constructed from const char*, then copied from the same
     * stack slot into ZipFile::password. The push adjusts ESP by four. */
    if (!xx_rt_memcmp(b + at + 5, "\x8d\x4c\x24", 3) && b[at + 8] >= 4U && !xx_rt_memcmp(b + at + 9, "\xff\x15", 2) &&
        demolition_slot(image->construct, image->constructs, xx_data_get_u32(b + at + 11, 4, 0, false))) {
        stop = section->size - at > 4096U ? at + 4096U : section->size;
        for (i = at + 15U; i + 14U <= stop; ++i) {
            unsigned reg;
            if (b[i] != 0x8dU || (b[i + 1] & 0xc7U) != 0x44U || b[i + 2] != 0x24U || b[i + 3] != b[at + 8] - 4U)
                continue;
            reg = (b[i + 1] >> 3) & 7U;
            if (b[i + 4] == 0x50U + reg && !xx_rt_memcmp(b + i + 5, "\x83\xc1\x20\xff\x15", 5) &&
                demolition_slot(image->copy, image->copies, xx_data_get_u32(b + i + 10, 4, 0, false)))
                return true;
        }
    }
    /* Newer clients assign a literal with explicit length to an owner field.
     * Require the observed source+length std::string routine, not any call. */
    if (at >= 2U && b[at - 2] == 0x6aU && b[at - 1] == length && !xx_rt_memcmp(b + at + 5, "\x8d\x8e", 2) &&
        xx_data_get_u32(b + at + 7, 4, 0, false) >= 0x100U && xx_data_get_u32(b + at + 7, 4, 0, false) <= 0x400U &&
        b[at + 11] == 0xe8U) {
        int64_t target = (int64_t)section->rva + at + 16U + (int32_t)xx_data_get_u32(b + at + 12, 4, 0, false);
        const uint8_t *callee;
        if (target >= 0 && target <= UINT32_MAX && demolition_executable(image, (uint32_t)target, 10) &&
            (callee = demolition_rva(image, (uint32_t)target, 10)) != NULL &&
            !xx_rt_memcmp(callee, "\x53\x8b\x5c\x24\x08\x56\x8b\xf1\x85\xdb", 10))
            return true;
    }
    return false;
}
static void demolition_locate(Abstractformat *f, demolition_state *state, xx_pd_struct *pd) {
    uint8_t head[64], *stub = NULL;
    demolition_image image;
    int64_t low, cab, cabend;
    uint32_t pe, table, import_rva, import_bytes;
    uint16_t optional;
    unsigned i, j, references = 0;
    char value[65];
    if (!carrier_pe(f, &low, &cab, &cabend, pd) || low < 64 || low > 1048576 || !pm_read(f, 0, head, sizeof(head)))
        return;
    stub = (uint8_t *)xx_mem_alloc((size_t)low);
    if (!stub || !pm_read(f, 0, stub, (size_t)low))
        goto done;
    xx_mem_zero(&image, sizeof(image));
    image.bytes = stub;
    image.size = (size_t)low;
    pe = xx_data_get_u32(head + 60, 4, 0, false);
    if (!demolition_span(image.size, pe, 24) || xx_rt_memcmp(stub + pe, "PE\0\0", 4) ||
        xx_data_get_u16(stub + pe + 4, 2, 0, false) != 0x14cU)
        goto done;
    image.count = xx_data_get_u16(stub + pe + 6, 2, 0, false);
    optional = xx_data_get_u16(stub + pe + 20, 2, 0, false);
    if (!image.count || image.count > 32U || optional < 224U ||
        !demolition_span(image.size, (uint64_t)pe + 24U, optional) ||
        xx_data_get_u16(stub + pe + 24, 2, 0, false) != 0x10bU ||
        xx_data_get_u32(stub + pe + 24 + 92, 4, 0, false) < 2U)
        goto done;
    image.base = xx_data_get_u32(stub + pe + 24 + 28, 4, 0, false);
    import_rva = xx_data_get_u32(stub + pe + 24 + 104, 4, 0, false);
    import_bytes = xx_data_get_u32(stub + pe + 24 + 108, 4, 0, false);
    table = pe + 24U + optional;
    if (!demolition_span(image.size, table, image.count * 40U))
        goto done;
    for (i = 0; i < image.count; ++i) {
        const uint8_t *row = stub + table + i * 40U;
        demolition_section *s = &image.sections[i];
        s->rva = xx_data_get_u32(row + 12, 4, 0, false);
        s->virtual_size = xx_data_get_u32(row + 8, 4, 0, false);
        s->raw = xx_data_get_u32(row + 20, 4, 0, false);
        s->size = xx_data_get_u32(row + 16, 4, 0, false);
        s->flags = xx_data_get_u32(row + 36, 4, 0, false);
        s->rdata = !xx_rt_memcmp(row, ".rdata\0\0", 8) && (s->flags & UINT32_C(0xe0000000)) == UINT32_C(0x40000000) &&
                   s->size <= 65536U;
        if (!demolition_span(image.size, s->raw, s->size) || (uint64_t)s->rva + s->size > UINT32_MAX ||
            (uint64_t)s->rva + s->virtual_size > UINT32_MAX)
            goto done;
        for (j = 0; j < i; ++j) {
            const demolition_section *other = &image.sections[j];
            uint64_t a = s->virtual_size > s->size ? s->virtual_size : s->size;
            uint64_t b = other->virtual_size > other->size ? other->virtual_size : other->size;
            if (a && b && s->rva < (uint64_t)other->rva + b && other->rva < (uint64_t)s->rva + a)
                goto done;
        }
    }
    if (!demolition_imports(&image, import_rva, import_bytes, pd))
        goto done;
    for (i = 0; i < image.count; ++i) {
        const demolition_section *code = &image.sections[i];
        const uint8_t *b = stub + code->raw;
        size_t at;
        if ((code->flags & UINT32_C(0xe0000000)) != UINT32_C(0x60000000))
            continue;
        for (at = 0; at + 16U <= code->size; ++at) {
            uint32_t va;
            size_t length, string_limit = 0;
            bool in_rdata = false;
            if (!(at & 4095U) && carrier_stop(pd))
                goto done;
            if (b[at] != 0x68U || (va = xx_data_get_u32(b + at + 1, 4, 0, false)) < image.base)
                continue;
            for (j = 0; j < image.count; ++j) {
                const demolition_section *s = &image.sections[j];
                if (s->rdata && va - image.base >= s->rva && (uint64_t)(va - image.base) - s->rva < s->size) {
                    in_rdata = true;
                    string_limit = s->size - ((va - image.base) - s->rva);
                }
            }
            if (!in_rdata)
                continue;
            if (++references > 256U) {
                state->count = 0;
                goto done;
            }
            if (string_limit > sizeof(value))
                string_limit = sizeof(value);
            if (!demolition_string(&image, va - image.base, value, string_limit) || !(length = xx_rt_strlen(value)) ||
                !demolition_argument(&image, code, at, length))
                continue;
            for (j = 0; j < state->count; ++j)
                if (!xx_rt_strcmp(state->candidates[j], value))
                    break;
            if (j == state->count) {
                if (state->count == DEMOLITION_CANDIDATES) {
                    state->count = 0;
                    goto done;
                }
                xx_rt_memcpy(state->candidates[state->count++], value, length + 1U);
            }
            xx_mem_zero(value, sizeof(value));
        }
    }
done:
    xx_mem_zero(value, sizeof(value));
    if (stub) {
        xx_mem_zero(stub, (size_t)low);
        xx_mem_free(stub);
    }
    if (carrier_stop(pd)) {
        xx_mem_zero(state->candidates, sizeof(state->candidates));
        state->count = 0;
    }
}
static void demolition_free(void *ptr) {
    demolition_state *state = (demolition_state *)ptr;
    if (!state)
        return;
    if (state->zip_free && state->zip_state)
        state->zip_free(state->zip_state);
    xx_mem_zero(state, sizeof(*state));
    xx_mem_free(state);
}
static void demolition_attach(xx_archive_record_state *state, demolition_state *demo) {
    state->internal_state = demo;
    state->free_internal = demolition_free;
}
static demolition_state *demolition_detach(xx_archive_record_state *state) {
    demolition_state *demo;
    if (!state || state->free_internal != demolition_free)
        return NULL;
    demo = (demolition_state *)state->internal_state;
    state->internal_state = demo->zip_state;
    state->free_internal = demo->zip_free;
    return demo;
}
static bool demolition_password_meta(xx_archive_record *record, const char *value) {
    xx_meta recovered;
    xx_meta_init(&recovered, XX_META_ID_PASSWORD);
    if (!xx_var_set_str(&recovered.var, value))
        return false;
    recovered.var.free_fn = pm_free_password;
    if (!xx_list_append(&record->list_meta, &recovered)) {
        xx_meta_cleanup(&recovered);
        return false;
    }
    return true;
}
/* Match ZIP's integer limit contract. Invalid limits prohibit recovery;
 * valid caller budgets can only lower the private discovery ceilings. */
static bool demolition_lower_limit(const Abstractformat *format, const xx_list_s *options, uint32_t id,
                                   uint64_t *ceiling) {
    const xx_var *value = xx_format_resolve_extra_parameter(format, options, id);
    uint64_t limit;
    if (!value)
        return true;
    switch ((xx_var_type_t)value->type) {
    case XX_VAR_TYPE_UINT8:
    case XX_VAR_TYPE_UINT16:
    case XX_VAR_TYPE_UINT32:
    case XX_VAR_TYPE_UINT64:
        limit = xx_var_get_u64(value);
        break;
    case XX_VAR_TYPE_INT8:
    case XX_VAR_TYPE_INT16:
    case XX_VAR_TYPE_INT32:
    case XX_VAR_TYPE_INT64:
        if (xx_var_get_i64(value) < 0)
            return false;
        limit = (uint64_t)xx_var_get_i64(value);
        break;
    default:
        return false;
    }
    if (limit < *ceiling)
        *ceiling = limit;
    return true;
}
/* Failed candidate probes must not replace the caller's error or options,
 * create output files, or turn a header-byte password match into proof. */
static bool demolition_record(xx_sfx_zipcentral *r, xx_archive_record_state *state, demolition_state *demo,
                              xx_pd_struct *pd) {
    xx_archive_record *record = &state->current_record;
    xx_list_s saved_options = state->options;
    xx_list_s saved_parameters = r->inner.format.list_extra_parameters;
    xx_list_s options;
    xx_meta password, limit, memory;
    int saved_error = pd ? pd->last_error : 0;
    char saved_text[128];
    int64_t cursor = xx_io_tell(r->inner.format.device);
    uint64_t plain = xx_archive_record_get_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
    uint64_t method = xx_archive_record_get_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, UINT64_MAX);
    uint64_t flags = xx_archive_record_get_meta_u64(record, XX_META_ID_FLAGS, UINT64_MAX);
    uint64_t member_ceiling = DEMOLITION_MEMBER_LIMIT;
    uint64_t memory_ceiling = UINT64_C(201326592);
    unsigned i;
    bool ok = true;
    if (!state->has_record || !demo || !demo->count ||
        !xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) ||
        xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false) || (method != 0U && method != 8U) ||
        (flags & ~UINT64_C(0x080f)) || plain > DEMOLITION_MEMBER_LIMIT || record->compressed_size < 12 ||
        (uint64_t)record->compressed_size > DEMOLITION_MEMBER_LIMIT)
        return !carrier_stop(pd);
    if (!demolition_lower_limit(&r->format, &saved_options, XX_META_ID_OPT_MAX_MEMBER_SIZE, &member_ceiling) ||
        !demolition_lower_limit(&r->format, &saved_options, XX_META_ID_OPT_MEMORY_LIMIT, &memory_ceiling) ||
        plain > member_ceiling ||
        (uint64_t)record->compressed_size +
                (record->compressed_size > 12 ? (uint64_t)record->compressed_size - 12U : 1U) + (plain ? plain : 1U) >
            memory_ceiling)
        return !carrier_stop(pd);
    if (pd)
        xx_rt_memcpy(saved_text, pd->error_string, sizeof(saved_text));
    xx_list_init(&options, sizeof(xx_meta), NULL);
    xx_meta_init(&password, XX_META_ID_OPT_PASSWORD);
    xx_meta_init(&limit, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    xx_meta_init(&memory, XX_META_ID_OPT_MEMORY_LIMIT);
    xx_var_set_u64(&limit.var, member_ceiling);
    xx_var_set_u64(&memory.var, memory_ceiling);
    /* Borrowed view metadata is consumed synchronously and never retained. */
    if (!xx_list_append(&options, &password) || !xx_list_append(&options, &limit) ||
        !xx_list_append(&options, &memory)) {
        xx_list_cleanup(&options);
        return false;
    }
    xx_mem_zero(&r->inner.format.list_extra_parameters, sizeof(xx_list_s));
    state->options = options;
    for (i = 0; i < demo->count && !carrier_stop(pd); ++i) {
        unsigned candidate = demo->preferred >= 0
                                 ? (i == 0U ? (unsigned)demo->preferred : (i <= (unsigned)demo->preferred ? i - 1U : i))
                                 : i;
        xx_meta *option = (xx_meta *)xx_list_at(&state->options, 0);
        uint64_t work = plain + (uint64_t)record->compressed_size;
        if (work > demo->remaining_work)
            break;
        demo->remaining_work -= work;
        xx_var_set_str_view(&option->var, demo->candidates[candidate], xx_rt_strlen(demo->candidates[candidate]));
        if (xx_zip_unpack_current_archive_record(&r->inner.format, state, pd) && !carrier_stop(pd)) {
            demo->preferred = (int)candidate;
            ok = demolition_password_meta(record, demo->candidates[candidate]);
            break;
        }
    }
    options = state->options;
    state->options = saved_options;
    r->inner.format.list_extra_parameters = saved_parameters;
    xx_list_cleanup(&options);
    if (cursor >= 0)
        (void)xx_io_seek64(r->inner.format.device, cursor, SEEK_SET);
    if (pd) {
        pd->last_error = saved_error;
        xx_rt_memcpy(pd->error_string, saved_text, sizeof(saved_text));
    }
    return ok && !carrier_stop(pd);
}
#endif
