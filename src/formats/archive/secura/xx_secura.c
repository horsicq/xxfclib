/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Independently implemented Secura SAC1 version-2 directory and block grammar. */
#include "xxfclib/formats/archive/secura/xx_secura.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#define SA_CAP UINT64_C(268435456)
#define SA_MEMBER UINT64_C(67108864)
#define SA_WORK UINT64_C(1048576)
typedef struct sa_member { int64_t header;uint64_t resident;uint32_t attributes,header_size; } sa_member;
static uint32_t sa_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t sa_u64(const uint8_t *p) { return sa_u32(p)|((uint64_t)sa_u32(p+4)<<32); }
static uint64_t sa_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):SA_CAP;return n<SA_CAP?n:SA_CAP;
}
static bool sa_block(const uint8_t *h,uint64_t left,uint32_t *bits,uint32_t *plain,uint32_t *extent)
{
    *bits=sa_u32(h+4);*plain=left>65536U?65536U:(uint32_t)left;
    if(!left || h[0]!='C' || h[1]>1 || h[2] || h[3] || h[8]!=(left<=65536U) || h[9] || h[10] || h[11] ||
        !*bits || *bits>*plain*9U || (!h[1] && *bits!=*plain*8U))return false;
    *extent=12U+((*bits+63U)/64U)*8U;return true;
}
static bool sa_extent(Abstractformat *f,uint64_t start,uint64_t size,uint64_t limit,uint64_t *end,xx_pd_struct *pd)
{
    uint8_t h[12];uint64_t at=start,left=size;uint32_t bits,plain,extent;
    while(left) {
        if((pd && xx_pd_is_stopped(pd)) || at>limit || limit-at<12U || !pm_read(f,(int64_t)at,h,12U) ||
            !sa_block(h,left,&bits,&plain,&extent) || extent>limit-at)return false;
        at+=extent;left-=plain;
    }
    *end=at;return true;
}
static char *sa_name(const uint8_t *h)
{
    char raw[512];size_t a=h[264],b=h[8],i;
    if(!b)return NULL;
    for(i=0;i<a;++i)if(!h[265+i])return NULL;
    for(i=0;i<b;++i)if(!h[9+i])return NULL;
    xx_rt_memcpy(raw,h+265,a);xx_rt_memcpy(raw+a,h+9,b);raw[a+b]=0;
    return xx_archive_output_name(raw,0,0);
}
static bool sa_table(Abstractformat *f,uint64_t archive,uint64_t footer,uint32_t count,uint64_t maximum,unsigned version,xx_pd_struct *pd)
{
    uint8_t h[856];uint64_t header=version==1U?552U:856U,width=version==1U?808U:856U,directory,next=archive+header,i;
    if(!count || count>100000U || footer<archive+header || (uint64_t)count*width>footer-archive-header)return false;
    directory=footer-(uint64_t)count*width;
    for(i=0;i<count;++i) {
        uint64_t size,offset,end;char *name;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(directory+i*width),h,(size_t)width))return false;
        size=version==1U?sa_u32(h+520):sa_u64(h+520);offset=version==1U?sa_u32(h+796):sa_u64(h+808);
        if(version==1U && offset>INT32_MAX)return false;
        if(size>maximum || offset>directory-archive || archive+offset!=next ||
            !sa_extent(f,next,size,directory,&end,pd))return false;
        name=sa_name(h);if(!name)return false;xx_mem_free(name);next=end;
    }
    return next==directory;
}
static bool sa_bits(const uint8_t *p,uint32_t limit,uint32_t *at,unsigned n,uint32_t *value)
{
    uint32_t v=0;unsigned i;if(*at>limit || n>limit-*at)return false;
    for(i=0;i<n;++i) { v=(v<<1)|((p[*at/8U]>>(7U-*at%8U))&1U);++*at; }*value=v;return true;
}
static bool sa_lzss(const uint8_t *p,uint32_t bits,uint8_t *out,uint32_t size,xx_pd_struct *pd)
{
    uint8_t ring[4096]={0};uint32_t bit=0,pos=0,done=0;
    while(done<size) {
        uint32_t flag,value,length,i;
        if((pd && xx_pd_is_stopped(pd)) || !sa_bits(p,bits,&bit,1,&flag))return false;
        if(flag) {
            if(!sa_bits(p,bits,&bit,8,&value))return false;
            ring[pos]=(uint8_t)value;out[done++]=(uint8_t)value;pos=(pos+1U)&4095U;
        } else {
            if(!sa_bits(p,bits,&bit,12,&value) || !sa_bits(p,bits,&bit,6,&length) || length+3U>size-done)return false;
            ++value;length+=3U;
            for(i=0;i<length;++i) { uint8_t c=ring[(pos-value)&4095U];out[done++]=c;ring[pos]=c;pos=(pos+1U)&4095U; }
        }
    }
    return bit==bits;
}
static bool sa_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    sa_member *ctx=(sa_member *)m->context;uint8_t h[12],*plain=NULL,*packed=NULL;uint64_t done=0,at=(uint64_t)(m->offset-f->base_address),end=at+(uint64_t)m->packed_size;bool ok=false;
    if(!ctx || ctx->resident+(uint64_t)m->size+SA_WORK>sa_budget(f) || (pd && xx_pd_is_stopped(pd)))return false;
    plain=(uint8_t *)xx_mem_alloc(m->size?(size_t)m->size:1U);packed=(uint8_t *)xx_mem_alloc(73728U);if(!plain || !packed)goto finished;
    while(done<(uint64_t)m->size) {
        uint32_t bits,size,extent;
        if((pd && xx_pd_is_stopped(pd)) || at>end || end-at<12U || !pm_read(f,(int64_t)at,h,12U) ||
            !sa_block(h,(uint64_t)m->size-done,&bits,&size,&extent) || extent>end-at ||
            !pm_read(f,(int64_t)at+12,packed,(bits+7U)/8U))goto finished;
        if(h[1]) { if(!sa_lzss(packed,bits,plain+(size_t)done,size,pd))goto finished; }
        else xx_rt_memcpy(plain+(size_t)done,packed,size);
        at+=extent;done+=size;
    }
    if(at!=end)goto finished;
    done=0;
    while(out && done<(uint64_t)m->size) {
        size_t n=(uint64_t)m->size-done>16384U?16384U:(size_t)((uint64_t)m->size-done);ssize_t wrote;
        if(pd && xx_pd_is_stopped(pd))goto finished;
        wrote=xx_io_write(out,plain+(size_t)done,n);if(wrote<=0 || (size_t)wrote>n)goto finished;done+=(size_t)wrote;
    }
    ok=!(pd && xx_pd_is_stopped(pd));
