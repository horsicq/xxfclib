/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Fury of the Furries DAT: bounded sequential 8.3 records and space-filled
 * 4 KiB LZSS. Layout reverse engineered by carbon14, documented at
 * https://moddingwiki.shikadi.net/wiki/DAT_Format_(Fury_of_the_Furries).
 * This streaming ring implementation is independent of FuryStudio's code. */
#include "xxfclib/formats/game/fury_dat/xx_fury_dat.h"
#include "../../xx_payload_members.h"
#include <stdlib.h>
#define FU_CAP UINT64_C(268435456)
#define FU_MEMBER UINT64_C(67108864)
#define FU_TOTAL INT64_C(536870912)
typedef struct fu_member { int64_t header; uint64_t resident; uint8_t pinned[22]; } fu_member;
typedef struct fu_input {
    Abstractformat *format; pm_member *member;
    uint64_t used; size_t index, count; uint8_t bytes[4096];
} fu_input;
typedef struct fu_output {
    xx_io_device *device; size_t cursor, count;
    uint8_t ring[4096], bytes[16384];
} fu_output;
static uint32_t fu_u32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t fu_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):FU_CAP;return n<FU_CAP?n:FU_CAP;
}
static bool fu_get(fu_input *in,uint8_t *value)
{
    if(in->used>=(uint64_t)in->member->packed_size)return false;
    if(in->index==in->count) {
        uint64_t left=(uint64_t)in->member->packed_size-in->used;
        in->count=left<sizeof(in->bytes)?(size_t)left:sizeof(in->bytes);in->index=0;
        if(!pm_read(in->format,in->member->offset-in->format->base_address+(int64_t)in->used,in->bytes,in->count))return false;
    }
    *value=in->bytes[in->index++];++in->used;return true;
}
static bool fu_flush(fu_output *out)
{
    size_t done=0;
    while(out->device && done<out->count) {
        ssize_t n=xx_io_write(out->device,out->bytes+done,out->count-done);
        if(n<=0 || (size_t)n>out->count-done)return false;
        done+=(size_t)n;
    }
    out->count=0;return true;
}
static bool fu_emit(fu_output *out,uint8_t byte)
{
    out->ring[out->cursor]=byte;out->cursor=(out->cursor+1U)&4095U;
    if(out->device) {out->bytes[out->count++]=byte;if(out->count==sizeof(out->bytes) && !fu_flush(out))return false;}
    return true;
}
static bool fu_read_all(Abstractformat *f,pm_member *m,xx_io_device *device,xx_pd_struct *pd)
{
    fu_member *ctx=(fu_member *)m->context;fu_input in;fu_output out;
    uint64_t produced=0;uint8_t header[22],flags=0;unsigned bits=0;
    if(!ctx || ctx->resident+sizeof(in)+sizeof(out)>fu_budget(f) ||
        !pm_read(f,ctx->header-f->base_address,header,sizeof(header)) || xx_rt_memcmp(header,ctx->pinned,sizeof(header)))return false;
    xx_mem_zero(&in,sizeof(in));in.format=f;in.member=m;
    xx_mem_zero(&out,sizeof(out));xx_rt_memset(out.ring,0x20,sizeof(out.ring));out.cursor=4078;out.device=device;
    while(produced<(uint64_t)m->size) {
        uint8_t a,b;size_t length=1,position=0;
        if(pd && xx_pd_is_stopped(pd))return false;
        if(m->compression_method) {
            if(!bits) {if(!fu_get(&in,&flags))return false;bits=8;}
            if(!(flags&1U)) {
                if(!fu_get(&in,&a) || !fu_get(&in,&b))return false;
                length=(b&15U)+3U;position=a|((size_t)(b&0xf0U)<<4U);
                if(length>(uint64_t)m->size-produced)return false;
                while(length--) {uint8_t byte=out.ring[position];position=(position+1U)&4095U;if(!fu_emit(&out,byte))return false;++produced;}
                flags>>=1U;--bits;continue;
            }
            flags>>=1U;--bits;
        }
        if(!fu_get(&in,&a) || !fu_emit(&out,a))return false;
        ++produced;
    }
    return in.used==(uint64_t)m->packed_size && !(pd && xx_pd_is_stopped(pd)) && fu_flush(&out);
}
static unsigned fu_lower(unsigned c) {return c>='A' && c<='Z'?c+32U:c;}
static int fu_compare(const void *a,const void *b)
{
    const unsigned char *x=(const unsigned char *)*(char *const *)a,*y=(const unsigned char *)*(char *const *)b;
    while(*x && *y && fu_lower(*x)==fu_lower(*y)) {++x;++y;}
    return (int)fu_lower(*x)-(int)fu_lower(*y);
}
static bool fu_name(const uint8_t *p,char name[13])
{
    size_t n=0,i,dot=13;char stem[9];
    while(n<13U && p[n])++n;
    if(!n || n>12U)return false;
    for(i=0;i<n;++i) {
        unsigned c=p[i];
        if(c=='.') {if(dot!=13 || !i || i>8U)return false;dot=i;}
        else if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
            c=='_' || c=='-' || c=='$' || c=='~' || c=='!' || c=='#' || c=='%' || c=='&' || c=='\'' || c=='(' || c==')' ||
            c=='+' || c==',' || c=='@' || c=='^' || c=='`' || c=='{' || c=='}'))return false;
        name[i]=(char)c;
    }
    if((dot==13 && n>8U) || (dot!=13 && (n-dot-1U<1U || n-dot-1U>3U)))return false;
    for(i=0;i<(dot==13?n:dot);++i)stem[i]=(char)fu_lower(p[i]);
    stem[i]=0;
    if(!xx_rt_strcmp(stem,"con") || !xx_rt_strcmp(stem,"prn") || !xx_rt_strcmp(stem,"aux") || !xx_rt_strcmp(stem,"nul") ||
       ((i==4U && stem[3]>='1' && stem[3]<='9') && (!xx_rt_memcmp(stem,"com",3) || !xx_rt_memcmp(stem,"lpt",3))))return false;
    name[n]=0;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[22];int64_t total=pm_available(f),at=2;size_t count,i,capacity=8;
    uint64_t resident,raw_total=0,maximum=FU_MEMBER;const xx_var *v;
    char **names;bool unique=true;
    if(total<24 || total>FU_TOTAL || !pm_read(f,0,h,2))return false;
    count=h[0]|((size_t)h[1]<<8);if(!count || count>(uint64_t)(total-2)/22U)return false;
    while(capacity<count)capacity*=2U;
    resident=sizeof(*s)+capacity*sizeof(pm_member)+count*(sizeof(fu_member)+sizeof(char *));
    if(resident+sizeof(fu_input)+sizeof(fu_output)>fu_budget(f))return false;
    v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    for(i=0;i<count;++i) {
        char name[13];uint32_t raw,packed;pm_member *m;fu_member *ctx;
        if((pd && xx_pd_is_stopped(pd)) || total-at<22 || !pm_read(f,at,h,22) || !fu_name(h,name) || h[21]>1)return false;
        raw=fu_u32(h+13);packed=fu_u32(h+17);
        if(raw>maximum || packed>(uint64_t)(total-at-22) || (h[21] && raw!=packed) || raw_total>(uint64_t)FU_TOTAL-raw)return false;
        if(!pm_add(f,s,name,at+22,packed))return false;
        m=&s->items[s->count-1U];xx_rt_memcpy(m->name,name,xx_rt_strlen(name)+1U);m->size=raw;m->compression_method=(uint16_t)(1U-h[21]);
        ctx=(fu_member *)xx_mem_alloc(sizeof(*ctx));if(!ctx)return false;
        ctx->header=f->base_address+at;ctx->resident=resident;xx_rt_memcpy(ctx->pinned,h,sizeof(h));
        m->context=ctx;m->free_context=xx_mem_free;m->read_all=fu_read_all;
        if(m->compression_method && !fu_read_all(f,m,NULL,pd))return false;
        raw_total+=raw;at+=22+(int64_t)packed;
    }
    if(at!=total)return false;
    names=(char **)xx_mem_alloc(count*sizeof(*names));if(!names)return false;
    for(i=0;i<count;++i)names[i]=s->items[i].name;
    qsort(names,count,sizeof(*names),fu_compare);
    for(i=1;i<count;++i)if(!fu_compare(&names[i-1U],&names[i]))unique=false;
    xx_mem_free(names);s->size=total;return unique && !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *fu_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) {pm_stream *s=(pm_stream *)state->internal_state;fu_member *m=(fu_member *)s->items[s->index].context;state->current_record.header_offset=m->header;state->current_record.header_size=22;}
    return r;
}
Abstractformat *xx_fury_dat_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) {pm_init(f,d,base,XX_FILE_TYPE_FURY_DAT,"dat");f->get_current_archive_record=fu_current;}return f;
}
void xx_fury_dat_free(Abstractformat *f) {if(f) {xx_format_destroy(f);xx_mem_free(f);}}
