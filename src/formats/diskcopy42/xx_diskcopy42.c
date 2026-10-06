/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Apple DiskCopy 4.2, based on Apple's File Type Note $e0/8005 and verified
 * with independent libdsk 1.5.22-created MFM images. No libdsk code used.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskcopy42/xx_diskcopy42.h"
#include "xxfclib/formats/prodos/xx_prodos.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef DISKCOPY42
#define DC_TYPE XX_FILE_TYPE_DISKCOPY42
#else
#define DC_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define DC_HEADER 84U
#define DC_COPY 65536U
#define DC_TAG_UNIT 12U
typedef struct dc_view_s {
    uint32_t refs, data_size, tag_size;
    uint64_t prodos_records;
    int64_t base, size;
} dc_view;
typedef struct dc_cursor_s {
    dc_view *view;
    uint32_t index;
    xx_io_device *prodos_device;
    xx_prodos *prodos;
    xx_archive_record_state *prodos_state;
} dc_cursor;
static bool dc_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static uint32_t dc_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void dc_release(dc_view *v) { if (v && !--v->refs) xx_mem_free(v); }
static bool dc_header_valid(const uint8_t *h) {
    uint32_t data=dc_be32(h+64U), tags=dc_be32(h+68U), expected;
    if (h[0] > 63U || h[82] != 1U || h[83] != 0U) return false;
    switch (h[80]) {
    case 0U: expected=409600U; break;
    case 1U: expected=819200U; break;
    case 2U: expected=737280U; break;
    case 3U: expected=1474560U; break;
    default: return false;
    }
    if (data != expected) return false;
    if (h[80] >= 2U) return tags == 0U && dc_be32(h+76U) == 0U;
    if (tags != 0U && tags != data/512U*DC_TAG_UNIT) return false;
    return tags != 0U || dc_be32(h+76U) == 0U;
}
static bool dc_read(xx_io_device *d, int64_t pos, uint8_t *p,
                    size_t n, xx_pd_struct *pd) {
    size_t done=0U;
    if (dc_stop(pd) || xx_io_seek64(d,pos,SEEK_SET)!=0) return false;
    while (done<n) {
        ssize_t got;
        if (dc_stop(pd)) return false;
        got=xx_io_read(d,p+done,n-done);
        if (got<=0 || (size_t)got>n-done || dc_stop(pd)) return false;
        done+=(size_t)got;
    }
    return !dc_stop(pd);
}
static bool dc_checksum(xx_io_device *d, int64_t start, uint32_t length,
                        uint32_t *result, xx_pd_struct *pd) {
    uint8_t buffer[DC_COPY]; uint32_t sum=0U, done=0U;
    while (done<length) {
        size_t n=(length-done)>DC_COPY ? DC_COPY : (size_t)(length-done), i;
        if (!dc_read(d,start+(int64_t)done,buffer,n,pd)) return false;
        for (i=0U;i<n;i+=2U) {
            sum+=((uint32_t)buffer[i]<<8)|buffer[i+1U];
            sum=(sum>>1)|((sum&1U)<<31);
        }
        done+=(uint32_t)n;
    }
    *result=sum; return !dc_stop(pd);
}
static dc_view *dc_parse(Abstractformat *f, xx_pd_struct *pd) {
    xx_io_device *d; int64_t saved,total; uint8_t h[DC_HEADER];
    uint32_t data,tags,sum; dc_view *v=NULL; bool ok=false;
    if (!f || !(d=f->device) || f->base_address<0 || dc_stop(pd)) return NULL;
    saved=xx_io_tell(d); total=xx_io_total_size(d);
    if (saved<0 || total<f->base_address ||
        total-f->base_address<DC_HEADER ||
        !dc_read(d,f->base_address,h,sizeof(h),pd) || !dc_header_valid(h)) goto done;
    data=dc_be32(h+64U); tags=dc_be32(h+68U);
    if (total-f->base_address!=(int64_t)DC_HEADER+data+tags ||
        !dc_checksum(d,f->base_address+DC_HEADER,data,&sum,pd) ||
        sum!=dc_be32(h+72U)) goto done;
    if (tags && (!dc_checksum(d,f->base_address+DC_HEADER+data+DC_TAG_UNIT,
                              tags-DC_TAG_UNIT,&sum,pd) ||
                 sum!=dc_be32(h+76U))) goto done;
    v=(dc_view *)xx_mem_calloc(1U,sizeof(*v)); if (!v) goto done;
    v->refs=1U; v->base=f->base_address; v->data_size=data;
    v->tag_size=tags; v->size=DC_HEADER+(int64_t)data+tags;
    ok=!dc_stop(pd);
done:
    if (xx_io_seek64(d,saved,SEEK_SET)!=0) ok=false;
    if (!ok) { dc_release(v); return NULL; }
    return v;
}
static void dc_destroy_format(Abstractformat *f) {
    xx_diskcopy42_destroy((xx_diskcopy42 *)f);
}
void xx_diskcopy42_init(xx_diskcopy42 *d, xx_io_device *source, int64_t base) {
    if (!d) return;
    xx_mem_zero(d,sizeof(*d)); xx_format_init(&d->format,source,base);
    d->format.file_type=DC_TYPE; d->format.format_type=XX_TYPE_ARCHIVE;
    d->format.is_archive=true;
    xx_format_set_mime_type(&d->format,"application/x-apple-diskcopy42");
    xx_format_set_extension(&d->format,"dc42");
    d->format.check_is_valid=xx_diskcopy42_check_is_valid;
    d->format.handle_base_info=xx_diskcopy42_handle_base_info;
    d->format.get_format_size=xx_diskcopy42_get_format_size;
    d->format.get_number_of_archive_records=xx_diskcopy42_get_number_of_archive_records;
    d->format.create_archive_records_reading=xx_diskcopy42_create_archive_records_reading;
    d->format.get_current_archive_record=xx_diskcopy42_get_current_archive_record;
    d->format.archive_record_move_to_next=xx_diskcopy42_archive_record_move_to_next;
    d->format.unpack_current_archive_record=xx_diskcopy42_unpack_current_archive_record;
    d->format.free_archive_records_reading=xx_diskcopy42_free_archive_records_reading;
    d->format.destroy=dc_destroy_format;
}
xx_diskcopy42 *xx_diskcopy42_create(xx_io_device *source, int64_t base) {
    xx_diskcopy42 *d=(xx_diskcopy42 *)xx_mem_alloc(sizeof(*d));
    if (d) { xx_diskcopy42_init(d,source,base); } return d;
}
void xx_diskcopy42_destroy(xx_diskcopy42 *d) {
    if (!d) return;
    dc_release((dc_view *)d->internal); d->internal=NULL;
    xx_format_cleanup_extra_parameters(&d->format);
}
void xx_diskcopy42_free(xx_diskcopy42 *d) {
    if (d) { xx_diskcopy42_destroy(d); xx_mem_free(d); }
}
bool xx_diskcopy42_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    dc_view *v=dc_parse(f,pd); if (!v) return false; dc_release(v); return true;
}
bool xx_diskcopy42_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    xx_diskcopy42 *d=(xx_diskcopy42 *)f; dc_view *v;
    int64_t saved;
    if (!f || dc_stop(pd)) return false;
    if (f->base_info_handled && d->internal) return f->is_valid;
    saved=xx_io_tell(f->device);
    if (saved<0) return false;
    v=dc_parse(f,pd);
    if (!v) { f->base_info_handled=false; f->is_valid=false; return false; }
    {
        xx_io_volume range={f->device,v->base+DC_HEADER,(int64_t)v->data_size};
        xx_io_device *window=xx_io_multivolume_open(&range,1U,false);
        xx_prodos nested;
        if (window) {
            xx_prodos_init(&nested,window,0);
            if (xx_prodos_check_is_valid(&nested.format,pd) &&
                xx_prodos_handle_base_info(&nested.format,pd) &&
                !nested.truncated &&
                (uint64_t)nested.total_blocks*512U<=v->data_size)
                v->prodos_records=nested.number_of_records;
            xx_prodos_destroy(&nested);
            (void)xx_io_close(window);
        }
    }
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) {
        dc_release(v); return false;
    }
    dc_release((dc_view *)d->internal); d->internal=v;
    d->number_of_members=v->prodos_records ? (uint32_t)v->prodos_records :
        (v->tag_size ? 2U : 1U);
    f->number_of_archive_records=d->number_of_members;
    f->format_size=v->size; f->overlay_offset=-1; f->overlay_size=0;
    f->is_valid=true; f->base_info_handled=true; return true;
}
int64_t xx_diskcopy42_get_format_size(Abstractformat *f, xx_pd_struct *pd) {
    return xx_diskcopy42_handle_base_info(f,pd) ? f->format_size : -1;
}
uint64_t xx_diskcopy42_get_number_of_archive_records(Abstractformat *f,
                                                        xx_pd_struct *pd) {
    return xx_diskcopy42_handle_base_info(f,pd) ?
        ((xx_diskcopy42 *)f)->number_of_members : 0U;
}
static void dc_cursor_free(void *p) {
    dc_cursor *c=(dc_cursor *)p;
    if (c) {
        if (c->prodos_state)
            xx_prodos_free_archive_records_reading(&c->prodos->format,c->prodos_state);
        if (c->prodos) xx_prodos_free(c->prodos);
        if (c->prodos_device) (void)xx_io_close(c->prodos_device);
        dc_release(c->view); xx_mem_free(c);
    }
}
static uint32_t dc_member_size(const dc_view *v, uint32_t index) {
    return index ? v->tag_size : v->data_size;
}
static int64_t dc_member_offset(const dc_view *v, uint32_t index) {
    return v->base+DC_HEADER+(index ? v->data_size : 0U);
}
static const char *dc_member_name(uint32_t index) {
    return index ? "diskcopy42-tags.bin" : "diskcopy42-disk.img";
}
static bool dc_record(xx_archive_record *r, const dc_view *v, uint32_t index) {
    uint32_t size=dc_member_size(v,index);
    xx_archive_record_cleanup(r); xx_archive_record_init(r);
    r->header_offset=v->base; r->header_size=DC_HEADER;
    r->data_offset=dc_member_offset(v,index); r->compressed_size=size;
    return xx_archive_record_set_original_name(r,dc_member_name(index)) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,size) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,size) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,0U) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false);
}
static bool dc_forward_parameters(Abstractformat *outer,
                                  const xx_list_s *options, xx_prodos *inner) {
    const xx_list_s *sources[2]={&outer->list_extra_parameters,options};
    const xx_var *path_option;
    const char *base=NULL;
    char *owned=NULL,*prefixed=NULL;
    xx_var target;
    size_t source,index;
    bool result=true;
    for (source=0U;source<2U;++source) {
        const xx_list_s *list=sources[source];
        if (!list) continue;
        for (index=0U;index<list->count;++index) {
            const xx_meta *meta=(const xx_meta *)xx_list_at(list,index);
            if (meta && !xx_format_set_extra_parameter(&inner->format,
                                                        meta->meta_id,&meta->var))
                return false;
        }
    }
    path_option=xx_format_resolve_extra_parameter(
        outer,options,XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type==XX_VAR_TYPE_STRING ||
        path_option->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(path_option);
    else if (path_option->type==XX_VAR_TYPE_WSTRING ||
             path_option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base=owned;
    }
    if (!base) { xx_str_free(owned); return false; }
    prefixed=*base && base[strlen(base)-1U]!='/' &&
             base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/","ProDOS") : xx_str_concat(base,"ProDOS");
    xx_var_init(&target);
    if (!prefixed || !xx_var_set_str(&target,prefixed) ||
        !xx_format_set_extra_parameter(&inner->format,
                                       XX_META_ID_OPT_UNPACK_PATH,&target))
        result=false;
    xx_var_cleanup(&target);
    xx_str_free(prefixed); xx_str_free(owned);
    return result;
}
static bool dc_prodos_record(xx_archive_record *record,dc_cursor *cursor) {
    const xx_archive_record *original=xx_prodos_get_current_archive_record(
        &cursor->prodos->format,cursor->prodos_state);
    const char *name;
    char *prefixed;
    size_t index;
    if (!original || !(name=xx_archive_record_get_original_name(original)))
        return false;
    if (original->header_offset<0 || original->header_size<0 ||
        original->header_offset>(int64_t)cursor->view->data_size-
                                original->header_size ||
        (original->data_offset>=0 &&
         original->data_offset>=(int64_t)cursor->view->data_size))
        return false;
    prefixed=xx_str_concat3("ProDOS","/",name);
    if (!prefixed) return false;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset=original->header_offset<0 ? -1 :
        original->header_offset+cursor->view->base+DC_HEADER;
    record->header_size=original->header_size;
    record->data_offset=original->data_offset<0 ? -1 :
        original->data_offset+cursor->view->base+DC_HEADER;
    record->compressed_size=original->compressed_size;
    for (index=0U;index<original->list_meta.count;++index) {
        const xx_meta *meta=(const xx_meta *)xx_list_at(&original->list_meta,index);
        if (meta && !xx_archive_record_set_meta(record,meta->meta_id,&meta->var)) {
            xx_str_free(prefixed); return false;
        }
    }
    {
        bool ok=xx_archive_record_set_original_name(record,prefixed);
        xx_str_free(prefixed); return ok;
    }
}
xx_archive_record_state *xx_diskcopy42_create_archive_records_reading(
    Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    xx_diskcopy42 *d=(xx_diskcopy42 *)f; dc_view *v; dc_cursor *c;
    xx_archive_record_state *state; size_t i;
    if (!xx_diskcopy42_handle_base_info(f,pd)) return NULL;
    v=(dc_view *)d->internal;
    state=(xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    c=(dc_cursor *)xx_mem_calloc(1U,sizeof(*c));
    if (!state || !c) { xx_mem_free(state); xx_mem_free(c); return NULL; }
    ++v->refs; c->view=v;
    xx_archive_record_state_init(state,f); state->internal_state=c;
    state->free_internal=dc_cursor_free; state->total_records=d->number_of_members;
    if (options) for (i=0U;i<options->count;++i) {
        const xx_meta *original=(const xx_meta *)xx_list_at(options,i); xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy,original->meta_id);
        if (!xx_var_copy(&copy.var,&original->var) ||
            !xx_list_append(&state->options,&copy)) {
            xx_meta_cleanup(&copy); xx_archive_record_state_free(state); return NULL;
        }
    }
    if (v->prodos_records) {
        xx_io_volume range={f->device,v->base+DC_HEADER,(int64_t)v->data_size};
        c->prodos_device=xx_io_multivolume_open(&range,1U,false);
        c->prodos=c->prodos_device ?
            xx_prodos_create(c->prodos_device,0) : NULL;
        if (!c->prodos ||
            !xx_prodos_handle_base_info(&c->prodos->format,pd) ||
            c->prodos->truncated ||
            (uint64_t)c->prodos->total_blocks*512U>v->data_size ||
            c->prodos->number_of_records!=v->prodos_records ||
            !dc_forward_parameters(f,options,c->prodos)) {
            xx_archive_record_state_free(state); return NULL;
        }
        c->prodos_state=xx_prodos_create_archive_records_reading(
            &c->prodos->format,&c->prodos->format.list_extra_parameters,pd);
        if (!c->prodos_state || !c->prodos_state->has_record ||
            !dc_prodos_record(&state->current_record,c)) {
            xx_archive_record_state_free(state); return NULL;
        }
    } else if (!dc_record(&state->current_record,v,0U)) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record=true; return state;
}
const xx_archive_record *xx_diskcopy42_get_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state) {
    return f && state && state->format==f && state->has_record ?
        &state->current_record : NULL;
}
bool xx_diskcopy42_archive_record_move_to_next(Abstractformat *f,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    dc_cursor *c;
    if (!f || !state || state->format!=f || !state->has_record || dc_stop(pd) ||
        !(c=(dc_cursor *)state->internal_state)) return false;
    if (c->prodos) {
        if (!xx_prodos_archive_record_move_to_next(&c->prodos->format,
                                                    c->prodos_state,pd)) {
            state->has_record=false;
            xx_archive_record_cleanup(&state->current_record);
            xx_archive_record_init(&state->current_record);
            return false;
        }
        ++state->current_index;
        state->has_record=dc_prodos_record(&state->current_record,c);
        return state->has_record;
    }
    ++c->index;
    if (c->index>=(c->view->tag_size ? 2U : 1U)) {
        state->has_record=false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record); return false;
    }
    ++state->current_index;
    state->has_record=dc_record(&state->current_record,c->view,c->index);
    return state->has_record;
}
static bool dc_limits(Abstractformat *f, xx_archive_record_state *state,
                      uint64_t size) {
    const xx_var *max=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed=sizeof(dc_view)+sizeof(dc_cursor)+DC_COPY;
    return (!max || size<=xx_var_get_u64(max)) &&
           (!mem || needed<=xx_var_get_u64(mem));
}
bool xx_diskcopy42_extract_record_to_device(Abstractformat *f,
                                             xx_archive_record_state *state,
                                             xx_io_device *out, xx_pd_struct *pd) {
    dc_cursor *c; xx_io_device *source; uint8_t buffer[DC_COPY];
    uint32_t size,done=0U; int64_t saved,offset; bool ok=true;
    if (!f || !state || state->format!=f || !state->has_record || dc_stop(pd) ||
        !(c=(dc_cursor *)state->internal_state) || c->index>1U ||
        !(source=f->device) || out==source) return false;
    if (c->prodos) return false;
    size=dc_member_size(c->view,c->index);
    offset=dc_member_offset(c->view,c->index);
    if (!size || !dc_limits(f,state,size) || (saved=xx_io_tell(source))<0) return false;
    while (done<size && !dc_stop(pd)) {
        size_t n=size-done>DC_COPY ? DC_COPY : (size_t)(size-done), wrote=0U;
        if (!dc_read(source,offset+done,buffer,n,pd)) { ok=false; break; }
        while (out && wrote<n && !dc_stop(pd)) {
            ssize_t count=xx_io_write(out,buffer+wrote,n-wrote);
            if (count<=0 || (size_t)count>n-wrote || dc_stop(pd)) {
                ok=false; break;
            }
            wrote+=(size_t)count;
        }
        if (!ok) break;
        done+=(uint32_t)n;
    }
    if (xx_io_seek64(source,saved,SEEK_SET)!=0) ok=false;
    return ok && done==size && !dc_stop(pd);
}
static bool dc_equal_path(const char *a, const char *b) {
    while (*a && *b) {
        char x=*a++,y=*b++;
        if (x=='\\') { x='/'; } if (y=='\\') y='/';
        if (x>='A' && x<='Z') x=(char)(x+32);
        if (y>='A' && y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *dc_stage(const char *destination, char **stage) {
    size_t i,parent=0U; unsigned attempt; char *directory;
    *stage=NULL; directory=xx_str_dup(destination); if (!directory) return NULL;
    for (i=0U;directory[i];++i)
        if (directory[i]=='/' || directory[i]=='\\') parent=i+1U;
    directory[parent]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate; xx_io_device *device;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_dc42.tmp.%u",attempt);
        candidate=xx_str_concat(directory,suffix); if (!candidate) break;
        if (dc_equal_path(candidate,destination)) { xx_str_free(candidate); continue; }
        device=xx_io_file_open(candidate,"wbx");
        if (device) { *stage=candidate; xx_str_free(directory); return device; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_diskcopy42_unpack_current_archive_record(Abstractformat *f,
                                                   xx_archive_record_state *state,
                                                   xx_pd_struct *pd) {
    dc_cursor *c; const xx_var *option,*ov; const char *base=NULL;
    char *owned=NULL,*path=NULL,*stage=NULL; bool ok=false,overwrite;
    if (!f || !state || state->format!=f || !state->has_record || dc_stop(pd) ||
        !(c=(dc_cursor *)state->internal_state)) return false;
    if (c->prodos)
        return xx_prodos_unpack_current_archive_record(&c->prodos->format,
                                                        c->prodos_state,pd);
    option=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=ov && xx_var_get_bool(ov);
    if (!option) return xx_diskcopy42_extract_record_to_device(f,state,NULL,pd);
    if (option->type==XX_VAR_TYPE_STRING || option->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(option);
    else if (option->type==XX_VAR_TYPE_WSTRING || option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base=owned;
    }
    if (!base) goto done;
    path=*base && base[strlen(base)-1U]!='/' && base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/",dc_member_name(c->index)) :
        xx_str_concat(base,dc_member_name(c->index));
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path,false) || dc_stop(pd)) goto done;
    {
        xx_io_device *output=dc_stage(path,&stage);
        if (!output) goto done;
        ok=xx_diskcopy42_extract_record_to_device(f,state,output,pd);
        if (xx_io_close(output)!=0) ok=false;
    }
    if (ok && !dc_stop(pd)) ok=xx_io_file_replace_a(stage,path,overwrite);
    else ok=false;
done:
    if (stage) { if (!ok) (void)xx_io_file_remove_a(stage); xx_str_free(stage); }
    xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_diskcopy42_free_archive_records_reading(Abstractformat *f,
                                                 xx_archive_record_state *state) {
    (void)f; xx_archive_record_state_free(state);
}
bool xx_diskcopy42_test_magic(const uint8_t *p, size_t n) {
    return p && n>=DC_HEADER && dc_header_valid(p);
}
