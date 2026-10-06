/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native dependency-free DGCA integration. */
#include "xxfclib/formats/legacy_archive_engine/xx_legacy_archive_engine.h"
#include "ue2_indexed.h"
#include "dgca_native.h"
#include <limits.h>

#define DGA_MEMORY (UINT64_C(256)*1024*1024)
#define DGA_MEMBERS 65535U
#define DGA_PATH_LENGTH 32768U
typedef union dga_allocation { struct { size_t bytes; } header; uint64_t align; void *pointer; long double alignment; } dga_allocation;
typedef struct dga_index {
    ue2_index records;
    dg_result decoded;
    Abstractformat *format;
    xx_io_device *source;
    int64_t base;
    xx_pd_struct *pd;
    uint64_t live, peak, cap;
    char *password;
} dga_index;
typedef struct dga_format { Abstractformat format; dga_index *index; uint64_t generation; } dga_format;
typedef struct dga_state { ue2_state reading; uint64_t generation; } dga_state;

static bool dga_fail(xx_pd_struct *pd,const char *detail) {xx_pd_set_error(pd,1,detail);return false;}
static void *dga_alloc(void *opaque,size_t size) {
    dga_index *ix=(dga_index *)opaque;dga_allocation *h;size_t n;
    if(xx_pd_is_stopped(ix->pd)||size>SIZE_MAX-sizeof(*h))return NULL;
    n=size+sizeof(*h);if(ix->live>ix->cap||n>ix->cap-ix->live)return NULL;
    h=(dga_allocation *)xx_mem_alloc(n);if(!h)return NULL;h->header.bytes=n;
    ix->live+=n;if(ix->live>ix->peak)ix->peak=ix->live;return h+1;
}
static void dga_release(void *opaque,void *pointer) {
    dga_index *ix=(dga_index *)opaque;dga_allocation *h;
    if(!pointer) {return; } h=((dga_allocation *)pointer)-1;ix->live-=h->header.bytes;xx_mem_free(h);
}
static int dga_cancel(void *opaque) {return xx_pd_is_stopped(((dga_index *)opaque)->pd);}
static size_t dga_read(void *opaque,uint64_t offset,void *bytes,size_t size) {
    dga_index *ix=(dga_index *)opaque;ssize_t n;int64_t at,total=xx_io_total_size(ix->source);
    if(xx_pd_is_stopped(ix->pd)||ix->base<0||total<ix->base||offset>(uint64_t)(total-ix->base))return 0;
    at=ix->base+(int64_t)offset;if(size>(uint64_t)(total-at))return 0;
    if(size>65536U)size=65536U;
    if(xx_io_seek64(ix->source,at,XX_RT_SEEK_SET))return 0;
    n=xx_io_read(ix->source,bytes,size);return n>0&&(size_t)n<=size?(size_t)n:0;
}
static uint64_t dga_limit(Abstractformat *f,const xx_list_s *options,xx_meta_id_t id,uint64_t fallback) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,options,id);return v?xx_var_get_u64(v):fallback;
}
/* Operation views may be unterminated. Copy by the variant's explicit length,
 * and bound the effective UTF-8 credential before decoding or cache lookup. */
