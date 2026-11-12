/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Aladdin DOS Self-UnStuffer's bounded stored-payload directory. */
#include "xxfclib/formats/archive/self_unstuffer/xx_self_unstuffer.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#define SU_CAP UINT64_C(268435456)
#define SU_MEMBER UINT64_C(67108864)
typedef struct su_member { int64_t header;uint64_t resident; } su_member;
static uint32_t su_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t su_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint64_t su_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):SU_CAP;return n<SU_CAP?n:SU_CAP;
}
static bool su_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    su_member *c=(su_member *)m->context;uint8_t buffer[16384];uint64_t at=0;
    if(!c || c->resident+sizeof(buffer)>su_budget(f))return false;
    while(at<(uint64_t)m->size) {
        size_t n=(uint64_t)m->size-at>sizeof(buffer)?sizeof(buffer):(size_t)((uint64_t)m->size-at),wrote=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,m->offset-f->base_address+(int64_t)at,buffer,n))return false;
        while(out && wrote<n) { ssize_t amount=xx_io_write(out,buffer+wrote,n-wrote);if(amount<=0 || (size_t)amount>n-wrote)return false;wrote+=(size_t)amount; }
        at+=n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    static const uint8_t magic[]={1,1,'A','l','a','d','d','i','n',0,0};
    uint8_t h[256];int64_t total=pm_available(f);uint64_t extent,directory,next,budget=su_budget(f),maximum=SU_MEMBER,names=0;uint32_t count,i;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if(total<64 || budget<sizeof(pm_stream)+16384U || (pd && xx_pd_is_stopped(pd)) || !pm_read(f,0,h,64) ||
        h[0]!='M' || h[1]!='Z' || !su_u16(h+4) || su_u16(h+2)>511U || su_u16(h+8)<4U)return false;
    extent=((uint64_t)su_u16(h+4)-1U)*512U+(su_u16(h+2)?su_u16(h+2):512U);
    if((uint64_t)su_u16(h+8)*16U>extent || extent>(uint64_t)total || (uint64_t)total-extent<17U ||
        !pm_read(f,(int64_t)extent,h,17U) || xx_rt_memcmp(h,magic,sizeof(magic)))return false;
    count=su_u16(h+11);directory=su_u32(h+13);
    if(!count || directory!=extent+17U || directory>(uint64_t)total || (uint64_t)count*26U>(uint64_t)total-directory)return false;
    next=directory+(uint64_t)count*26U;
    for(i=0;i<count;++i) {
        uint64_t offset,size,resident;size_t n,capacity;char raw[14],*safe,*name;su_member *ctx;pm_member *m;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(directory+(uint64_t)i*26U),h,26U) || h[0]!=1U)return false;
        for(n=0;n<13U && h[1+n];++n)raw[n]=(char)h[1+n];
        if(!n || n==13U)return false;
        raw[n]=0;for(++n;n<13U;++n)if(h[1+n])return false;
        offset=su_u32(h+14);size=su_u32(h+18);
        if(offset!=next || size>maximum || next>(uint64_t)total || size>(uint64_t)total-next)return false;
        next+=size;safe=xx_archive_output_name(raw,0,0);if(!safe)return false;
        n=xx_rt_strlen(safe)+16U;capacity=s->count==s->capacity?(s->capacity?s->capacity*2U:8U):s->capacity;
        resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+((uint64_t)i+1U)*sizeof(su_member)+names+n;
        if(resident+16384U>budget) { xx_mem_free(safe);return false; }
        name=(char *)xx_mem_alloc(n);ctx=(su_member *)xx_mem_alloc(sizeof(*ctx));
        if(!name || !ctx) { xx_mem_free(safe);xx_mem_free(name);xx_mem_free(ctx);return false; }
        (void)xx_rt_snprintf(name,n,"%04u-%s",i,safe);xx_mem_free(safe);
        if(!pm_add(f,s,raw,(int64_t)offset,(int64_t)size)) { xx_mem_free(name);xx_mem_free(ctx);return false; }
        m=&s->items[i];m->display_name=name;m->context=ctx;m->free_context=xx_mem_free;m->read_all=su_read_all;
        ctx->header=f->base_address+(int64_t)(directory+(uint64_t)i*26U);ctx->resident=0;names+=n;
    }
    if(((uint64_t)total!=next && (uint64_t)total!=((next+255U)&~UINT64_C(255))) ||
        !pm_read(f,(int64_t)next,h,(size_t)((uint64_t)total-next)))return false;
    for(i=0;i<(uint64_t)total-next;++i)if(h[i])return false;
    for(i=0;i<count;++i)((su_member *)s->items[i].context)->resident=sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+(uint64_t)count*sizeof(su_member)+names;
    s->size=(int64_t)next;return !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *su_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) { pm_stream *s=(pm_stream *)state->internal_state;su_member *c=(su_member *)s->items[s->index].context;
        state->current_record.header_offset=c->header;state->current_record.header_size=26; }
    return r;
}
Abstractformat *xx_self_unstuffer_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_SELF_UNSTUFFER,"exe");f->get_current_archive_record=su_current; }return f;
}
void xx_self_unstuffer_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *su_open(xx_io_device *d) { return xx_self_unstuffer_create(d,0); }
static const xx_file_type_t su_types[]={XX_FILE_TYPE_SELF_UNSTUFFER};
const xx_format_search_desc xx_self_unstuffer_search_desc={su_types,1U,NULL,0U,su_open,xx_self_unstuffer_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(self_unstuffer,xx_self_unstuffer_search_desc)
