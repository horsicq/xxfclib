/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for RDC compressed stream.
 */
#include "xxfclib/formats/rdc/xx_rdc.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define RDC_MEMORY_CAP (256U*1024U*1024U)


static uint16_t rdc_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint64_t rdc_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):RDC_MEMORY_CAP;
    return n<RDC_MEMORY_CAP?n:RDC_MEMORY_CAP;
}
static bool rdc_take(Abstractformat *f,int64_t *at,uint64_t n,void *p) {
    if(*at<0 || (uint64_t)*at>(uint64_t)pm_available(f) || n>(uint64_t)(pm_available(f)-*at)) return false;
    if(p && !pm_read(f,*at,p,(size_t)n)) return false;
    *at+=(int64_t)n; return true;
}
static bool rdc_room(Abstractformat *f,pm_stream *s,uint64_t extra,bool adding) {
    size_t i,capacity=s->capacity;
    uint64_t used=extra,budget=rdc_budget(f);
    if(adding && s->count==capacity) capacity=capacity?capacity*2U:8U;
    if(used>budget || (uint64_t)capacity*sizeof(pm_member)>budget-used) return false;
    used+=(uint64_t)capacity*sizeof(pm_member);
    for(i=0;i<s->count;++i) {
        uint64_t n=s->items[i].memory?(uint64_t)s->items[i].size:0;
        if(s->items[i].password) n+=xx_rt_strlen(s->items[i].password)+1U;
        if(n>budget-used) return false;
        used+=n;
    }
    return true;
}
static bool rdc_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    return rdc_room(f,s,0,true) && pm_add(f,s,name,at,n);
}
static bool rdc_owned(Abstractformat *f,pm_stream *s,const char *name,uint8_t *data,size_t plain,int64_t offset,int64_t packed) {
    if(!rdc_room(f,s,plain,true) || !rdc_add(f,s,name,offset,packed)) { xx_mem_free(data); return false; }
    s->items[s->count-1U].memory=data; s->items[s->count-1U].size=(int64_t)plain; return true;
}
static bool rdc_rdc_block(const uint8_t *p,size_t size,uint8_t *dst,size_t cap,size_t *written,bool zero_history,xx_pd_struct *pd) {
    size_t at=0,out=0; uint16_t mask=0,flags=0;
    while(at<size) {
        unsigned token,kind,low,length,distance,i;
        if(pd && xx_pd_is_stopped(pd)) return false;
        mask>>=1;
        if(!mask) { if(size-at<2U) return false; flags=rdc_u16(p+at); at+=2; mask=0x8000U; }
        if(at==size) return false;
        token=p[at++];
        if(!(mask&flags)) { if(out==cap) return false; dst[out++]=(uint8_t)token; continue; }
        kind=token>>4; low=token&15U;
        if(at==size) return false;
        if(kind<2U) {
            if(kind==0) length=low+3U;
            else { length=low+(unsigned)p[at++]*16U+19U; if(at==size) return false; }
            token=p[at++]; if(length>cap-out) return false;
            for(i=0;i<length;++i) dst[out++]=(uint8_t)token;
        } else {
            distance=low+3U+(unsigned)p[at++]*16U; length=kind;
            if(kind==2U) { if(at==size) return false; length=(unsigned)p[at++]+16U; }
            if((!zero_history && distance>out) || length>cap-out) return false;
            for(i=0;i<length;++i) { dst[out]=distance>out?0:dst[out-distance]; ++out; }
        }
    }
    *written=out; return true;
}
static bool rdc_rdc(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t at=0,size=pm_available(f); uint8_t h[2],*image=NULL; size_t used=0,capacity=0;
    bool result=false;
    while(at<size) {
        uint16_t word; size_t n,wrote=0; uint8_t *packed,block[16384];
        if((pd && xx_pd_is_stopped(pd)) || !rdc_take(f,&at,2,h)) goto done;
        word=rdc_u16(h);
        if(!word) { if(at!=size) goto done; break; }
        n=(word&0x8000U)?(size_t)(0x10000U-word):word;
        if(n>(uint64_t)(size-at)) goto done;
        packed=(uint8_t *)xx_mem_alloc(n);
        if(!packed || !pm_read(f,at,packed,n)) { xx_mem_free(packed); goto done; }
        if(word&0x8000U) wrote=n;
        else if(!rdc_rdc_block(packed,n,block,sizeof(block),&wrote,false,pd)) { xx_mem_free(packed); goto done; }
        if(used+wrote>rdc_budget(f)) { xx_mem_free(packed); goto done; }
        if(capacity<used+wrote) {
            uint8_t *next; size_t cap=capacity?capacity:16384U;
            while(cap<used+wrote) { if(cap>rdc_budget(f)/2U) {cap=(size_t)rdc_budget(f);break;} cap*=2U; }
            if(!rdc_room(f,s,(uint64_t)cap+n+sizeof(block),true)) { xx_mem_free(packed); goto done; }
            next=(uint8_t *)xx_mem_realloc(image,cap);
            if(!next) { xx_mem_free(packed); goto done; } image=next; capacity=cap;
        }
        xx_mem_copy(image+used,(word&0x8000U)?packed:block,wrote); used+=wrote; at+=(int64_t)n;
        xx_mem_free(packed);
    }
    if(!used) goto done;
    result=rdc_owned(f,s,"decompressed.bin",image,used,0,size); image=NULL; s->size=size;
done:
    xx_mem_free(image); return result;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd&&xx_pd_is_stopped(pd))return false;
    return rdc_rdc(f,s,pd);
}

Abstractformat *xx_rdc_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f)pm_init(f,d,base,XX_FILE_TYPE_RDC,"bin");
    return f;
}
void xx_rdc_free(Abstractformat *f) {
    if(f){xx_format_destroy(f);xx_mem_free(f);}
}
static bool rdc_probe(xx_io_device *d,int64_t base) {
    Abstractformat f;bool valid;pm_init(&f,d,base,XX_FILE_TYPE_RDC,"bin");
    valid=pm_valid(&f,NULL);xx_format_destroy(&f);return valid;
}

xx_file_type_t xx_rdc_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>32)n=32;
    if(!pm_read(&f,0,h,(size_t)n))goto done;
    {const char *path=xx_io_source_path(d);size_t len=path?xx_rt_strlen(path):0;if(len>=4U&&path[len-1U]=='_'&&path[len-4U]=='.'&&rdc_probe(d,base))type=XX_FILE_TYPE_RDC;}
done:
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_rdc_open(xx_io_device *d) {return xx_rdc_create(d,0);}
static const xx_file_type_t xx_rdc_types[]={XX_FILE_TYPE_RDC};
static const xx_format_search_desc xx_rdc_desc={xx_rdc_types,1,NULL,0,xx_rdc_open,xx_rdc_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(rdc,xx_rdc_desc)