static bool dga_password(Abstractformat *f,const xx_list_s *options,char out[1025],xx_pd_struct *pd) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,options,XX_META_ID_OPT_PASSWORD);
    size_t i,n,written=0;uint32_t cp;const wchar_t *wide;
    out[0]=0;if(!v)return true;
    if(v->type==XX_VAR_TYPE_STRING||v->type==XX_VAR_TYPE_STRING_VIEW){
        n=v->val.str.len;
        if(n>1024||(!v->val.str.ptr&&n))return dga_fail(pd,"DGCA password exceeds the supported size");
        for(i=0;i<n;++i)if(!v->val.str.ptr[i])return dga_fail(pd,"DGCA password contains an embedded NUL");
        if(n) {xx_rt_memcpy(out,v->val.str.ptr,n); } out[n]=0;return true;
    }
    if(v->type!=XX_VAR_TYPE_WSTRING&&v->type!=XX_VAR_TYPE_WSTRING_VIEW)
        return dga_fail(pd,"DGCA password must be a string");
    n=v->val.wstr.len;wide=v->val.wstr.ptr;
    if(n>1024||(!wide&&n))return dga_fail(pd,"DGCA password exceeds the supported size");
    for(i=0;i<n;++i){
        cp=(uint32_t)wide[i];
        if(sizeof(wchar_t)==2&&cp>=0xd800&&cp<=0xdbff){
            uint32_t low;if(++i>=n)return dga_fail(pd,"DGCA password has invalid Unicode");
            low=(uint32_t)wide[i];if(low<0xdc00||low>0xdfff)return dga_fail(pd,"DGCA password has invalid Unicode");
            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
        }
        if(!cp||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return dga_fail(pd,"DGCA password has invalid Unicode");
        if(cp<0x80){if(written>1023)goto too_long;out[written++]=(char)cp;}
        else if(cp<0x800){if(written>1022)goto too_long;out[written++]=(char)(0xc0|(cp>>6));out[written++]=(char)(0x80|(cp&63));}
        else if(cp<0x10000){if(written>1021)goto too_long;out[written++]=(char)(0xe0|(cp>>12));out[written++]=(char)(0x80|((cp>>6)&63));out[written++]=(char)(0x80|(cp&63));}
        else{if(written>1020)goto too_long;out[written++]=(char)(0xf0|(cp>>18));out[written++]=(char)(0x80|((cp>>12)&63));out[written++]=(char)(0x80|((cp>>6)&63));out[written++]=(char)(0x80|(cp&63));}
    }
    out[written]=0;return true;
too_long:
    return dga_fail(pd,"DGCA password exceeds the supported size");
}
static void dga_index_free(dga_index *ix) {
    if(!ix) {return; } dg_native_result_free(&ix->decoded);dga_release(ix,ix->records.members);
    dga_release(ix,ix->password);xx_mem_free(ix);
}
static void dga_invalidate(dga_format *f) {
    dga_index_free(f->index);f->index=NULL;++f->generation;if(!f->generation)++f->generation;
    f->format.base_info_handled=f->format.is_valid=false;f->format.number_of_archive_records=0;f->format.format_size=-1;
}
static bool dga_limits_match(dga_index *ix,Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    size_t i;uint64_t cap=dga_limit(f,options,XX_META_ID_OPT_MEMORY_LIMIT,DGA_MEMORY);
    uint64_t max_member=dga_limit(f,options,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX);
    uint64_t max_count=DGA_MEMBERS;
    if(ix->live>cap)return dga_fail(pd,"DGCA retained data exceeds the memory limit");
    if(ix->decoded.count>max_count)return dga_fail(pd,"DGCA member count exceeds the configured limit");
    for(i=0;i<ix->decoded.count;++i)if(ix->decoded.members[i].size>max_member)return dga_fail(pd,"DGCA member exceeds the configured size limit");
    return true;
}
static bool dga_info_options(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    dga_format *format=(dga_format *)f;dga_index *ix;dg_callbacks cb;dg_status status;char pwd[1025];
    uint64_t cap,member_limit,max_members;int64_t saved,total;size_t i,j,n;bool success=false;
    if(!f||!f->device||xx_pd_is_stopped(pd))return false;
    if(!dga_password(f,options,pwd,pd)) {return false; } ix=format->index;
    if(ix&&f->base_info_handled&&f->is_valid&&ix->source==f->device&&ix->base==f->base_address&&!xx_rt_strcmp(ix->password,pwd))return dga_limits_match(ix,f,options,pd);
    dga_invalidate(format);total=xx_io_total_size(f->device);
    if(f->base_address<0||total<f->base_address)return dga_fail(pd,"DGCA invalid source range");
    cap=dga_limit(f,options,XX_META_ID_OPT_MEMORY_LIMIT,DGA_MEMORY);
    member_limit=dga_limit(f,options,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX);
    max_members=DGA_MEMBERS;
    if(max_members>DGA_MEMBERS)max_members=DGA_MEMBERS;
    if(cap<sizeof(*ix))return dga_fail(pd,"DGCA metadata exceeds the memory limit");
    saved=xx_io_tell(f->device);ix=(dga_index *)xx_mem_calloc(1,sizeof(*ix));if(!ix)return false;
    ix->format=f;ix->source=f->device;ix->base=f->base_address;ix->pd=pd;ix->cap=cap;ix->live=ix->peak=sizeof(*ix);
    cb.opaque=ix;cb.read_at=dga_read;cb.allocate=dga_alloc;cb.release=dga_release;cb.cancelled=dga_cancel;
    n=xx_rt_strlen(pwd);if(n>1024){dga_fail(pd,"DGCA password exceeds the supported size");goto done;}
    ix->password=(char *)dga_alloc(ix,n+1);if(!ix->password)goto done;xx_rt_memcpy(ix->password,pwd,n+1);
    status=dg_native_decode(&cb,(uint64_t)(total-f->base_address),pwd,member_limit,max_members,&ix->decoded);
    if(status!=DG_OK){dga_fail(pd,ix->decoded.detail?ix->decoded.detail:"DGCA native decoding failed");goto done;}
    if(ix->decoded.format_size>INT64_MAX||ix->decoded.count>SIZE_MAX/sizeof(ue2_member))goto done;
    if(ix->decoded.count){
        ix->records.members=(ue2_member *)dga_alloc(ix,ix->decoded.count*sizeof(ue2_member));if(!ix->records.members)goto done;
        xx_mem_zero(ix->records.members,ix->decoded.count*sizeof(ue2_member));
    }
    ix->records.count=ix->records.capacity=ix->decoded.count;ix->records.size=(int64_t)ix->decoded.format_size;
    for(i=0;i<ix->decoded.count;++i){dg_member *m=ix->decoded.members+i;ue2_member *r=ix->records.members+i;
        if(!ue2_safe_name(m->name)||m->size>INT64_MAX||m->compressed_size>INT64_MAX)goto done;
        for(j=0;j<i;++j)if(!xx_rt_strcmp(ix->decoded.members[j].name,m->name))goto done;
        r->name=m->name;r->offset=0;r->size=(int64_t)m->compressed_size;r->original_size=(int64_t)m->size;r->tag=i;r->is_folder=(m->attributes&0x10)!=0;
    }
    if(!dga_limits_match(ix,f,options,pd))goto done;
    format->index=ix;f->format_size=ix->records.size;f->number_of_archive_records=ix->records.count;f->is_valid=f->base_info_handled=true;success=true;
done:
    if(saved>=0&&xx_io_seek64(f->device,saved,XX_RT_SEEK_SET)){if(success){format->index=NULL;f->is_valid=f->base_info_handled=false;}success=false;}
    if(!success) {dga_index_free(ix); } return success;
}
static bool dga_info(Abstractformat *f,xx_pd_struct *pd) {return dga_info_options(f,NULL,pd);}
static int64_t dga_size(Abstractformat *f,xx_pd_struct *pd) {return f&&dga_info(f,pd)?f->format_size:-1;}
static uint64_t dga_count(Abstractformat *f,xx_pd_struct *pd) {return f&&dga_info(f,pd)?f->number_of_archive_records:0;}
/* Paths are operation state, not borrowed views. Preserve the explicit length
 * and add a terminator before any conversion or filesystem call. */
