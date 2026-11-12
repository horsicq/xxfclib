/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * PatchWise ZSB: bounded bzip2 chunks holding name/size/data records. */
#include "xxfclib/formats/archive/patchwise/xx_patchwise.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#define PW_CAP UINT64_C(268435456)
#define PW_CHUNK UINT64_C(67108864)
#define PW_WORK UINT64_C(8388608)
typedef struct pw_member { uint64_t resident;int64_t header; } pw_member;
static uint32_t pw_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t pw_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):PW_CAP;return n<PW_CAP?n:PW_CAP;
}
static bool pw_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    pw_member *c=(pw_member *)m->context;size_t at=0;
    if(!c || c->resident>pw_budget(f))return false;
    while(at<(uint64_t)m->size) {
        size_t n=(uint64_t)m->size-at>16384U?16384U:(size_t)m->size-at;
        if(pd && xx_pd_is_stopped(pd))return false;
        if(out) { ssize_t wrote=xx_io_write(out,m->memory+at,n);if(wrote<=0 || (size_t)wrote>n)return false;at+=(size_t)wrote; }
        else at+=n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[18],*decoded=NULL;int64_t total=pm_available(f),at=0,consumed;
    uint64_t budget=pw_budget(f),retained=sizeof(pm_stream),maximum=PW_CHUNK;size_t chunks=0,i;bool ok=false;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if(total<18 || budget<PW_WORK || (pd && xx_pd_is_stopped(pd)) || !pm_read(f,0,h,18))return false;
    if(h[0]=='M' && h[1]=='Z') {
        xx_pe *pe=xx_pe_create(f->device,f->base_address);if(!pe)return false;
        at=xx_pe_get_format_size(&pe->format,pd);xx_pe_free(pe);
        if(at<=0 || at>total-18)return false;
    }
    while(at<total) {
        uint32_t packed,plain;size_t pos=0;xx_io_device *out;
        if(++chunks>65536U || (pd && xx_pd_is_stopped(pd)) || total-at<18 || !pm_read(f,at,h,18))goto done;
        packed=pw_u32(h);plain=pw_u32(h+4);
        if(packed<14U || packed>PW_CHUNK || plain<6U || plain>PW_CHUNK || (uint64_t)packed+4U>(uint64_t)(total-at) ||
            xx_rt_memcmp(h+8,"BZh",3) || h[11]<'1' || h[11]>'9' || retained+plain+PW_WORK>budget)goto done;
        decoded=(uint8_t *)xx_mem_alloc(plain);if(!decoded)goto done;
        out=xx_io_mem_open(decoded,plain);if(!out)goto done;
        ok=xx_bzip2_unpack_device_ex(f->device,f->base_address+at+8,(int64_t)packed-4,out,&consumed,pd) &&
            consumed==(int64_t)packed-4 && xx_io_tell(out)==plain;
        xx_io_close(out);if(!ok)goto done;ok=false;
        while(pos<plain) {
            size_t start=pos,n,capacity;uint32_t size;uint64_t need;char raw[1025],*safe,*name;pw_member *ctx;pm_member *m;
            if((pd && xx_pd_is_stopped(pd)) || s->count>=100000U)goto done;
            while(pos<plain && decoded[pos] && pos-start<1024U)++pos;
            if(pos==start || pos>=plain || decoded[pos] || plain-pos<5U)goto done;
            n=pos-start;xx_rt_memcpy(raw,decoded+start,n);raw[n]=0;++pos;size=pw_u32(decoded+pos);pos+=4;
            if(size>maximum || size>plain-pos)goto done;
            safe=xx_archive_output_name(raw,0,0);if(!safe)goto done;
            n=xx_rt_strlen(safe)+16U;capacity=s->count==s->capacity?(s->capacity?s->capacity*2U:8U):s->capacity;
            need=retained+(uint64_t)(capacity-s->capacity)*sizeof(pm_member)+sizeof(pw_member)+n+(size?size:1U);
            if(need+plain+PW_WORK>budget) { xx_mem_free(safe);goto done; }
            name=(char *)xx_mem_alloc(n);ctx=(pw_member *)xx_mem_alloc(sizeof(*ctx));
            if(!name || !ctx) { xx_mem_free(safe);xx_mem_free(name);xx_mem_free(ctx);goto done; }
            (void)xx_rt_snprintf(name,n,"%04u-%s",(unsigned)s->count,safe);xx_mem_free(safe);
            if(!pm_add(f,s,raw,at+8,(int64_t)packed-4)) { xx_mem_free(name);xx_mem_free(ctx);goto done; }
            m=&s->items[s->count-1];m->context=ctx;m->free_context=xx_mem_free;m->display_name=name;m->read_all=pw_read_all;
            m->size=size;m->compression_method=12;ctx->header=f->base_address+at;ctx->resident=0;
            m->memory=(uint8_t *)xx_mem_alloc(size?size:1U);if(!m->memory)goto done;
            xx_rt_memcpy(m->memory,decoded+pos,size);pos+=size;retained=need;
        }
        xx_mem_free(decoded);decoded=NULL;at+=(int64_t)packed+4;
    }
    if(!s->count)goto done;
    for(i=0;i<s->count;++i)((pw_member *)s->items[i].context)->resident=retained;
    s->size=total;ok=!(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(decoded);return ok;
}
static const xx_archive_record *pw_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) { pm_stream *s=(pm_stream *)state->internal_state;pw_member *c=(pw_member *)s->items[s->index].context;
        state->current_record.header_offset=c->header;state->current_record.header_size=8; }
    return r;
}
Abstractformat *xx_patchwise_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_PATCHWISE,"zsb");f->get_current_archive_record=pw_current; }return f;
}
void xx_patchwise_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *pw_open(xx_io_device *d) { return xx_patchwise_create(d,0); }
static const xx_file_type_t pw_types[]={XX_FILE_TYPE_PATCHWISE};
const xx_format_search_desc xx_patchwise_search_desc={pw_types,1U,NULL,0U,pw_open,xx_patchwise_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(patchwise,xx_patchwise_search_desc)
