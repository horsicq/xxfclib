/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Spear of Destiny installer volumes: dated headers and stored block chains.
 * No signature/checksum is invented; every header and extent must validate. */
#include "xxfclib/formats/archive/sod_installer/xx_sod_installer.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/softronics/xx_softronics.h"
#define SI_CAP UINT64_C(268435456)
#define SI_MEMBER UINT64_C(67108864)
#define SI_BUFFER 16384U
typedef struct si_block { int64_t at;uint32_t size,packed; } si_block;
typedef struct si_member { si_block *blocks;size_t count,capacity;uint64_t resident;int64_t header; } si_member;
static uint16_t si_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t si_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t si_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):SI_CAP;return n<SI_CAP?n:SI_CAP;
}
static bool si_date(const uint8_t *p)
{
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned y=si_u16(p),m=si_u16(p+2),d=si_u16(p+4),limit;
    if(y<1980 || y>2100 || m<1 || m>12 || si_u16(p+6)>23 || si_u16(p+8)>59 || si_u16(p+10)>59)return false;
    limit=days[m-1];if(m==2 && (!(y%400) || (!(y%4) && y%100)))++limit;
    return d>=1 && d<=limit;
}
static bool si_name(const uint8_t *p,char name[13])
{
    size_t n=12,i,dot=14;
    while(n && (p[n-1]==0 || p[n-1]==' '))--n;
    if(!n || n>12)return false;
    for(i=0;i<n;++i) {
        unsigned c=p[i];
        if(c=='.') { if(dot!=14 || !i || i>8)return false;dot=i; }
        else if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='$' || c=='~'))return false;
        name[i]=(char)c;
    }
    if((dot==14 && n>8) || (dot!=14 && (n-dot-1<1 || n-dot-1>3)))return false;
    name[n]=0;return true;
}
static void si_free(void *p)
{
    si_member *m=(si_member *)p;if(m) { xx_mem_free(m->blocks);xx_mem_free(m); }
}
static bool si_lzw(Abstractformat *f,const si_block *b,xx_io_device *out,uint64_t resident,xx_pd_struct *pd)
{
    uint8_t *input=NULL,*plain=NULL;bool ok=false;size_t at=0;
    if((pd && xx_pd_is_stopped(pd)) || resident+b->size+b->packed+131072U>si_budget(f))return false;
    input=(uint8_t *)xx_mem_alloc(b->packed);plain=(uint8_t *)xx_mem_alloc(b->size);
    if(!input || !plain || !pm_read(f,b->at,input,b->packed) || !xx_softronics_lzw_decompress_memory_exact(input,b->packed,plain,b->size))goto done;
    while(out && at<b->size) {
        size_t n=b->size-at>SI_BUFFER?SI_BUFFER:b->size-at;ssize_t w;
        if(pd && xx_pd_is_stopped(pd))goto done;
        w=xx_io_write(out,plain+at,n);if(w<=0 || (size_t)w>n)goto done;at+=(size_t)w;
    }
    ok=!(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(input);xx_mem_free(plain);return ok;
}
static bool si_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    si_member *c=(si_member *)m->context;uint8_t buffer[SI_BUFFER];size_t i;
    if(!c || c->resident+sizeof(buffer)>si_budget(f))return false;
    for(i=0;i<c->count;++i) {
        uint32_t at=0;
        if(c->blocks[i].packed!=c->blocks[i].size) {
            if(!si_lzw(f,&c->blocks[i],out,c->resident,pd))return false;
            continue;
        }
        while(at<c->blocks[i].size) {
            size_t n=c->blocks[i].size-at>sizeof(buffer)?sizeof(buffer):c->blocks[i].size-at,w=0;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,c->blocks[i].at+at,buffer,n))return false;
            while(out && w<n) { ssize_t got=xx_io_write(out,buffer+w,n-w);if(got<=0 || (size_t)got>n-w)return false;w+=(size_t)got; }
            at+=(uint32_t)n;
        }
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    int64_t total=pm_available(f),at=0;uint8_t h[32];uint64_t budget=si_budget(f),resident=sizeof(pm_stream),maximum=SI_MEMBER;size_t i;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if(total<32 || budget<resident+SI_BUFFER || (pd && xx_pd_is_stopped(pd)))return false;
    while(at<total) {
        char name[13];uint32_t size,left;size_t capacity;pm_member *m;si_member *ctx;
        if((pd && xx_pd_is_stopped(pd)) || s->count>=100000U || total-at<32 || !pm_read(f,at,h,32) || !si_date(h) ||
            (si_u16(h+16)&~0x27U) || !si_name(h+18,name))return false;
        size=si_u32(h+12);if(size>maximum)return false;
        capacity=s->count==s->capacity?(s->capacity?s->capacity*2U:8U):s->capacity;
        resident+=(uint64_t)(capacity-s->capacity)*sizeof(pm_member)+sizeof(si_member);
        if(resident+SI_BUFFER>budget || !pm_add(f,s,name,at+32,0))return false;
        m=&s->items[s->count-1];ctx=(si_member *)xx_mem_calloc(1,sizeof(*ctx));if(!ctx)return false;
        m->size=size;m->context=ctx;m->free_context=si_free;m->read_all=si_read_all;ctx->header=f->base_address+at;at+=32;left=size;
        while(left) {
            uint32_t plain,packed;
            if((pd && xx_pd_is_stopped(pd)) || ctx->count>=65536U || total-at<4 || !pm_read(f,at,h,4))return false;
            plain=si_u16(h);packed=si_u16(h+2);
            if(!plain || !packed || plain>left || packed>(uint64_t)(total-at-4))return false;
            if(ctx->count==ctx->capacity) {
                size_t next=ctx->capacity?ctx->capacity*2U:8U;void *grown;
                resident+=(next-ctx->capacity)*sizeof(si_block);if(resident+SI_BUFFER>budget)return false;
                grown=xx_mem_realloc(ctx->blocks,next*sizeof(si_block));if(!grown)return false;ctx->blocks=(si_block *)grown;ctx->capacity=next;
            }
            ctx->blocks[ctx->count].at=at+4;ctx->blocks[ctx->count].size=plain;ctx->blocks[ctx->count].packed=packed;
            if(plain!=packed && !si_lzw(f,&ctx->blocks[ctx->count],NULL,resident,pd))return false;
            if(plain!=packed)m->compression_method=1;
            ++ctx->count;left-=plain;at+=4+packed;
        }
        m->offset=f->base_address+(ctx->count?ctx->blocks[0].at:at);m->packed_size=at-(ctx->header-f->base_address)-32;
    }
    if(!s->count)return false;
    for(i=0;i<s->count;++i)((si_member *)s->items[i].context)->resident=resident;
    s->size=total;return true;
}
static const xx_archive_record *si_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) { pm_stream *s=(pm_stream *)state->internal_state;si_member *c=(si_member *)s->items[s->index].context;state->current_record.header_offset=c->header;state->current_record.header_size=32; }
    return r;
}
Abstractformat *xx_sod_installer_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_SOD_INSTALLER,"sod_package");f->get_current_archive_record=si_current; }return f;
}
void xx_sod_installer_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static const xx_file_type_t si_types[]={XX_FILE_TYPE_SOD_INSTALLER};
static Abstractformat *si_open(xx_io_device *d) { return xx_sod_installer_create(d,0); }
const xx_format_search_desc xx_sod_installer_search_desc={si_types,1U,NULL,0U,si_open,xx_sod_installer_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sod_installer,xx_sod_installer_search_desc)
