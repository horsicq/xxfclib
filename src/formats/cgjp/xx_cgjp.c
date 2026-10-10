/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CGJP bounded game-resource packet archive.
 */
#include "xxfclib/formats/cgjp/xx_cgjp.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

typedef struct cgjp_bits { const uint8_t *bytes;size_t size,bit; } cgjp_bits;
static bool cgjp_getbits(cgjp_bits *in,unsigned count,unsigned *value) {
    unsigned i,v=0;if(count>24 || count>in->size*8-in->bit) return false;
    for(i=0;i<count;++i) { v|=((in->bytes[in->bit>>3]>>(in->bit&7))&1U)<<i;++in->bit; }
    *value=v;return true;
}
static bool cgjp_cgjp_decode(const uint8_t *src,size_t packed,uint8_t *out,size_t size,xx_pd_struct *pd) {
    cgjp_bits in;size_t p=0;
    if(packed<6 || memcmp(src,"DS\0\1",4)) return false;
    in.bytes=src+4;in.size=packed-4;in.bit=0;
    while(p<size) {
        unsigned tag,value,distance,zeros=0,length,i;
        if(xgr_stop(pd) || !cgjp_getbits(&in,2,&tag)) return false;
        if(tag==1 || tag==2) { if(!cgjp_getbits(&in,7,&value)) return false;out[p++]=(uint8_t)(value|(tag==1?128:0));continue; }
        if(tag==0) { if(!cgjp_getbits(&in,6,&distance)) return false; }
        else {
            if(!cgjp_getbits(&in,1,&value)) return false;
            if(!cgjp_getbits(&in,value?12:8,&distance)) return false;distance+=value?320:64;
        }
        if(distance==4415) continue;
        if(!distance || distance>p) return false;
        for(;;) { if(!cgjp_getbits(&in,1,&value)) return false;if(value) break;if(++zeros>15) return false; }
        if(!zeros) length=2;
        else { if(!cgjp_getbits(&in,zeros,&value)) return false;length=value+(1U<<zeros)+1; }
        if(length>size-p) return false;
        for(i=0;i<length;++i) { out[p]=out[p-distance];++p; }
    }
    /* The packet ends with the 15-bit maximum-distance marker. Only zero
     * alignment bits may follow; no undeclared compressed material is used. */
    {unsigned marker,padding;if(!cgjp_getbits(&in,15,&marker) || marker!=32767 || in.size*8-in.bit>7) return false;while(in.bit<in.size*8) if(!cgjp_getbits(&in,1,&padding) || padding) return false;}
    return !xgr_stop(pd);
}
static bool cgjp_cgjp_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t input[32768],plain[32768],h[4];uint64_t p=(uint64_t)(m->offset-f->base_address),end=p+(uint64_t)m->packed_size,total=0;
    while(p<end) {
        uint32_t size,packed;
        if(xgr_stop(pd) || !xgr_range(p,4,end) || !pm_read(f,(int64_t)p,h,4)) return false;
        size=xgr_le16(h);packed=xgr_le16(h+2);p+=4;
        if(!size || size>32768 || packed>32768 || !xgr_range(p,packed?packed:size,end)) return false;
        if(packed) { if(!pm_read(f,(int64_t)p,input,packed) || !cgjp_cgjp_decode(input,packed,plain,size,pd)) return false;p+=packed; }
        else { if(!pm_read(f,(int64_t)p,plain,size)) return false;p+=size; }
        if(size>(uint64_t)m->size-total || !xgr_write(out,plain,size)) return false;total+=size;
    }
    return total==(uint64_t)m->size && !xgr_stop(pd);
}
static bool cgjp_cgjp(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t n=(uint64_t)pm_available(f),p=0;
    while(p<n) {
        uint8_t h[4];uint64_t start,total=0;uint32_t chunks=0;bool compressed=false;char name[40];
        if(xgr_stop(pd) || !xgr_range(p,4,n) || !pm_read(f,(int64_t)p,h,4) || memcmp(h,"CGJP",4)) return false;
        p+=4;start=p;
        while(p<n) {
            uint32_t size,packed;
            if(xgr_stop(pd) || !xgr_range(p,4,n) || !pm_read(f,(int64_t)p,h,4)) return false;
            if(!memcmp(h,"CGJP",4)) break;
            size=xgr_le16(h);packed=xgr_le16(h+2);p+=4;
            if(!size || size>32768 || packed>32768 || !xgr_range(p,packed?packed:size,n) || total>(uint64_t)INT64_MAX-size || ++chunks>XGR_MAX_RECORDS) return false;
            if(packed) { if(packed<6 || !pm_read(f,(int64_t)p,h,4) || memcmp(h,"DS\0\1",4)) return false;compressed=true; }
            total+=size;p+=packed?packed:size;
        }
        if(!chunks) return false;
        xx_rt_snprintf(name,sizeof(name),"File%u.bin",(unsigned)s->count+1);
        if(!xgr_add(f,s,name,start,p-start)) return false;
        {pm_member *m=&s->items[s->count-1];m->size=(int64_t)total;m->read_all=cgjp_cgjp_read;m->compression_method=compressed?1:0;}
    }
    s->size=(int64_t)n;return s->count!=0;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && cgjp_cgjp(f,s,pd);
}
Abstractformat *xx_cgjp_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_CGJP,"pak");return f;
}
void xx_cgjp_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_cgjp_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    { uint32_t magic=xgr_le32(h);
    if(magic==0x504a4743U) type=XX_FILE_TYPE_CGJP;
    }
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *cgjp_open(xx_io_device *d) { return xx_cgjp_create(d,0); }
static const xx_file_type_t cgjp_types[]={XX_FILE_TYPE_CGJP};
static const xx_format_search_desc cgjp_desc={
    cgjp_types,1,NULL,0,cgjp_open,xx_cgjp_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(cgjp,cgjp_desc)