finished:
    xx_mem_free(plain);xx_mem_free(packed);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t header[856],tail[65536];int64_t total=pm_available(f);uint64_t archive=0,footer=0,directory,maximum=SA_MEMBER,budget=sa_budget(f),names=0;
    uint32_t count=0,i,width;unsigned version;size_t n,at,candidates=0;bool pe=false;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if(total<560 || budget<SA_WORK || (pd && xx_pd_is_stopped(pd)) || !pm_read(f,0,header,8))return false;
    if(header[0]=='M' && header[1]=='Z') {
        xx_pe *runtime=xx_pe_create(f->device,f->base_address);int64_t extent;
        if(!runtime)return false;
        extent=xx_pe_get_format_size(&runtime->format,pd);xx_pe_free(runtime);
        if(extent<=0 || extent>total-560)return false;
        archive=(uint64_t)extent;pe=true;
    }
    if(!pm_read(f,(int64_t)archive,header,8U) || xx_rt_memcmp(header,"SAC1",4) ||
        (header[4]!=1U && header[4]!=2U) || header[5] || header[6])return false;
    version=header[4];width=version==1U?808U:856U;
    n=pe?((uint64_t)total-archive<sizeof(tail)?(size_t)((uint64_t)total-archive):sizeof(tail)):8U;
    if(!pm_read(f,total-(int64_t)n,tail,n))return false;
    for(at=0;at+8U<=n;++at) {
        uint64_t candidate=(uint64_t)total-n+at;uint32_t records;
        if(xx_rt_memcmp(tail+at,"SAC1",4))continue;
        if(++candidates>128U)return false;
        records=sa_u32(tail+at+4);
        if(sa_table(f,archive,candidate,records,maximum,version,pd)) { if(count)return false;footer=candidate;count=records; }
    }
    if(!count)return false;
    directory=footer-(uint64_t)count*width;
    for(i=0;i<count;++i) {
        uint64_t offset,size,end,resident;size_t capacity;char *safe,*name;sa_member *ctx;pm_member *m;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(directory+(uint64_t)i*width),header,width))return false;
        offset=archive+(version==1U?sa_u32(header+796):sa_u64(header+808));size=version==1U?sa_u32(header+520):sa_u64(header+520);
        if(!sa_extent(f,offset,size,directory,&end,pd) || !(safe=sa_name(header)))return false;
        n=xx_rt_strlen(safe)+16U;capacity=s->count==s->capacity?(s->capacity?s->capacity*2U:8U):s->capacity;
        resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+((uint64_t)i+1U)*sizeof(sa_member)+names+n;
        if(resident+SA_WORK>budget) { xx_mem_free(safe);return false; }
        name=(char *)xx_mem_alloc(n);ctx=(sa_member *)xx_mem_alloc(sizeof(*ctx));
        if(!name || !ctx) { xx_mem_free(safe);xx_mem_free(name);xx_mem_free(ctx);return false; }
        (void)xx_rt_snprintf(name,n,"%04u-%s",i,safe);xx_mem_free(safe);
        if(!pm_add(f,s,"member",(int64_t)offset,(int64_t)(end-offset))) { xx_mem_free(name);xx_mem_free(ctx);return false; }
        m=&s->items[i];m->display_name=name;m->context=ctx;m->free_context=xx_mem_free;m->read_all=sa_read_all;m->size=(int64_t)size;
        ctx->header=f->base_address+(int64_t)(directory+(uint64_t)i*width);ctx->header_size=width;
        ctx->attributes=sa_u32(header+(version==1U?784U:792U));ctx->resident=0;names+=n;
    }
    for(i=0;i<count;++i)((sa_member *)s->items[i].context)->resident=sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+(uint64_t)count*sizeof(sa_member)+names;
    s->size=(int64_t)footer+8;return !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *sa_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) { pm_stream *s=(pm_stream *)state->internal_state;sa_member *c=(sa_member *)s->items[s->index].context;
        state->current_record.header_offset=c->header;state->current_record.header_size=c->header_size;
        if(!xx_archive_record_set_meta_u64(&state->current_record,XX_META_ID_ATTRIBUTES,c->attributes) ||
            !xx_archive_record_set_meta_str(&state->current_record,XX_META_ID_COMPRESSION_METHOD,"Secura stored/LZSS blocks"))return NULL; }
    return r;
}
Abstractformat *xx_secura_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_SECURA,"sac");f->get_current_archive_record=sa_current; }return f;
}
void xx_secura_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *sa_open(xx_io_device *d) { return xx_secura_create(d,0); }
static const xx_file_type_t sa_types[]={XX_FILE_TYPE_SECURA};
const xx_format_search_desc xx_secura_search_desc={sa_types,1U,NULL,0U,sa_open,xx_secura_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(secura,xx_secura_search_desc)
