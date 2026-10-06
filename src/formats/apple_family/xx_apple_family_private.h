/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded member adapter for native Apple-family grammars.
 */
#ifndef XX_APPLE_FAMILY_PRIVATE_H
#define XX_APPLE_FAMILY_PRIVATE_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#include "../xx_payload_members.h"
#define AF_INPUT_MAX (64U * 1024U * 1024U)
#define AF_MEMBER_MAX (64U * 1024U * 1024U)
#define AF_COUNT_MAX 32768U
typedef struct af_work { Abstractformat *f; pm_stream *s; xx_pd_struct *pd; uint64_t used,limit,member_limit; } af_work;
typedef struct af_blob { uint8_t *p; uint32_t n; } af_blob;
static bool af_poll(const af_work *w) { return !w->pd || !xx_pd_is_stopped(w->pd); }
static bool af_init(af_work *w,Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    const xx_var *v; xx_apple_family_info *r=(xx_apple_family_info *)f;
    xx_mem_zero(w,sizeof(*w)); w->f=f; w->s=s; w->pd=pd;
    w->limit=UINT64_C(64)*1024U*1024U; w->member_limit=UINT64_MAX;
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);
    if(v && xx_var_get_u64(v)<w->limit) w->limit=xx_var_get_u64(v);
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<w->member_limit) w->member_limit=xx_var_get_u64(v);
    return af_poll(w) && w->limit>=4096U && pm_available(f)>=0;
}
static uint8_t *af_alloc(af_work *w,uint64_t n,bool member) {
    uint8_t *p;
    if(n>SIZE_MAX || n>AF_INPUT_MAX || (member && n>w->member_limit) ||
       w->used>w->limit || n>w->limit-w->used || !af_poll(w)) return NULL;
    p=(uint8_t *)xx_mem_alloc(n ? (size_t)n:1U);
    if(p) { w->used+=n; if(n) xx_mem_zero(p,(size_t)n); } return p;
}
static void af_release(af_work *w,void *p,uint64_t n) { if(p) { xx_mem_free(p); if(w->used>=n) w->used-=n; } }
static bool af_read(af_work *w,int64_t at,void *p,size_t n) {
    size_t done=0U;
    while(done<n) { size_t part=n-done; if(part>4096U) part=4096U;
        if(!af_poll(w) || !pm_read(w->f,at+(int64_t)done,(uint8_t *)p+done,part)) return false;
        done+=part; }
    return af_poll(w);
}
static XXFC_MAYBE_UNUSED bool af_load(af_work *w,af_blob *b) {
    int64_t n=pm_available(w->f); xx_mem_zero(b,sizeof(*b));
    if(n<0 || n>AF_INPUT_MAX || !(b->p=af_alloc(w,(uint64_t)n,false))) return false;
    b->n=(uint32_t)n;
    if(!af_read(w,0,b->p,b->n)) { af_release(w,b->p,b->n); b->p=NULL; return false; } return true;
}
static XXFC_MAYBE_UNUSED bool af_range(const af_blob *b,uint64_t at,uint64_t n) { return at<=b->n && n<=b->n-at; }
static XXFC_MAYBE_UNUSED bool af_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i)if(p[i])return false;return true; }
static XXFC_MAYBE_UNUSED uint16_t af_crc16(af_work *w,const uint8_t *p,size_t n,uint16_t crc) {
    size_t i;unsigned bit;
    for(i=0;i<n;++i) { if(!(i&4095U) && !af_poll(w))return 0U;
        crc^=(uint16_t)p[i]<<8U; for(bit=0;bit<8;++bit)crc=(uint16_t)((crc<<1U)^((crc&0x8000U)?0x1021U:0U)); }
    return crc;
}
static bool af_safe(const char *name) {
    const char *p=name,*part=name;
    if(!name || !*name || *name=='/' || *name=='\\')return false;
    for(;;++p) {
        unsigned char c=(unsigned char)*p;
        if(c=='/' || !c) {
            size_t n=(size_t)(p-part),i; char upper[8];
            if(!n || (n==1U && part[0]=='.') || (n==2U && part[0]=='.' && part[1]=='.') || part[n-1U]=='.' || part[n-1U]==' ')return false;
            for(i=0;i<n && i<sizeof(upper)-1U && part[i]!='.';++i) {
                upper[i]=part[i]>='a'&&part[i]<='z'?(char)(part[i]-32):part[i]; }
            upper[i]=0;
            if(!xx_rt_strcmp(upper,"CON") || !xx_rt_strcmp(upper,"PRN") || !xx_rt_strcmp(upper,"AUX") || !xx_rt_strcmp(upper,"NUL") ||
               (i==4U && (!xx_rt_memcmp(upper,"COM",3) || !xx_rt_memcmp(upper,"LPT",3)) && upper[3]>='1' && upper[3]<='9'))return false;
            if(!c) {return true; } part=p+1;
        } else if(c<32U || c>126U || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*')return false;
    }
}
static XXFC_MAYBE_UNUSED bool af_name(char *name,size_t capacity,const uint8_t *bytes,size_t n,bool high) {
    size_t i;while(n && (!bytes[n-1U] || (bytes[n-1U]&0x7fU)==' '))--n;
    if(!n || n>=capacity)return false;
    for(i=0;i<n;++i) { uint8_t c=high ? bytes[i]&0x7fU:bytes[i];
        if(c<32U || c>126U) {return false; } name[i]=(char)c; }
    name[n]=0;return af_safe(name);
}
/* Reversible leaf encoding: a source slash is a filename byte, never a host
 * path separator. Tilde itself is quoted, so encodings cannot collide. */
static XXFC_MAYBE_UNUSED bool af_leaf(char *out,size_t cap,const uint8_t *p,size_t n) {
    static const char hex[]="0123456789ABCDEF";size_t i,at=0;
    if(!n)return false;
    for(i=0;i<n;++i){uint8_t c=p[i];if(!c)return false;
        if(c>=33U && c<=126U && c!='~' && c!='/' && c!='\\' && c!=':' && c!='<' && c!='>' && c!='"' && c!='|' && c!='?' && c!='*' && c!='.'){
            if(at+1U>=cap) {return false; } out[at++]=(char)c;
        }else{if(at+3U>=cap)return false;out[at++]='~';out[at++]=hex[c>>4];out[at++]=hex[c&15];}}
    out[at]=0;
    if(!af_safe(out)){uint8_t c=(uint8_t)out[0];if(at+2U>=cap)return false;xx_rt_memmove(out+3,out+1,at);out[0]='~';out[1]=hex[c>>4];out[2]=hex[c&15];}
    return af_safe(out);
}
static bool af_add(af_work *w,const char *name,int64_t at,uint64_t n,uint8_t *owned) {
    size_t i,extra=0;pm_member *m;
    if(!af_poll(w) || n>INT64_MAX || n>w->member_limit || !af_safe(name) ||
       xx_rt_strlen(name)>=sizeof(w->s->items[0].name) || w->s->count>=AF_COUNT_MAX)return false;
    for(i=0;i<w->s->count;++i)if(!xx_rt_strcmp(w->s->items[i].name,name))return false;
    if(w->s->count==w->s->capacity)extra=(w->s->capacity?w->s->capacity:8U)*sizeof(pm_member);
    if(w->used>w->limit || extra>w->limit-w->used)return false;
    if(!pm_add(w->f,w->s,name,owned ? 0:at,owned ? 0:(int64_t)n))return false;
    w->used+=extra;
    m=&w->s->items[w->s->count-1U];xx_rt_strncpy(m->name,name,sizeof(m->name)-1U);
    if(owned) { m->memory=owned;m->size=(int64_t)n;m->packed_size=0;m->offset=-1; }
    return true;
}
static XXFC_MAYBE_UNUSED bool af_copy(af_work *w,const char *name,const uint8_t *p,uint64_t n) {
    uint8_t *out=af_alloc(w,n,true); if(!out)return false;if(n)xx_rt_memcpy(out,p,(size_t)n);
    if(!af_add(w,name,0,n,out)) { af_release(w,out,n);return false; }return true;
}
static bool af_extract(Abstractformat *f,xx_archive_record_state *st,xx_io_device *out,xx_pd_struct *pd) {
    pm_stream *s;pm_member *m;af_work w;uint8_t buffer[4096];int64_t saved;uint64_t at=0U;bool ok=false;
    if(!f || !st || st->format!=f || !st->has_record || !(s=(pm_stream *)st->internal_state) || s->index>=s->count)return false;
    {
        xx_apple_family_info *r=(xx_apple_family_info *)f;const xx_list_s *old=r->parse_options;bool initialized;
        r->parse_options=&st->options;initialized=af_init(&w,f,s,pd);r->parse_options=old;
        if(!initialized)return false;
    }
    m=&s->items[s->index];
    if(m->compression_method==65535U)return af_poll(&w);
    if((uint64_t)m->size>w.member_limit || sizeof(buffer)>w.limit || (saved=xx_io_tell(f->device))<0)return false;
    while(at<(uint64_t)m->size) { size_t n=(uint64_t)m->size-at>sizeof(buffer)?sizeof(buffer):(size_t)((uint64_t)m->size-at),written=0;const uint8_t *p=m->memory?m->memory+(size_t)at:buffer;
        if(!af_poll(&w) || (!m->memory && !af_read(&w,m->offset-f->base_address+(int64_t)at,buffer,n)))goto done;
        while(out && written<n) { ssize_t z=xx_io_write(out,p+written,n-written);if(z<=0 || (size_t)z>n-written)goto done;written+=(size_t)z; }
        at+=n;
    }
    ok=af_poll(&w);
done:if(xx_io_seek64(f->device,saved,SEEK_SET)!=0)ok=false;return ok;
}
static xx_archive_record_state *af_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_apple_family_info *r=(xx_apple_family_info *)f;const xx_list_s *old;xx_archive_record_state *st;
    if(!f) {return NULL; } old=r->parse_options;r->parse_options=options;st=pm_create_records(f,options,pd);r->parse_options=old;return st;
}
static const xx_archive_record *af_current(Abstractformat *f,xx_archive_record_state *st) {
    const xx_archive_record *r=pm_current(f,st);xx_apple_family_info *info=(xx_apple_family_info *)f;
    if(r && ((pm_stream *)st->internal_state)->items[((pm_stream *)st->internal_state)->index].compression_method==65535U) {
        if(!xx_archive_record_set_meta_bool(&st->current_record,XX_META_ID_IS_FOLDER,true) ||
           !xx_archive_record_set_meta_u64(&st->current_record,XX_META_ID_COMPRESSION_METHOD,0U))return NULL;
    }
    if(r && info->note && !xx_archive_record_set_meta_str(&st->current_record,XX_META_ID_COMMENT,info->note)) {return NULL; } return r;
}
static bool af_unpack(Abstractformat *f,xx_archive_record_state *st,xx_pd_struct *pd) {
    const xx_var *v;pm_stream *s;pm_member *m;
    if(!f || !st || st->format!=f || !st->has_record)return false;
    v=xx_format_resolve_extra_parameter(f,&st->options,XX_META_ID_OPT_UNPACK_PATH);
    if(!v)return af_extract(f,st,NULL,pd);
    s=(pm_stream *)st->internal_state;m=&s->items[s->index];
    if(m->compression_method==65535U) {
        const char *base=NULL;char *owned=NULL,*path;bool result;
        if(pd && xx_pd_is_stopped(pd))return false;
        if(v->type==XX_VAR_TYPE_STRING || v->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(v);
        else if(v->type==XX_VAR_TYPE_WSTRING || v->type==XX_VAR_TYPE_WSTRING_VIEW)base=owned=xx_str_unicode_to_utf8(xx_var_get_wstr(v));
        if(!base)return false;
        path=base[0]?xx_str_concat3(base,"/",m->name):xx_str_dup(m->name);
        result=path && af_safe(m->name) && xx_store_create_dirs_a(path,true);
        xx_str_free(path);xx_str_free(owned);return result;
    }
    return pm_unpack(f,st,pd);
}
#define AF_DEFINE_READER(stem,type,extension) \
void xx_##stem##_init(xx_##stem *r,xx_io_device *d,int64_t base) { \
    if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,base,type,extension);r->format.create_archive_records_reading=af_records;r->format.get_current_archive_record=af_current;r->format.unpack_current_archive_record=af_unpack; } } \
xx_##stem *xx_##stem##_create(xx_io_device *d,int64_t base) { xx_##stem *r=(xx_##stem *)xx_mem_alloc(sizeof(*r));if(r)xx_##stem##_init(r,d,base);return r; } \
void xx_##stem##_destroy(xx_##stem *r) { if(r)xx_format_cleanup_extra_parameters(&r->format); } \
void xx_##stem##_free(xx_##stem *r) { if(r) { xx_##stem##_destroy(r);xx_mem_free(r); } } \
bool xx_##stem##_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); } \
bool xx_##stem##_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); } \
bool xx_##stem##_extract_record_to_device(Abstractformat *f,xx_archive_record_state *st,xx_io_device *d,xx_pd_struct *pd) { return af_extract(f,st,d,pd); }
#endif
