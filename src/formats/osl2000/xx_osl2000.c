/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for OSL2000 compressed resources.
 */
#include "xxfclib/formats/osl2000/xx_osl2000.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define OSL2000_MEMORY_CAP (256U*1024U*1024U)


static uint16_t osl2000_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t osl2000_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t osl2000_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):OSL2000_MEMORY_CAP;
    return n<OSL2000_MEMORY_CAP?n:OSL2000_MEMORY_CAP;
}
static bool osl2000_take(Abstractformat *f,int64_t *at,uint64_t n,void *p) {
    if(*at<0 || (uint64_t)*at>(uint64_t)pm_available(f) || n>(uint64_t)(pm_available(f)-*at)) return false;
    if(p && !pm_read(f,*at,p,(size_t)n)) return false;
    *at+=(int64_t)n; return true;
}
static bool osl2000_room(Abstractformat *f,pm_stream *s,uint64_t extra,bool adding) {
    size_t i,capacity=s->capacity;
    uint64_t used=extra,budget=osl2000_budget(f);
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
static bool osl2000_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    return osl2000_room(f,s,0,true) && pm_add(f,s,name,at,n);
}
static bool osl2000_owned(Abstractformat *f,pm_stream *s,const char *name,uint8_t *data,size_t plain,int64_t offset,int64_t packed) {
    if(!osl2000_room(f,s,plain,true) || !osl2000_add(f,s,name,offset,packed)) { xx_mem_free(data); return false; }
    s->items[s->count-1U].memory=data; s->items[s->count-1U].size=(int64_t)plain; return true;
}
static uint8_t osl2000_rol(uint8_t v) { return (uint8_t)((v<<1)|(v>>7)); }
static uint8_t osl2000_ror(uint8_t v) { return (uint8_t)((v>>1)|(v<<7)); }
static void osl2000_osl_decode(uint8_t *p,size_t n) {
    size_t i=n;
    while(i) { uint8_t v; --i; v=p[i]; if(i) v=p[i-1U]^(uint8_t)(osl2000_rol(v)+i); p[i]=(uint8_t)(osl2000_ror(v)-i)^90U; }
}
static uint16_t osl2000_osl_sum(const uint8_t *p,size_t n) {
    uint16_t sum=0; size_t i;
    for(i=0;i<n;++i) { sum=(uint16_t)(sum+p[i]); sum=(uint16_t)((sum<<1)|(sum>>15)); }
    return sum;
}
static bool osl2000_osl(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16],tail[2]; int64_t at=3,size=pm_available(f); uint64_t retained=0;
    if(!pm_read(f,0,h,3) || xx_mem_compare(h,"CAB",3)) return false;
    while(at<size) {
        uint32_t n; uint8_t *plain; char name[11]; size_t i;
        if((pd && xx_pd_is_stopped(pd)) || !osl2000_take(f,&at,16,h)) return false;
        osl2000_osl_decode(h,16); n=osl2000_u32(h+2);
        if(osl2000_u16(h)!=0x4948U || n>osl2000_budget(f)-retained || !osl2000_room(f,s,(uint64_t)n+1024U,true) || n>(uint64_t)(size-at) ||
           (uint64_t)at+n+2U>(uint64_t)size) return false;
        for(i=0;i<10;++i) name[i]=h[i+6]>=32 && h[i+6]<127?(char)h[i+6]:0;
        name[10]=0; plain=(uint8_t *)xx_mem_alloc(n?n:1U);
        if(!plain || !pm_read(f,at,plain,n) || !pm_read(f,at+n,tail,2)) { xx_mem_free(plain); return false; }
        osl2000_osl_decode(plain,n);
        if(osl2000_osl_sum(plain,n)!=osl2000_u16(tail)) { xx_mem_free(plain); return false; }
        if(!xx_rt_strcmp(name,"MENU")) {
            uint8_t key=90;
            for(i=0;i<n;++i) { key=(uint8_t)((key>>4)+(key<<4)+1U); plain[i]^=key; }
        }
        if(!osl2000_owned(f,s,name,plain,n,at,n)) return false;
        retained+=n; at+=(int64_t)n+2;
    }
    if(!s->count) return false;
    s->size=size; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd&&xx_pd_is_stopped(pd))return false;
    return osl2000_osl(f,s,pd);
}

Abstractformat *xx_osl2000_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f)pm_init(f,d,base,XX_FILE_TYPE_OSL2000,"bin");
    return f;
}
void xx_osl2000_free(Abstractformat *f) {
    if(f){xx_format_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_osl2000_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>32)n=32;
    if(!pm_read(&f,0,h,(size_t)n))goto done;
    if(n>=19 && !xx_mem_compare(h,"CAB",3)) {uint8_t header[16];xx_mem_copy(header,h+3,16);osl2000_osl_decode(header,16);if(osl2000_u16(header)==0x4948U)type=XX_FILE_TYPE_OSL2000;}
done:
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_osl2000_open(xx_io_device *d) {return xx_osl2000_create(d,0);}
static const xx_file_type_t xx_osl2000_types[]={XX_FILE_TYPE_OSL2000};
static const xx_format_search_desc xx_osl2000_desc={xx_osl2000_types,1,NULL,0,xx_osl2000_open,xx_osl2000_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(osl2000,xx_osl2000_desc)