static bool dga_path_copy(xx_var *dst,const xx_var *src,xx_pd_struct *pd) {
    size_t n,i;char *narrow;wchar_t *wide;
    if(src->type==XX_VAR_TYPE_STRING||src->type==XX_VAR_TYPE_STRING_VIEW){
        n=src->val.str.len;
        if(n>DGA_PATH_LENGTH||(!src->val.str.ptr&&n))return dga_fail(pd,"DGCA extraction path exceeds the supported size");
        for(i=0;i<n;++i)if(!src->val.str.ptr[i])return dga_fail(pd,"DGCA extraction path contains an embedded NUL");
        narrow=(char *)xx_mem_alloc(n+1);if(!narrow)return false;
        if(n) {xx_rt_memcpy(narrow,src->val.str.ptr,n); } narrow[n]=0;
        return xx_var_set_str_take(dst,narrow,n);
    }
    if(src->type==XX_VAR_TYPE_WSTRING||src->type==XX_VAR_TYPE_WSTRING_VIEW){
        n=src->val.wstr.len;
        if(n>DGA_PATH_LENGTH||(!src->val.wstr.ptr&&n))return dga_fail(pd,"DGCA extraction path exceeds the supported size");
        for(i=0;i<n;++i)if(!src->val.wstr.ptr[i])return dga_fail(pd,"DGCA extraction path contains an embedded NUL");
        wide=(wchar_t *)xx_mem_alloc((n+1)*sizeof(*wide));if(!wide)return false;
        if(n) {xx_rt_memcpy(wide,src->val.wstr.ptr,n*sizeof(*wide)); } wide[n]=0;
        return xx_var_set_wstr_take(dst,wide,n);
    }
    return dga_fail(pd,"DGCA extraction path must be a string");
}
static xx_archive_record_state *dga_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_archive_record_state *s=NULL;dga_state *state;size_t i;xx_meta path;
    const xx_var *effective_path=xx_format_resolve_extra_parameter(f,options,XX_META_ID_OPT_UNPACK_PATH);
    bool path_in_options=false;
    xx_meta_init(&path,XX_META_ID_OPT_UNPACK_PATH);
    if(effective_path&&!dga_path_copy(&path.var,effective_path,pd))goto fail;
    if(!dga_info_options(f,options,pd)) {goto fail; } s=ue2_records(f,options,pd);if(!s)goto fail;
    /* The common copier retains string-view pointers. An operation password
     * must remain owned after its caller releases or changes those views. */
    for(i=0;i<s->options.count;++i){
        xx_meta *meta=(xx_meta *)xx_list_at(&s->options,i);
        if(meta&&meta->meta_id==XX_META_ID_OPT_PASSWORD&&
           !xx_var_set_str(&meta->var,((dga_format *)f)->index->password)){
            goto fail;
        }
        if(meta&&meta->meta_id==XX_META_ID_OPT_UNPACK_PATH){
            if(!xx_var_copy(&meta->var,&path.var))goto fail;
            path_in_options=true;
        }
    }
    if(effective_path&&!path_in_options){
        if(!xx_list_append(&s->options,&path))goto fail;
        xx_meta_init(&path,XX_META_ID_OPT_UNPACK_PATH); /* State owns the snapshot. */
    }
    state=(dga_state *)xx_mem_alloc(sizeof(*state));if(!state)goto fail;
    state->reading=*(ue2_state *)s->internal_state;state->generation=((dga_format *)f)->generation;
    xx_mem_free(s->internal_state);s->internal_state=state;xx_meta_cleanup(&path);return s;
