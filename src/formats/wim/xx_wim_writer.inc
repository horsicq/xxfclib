/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Single-image WIM writer with stored and independently compressed resources.
 * Stored resources are ordinary
 * WIM resources, readable by Windows deployment tools, wimlib and 7-Zip.
 * Directory framing is independently implemented from the documented WIM
 * layout and cross-checked with wimlib/include/wimlib/dentry.h and 7-Zip's
 * CPP/7zip/Archive/Wim/WimIn.cpp. No source from those projects is included.
 */
#include "xx_wim_compressor_pipe.inc"
#include "xxfclib/data/xx_data.h"
#define WIMW_MAX_ENTRIES 65536U
#define WIMW_MAX_METADATA (64U * 1024U * 1024U)

typedef struct wimw_entry {
    uint8_t *name;
    size_t name_size;
    uint32_t parent, attributes, refs;
    uint64_t timestamp, size, packed_size, offset, child_offset, entry_offset;
    uint8_t resource_flags;
    uint8_t hash[20];
    bool directory, explicit_record;
} wimw_entry;

typedef struct wimw_state {
    wimw_entry *entries;
    size_t count, capacity, names_size;
    int64_t offset;
    bool finalized, failed;
    unsigned method, level;
    uint64_t memory_limit, native_limit, max_member_size;
    wimw_encoder *encoder;
} wimw_state;

static void wimw_le(uint8_t *p, uint64_t value, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static bool wimw_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, message);
    return false;
}

static bool wimw_unsigned(const xx_var *value, uint64_t *number) {
    if (!value || value->type < XX_VAR_TYPE_INT8 || value->type > XX_VAR_TYPE_UINT64 ||
        (value->type < XX_VAR_TYPE_UINT8 && xx_var_get_i64(value) < 0)) return false;
    *number = xx_var_get_u64(value);
    return true;
}

