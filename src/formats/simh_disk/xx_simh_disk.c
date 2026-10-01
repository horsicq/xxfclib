/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native fixed-record SIMH CP/M image extraction. LibDsk is only the
 * independent fixture producer; this code does not import its sources.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/simh_disk/xx_simh_disk.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include <limits.h>

#define SIMH_SECTORS (127U*2U*32U)
#define SIMH_DATA_SIZE (SIMH_SECTORS*128U)
#define SIMH_PHYSICAL_SIZE ((SIMH_SECTORS-1U)*137U+3U+128U+4U)
#define SIMH_COPY_BUFFER 65536U
typedef struct simh_stream_s { size_t index; } simh_stream;

static bool simh_read_at(xx_io_device *source,int64_t offset,
                         uint8_t *data,size_t n,xx_pd_struct *pd) {
    size_t done=0U;
    if (!source || offset<0 || (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(source,offset,SEEK_SET)!=0) return false;
    while (done<n) {
        ssize_t got=xx_io_read(source,data+done,n-done);
        if (got<=0 || (size_t)got>n-done ||
            (pd && xx_pd_is_stopped(pd))) return false;
        done+=(size_t)got;
    }
    return true;
}
static bool simh_write(xx_io_device *target,const uint8_t *data,
                       size_t n,xx_pd_struct *pd) {
    size_t done=0U;
    if (!target || (pd && xx_pd_is_stopped(pd))) return false;
    while (done<n) {
        ssize_t put=xx_io_write(target,data+done,n-done);
        if (put<=0 || (size_t)put>n-done ||
            (pd && xx_pd_is_stopped(pd))) return false;
        done+=(size_t)put;
    }
    return true;
}
static bool simh_parse(Abstractformat *format,xx_pd_struct *pd) {
    int64_t total,saved;
    bool valid;
    if (!format || !format->device || format->base_address<0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    saved=xx_io_tell(format->device);
    if (saved<0) return false;
    total=xx_io_total_size(format->device);
    valid=total>=format->base_address &&
          total-format->base_address==SIMH_PHYSICAL_SIZE &&
          !(pd && xx_pd_is_stopped(pd));
    if (xx_io_seek64(format->device,saved,SEEK_SET)!=0) valid=false;
    return valid;
}
static bool simh_check(Abstractformat *f,xx_pd_struct *pd) {
    return simh_parse(f,pd);
}
static bool simh_handle(Abstractformat *f,xx_pd_struct *pd) {
    if (!simh_parse(f,pd)) {
        if (f) { f->is_valid=false;f->base_info_handled=false; }
        return false;
    }
    f->format_size=SIMH_PHYSICAL_SIZE;
    f->overlay_offset=-1;
    f->overlay_size=0;
    f->number_of_archive_records=2U;
    f->is_valid=true;
    f->base_info_handled=true;
    return true;
}
static bool simh_limit(Abstractformat *f,const xx_list_s *options,
                       size_t index) {
    const xx_var *v;
    uint64_t length=index==0U ? SIMH_DATA_SIZE : SIMH_PHYSICAL_SIZE;
    if (index>=2U) return false;
    v=xx_format_resolve_extra_parameter(f,options,
                                         XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && length>xx_var_get_u64(v)) return false;
    v=xx_format_resolve_extra_parameter(f,options,
                                         XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || xx_var_get_u64(v)>=
                    SIMH_COPY_BUFFER+sizeof(simh_stream)+4096U;
}
static bool simh_emit(Abstractformat *f,size_t index,xx_io_device *target,
                      xx_pd_struct *pd) {
    uint64_t base=(uint64_t)f->base_address;
    if (index==0U) {
        unsigned sector;
        uint8_t data[128];
        for (sector=0U;sector<SIMH_SECTORS;++sector) {
            uint64_t at=base+(uint64_t)sector*137U+3U;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !simh_read_at(f->device,(int64_t)at,data,
                              sizeof(data),pd) ||
                !simh_write(target,data,sizeof(data),pd)) return false;
        }
        return true;
    }
    if (index==1U) {
        uint8_t buffer[SIMH_COPY_BUFFER];
        uint64_t at=0U;
        while (at<SIMH_PHYSICAL_SIZE) {
            size_t n=SIMH_PHYSICAL_SIZE-at>sizeof(buffer) ?
                     sizeof(buffer) : (size_t)(SIMH_PHYSICAL_SIZE-at);
            if ((pd && xx_pd_is_stopped(pd)) ||
                !simh_read_at(f->device,(int64_t)(base+at),buffer,n,pd) ||
                !simh_write(target,buffer,n,pd)) return false;
            at+=n;
        }
        return true;
    }
    return false;
}
static bool simh_unpack_device(Abstractformat *f,size_t index,
                               const xx_list_s *options,
                               xx_io_device *target,xx_pd_struct *pd) {
    int64_t saved;
    bool result;
    if (!f || !target || target==f->device || !simh_parse(f,pd) ||
        !simh_limit(f,options,index) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    saved=xx_io_tell(f->device);
    if (saved<0) return false;
    result=simh_emit(f,index,target,pd);
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) result=false;
    return result && !(pd && xx_pd_is_stopped(pd));
}
static int64_t simh_size(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || simh_handle(f,pd)) ?
           f->format_size : -1;
}
static uint64_t simh_count(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || simh_handle(f,pd)) ?
           f->number_of_archive_records : 0U;
}
static void simh_free_stream(void *p) { if (p) xx_mem_free(p); }
static bool simh_record(xx_archive_record_state *state) {
    simh_stream *stream=(simh_stream *)state->internal_state;
    xx_archive_record *record=&state->current_record;
    const char *name=stream->index==0U ? "simh.img" :
                                            "simh-physical.raw";
    uint64_t n=stream->index==0U ? SIMH_DATA_SIZE : SIMH_PHYSICAL_SIZE;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset=state->format->base_address;
    record->data_offset=state->format->base_address+
                        (stream->index==0U ? 3 : 0);
    record->compressed_size=SIMH_PHYSICAL_SIZE;
    return xx_archive_record_set_original_name(record,name) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_COMPRESSED_SIZE,
                                          SIMH_PHYSICAL_SIZE) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_UNCOMPRESSED_SIZE,
                                          n) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_COMPRESSION_METHOD,
                                          stream->index==0U ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record,XX_META_ID_IS_FOLDER,false)&&
           xx_archive_record_set_meta_bool(record,XX_META_ID_IS_ENCRYPTED,false);
}
static xx_archive_record_state *simh_create(Abstractformat *f,
                                             const xx_list_s *opts,
                                             xx_pd_struct *pd) {
    xx_archive_record_state *state;
    simh_stream *stream;
    size_t i;
    if (!f || !f->device || (pd && xx_pd_is_stopped(pd)) ||
        (!f->base_info_handled && !simh_handle(f,pd))) return NULL;
    state=(xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream=(simh_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    stream->index=0U;
    xx_archive_record_state_init(state,f);
    state->internal_state=stream;
    state->free_internal=simh_free_stream;
    state->total_records=2;
    for (i=0U;opts && i<opts->count;++i) {
        const xx_meta *m=(const xx_meta *)xx_list_at(opts,i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy,m->meta_id);
        if (!xx_var_copy(&copy.var,&m->var) ||
            !xx_list_append(&state->options,&copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record=simh_record(state);
    if (!state->has_record) {
        xx_archive_record_state_free(state);return NULL;
    }
    return state;
}
static const xx_archive_record *simh_current(Abstractformat *f,
                                              xx_archive_record_state *s) {
    return f && s && s->format==f && s->has_record ?
           &s->current_record : NULL;
}
static bool simh_next(Abstractformat *f,xx_archive_record_state *s,
                      xx_pd_struct *pd) {
    simh_stream *stream;
    if (!f || !s || s->format!=f || !s->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream=(simh_stream *)s->internal_state;
    if (++stream->index>=2U) { s->has_record=false;return false; }
    ++s->current_index;
    s->has_record=simh_record(s);
    return s->has_record;
}
static ssize_t simh_discard(xx_io_device *d,const void *p,size_t n) {
    (void)d;(void)p;return (ssize_t)n;
}
static bool simh_same_path(const char *a,const char *b) {
    while (*a && *b) {
        char x=*a++,y=*b++;
        if (x=='\\') x='/';
        if (y=='\\') y='/';
        if (x>='A'&&x<='Z') x=(char)(x+32);
        if (y>='A'&&y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *simh_stage(const char *dest,char **stage) {
    char *parent=xx_str_dup(dest);
    size_t i,cut=0U;
    unsigned attempt;
    *stage=NULL;
    if (!parent) return NULL;
    for (i=0U;parent[i];++i)
        if (parent[i]=='/'||parent[i]=='\\') cut=i+1U;
    parent[cut]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),
                             ".xx_simh.tmp.%u",attempt);
        candidate=xx_str_concat(parent,suffix);
        if (!candidate) break;
        if (simh_same_path(candidate,dest)) {
            xx_str_free(candidate);continue;
        }
        device=xx_io_file_open(candidate,"wbx");
        if (device) {
            *stage=candidate;xx_str_free(parent);return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);return NULL;
}
static bool simh_unpack(Abstractformat *f,xx_archive_record_state *s,
                        xx_pd_struct *pd) {
    simh_stream *stream;
    const xx_var *v,*ov;
    const char *base=NULL,*name;
    char *owned=NULL,*dest=NULL,*stage=NULL;
    xx_io_device *out=NULL;
    xx_io_device discard;
    bool result=false,overwrite=false;
    if (!f || !s || s->format!=f || !s->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream=(simh_stream *)s->internal_state;
    if (!simh_limit(f,&s->options,stream->index)) return false;
    v=xx_format_resolve_extra_parameter(f,&s->options,
                                         XX_META_ID_OPT_UNPACK_PATH);
    if (!v) {
        xx_rt_memset(&discard,0,sizeof(discard));
        discard.write=simh_discard;
        return simh_unpack_device(f,stream->index,&s->options,&discard,pd);
    }
    if (v->type==XX_VAR_TYPE_STRING || v->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(v);
    else if (v->type==XX_VAR_TYPE_WSTRING ||
             v->type==XX_VAR_TYPE_WSTRING_VIEW)
        base=owned=xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (!base) goto done;
    name=stream->index==0U ? "simh.img" : "simh-physical.raw";
    dest=base[0] && base[xx_str_len(base)-1U]!='/' &&
         base[xx_str_len(base)-1U]!='\\' ?
         xx_str_concat3(base,"/",name) : xx_str_concat(base,name);
    if (!dest) goto done;
    ov=xx_format_resolve_extra_parameter(f,&s->options,
                                          XX_META_ID_OPT_OVERWRITE);
    overwrite=ov && xx_var_get_bool(ov);
    if ((!overwrite && xx_io_file_exists_a(dest)) ||
        !xx_store_create_dirs_a(dest,false) ||
        (pd && xx_pd_is_stopped(pd))) goto done;
    out=simh_stage(dest,&stage);
    if (!out) goto done;
    result=simh_unpack_device(f,stream->index,&s->options,out,pd);
    if (xx_io_close(out)!=0) result=false;
    out=NULL;
    if (result && !(pd && xx_pd_is_stopped(pd)))
        result=xx_io_file_replace_a(stage,dest,overwrite);
    else result=false;
done:
    if (out) { (void)xx_io_close(out);result=false; }
    if (stage) {
        if (!result) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (dest) xx_str_free(dest);
    if (owned) xx_str_free(owned);
    return result;
}
static void simh_free_records(Abstractformat *f,xx_archive_record_state *s) {
    (void)f;xx_archive_record_state_free(s);
}
static void simh_destroy_vtable(Abstractformat *f) {
    if (f) xx_format_cleanup_extra_parameters(f);
}
void xx_simh_disk_init(xx_simh_disk *r,xx_io_device *d,int64_t b) {
    if (!r) return;
    xx_rt_memset(r,0,sizeof(*r));
    xx_format_init(&r->format,d,b);
    r->format.file_type=XX_FILE_TYPE_SIMH_DISK;
    r->format.format_type=XX_TYPE_ARCHIVE;
    r->format.is_archive=true;
    xx_format_set_extension(&r->format,"simh");
    r->format.check_is_valid=simh_check;
    r->format.handle_base_info=simh_handle;
    r->format.get_format_size=simh_size;
    r->format.get_number_of_archive_records=simh_count;
    r->format.create_archive_records_reading=simh_create;
    r->format.get_current_archive_record=simh_current;
    r->format.archive_record_move_to_next=simh_next;
    r->format.unpack_current_archive_record=simh_unpack;
    r->format.free_archive_records_reading=simh_free_records;
    r->format.destroy=simh_destroy_vtable;
}
xx_simh_disk *xx_simh_disk_create(xx_io_device *d,int64_t b) {
    xx_simh_disk *r=(xx_simh_disk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_simh_disk_init(r,d,b);
    return r;
}
void xx_simh_disk_destroy(xx_simh_disk *r) {
    if (r) simh_destroy_vtable(&r->format);
}
void xx_simh_disk_free(xx_simh_disk *r) {
    if (r) { xx_simh_disk_destroy(r);xx_mem_free(r); }
}
bool xx_simh_disk_unpack_to_device(xx_simh_disk *r,xx_io_device *d,
                                   xx_pd_struct *pd) {
    return r && simh_unpack_device(&r->format,0U,NULL,d,pd);
}