fail:
    xx_meta_cleanup(&path);ue2_free_records(f,s);return NULL;
}
static const xx_archive_record *dga_current(Abstractformat *f,xx_archive_record_state *s) {
    dga_index *ix=f?((dga_format *)f)->index:NULL;dga_state *state=s?(dga_state *)s->internal_state:NULL;
    const xx_archive_record *record=ue2_current(f,s);const dg_member *m;char pwd[1025];
    if(!record||!state||!ix||!f->is_valid||!f->base_info_handled||state->generation!=((dga_format *)f)->generation||state->reading.index!=&ix->records||ix->source!=f->device||ix->base!=f->base_address||state->reading.cursor>=ix->decoded.count||!dga_password(f,&s->options,pwd,NULL)||xx_rt_strcmp(ix->password,pwd))return NULL;
    m=ix->decoded.members+state->reading.cursor;
    if(!xx_archive_record_set_meta_u64(&s->current_record,XX_META_ID_CRC32,m->crc32)||
       !xx_archive_record_set_meta_u64(&s->current_record,XX_META_ID_ATTRIBUTES,m->attributes)||
       !xx_archive_record_set_meta_u64(&s->current_record,XX_META_ID_TIMESTAMP,m->timestamp))return NULL;
    return record;
}
static bool dga_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {return dga_current(f,s)&&ue2_next(f,s,pd);}
static bool dga_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {
    const xx_archive_record *r=dga_current(f,s);dga_index *ix=f?((dga_format *)f)->index:NULL;
    dga_state *state=s?(dga_state *)s->internal_state:NULL;const dg_member *m;const xx_var *v;
    const char *base=NULL;char *converted=NULL,*path=NULL;xx_io_device *memory=NULL;bool result=false;
    if(!r||xx_pd_is_stopped(pd)||!dga_limits_match(ix,f,&s->options,pd))return false;
    m=ix->decoded.members+state->reading.cursor;v=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);
    if(!v)return true; /* Parsing already validated payloads entirely in RAM. */
    if(v->type==XX_VAR_TYPE_STRING||v->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(v);
    else if(v->type==XX_VAR_TYPE_WSTRING||v->type==XX_VAR_TYPE_WSTRING_VIEW){converted=xx_str_unicode_to_utf8(xx_var_get_wstr(v));base=converted;}
    if(!base||!ue2_safe_name(m->name)||!(path=xx_str_concat3(base,"/",m->name))||!xx_store_create_dirs_a(path,(m->attributes&0x10)!=0))goto done;
    if(m->attributes&0x10)result=true;
    else if((memory=xx_io_mem_open_ro(m->bytes,(size_t)m->size))!=NULL)result=xx_store_unpack_device_to_file(memory,0,(int64_t)m->size,path,pd);
done:
    xx_io_close(memory);xx_str_free(path);xx_str_free(converted);return result;
}
static void dga_destroy(Abstractformat *f) {if(f){dga_invalidate((dga_format *)f);xx_format_cleanup_extra_parameters(f);}}
Abstractformat *xx_dgca_native_create(xx_io_device *device,int64_t base) {
    dga_format *f=(dga_format *)xx_mem_calloc(1,sizeof(*f));if(!f)return NULL;f->generation=1;
    ue2_init_format(&f->format,device,base,XX_FILE_TYPE_DGCA,"dgc","application/x-dgca");
    f->format.check_is_valid=dga_info;f->format.handle_base_info=dga_info;f->format.create_archive_records_reading=dga_records;
    f->format.get_format_size=dga_size;f->format.get_number_of_archive_records=dga_count;
    f->format.get_current_archive_record=dga_current;f->format.archive_record_move_to_next=dga_next;
    f->format.unpack_current_archive_record=dga_unpack;f->format.destroy=dga_destroy;return &f->format;
}
void xx_dgca_native_free(Abstractformat *f) {if(f){dga_destroy(f);xx_mem_free(f);}}