static bool wimw_write(xx_io_device *device, const void *data, size_t size,
                        xx_pd_struct *pd) {
    size_t done = 0;
    while (done < size) {
        ssize_t amount;
        if (xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void wimw_free(void *opaque) {
    wimw_state *w = (wimw_state *)opaque;
    size_t i;
    if (!w) return;
    wimw_encoder_free(w->encoder);
    for (i = 0; i < w->count; ++i) xx_mem_free(w->entries[i].name);
    xx_mem_free(w->entries);
    xx_mem_free(w);
}

/* Strict UTF-8 -> UTF-16LE component. Reject NULs, separators, colons and
 * malformed Unicode rather than accepting a differently named output file. */
static bool wimw_component(const uint8_t *text, size_t size,
                            uint8_t *output, size_t *output_size) {
    size_t at = 0, done = 0;
    while (at < size) {
        uint32_t code = text[at++], minimum = 0;
        unsigned extra = 0;
        if (code >= 0x80U) {
            if (code >= 0xc2U && code <= 0xdfU) { code &= 31U; extra = 1; minimum = 0x80; }
            else if (code >= 0xe0U && code <= 0xefU) { code &= 15U; extra = 2; minimum = 0x800; }
            else if (code >= 0xf0U && code <= 0xf4U) { code &= 7U; extra = 3; minimum = 0x10000; }
            else return false;
            if (extra > size - at) return false;
            while (extra--) {
                uint8_t next = text[at++];
                if ((next & 0xc0U) != 0x80U) return false;
                code = (code << 6U) | (next & 63U);
            }
            if (code < minimum) return false;
        }
        if (!code || code == ':' || code == '/' || code == '\\' ||
            code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
        if (code > 0xffffU) {
            if (done > 506U) return false;
            code -= 0x10000U;
            wimw_le(output + done, 0xd800U + (code >> 10U), 2); done += 2;
            wimw_le(output + done, 0xdc00U + (code & 1023U), 2); done += 2;
        } else {
            if (done > 508U) return false;
            wimw_le(output + done, code, 2); done += 2;
        }
    }
    if (!done || (done == 2 && xx_data_get_u16(output, 2, 0, false) == '.') ||
        (done == 4 && xx_data_get_u16(output, 2, 0, false) == '.' && xx_data_get_u16(output + 2, 2, 0, false) == '.')) return false;
    *output_size = done;
    return true;
}

static uint32_t wimw_add(wimw_state *w, uint32_t parent,
                          const uint8_t *name, size_t size, bool directory) {
    wimw_entry *entry;
    size_t i;
    for (i = 1; i < w->count; ++i) {
        entry = w->entries + i;
        if (entry->parent == parent && entry->name_size == size &&
            !xx_rt_memcmp(entry->name, name, size)) return (uint32_t)i;
    }
    if (w->count == WIMW_MAX_ENTRIES || size > WIMW_MAX_METADATA - w->names_size ||
        w->native_limit < w->capacity * sizeof(*entry) + w->names_size + size + 65536U) return WIM_NONE;
    if (w->count == w->capacity) {
        size_t capacity = w->capacity ? w->capacity * 2U : 16U;
        wimw_entry *replacement;
        if (w->native_limit < (capacity + w->capacity) * sizeof(*entry) + w->names_size + size + 65536U) return WIM_NONE;
        replacement = (wimw_entry *)xx_mem_realloc(w->entries, capacity * sizeof(*replacement));
        if (!replacement) return WIM_NONE;
        w->entries = replacement; w->capacity = capacity;
    }
    entry = w->entries + w->count;
    xx_mem_zero(entry, sizeof(*entry));
    entry->name = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!entry->name) return WIM_NONE;
    if (size) xx_rt_memcpy(entry->name, name, size);
    entry->name_size = size;
    entry->parent = parent;
    entry->directory = directory;
    entry->attributes = directory ? 0x10U : 0x20U;
    w->names_size += size;
    return (uint32_t)w->count++;
}

static bool wimw_method(const xx_var *v, uint64_t *method) {
    const char *t;
    if (wimw_unsigned(v, method)) return *method <= 3;
    if (!v || (v->type != XX_VAR_TYPE_STRING && v->type != XX_VAR_TYPE_STRING_VIEW)) return false;
    t = xx_var_get_str(v); if (!t) return false;
#define WIMW_NAME(name,id) if (v->val.str.len == sizeof(name)-1 && !xx_rt_memcmp(t,name,sizeof(name)-1)) { *method=id;return true; }
    WIMW_NAME("stored",0) WIMW_NAME("xpress",1) WIMW_NAME("lzx",2) WIMW_NAME("lzms",3)
#undef WIMW_NAME
    return false;
}
static bool wimw_options(wimw_state *w, const xx_list_s *options, xx_pd_struct *pd) {
    size_t i;
    for (i = 0; options && i < xx_list_size(options); ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i); uint64_t n;
        if (!item) return false;
        if (item->meta_id == XX_META_ID_COMPRESSION_METHOD) {
            if (!wimw_method(&item->var,&n)) { return wimw_error(pd,"Unknown WIM compression method"); } w->method=(unsigned)n;
        } else if (item->meta_id == XX_META_ID_COMPRESSION_LEVEL) {
            if (!wimw_unsigned(&item->var,&n)||n>100) { return wimw_error(pd,"WIM compression level must be 0..100"); } w->level=(unsigned)n;
        } else if (item->meta_id == XX_META_ID_OPT_MEMORY_LIMIT) {
            if (!wimw_unsigned(&item->var,&n)) { return wimw_error(pd,"Invalid WIM memory limit"); } w->memory_limit=n;
        } else if (item->meta_id == XX_META_ID_OPT_MAX_MEMBER_SIZE) {
            if (!wimw_unsigned(&item->var,&n)) { return wimw_error(pd,"Invalid WIM member limit"); } w->max_member_size=n;
        } else if (item->meta_id == XX_META_ID_ENCRYPTION_METHOD && wimw_unsigned(&item->var,&n) && !n) {
        } else return wimw_error(pd,"Unsupported WIM writer option; password encryption is unavailable");
    }
    return true;
}
/* A resource is streamed with constant RAM. The offset table is patched in
 * the final requested output; the source and compression buffers never spool.
 * A wholly incompressible resource is rewritten raw at the same start. The
 * unused candidate tail remains an unreferenced gap before the next resource. */
static bool wimw_stream(Abstractformat *self,wimw_state *w,xx_io_device *source,
                        uint64_t size,wimw_entry *entry,xx_pd_struct *pd) {
    uint8_t *raw=NULL,*packed=NULL,zero[512],offset[8],raw_hash[20];xx_hash_context hash;
    uint64_t start=(uint64_t)w->offset,done=0,encoded=0,table_size=0,chunks=0,index=0;
    size_t offset_size=size>UINT32_MAX?8:4;int64_t saved=source?xx_io_tell(source):0;
    int progress=-1;bool okay=false,any=false;
    if(size>INT64_MAX-start||size>=(UINT64_C(1)<<56)||
        (source&&(saved<0||xx_io_seek64(source,0,SEEK_SET))))goto cleanup;
    entry->size=size;entry->offset=start-(uint64_t)self->base_address;
    if(!size){okay=true;goto cleanup;}
    if(!source||!xx_hash_init(&hash,XX_HASH_SHA1))goto cleanup;
    raw=(uint8_t *)xx_mem_alloc(32768);if(w->method)packed=(uint8_t *)xx_mem_alloc(32768);
    if(!raw||(w->method&&!packed)||xx_io_seek64(self->device,w->offset,SEEK_SET))goto cleanup;
    if(w->method){xx_mem_zero(zero,sizeof(zero));chunks=(size+32767)/32768;table_size=(chunks-1)*offset_size;
        if(table_size>INT64_MAX-start-size)goto cleanup;
        while(encoded<table_size){size_t n=table_size-encoded>sizeof(zero)?sizeof(zero):(size_t)(table_size-encoded);if(!wimw_write(self->device,zero,n,pd))goto cleanup;encoded+=n;}encoded=0;}
    progress=xx_pd_enter_level(pd,size,"Compressing WIM resource");
    while(done<size){size_t n=size-done>32768?32768:(size_t)(size-done),got=0,k=0;
        while(got<n){ssize_t amount;if(xx_pd_is_stopped(pd))goto cleanup;amount=xx_io_read(source,raw+got,n-got);if(amount<=0||(size_t)amount>n-got)goto cleanup;got+=(size_t)amount;}
        xx_hash_update(&hash,raw,n);
        if(w->method&&!wimw_encoder_chunk(w->encoder,raw,n,packed,&k,pd))goto cleanup;
        if(k)any=true;else k=n;
        if(xx_io_seek64(self->device,(int64_t)(start+table_size+encoded),SEEK_SET)||!wimw_write(self->device,k==n?raw:packed,k,pd))goto cleanup;
        encoded+=k;done+=n;++index;
        if(w->method&&index<chunks){wimw_le(offset,encoded,offset_size);if(xx_io_seek64(self->device,(int64_t)(start+(index-1)*offset_size),SEEK_SET)||!wimw_write(self->device,offset,offset_size,pd))goto cleanup;}
        xx_pd_set_current(pd,progress,done);if(xx_pd_is_stopped(pd))goto cleanup;
    }
    if(!xx_hash_final(&hash,entry->hash,20))goto cleanup;
    entry->packed_size=table_size+encoded;entry->resource_flags=w->method?WIM_RES_COMPRESSED:0;
    if(w->method&&(!any||entry->packed_size>=size)){
        if(!xx_hash_init(&hash,XX_HASH_SHA1)||xx_io_seek64(source,0,SEEK_SET)||xx_io_seek64(self->device,(int64_t)start,SEEK_SET))goto cleanup;
        done=0;while(done<size){size_t n=size-done>32768?32768:(size_t)(size-done);ssize_t amount;if(xx_pd_is_stopped(pd))goto cleanup;amount=xx_io_read(source,raw,n);if(amount<=0||(size_t)amount>n||!wimw_write(self->device,raw,(size_t)amount,pd))goto cleanup;xx_hash_update(&hash,raw,(size_t)amount);done+=(size_t)amount;}
        if(!xx_hash_final(&hash,raw_hash,20)||xx_rt_memcmp(raw_hash,entry->hash,20))goto cleanup;
        entry->packed_size=size;entry->resource_flags=0;
    }
    w->offset=(int64_t)(start+table_size+encoded);
    if(xx_io_seek64(self->device,w->offset,SEEK_SET)) {goto cleanup; } okay=true;
cleanup:
    if(progress>=0)xx_pd_leave_level(pd,progress);
    if(xx_pd_is_stopped(pd))okay=false;
    if(source&&saved>=0&&xx_io_seek64(source,saved,SEEK_SET))okay=false;
    xx_mem_free(raw);xx_mem_free(packed);return okay;
}

xx_archive_write_state *xx_wim_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_write_state *state;
    wimw_state *w;
    uint8_t zero[WIM_HEADER_MAX];
    if (!self || !self->device || !self->device->write || self->base_address < 0 ||
        self->base_address > INT64_MAX - WIM_HEADER_MAX || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    w = (wimw_state *)xx_mem_calloc(1, sizeof(*w));
    if (!state || !w) { xx_mem_free(state); xx_mem_free(w); return NULL; }
    w->memory_limit=UINT64_C(256)*1024*1024;w->max_member_size=UINT64_MAX;
    if(!wimw_options(w,&self->list_extra_parameters,pd)||!wimw_options(w,options,pd)){wimw_free(w);xx_mem_free(state);return NULL;}
    if(w->memory_limit<WIMW_TRANSPORT_RESERVE+131072U || w->memory_limit>UINT64_C(1024)*1024*1024){wimw_free(w);xx_mem_free(state);wimw_error(pd,"WIM writer memory limit is out of range");return NULL;}
    w->native_limit=w->memory_limit-WIMW_TRANSPORT_RESERVE;
    if(w->method){uint64_t worker=w->native_limit/2;w->native_limit-=worker;
        w->encoder=wimw_encoder_create(w->method,w->level,worker,pd);if(!w->encoder){wimw_free(w);xx_mem_free(state);return NULL;}}
    else if(w->level){wimw_free(w);xx_mem_free(state);wimw_error(pd,"Stored WIM does not accept a compression level");return NULL;}
    xx_archive_write_state_init(state, self);
    state->internal_state = w;
    state->free_internal = wimw_free;
    if (wimw_add(w, WIM_NONE, NULL, 0, true) != 0U ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        xx_archive_write_state_free(state); return NULL;
    }
    self->is_valid = false; self->base_info_handled = false; self->format_size = -1;
    xx_mem_zero(zero,sizeof(zero));
    if (!wimw_write(self->device, zero, sizeof(zero), pd)) {
        xx_archive_write_state_free(state); return NULL;
    }
    w->offset = self->base_address + WIM_HEADER_MAX;
    state->total_records = 0;
    return state;
}

bool xx_wim_pack_archive_record(Abstractformat *self, xx_archive_write_state *state,
                                const xx_archive_record *record,
                                xx_io_device *source_dev, xx_pd_struct *pd) {
    wimw_state *w;
    wimw_entry *entry;
    const xx_var *name, *value;
    const char *text;
    char *owned_text = NULL;
    xx_io_device *source = source_dev, *owned_source = NULL;
    uint8_t component[510];
    size_t size, start = 0, at, component_size, depth = 0;
    uint32_t parent = 0;
    int64_t source_size = 0;
    bool directory, okay = false;
    if (!self || !state || state->format != self || !state->internal_state || !record || state->has_record) return false;
    w = (wimw_state *)state->internal_state;
    if (w->failed || w->finalized || xx_pd_is_stopped(pd)) return false;
    name = xx_archive_record_find_meta(record, XX_META_ID_ORIGINAL_NAME);
    if (!name || xx_archive_record_find_meta(record, XX_META_ID_LINK_TARGET) ||
        xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false)) return false;
    value = xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_METHOD);
    if (value) {uint64_t method;if(!wimw_method(value,&method)||method!=w->method)return wimw_error(pd,"Per-record WIM method must match the writer");}
    value = xx_archive_record_find_meta(record, XX_META_ID_ENCRYPTION_METHOD);
    if ((value && xx_var_get_u64(value) != 0) ||
        xx_archive_record_find_meta(record, XX_META_ID_OPT_PASSWORD)) return wimw_error(pd, "WIM does not support password encryption");
    if (name->type == XX_VAR_TYPE_STRING || name->type == XX_VAR_TYPE_STRING_VIEW) {
        text = xx_var_get_str(name); size = name->val.str.len;
        if (!text || !size || size > WIM_MAX_PATH) return false;
        owned_text = (char *)xx_mem_alloc(size + 1U);
        if (!owned_text) return false;
        xx_rt_memcpy(owned_text, text, size); owned_text[size] = 0;
        text = owned_text;
    } else if (name->type == XX_VAR_TYPE_WSTRING || name->type == XX_VAR_TYPE_WSTRING_VIEW) {
        size_t units = name->val.wstr.len, i;
        wchar_t *wide;
        const wchar_t *input = xx_var_get_wstr(name);
        if (!input || !units || units > WIM_MAX_PATH) return false;
        for (i = 0; i < units; ++i) {
            uint32_t code = (uint32_t)input[i];
            if (!code || code > 0x10ffffU) return false;
            if (code >= 0xd800U && code <= 0xdbffU && sizeof(wchar_t) == 2U) {
                if (++i == units || (uint32_t)input[i] < 0xdc00U || (uint32_t)input[i] > 0xdfffU) return false;
            } else if (code >= 0xd800U && code <= 0xdfffU) return false;
        }
        wide = (wchar_t *)xx_mem_alloc((units + 1U) * sizeof(wchar_t));
        if (!wide) return false;
        xx_rt_memcpy(wide, input, units * sizeof(wchar_t)); wide[units] = 0;
        owned_text = xx_str_unicode_to_utf8(wide);
        xx_mem_free(wide);
        if (!owned_text) return false;
        text = owned_text; size = xx_rt_strlen(text);
    } else return false;
    directory = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (size && (text[size - 1] == '/' || text[size - 1] == '\\')) { directory = true; --size; }
    if (!size || size > WIM_MAX_PATH) goto cleanup;
    for (at = 0; at <= size; ++at) {
        if (at != size && text[at] != '/' && text[at] != '\\') continue;
        if (++depth > WIM_MAX_DEPTH) goto cleanup;
        if (!wimw_component((const uint8_t *)text + start, at - start, component, &component_size)) goto cleanup;
        parent = wimw_add(w, parent, component, component_size, at != size || directory);
        if (parent == WIM_NONE) goto cleanup;
        entry = w->entries + parent;
        if (entry->directory != (at != size || directory) || (at == size && entry->explicit_record)) goto cleanup;
        start = at + 1;
    }
    entry = w->entries + parent;
    value = xx_archive_record_find_meta(record, XX_META_ID_TIMESTAMP);
    if (value && !wimw_unsigned(value, &entry->timestamp)) goto cleanup;
    value = xx_archive_record_find_meta(record, XX_META_ID_EXTERNAL_ATTRS);
    if (!value) value = xx_archive_record_find_meta(record, XX_META_ID_ATTRIBUTES);
    if (value) {
        uint64_t attributes;
        if (!wimw_unsigned(value, &attributes) || attributes > UINT32_MAX) goto cleanup;
        entry->attributes = (uint32_t)attributes;
    }
    if (entry->attributes & (WIM_ATTR_ENCRYPTED | WIM_ATTR_REPARSE)) goto cleanup;
    entry->attributes = (entry->attributes & ~0x10U) | (directory ? 0x10U : 0U);
    value = xx_archive_record_find_meta(record, XX_META_ID_UNCOMPRESSED_SIZE);
    if (value) { uint64_t declared; if (!wimw_unsigned(value, &declared)) goto cleanup; }
    if (directory) {
        if (source || (value && xx_var_get_u64(value))) goto cleanup;
    } else {
        if (!source && !(value && xx_var_get_u64(value) == 0)) {
            owned_source = xx_io_file_open(text, "rb"); source = owned_source;
            if (!source) goto cleanup;
        }
        if (source && (source == self->device || (source_size = xx_io_total_size(source)) < 0)) goto cleanup;
        if (value && xx_var_get_u64(value) != (uint64_t)source_size) goto cleanup;
        if ((uint64_t)source_size>w->max_member_size || source_size > INT64_MAX - w->offset || (uint64_t)source_size >= (UINT64_C(1) << 56U)) goto cleanup;
        state->has_record = true;
        if(!wimw_stream(self,w,source,(uint64_t)source_size,entry,pd))goto cleanup;
    }
    entry->explicit_record = true;
    state->current_index = state->total_records++;
    okay = true;
cleanup:
    state->has_record = false;
    if (!okay) w->failed = true;
    xx_str_free(owned_text);
    if (owned_source) xx_io_close(owned_source);
    return okay;
}

