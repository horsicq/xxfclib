/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for ASD compressed archive.
 */
#include "xxfclib/formats/asd/xx_asd.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define ASD_MEMORY_CAP (256U*1024U*1024U)


static uint16_t asd_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t asd_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t asd_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):ASD_MEMORY_CAP;
    return n<ASD_MEMORY_CAP?n:ASD_MEMORY_CAP;
}
static bool asd_name(Abstractformat *f,int64_t *at,char *name,size_t cap,bool long_size) {
    uint8_t h[4],raw[1024]; uint32_t n; size_t i,copy;
    if(!pm_read(f,*at,h,long_size?4:1)) return false;
    *at+=long_size?4:1; n=long_size?asd_u32(h):h[0];
    if(n>1024U || !pm_read(f,*at,raw,n)) return false;
    *at+=n;
    if(name && cap) {
        copy=n<cap-1U?n:cap-1U;
        for(i=0;i<copy;++i) name[i]=raw[i]>=32 && raw[i]<127?(char)raw[i]:'_';
        name[copy]=0;
    }
    return true;
}
static bool asd_take(Abstractformat *f,int64_t *at,uint64_t n,void *p) {
    if(*at<0 || (uint64_t)*at>(uint64_t)pm_available(f) || n>(uint64_t)(pm_available(f)-*at)) return false;
    if(p && !pm_read(f,*at,p,(size_t)n)) return false;
    *at+=(int64_t)n; return true;
}
static bool asd_room(Abstractformat *f,pm_stream *s,uint64_t extra,bool adding) {
    size_t i,capacity=s->capacity;
    uint64_t used=extra,budget=asd_budget(f);
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
static bool asd_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    return asd_room(f,s,0,true) && pm_add(f,s,name,at,n);
}
static bool asd_asd(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[14],*packed=NULL,*plain=NULL,history[4096]; int64_t at=8,size=pm_available(f),start;
    uint32_t count,i; uint64_t total=0; uint32_t *crcs=NULL; size_t in=0,out=0,pos=0; unsigned flags=0,mask=0,extra;
    bool result=false;
    if(!pm_read(f,0,h,8) || xx_mem_compare(h,"ASD01\x1a",6) || !(count=asd_u16(h+6))) return false;
    crcs=(uint32_t *)xx_mem_alloc(count*sizeof(*crcs)); if(!crcs) return false;
    for(i=0;i<count;++i) {
        char name[96]; uint32_t n;
        if((pd && xx_pd_is_stopped(pd)) || !asd_name(f,&at,name,sizeof(name),false) || !asd_take(f,&at,14,h)) goto done;
        n=asd_u32(h); crcs[i]=asd_u32(h+4);
        if(total+n>asd_budget(f)/2U || !asd_add(f,s,name,0,0)) goto done;
        s->items[s->count-1U].size=n; total+=n;
    }
    start=at;
    if(size-at<1 || !asd_room(f,s,(uint64_t)(size-at)+2U*total+count*sizeof(*crcs)+sizeof(history),false)) goto done;
    packed=(uint8_t *)xx_mem_alloc((size_t)(size-at)); plain=(uint8_t *)xx_mem_alloc(total?(size_t)total:1U);
    if(!packed || !plain || !pm_read(f,at,packed,(size_t)(size-at))) goto done;
    xx_rt_memset(history,'0',sizeof(history)); extra=packed[in++];
    while(out<total) {
        if(pd && xx_pd_is_stopped(pd)) goto done;
        mask>>=1;
        if(!mask) { if(in>=(uint64_t)(size-start)) goto done; flags=packed[in++]; mask=128U; }
        if(flags&mask) {
            unsigned token,length,distance,j;
            if(in+2U>(uint64_t)(size-start)) goto done;
            token=((unsigned)packed[in]<<8)|packed[in+1U]; in+=2;
            length=(token>>12); if(length==15U) length+=extra; length+=3U;
            distance=(token&4095U)+1U;
            if(length>total-out) goto done;
            for(j=0;j<length;++j) { uint8_t v=history[(pos-distance)&4095U]; plain[out++]=history[pos]=v;pos=(pos+1U)&4095U; }
        } else {
            uint8_t v; if(in>=(uint64_t)(size-start)) goto done; v=packed[in++]; plain[out++]=history[pos]=v; pos=(pos+1U)&4095U;
        }
    }
    /* ASD writers finish their last buffered group with ASCII '0' literals.
     * The archive CRCs and declared sizes cover only the real plaintext. */
    if((uint64_t)(size-start)-in>64U) goto done;
    while(in<(uint64_t)(size-start)) { if(packed[in]!=0 && packed[in]!='0') goto done; ++in; }
    out=0;
    for(i=0;i<count;++i) {
        size_t n=(size_t)s->items[i].size; uint8_t *copy;
        if(xx_crc32(XX_CRC_TYPE_CRC32,plain+out,n)!=crcs[i]) goto done;
        copy=(uint8_t *)xx_mem_alloc(n?n:1U); if(!copy) goto done;
        xx_mem_copy(copy,plain+out,n); s->items[i].memory=copy; s->items[i].compression_method=1; out+=n;
    }
    if(count) s->items[0].packed_size=size-start;
    s->size=size; result=true;
done:
    xx_mem_free(packed); xx_mem_free(plain); xx_mem_free(crcs); return result;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd&&xx_pd_is_stopped(pd))return false;
    return asd_asd(f,s,pd);
}

Abstractformat *xx_asd_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f)pm_init(f,d,base,XX_FILE_TYPE_ASD,"bin");
    return f;
}
void xx_asd_free(Abstractformat *f) {
    if(f){xx_format_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_asd_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>32)n=32;
    if(!pm_read(&f,0,h,(size_t)n))goto done;
    if(n>=8 && !xx_mem_compare(h,"ASD01\x1a",6))type=XX_FILE_TYPE_ASD;
done:
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_asd_open(xx_io_device *d) {return xx_asd_create(d,0);}
static const xx_file_type_t xx_asd_types[]={XX_FILE_TYPE_ASD};
static const xx_format_search_desc xx_asd_desc={xx_asd_types,1,NULL,0,xx_asd_open,xx_asd_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(asd,xx_asd_desc)
