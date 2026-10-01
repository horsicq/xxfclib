/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded FDCOPY .CFI reader; layout from LibDsk's doc/cfi.html.
 * Independently implemented: no LibDsk source is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fdcopy_cfi/xx_fdcopy_cfi.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef FDCOPY_CFI
#define CFI_TYPE XX_FILE_TYPE_FDCOPY_CFI
#else
#define CFI_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CFI_TRACKS 256U
#define CFI_SOURCE_MAX (6U*1024U*1024U)
#define CFI_RAW_MAX (4U*1024U*1024U)
#define CFI_TRACK_RAW_MAX 32768U
#define CFI_TRACK_STORED_MAX 65535U
typedef struct cfi_track_s {
    int64_t offset;
    uint32_t encoded;
} cfi_track;
typedef struct cfi_view_s {
    uint32_t refs,count,track_bytes,raw_bytes;
    int64_t base,size;
    cfi_track tracks[CFI_TRACKS];
} cfi_view;
typedef struct cfi_cursor_s { cfi_view *view; uint32_t index; } cfi_cursor;
static bool cfi_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static uint32_t cfi_le16(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8); }
static void cfi_release(cfi_view *v) { if (v && !--v->refs) xx_mem_free(v); }
static bool cfi_read(xx_io_device *d,int64_t at,uint8_t *p,size_t n,
                     xx_pd_struct *pd) {
    size_t done=0U;
    if (cfi_stop(pd) || xx_io_seek64(d,at,SEEK_SET)!=0) return false;
    while (done<n) {
        ssize_t got;
        if (cfi_stop(pd)) return false;
        got=xx_io_read(d,p+done,n-done);
        if (got<=0 || (size_t)got>n-done || cfi_stop(pd)) return false;
        done+=(size_t)got;
    }
    return !cfi_stop(pd);
}
static bool cfi_decode(const uint8_t *encoded,uint32_t size,
                       uint8_t *decoded,uint32_t *length) {
    uint32_t at=0U,out=0U;
    while (at<size) {
        uint32_t word,n;
        if (size-at<2U) return false;
        word=cfi_le16(encoded+at); at+=2U; n=word&0x7FFFU;
        if (!n || n>CFI_TRACK_RAW_MAX-out) return false;
        if (word&0x8000U) {
            if (at>=size) return false;
            if (decoded) memset(decoded+out,encoded[at],n);
            ++at;
        } else {
            if (n>size-at) return false;
            if (decoded) memcpy(decoded+out,encoded+at,n);
            at+=n;
        }
        out+=n;
    }
    if (out<512U || (out&511U)) return false;
    *length=out; return true;
}
static cfi_view *cfi_parse(Abstractformat *f,xx_pd_struct *pd) {
    xx_io_device *d; int64_t saved,total,pos; uint8_t encoded[CFI_TRACK_STORED_MAX],head[2];
    cfi_view *v=NULL; bool ok=false;
    if (!f || !(d=f->device) || f->base_address<0 || cfi_stop(pd)) return NULL;
    saved=xx_io_tell(d); total=xx_io_total_size(d);
    if (saved<0 || total<f->base_address ||
        total-f->base_address<5 || total-f->base_address>CFI_SOURCE_MAX) return NULL;
    v=(cfi_view *)xx_mem_calloc(1U,sizeof(*v)); if (!v) return NULL;
    v->refs=1U; v->base=f->base_address; v->size=total-f->base_address;
    pos=f->base_address;
    while (pos<total) {
        uint32_t stored,decoded;
        if (v->count==CFI_TRACKS || total-pos<2 ||
            !cfi_read(d,pos,head,sizeof(head),pd)) goto done;
        stored=cfi_le16(head);
        if (stored<3U || stored>CFI_TRACK_STORED_MAX ||
            stored>(uint64_t)(total-pos-2) ||
            !cfi_read(d,pos+2,encoded,stored,pd) ||
            !cfi_decode(encoded,stored,NULL,&decoded)) goto done;
        if (v->count) { if (decoded!=v->track_bytes) goto done; }
        else v->track_bytes=decoded;
        if (decoded>CFI_RAW_MAX-v->raw_bytes) goto done;
        v->tracks[v->count].offset=pos+2;
        v->tracks[v->count].encoded=stored;
        ++v->count; v->raw_bytes+=decoded;
        pos+=2+(int64_t)stored;
    }
    ok=v->count>0U && !cfi_stop(pd);
done:
    if (xx_io_seek64(d,saved,SEEK_SET)!=0) ok=false;
    if (!ok) { cfi_release(v); return NULL; }
    return v;
}
static void cfi_destroy_format(Abstractformat *f) {
    xx_fdcopy_cfi_destroy((xx_fdcopy_cfi *)f);
}
void xx_fdcopy_cfi_init(xx_fdcopy_cfi *c,xx_io_device *d,int64_t base) {
    if (!c) return;
    xx_mem_zero(c,sizeof(*c)); xx_format_init(&c->format,d,base);
    c->format.file_type=CFI_TYPE; c->format.format_type=XX_TYPE_ARCHIVE;
    c->format.is_archive=true;
    xx_format_set_mime_type(&c->format,"application/x-fdcopy-cfi");
    xx_format_set_extension(&c->format,"cfi");
    c->format.check_is_valid=xx_fdcopy_cfi_check_is_valid;
    c->format.handle_base_info=xx_fdcopy_cfi_handle_base_info;
    c->format.get_format_size=xx_fdcopy_cfi_get_format_size;
    c->format.get_number_of_archive_records=xx_fdcopy_cfi_get_number_of_archive_records;
    c->format.create_archive_records_reading=xx_fdcopy_cfi_create_archive_records_reading;
    c->format.get_current_archive_record=xx_fdcopy_cfi_get_current_archive_record;
    c->format.archive_record_move_to_next=xx_fdcopy_cfi_archive_record_move_to_next;
    c->format.unpack_current_archive_record=xx_fdcopy_cfi_unpack_current_archive_record;
    c->format.free_archive_records_reading=xx_fdcopy_cfi_free_archive_records_reading;
    c->format.destroy=cfi_destroy_format;
}
xx_fdcopy_cfi *xx_fdcopy_cfi_create(xx_io_device *d,int64_t base) {
    xx_fdcopy_cfi *c=(xx_fdcopy_cfi *)xx_mem_alloc(sizeof(*c));
    if (c) xx_fdcopy_cfi_init(c,d,base); return c;
}
void xx_fdcopy_cfi_destroy(xx_fdcopy_cfi *c) {
    if (!c) return;
    cfi_release((cfi_view *)c->internal); c->internal=NULL;
    xx_format_cleanup_extra_parameters(&c->format);
}
void xx_fdcopy_cfi_free(xx_fdcopy_cfi *c) {
    if (c) { xx_fdcopy_cfi_destroy(c); xx_mem_free(c); }
}
bool xx_fdcopy_cfi_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    cfi_view *v=cfi_parse(f,pd); if (!v) return false; cfi_release(v); return true;
}
bool xx_fdcopy_cfi_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_fdcopy_cfi *c=(xx_fdcopy_cfi *)f; cfi_view *v;
    if (!f || cfi_stop(pd)) return false;
    if (f->base_info_handled && c->internal) return f->is_valid;
    v=cfi_parse(f,pd);
    if (!v) { f->base_info_handled=false; f->is_valid=false; return false; }
    cfi_release((cfi_view *)c->internal); c->internal=v;
    c->number_of_members=v->count+1U;
    f->number_of_archive_records=c->number_of_members;
    f->format_size=v->size; f->overlay_offset=-1; f->overlay_size=0;
    f->is_valid=true; f->base_info_handled=true; return true;
}
int64_t xx_fdcopy_cfi_get_format_size(Abstractformat *f,xx_pd_struct *pd) {
    return xx_fdcopy_cfi_handle_base_info(f,pd) ? f->format_size : -1;
}
uint64_t xx_fdcopy_cfi_get_number_of_archive_records(Abstractformat *f,
                                                       xx_pd_struct *pd) {
    return xx_fdcopy_cfi_handle_base_info(f,pd) ?
        ((xx_fdcopy_cfi *)f)->number_of_members : 0U;
}
static void cfi_cursor_free(void *ptr) {
    cfi_cursor *c=(cfi_cursor *)ptr; if (c) { cfi_release(c->view); xx_mem_free(c); }
}
static void cfi_name(char name[32],uint32_t index) {
    if (!index) (void)xx_rt_snprintf(name,32U,"fdcopy-disk.img");
    else (void)xx_rt_snprintf(name,32U,"fdcopy-track%03u.bin",index);
}
static bool cfi_record(xx_archive_record *r,const cfi_view *v,uint32_t index) {
    uint32_t size=index ? v->track_bytes : v->raw_bytes;
    uint64_t stored=index ? 2U+v->tracks[index-1U].encoded : (uint64_t)v->size;
    char name[32]; cfi_name(name,index);
    xx_archive_record_cleanup(r); xx_archive_record_init(r);
    r->header_offset=index ? v->tracks[index-1U].offset-2 : v->base;
    r->header_size=index ? 2 : 0;
    r->data_offset=index ? v->tracks[index-1U].offset : -1;
    r->compressed_size=(int64_t)stored;
    return xx_archive_record_set_original_name(r,name) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,size) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,stored) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,1U) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false);
}
xx_archive_record_state *xx_fdcopy_cfi_create_archive_records_reading(
    Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_fdcopy_cfi *c=(xx_fdcopy_cfi *)f; cfi_view *v; cfi_cursor *cursor;
    xx_archive_record_state *state; size_t i;
    if (!xx_fdcopy_cfi_handle_base_info(f,pd)) return NULL;
    v=(cfi_view *)c->internal;
    state=(xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    cursor=(cfi_cursor *)xx_mem_calloc(1U,sizeof(*cursor));
    if (!state || !cursor) { xx_mem_free(state); xx_mem_free(cursor); return NULL; }
    ++v->refs; cursor->view=v;
    xx_archive_record_state_init(state,f); state->internal_state=cursor;
    state->free_internal=cfi_cursor_free; state->total_records=v->count+1U;
    if (options) for (i=0U;i<options->count;++i) {
        const xx_meta *original=(const xx_meta *)xx_list_at(options,i); xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy,original->meta_id);
        if (!xx_var_copy(&copy.var,&original->var) ||
            !xx_list_append(&state->options,&copy)) {
            xx_meta_cleanup(&copy); xx_archive_record_state_free(state); return NULL;
        }
    }
    if (!cfi_record(&state->current_record,v,0U)) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record=true; return state;
}
const xx_archive_record *xx_fdcopy_cfi_get_current_archive_record(
    Abstractformat *f,xx_archive_record_state *state) {
    return f && state && state->format==f && state->has_record ?
        &state->current_record : NULL;
}
bool xx_fdcopy_cfi_archive_record_move_to_next(Abstractformat *f,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    cfi_cursor *c;
    if (!f || !state || state->format!=f || !state->has_record || cfi_stop(pd) ||
        !(c=(cfi_cursor *)state->internal_state)) return false;
    ++c->index;
    if (c->index>c->view->count) {
        state->has_record=false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record); return false;
    }
    ++state->current_index;
    state->has_record=cfi_record(&state->current_record,c->view,c->index);
    return state->has_record;
}
static bool cfi_limits(Abstractformat *f,xx_archive_record_state *state,
                       uint64_t length) {
    const xx_var *max=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed=sizeof(cfi_view)+sizeof(cfi_cursor)+
        CFI_TRACK_STORED_MAX+CFI_TRACK_RAW_MAX;
    return (!max || length<=xx_var_get_u64(max)) &&
           (!mem || needed<=xx_var_get_u64(mem));
}
bool xx_fdcopy_cfi_extract_record_to_device(Abstractformat *f,
                                             xx_archive_record_state *state,
                                             xx_io_device *out,xx_pd_struct *pd) {
    cfi_cursor *c; xx_io_device *source;
    uint8_t encoded[CFI_TRACK_STORED_MAX],decoded[CFI_TRACK_RAW_MAX];
    uint32_t begin,end,i,length; int64_t saved; bool ok=true;
    if (!f || !state || state->format!=f || !state->has_record || cfi_stop(pd) ||
        !(c=(cfi_cursor *)state->internal_state) || c->index>c->view->count ||
        !(source=f->device) || source==out) return false;
    length=c->index ? c->view->track_bytes : c->view->raw_bytes;
    if (!cfi_limits(f,state,length) || (saved=xx_io_tell(source))<0) return false;
    begin=c->index ? c->index-1U : 0U;
    end=c->index ? c->index : c->view->count;
    for (i=begin;i<end && !cfi_stop(pd);++i) {
        const cfi_track *track=&c->view->tracks[i]; uint32_t got;
        size_t written=0U;
        if (!cfi_read(source,track->offset,encoded,track->encoded,pd) ||
            !cfi_decode(encoded,track->encoded,decoded,&got) ||
            got!=c->view->track_bytes) { ok=false; break; }
        while (out && written<got && !cfi_stop(pd)) {
            ssize_t step=xx_io_write(out,decoded+written,got-written);
            if (step<=0 || (size_t)step>got-written || cfi_stop(pd)) {
                ok=false; break;
            }
            written+=(size_t)step;
        }
        if (!ok) break;
    }
    if (xx_io_seek64(source,saved,SEEK_SET)!=0) ok=false;
    return ok && i==end && !cfi_stop(pd);
}
static bool cfi_equal_path(const char *a,const char *b) {
    while (*a && *b) {
        char x=*a++,y=*b++;
        if (x=='\\') x='/'; if (y=='\\') y='/';
        if (x>='A' && x<='Z') x=(char)(x+32);
        if (y>='A' && y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *cfi_stage(const char *destination,char **stage) {
    size_t i,parent=0U; unsigned attempt; char *directory;
    *stage=NULL; directory=xx_str_dup(destination); if (!directory) return NULL;
    for (i=0U;directory[i];++i)
        if (directory[i]=='/' || directory[i]=='\\') parent=i+1U;
    directory[parent]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate; xx_io_device *device;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_cfi.tmp.%u",attempt);
        candidate=xx_str_concat(directory,suffix); if (!candidate) break;
        if (cfi_equal_path(candidate,destination)) { xx_str_free(candidate); continue; }
        device=xx_io_file_open(candidate,"wbx");
        if (device) { *stage=candidate; xx_str_free(directory); return device; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_fdcopy_cfi_unpack_current_archive_record(Abstractformat *f,
                                                   xx_archive_record_state *state,
                                                   xx_pd_struct *pd) {
    cfi_cursor *c; const xx_var *option,*ov; const char *base=NULL;
    char *owned=NULL,*path=NULL,*stage=NULL,name[32]; bool ok=false,overwrite;
    if (!f || !state || state->format!=f || !state->has_record || cfi_stop(pd) ||
        !(c=(cfi_cursor *)state->internal_state)) return false;
    option=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=ov && xx_var_get_bool(ov);
    if (!option) return xx_fdcopy_cfi_extract_record_to_device(f,state,NULL,pd);
    if (option->type==XX_VAR_TYPE_STRING || option->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(option);
    else if (option->type==XX_VAR_TYPE_WSTRING || option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base=owned;
    }
    if (!base) goto done;
    cfi_name(name,c->index);
    path=*base && base[strlen(base)-1U]!='/' && base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/",name) : xx_str_concat(base,name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path,false) || cfi_stop(pd)) goto done;
    {
        xx_io_device *output=cfi_stage(path,&stage);
        if (!output) goto done;
        ok=xx_fdcopy_cfi_extract_record_to_device(f,state,output,pd);
        if (xx_io_close(output)!=0) ok=false;
    }
    if (ok && !cfi_stop(pd)) ok=xx_io_file_replace_a(stage,path,overwrite);
    else ok=false;
done:
    if (stage) { if (!ok) (void)xx_io_file_remove_a(stage); xx_str_free(stage); }
    xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_fdcopy_cfi_free_archive_records_reading(Abstractformat *f,
                                                 xx_archive_record_state *state) {
    (void)f; xx_archive_record_state_free(state);
}