static void wimw_resource(uint8_t *p, uint64_t size, uint64_t unpacked, uint64_t offset, uint8_t flags) {
    wimw_le(p, size, 7); p[7] = flags;
    wimw_le(p + 8, offset, 8); wimw_le(p + 16, unpacked, 8);
}

static void wimw_dentry(uint8_t *p, const wimw_entry *entry) {
    size_t length = (WIM_DENTRY_FIXED + entry->name_size + 2U + 7U) & ~(size_t)7U;
    wimw_le(p, length, 8); wimw_le(p + 8, entry->attributes, 4);
    wimw_le(p + 0x0c, UINT32_MAX, 4);
    wimw_le(p + 0x10, entry->child_offset, 8);
    wimw_le(p + 0x28, entry->timestamp, 8);
    wimw_le(p + 0x30, entry->timestamp, 8);
    wimw_le(p + 0x38, entry->timestamp, 8);
    xx_rt_memcpy(p + 0x40, entry->hash, sizeof(entry->hash));
    wimw_le(p + 0x64, entry->name_size, 2);
    if (entry->name_size) xx_rt_memcpy(p + WIM_DENTRY_FIXED, entry->name, entry->name_size);
}

bool xx_wim_finalize_archive_records_writing(Abstractformat *self,
    xx_archive_write_state *state, xx_pd_struct *pd) {
    wimw_state *w;
    uint8_t header[WIM_HEADER_MAX], *metadata = NULL, *table = NULL;
    uint8_t xml[1024], metadata_hash[20];
    static const char xml_text[] = "<WIM><IMAGE INDEX=\"1\"><NAME>XFU</NAME><DESCRIPTION>XFU archive</DESCRIPTION></IMAGE></WIM>";
    size_t i, j, metadata_size = 120U, table_size = 50U, table_at = 0, xml_size = 2U;
    uint64_t meta_offset, table_offset, xml_offset;
    wimw_entry meta_entry;xx_io_device *meta_source=NULL;
    bool okay = false;
    if (!self || !state || state->format != self || !state->internal_state || state->has_record) return false;
    w = (wimw_state *)state->internal_state;
    if (w->failed || xx_pd_is_stopped(pd)) return false;
    if (w->finalized) return true;
    /* Assign each directory's contiguous child list before serializing. */
    for (i = 0; i < w->count; ++i) {
        if (!w->entries[i].directory) continue;
        w->entries[i].child_offset = metadata_size;
        for (j = 1; j < w->count; ++j) {
            wimw_entry *entry = w->entries + j;
            size_t length;
            if (entry->parent != i) continue;
            length = (WIM_DENTRY_FIXED + entry->name_size + 2U + 7U) & ~(size_t)7U;
            if (length > WIMW_MAX_METADATA - metadata_size) goto cleanup;
            entry->entry_offset = metadata_size; metadata_size += length;
        }
        if (metadata_size > WIMW_MAX_METADATA - 8U) goto cleanup;
        metadata_size += 8U;
    }
    if(w->native_limit < w->capacity*sizeof(wimw_entry)+w->names_size+metadata_size+65536U)goto cleanup;
    metadata = (uint8_t *)xx_mem_calloc(1, metadata_size);
    if (!metadata) goto cleanup;
    wimw_le(metadata, 8, 4); /* Empty security descriptor table. */
    wimw_dentry(metadata + 8, w->entries);
    for (i = 1; i < w->count; ++i) wimw_dentry(metadata + w->entries[i].entry_offset, w->entries + i);
    if (!xx_sha1_memory(metadata, metadata_size, metadata_hash)) goto cleanup;
    for (i = 1; i < w->count; ++i) {
        wimw_entry *entry = w->entries + i;
        if (entry->directory || !entry->size) continue;
        entry->refs = 1;
        for (j = 1; j < i; ++j) {
            wimw_entry *prior = w->entries + j;
            if (prior->size == entry->size && !prior->directory &&
                !xx_rt_memcmp(prior->hash, entry->hash, 20)) {
                ++prior->refs; entry->refs = 0; break;
            }
        }
        if (entry->refs) table_size += 50U;
    }
    if(w->native_limit < w->capacity*sizeof(wimw_entry)+w->names_size+metadata_size+table_size+65536U)goto cleanup;
    table = (uint8_t *)xx_mem_calloc(1, table_size);
    if (!table) goto cleanup;
    meta_offset = (uint64_t)(w->offset - self->base_address);
    xx_mem_zero(&meta_entry,sizeof(meta_entry));
    meta_source=xx_io_mem_open_ro(metadata,metadata_size);if(!meta_source||!wimw_stream(self,w,meta_source,metadata_size,&meta_entry,pd))goto cleanup;
    table_offset = (uint64_t)(w->offset-self->base_address);
    xml_offset = table_offset + table_size;
    if (xml_offset > (uint64_t)(INT64_MAX - self->base_address) - sizeof(xml)) goto cleanup;
    for (i = 1; i < w->count; ++i) {
        const wimw_entry *entry = w->entries + i;
        if (!entry->refs) continue;
        wimw_resource(table + table_at, entry->packed_size, entry->size, entry->offset, entry->resource_flags);
        wimw_le(table + table_at + 24, 1, 2);
        wimw_le(table + table_at + 26, entry->refs, 4);
        xx_rt_memcpy(table + table_at + 30, entry->hash, 20); table_at += 50;
    }
    wimw_resource(table + table_at, meta_entry.packed_size, metadata_size, meta_offset, WIM_RES_METADATA|meta_entry.resource_flags);
    wimw_le(table + table_at + 24, 1, 2); wimw_le(table + table_at + 26, 1, 4);
    xx_rt_memcpy(table + table_at + 30, metadata_hash, 20);
    xml[0] = 0xff; xml[1] = 0xfe;
    for (i = 0; i < sizeof(xml_text) - 1U; ++i) { xml[xml_size++] = (uint8_t)xml_text[i]; xml[xml_size++] = 0; }
    if (xx_io_seek64(self->device, w->offset, SEEK_SET) != 0 ||
        !wimw_write(self->device, table, table_size, pd) || !wimw_write(self->device, xml, xml_size, pd)) goto cleanup;
    xx_mem_zero(header,sizeof(header));
    xx_rt_memcpy(header, "MSWIM\0\0\0", 8);
    wimw_le(header + 8, WIM_HEADER_MAX, 4); wimw_le(header + 0x0c, w->method==3?0x00000e00U:0x00010d00U, 4);
    if(w->method){wimw_le(header+0x10,WIM_HDR_COMPRESSION|(w->method==1?WIM_HDR_XPRESS:w->method==2?WIM_HDR_LZX:WIM_HDR_LZMS),4);wimw_le(header+0x14,32768,4);}
    xx_rt_memcpy(header + 0x18, metadata_hash, 16);
    wimw_le(header + 0x28, 1, 2); wimw_le(header + 0x2a, 1, 2); wimw_le(header + 0x2c, 1, 4);
    wimw_resource(header + 0x30, table_size, table_size, table_offset, 0);
    wimw_resource(header + 0x48, xml_size, xml_size, xml_offset, 0);
    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0 ||
        !wimw_write(self->device, header, sizeof(header), pd) ||
        xx_io_seek64(self->device, self->base_address + (int64_t)xml_offset + (int64_t)xml_size, SEEK_SET) != 0) goto cleanup;
    w->finalized = true;
    self->format_size = (int64_t)xml_offset + (int64_t)xml_size;
    self->is_valid = true; self->is_crypted = false; self->base_info_handled = false;
    ((xx_wim *)self)->archive_end = self->base_address + self->format_size;
    okay = true;
cleanup:
    if (!okay) w->failed = true;
    if(meta_source)xx_io_close(meta_source);
    xx_mem_free(metadata); xx_mem_free(table);
    return okay;
}

void xx_wim_free_archive_records_writing(Abstractformat *self,
                                          xx_archive_write_state *state) {
    (void)self;
    xx_archive_write_state_free(state);
}
