/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded, stored-payload member adapter. Parsers own their grammar.
 */
#ifndef XX_PAYLOAD_MEMBERS_H
#define XX_PAYLOAD_MEMBERS_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include <limits.h>

typedef struct pm_member {
    char name[96]; int64_t offset, size, packed_size; uint8_t *memory;
    /* Optional recovered credential, owned independently of decoded data. */
    char *password;
    bool source_encrypted;
    uint16_t compression_method;
    /* Optional logical-image reader. This also runs for a NULL destination:
     * validation must consume mapped, sparse or decoded bytes in memory. */
    bool (*read_range)(Abstractformat *, struct pm_member *, uint64_t, void *, size_t, xx_pd_struct *);
    void *context;
    void (*free_context)(void *);
    bool (*read_all)(Abstractformat *, struct pm_member *, xx_io_device *, xx_pd_struct *);
    char *display_name;
    bool directory;
} pm_member;
typedef struct pm_stream { pm_member *items; size_t count, capacity, index; int64_t size; } pm_stream;
static bool pm_parse(Abstractformat *, pm_stream *, xx_pd_struct *);
static int64_t pm_available(Abstractformat *f) {
    int64_t n;
    if (!f || !f->device || f->base_address<0 || (n=xx_io_size(f->device))<f->base_address) return -1;
    return n-f->base_address;
}
static XXFC_MAYBE_UNUSED bool pm_read(Abstractformat *f, int64_t at, void *p, size_t n) {
    int64_t left=pm_available(f);
    size_t capacity=xx_get_file_buffer_size(),done=0;
    if(at<0 || left<at || n>(uint64_t)(left-at) || (!p && n) ||
       xx_io_seek64(f->device,f->base_address+at,SEEK_SET)!=0) return false;
    while(done<n) {
        size_t request=n-done<capacity?n-done:capacity;
        ssize_t got=xx_io_read(f->device,(uint8_t *)p+done,request);
        if(got<=0 || (size_t)got>request) return false;
        done+=(size_t)got;
    }
    return true;
}
static bool pm_add(Abstractformat *f, pm_stream *s, const char *label, int64_t at, int64_t size) {
    pm_member *m; int64_t left=pm_available(f);
    if (at<0 || size<0 || at>left || size>left-at || s->count>=1000000U) return false;
    if (s->count==s->capacity) {
        size_t cap=s->capacity ? s->capacity*2U : 8U;
        void *next=xx_mem_realloc(s->items,cap*sizeof(*s->items));
        if (!next) return false;
        s->items=(pm_member *)next; s->capacity=cap;
    }
    m=&s->items[s->count]; xx_mem_zero(m,sizeof(*m));
    xx_rt_snprintf(m->name,sizeof(m->name),"%04u-%s",(unsigned)s->count,label);
    /* Names are single output leaves, even when metadata contains a path.
     * The numeric prefix keeps duplicates distinct after replacement. */
    {
        size_t i,n=xx_rt_strlen(m->name);
        for(i=0;i<n;++i) {
            unsigned char c=(unsigned char)m->name[i];
            if(c<32 || c>126 || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') m->name[i]='_';
        }
        while(n && (m->name[n-1]=='.' || m->name[n-1]==' ')) m->name[--n]='_';
    }
    m->offset=f->base_address+at; m->size=m->packed_size=size; ++s->count; return true;
}
static void pm_free_password(void *p) {
    char *password=(char *)p;
    if(password) {
        xx_mem_zero(password,xx_rt_strlen(password)+1U);
        xx_mem_free(password);
    }
}
static void pm_free_stream(void *p) {
    pm_stream *s=(pm_stream *)p; size_t i;
    if (!s) return;
    for(i=0;i<s->count;++i) {
        if(s->items[i].memory) xx_mem_free(s->items[i].memory);
        if(s->items[i].free_context) s->items[i].free_context(s->items[i].context);
        xx_mem_free(s->items[i].display_name);
        pm_free_password(s->items[i].password);
    }
    if(s->items) { xx_mem_free(s->items); } xx_mem_free(s);
}
static pm_stream *pm_open(Abstractformat *f, xx_pd_struct *pd) {
    pm_stream *s; int64_t cursor;
    if (!f || !f->device || (pd && xx_pd_is_stopped(pd))) return NULL;
    cursor=xx_io_tell(f->device); s=(pm_stream *)xx_mem_alloc(sizeof(*s));
    if(s) {
        xx_mem_zero(s,sizeof(*s));
        if(!pm_parse(f,s,pd) || (pd && xx_pd_is_stopped(pd))) { pm_free_stream(s); s=NULL; }
    }
    (void)xx_io_seek64(f->device,cursor,SEEK_SET); return s;
}
static bool pm_valid(Abstractformat *f, xx_pd_struct *pd) {
    pm_stream *s=pm_open(f,pd); if(!s) return false; pm_free_stream(s); return true;
}
static bool pm_handle(Abstractformat *f, xx_pd_struct *pd) {
    pm_stream *s=pm_open(f,pd); int64_t end;
    if(!s) { if(f) { f->is_valid=false; f->base_info_handled=false; } return false; }
    f->format_size=s->size; f->number_of_archive_records=s->count;
    end=f->base_address+s->size;
    f->overlay_size=xx_io_size(f->device)-end;
    f->overlay_offset=f->overlay_size>0 ? end : -1;
    f->is_valid=true; f->base_info_handled=true; pm_free_stream(s); return true;
}
static int64_t pm_size(Abstractformat *f, xx_pd_struct *pd) { return f && (f->base_info_handled || pm_handle(f,pd)) ? f->format_size : -1; }
static uint64_t pm_count(Abstractformat *f, xx_pd_struct *pd) { return f && (f->base_info_handled || pm_handle(f,pd)) ? f->number_of_archive_records : 0; }
static bool pm_record(xx_archive_record_state *st) {
    pm_stream *s=(pm_stream *)st->internal_state; pm_member *m=&s->items[s->index];
    xx_archive_record *r=&st->current_record;
    xx_archive_record_cleanup(r); xx_archive_record_init(r);
    r->header_offset=st->format->base_address; r->header_size=0;
    r->data_offset=m->offset; r->compressed_size=m->packed_size;
    if(!(xx_archive_record_set_original_name(r,m->display_name?m->display_name:m->name) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,(uint64_t)m->packed_size) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,(uint64_t)m->size) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,m->compression_method) &&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,m->directory) &&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,m->source_encrypted))) return false;
    if(m->password) {
        xx_meta recovered;
        xx_meta_init(&recovered,XX_META_ID_PASSWORD);
        if(!xx_var_set_str(&recovered.var,m->password)) return false;
        /* The record owns its copy; neither iterator movement nor stream
         * destruction leaves metadata pointing into a credential buffer. */
        recovered.var.free_fn=pm_free_password;
        if(!xx_list_append(&r->list_meta,&recovered)) {
            xx_meta_cleanup(&recovered); return false;
        }
    }
    return true;
}
static xx_archive_record_state *pm_create_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd) {
    pm_stream *s=pm_open(f,pd); xx_archive_record_state *st; size_t i;
    if(!s) return NULL;
    st=(xx_archive_record_state *)xx_mem_alloc(sizeof(*st));
    if(!st) { pm_free_stream(s); return NULL; }
    xx_archive_record_state_init(st,f); st->internal_state=s; st->free_internal=pm_free_stream; st->total_records=s->count;
    for(i=0;opts && i<opts->count;++i) {
        const xx_meta *m=(const xx_meta *)xx_list_at(opts,i); xx_meta copy;
        if(!m) { continue; } xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var) || !xx_list_append(&st->options,&copy)) {
            xx_meta_cleanup(&copy); xx_archive_record_state_free(st); return NULL;
        }
    }
    st->has_record=s->count!=0 && pm_record(st);
    if(s->count && !st->has_record) { xx_archive_record_state_free(st); return NULL; }
    return st;
}
static const xx_archive_record *pm_current(Abstractformat *f, xx_archive_record_state *st) { return f && st && st->format==f && st->has_record ? &st->current_record : NULL; }
static bool pm_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd) {
    pm_stream *s;
    if(!f || !st || st->format!=f || !st->has_record) return false;
    s=(pm_stream *)st->internal_state;
    if((pd && xx_pd_is_stopped(pd)) || ++s->index>=s->count) { st->has_record=false; return false; }
    ++st->current_index; st->has_record=pm_record(st); return st->has_record;
}
static bool pm_same_path(const char *a,const char *b) {
    while(*a && *b) {
        char x=*a++,y=*b++;
        if(x=='\\') x='/';
        if(y=='\\') y='/';
        if(x>='A' && x<='Z') x=(char)(x+32);
        if(y>='A' && y<='Z') y=(char)(y+32);
        if(x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *pm_stage(const char *dest,char **stage) {
    char *parent=xx_str_dup(dest);
    size_t i,cut=0U;
    unsigned attempt;
    *stage=NULL;
    if(!parent) return NULL;
    for(i=0U;parent[i];++i)
        if(parent[i]=='/' || parent[i]=='\\') cut=i+1U;
    parent[cut]=0;
    for(attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_pm.tmp.%u",attempt);
        candidate=xx_str_concat(parent,suffix);
        if(!candidate) break;
        if(pm_same_path(candidate,dest)) {
            xx_str_free(candidate);continue;
        }
        device=xx_io_file_open(candidate,"wbx");
        if(device) {
            *stage=candidate;xx_str_free(parent);return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);return NULL;
}
static bool pm_unpack(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd) {
    pm_stream *s; pm_member *m; const xx_var *v; char *path=NULL,*owned=NULL,*stage=NULL;
    const char *base=NULL; bool result=false,overwrite=false; int64_t cursor;
    xx_io_device *output=NULL;
    if(!f || !st || st->format!=f || !st->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    s=(pm_stream *)st->internal_state; m=&s->items[s->index];
    v=xx_format_resolve_extra_parameter(f,&st->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && (uint64_t)m->size>xx_var_get_u64(v)) return false;
    v=xx_format_resolve_extra_parameter(f,&st->options,XX_META_ID_OPT_MEMORY_LIMIT);
    if(v && m->memory && (uint64_t)m->size>xx_var_get_u64(v)) return false;
    v=xx_format_resolve_extra_parameter(f,&st->options,XX_META_ID_OPT_UNPACK_PATH);
    if(v) {
        if(v->type==XX_VAR_TYPE_STRING || v->type==XX_VAR_TYPE_STRING_VIEW) base=xx_var_get_str(v);
        else if(v->type==XX_VAR_TYPE_WSTRING || v->type==XX_VAR_TYPE_WSTRING_VIEW) base=owned=xx_str_unicode_to_utf8(xx_var_get_wstr(v));
        if(!base) goto done;
        path=base[0] ? xx_str_concat3(base,"/",m->display_name?m->display_name:m->name) : xx_str_dup(m->display_name?m->display_name:m->name);
        if(!path || !xx_store_create_dirs_a(path,false)) goto done;
        if(m->directory) { result=xx_store_create_dirs_a(path,true); goto done; }
        v=xx_format_resolve_extra_parameter(f,&st->options,XX_META_ID_OPT_OVERWRITE);
        overwrite=v && xx_var_get_bool(v);
        if((!overwrite && xx_io_file_exists_a(path)) ||
           (pd && xx_pd_is_stopped(pd))) goto done;
        output=pm_stage(path,&stage);
        if(!output) goto done;
    }
    if(m->directory) { result=true; goto done; }
    cursor=xx_io_tell(f->device);
    if(m->read_all) result=m->read_all(f,m,output,pd);
    else {
        size_t capacity=xx_get_file_buffer_size();
        uint8_t *buffer=NULL; int64_t at=m->offset,left=m->size,position=0;
        if(!m->memory && left>0) buffer=(uint8_t *)xx_mem_alloc(capacity);
        result=m->memory || left<=0 || buffer!=NULL;
        while(result && left>0) {
            size_t n=(uint64_t)left>capacity ? capacity : (size_t)left;
            const uint8_t *data=m->memory ? m->memory+(size_t)position : buffer;
            size_t written=0;
            if(pd && xx_pd_is_stopped(pd)) { result=false; break; }
            if(m->read_range) {
                if(!m->read_range(f,m,(uint64_t)position,buffer,n,pd)) { result=false; break; }
            } else if(!m->memory) {
                size_t received=0;
                if(xx_io_seek64(f->device,at,SEEK_SET)!=0) { result=false; break; }
                while(received<n) {
                    size_t request=n-received;
                    ssize_t amount;
                    if(pd && xx_pd_is_stopped(pd)) { result=false; break; }
                    amount=xx_io_read(f->device,buffer+received,request);
                    if((pd && xx_pd_is_stopped(pd)) || amount<=0 || (size_t)amount>request) { result=false; break; }
                    received+=(size_t)amount;
                }
                if(!result) break;
            }
            while(output && written<n) {
                ssize_t amount=xx_io_write(output,data+written,n-written);
                if(amount<=0 || (size_t)amount>n-written) { result=false; break; }
                written+=(size_t)amount;
            }
            if(!result) break;
            at+=n; position+=n; left-=n;
        }
        if(pd && xx_pd_is_stopped(pd)) result=false;
        if(buffer) xx_mem_free(buffer);
    }
    (void)xx_io_seek64(f->device,cursor,SEEK_SET);
done:
    if(output && xx_io_close(output)!=0) result=false;
    if(result && stage) {
        if(pd && xx_pd_is_stopped(pd)) result=false;
        else result=xx_io_file_replace_a(stage,path,overwrite);
    }
    if(stage) {
        if(!result) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if(path) { xx_str_free(path); } if(owned) xx_str_free(owned); return result;
}
static void pm_free_records(Abstractformat *f, xx_archive_record_state *st) { (void)f; xx_archive_record_state_free(st); }
static void pm_init(Abstractformat *f, xx_io_device *d, int64_t b, xx_file_type_t type, const char *ext) {
    xx_format_init(f,d,b); f->file_type=type; f->format_type=XX_TYPE_ARCHIVE; f->is_archive=true;
    xx_format_set_extension(f,ext); f->check_is_valid=pm_valid; f->handle_base_info=pm_handle;
    f->get_format_size=pm_size; f->get_number_of_archive_records=pm_count;
    f->create_archive_records_reading=pm_create_records; f->get_current_archive_record=pm_current;
    f->archive_record_move_to_next=pm_next; f->unpack_current_archive_record=pm_unpack; f->free_archive_records_reading=pm_free_records;
}
#endif
