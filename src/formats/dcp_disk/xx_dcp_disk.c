/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for DCP floppy image.
 */
#include "xxfclib/formats/dcp_disk/xx_dcp_disk.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define DCP_DISK_MEMORY_CAP (256U*1024U*1024U)


static uint16_t dcp_disk_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint64_t dcp_disk_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):DCP_DISK_MEMORY_CAP;return n<DCP_DISK_MEMORY_CAP?n:DCP_DISK_MEMORY_CAP;
}
static bool dcp_disk_dcp_header(Abstractformat *f,uint64_t *plain,unsigned *method) {
    uint8_t h[96];unsigned sectors,heads,cylinders;
    if(pm_available(f)<512 || !pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h+92,"DCP\0",4) ||
       h[88]<4 || h[88]>32 || dcp_disk_u16(h+90)>1U) return false;
    sectors=dcp_disk_u16(h);heads=dcp_disk_u16(h+2);cylinders=dcp_disk_u16(h+4);
    if(!sectors || sectors>63 || !heads || heads>2 || !cylinders || cylinders>255) return false;
    *plain=(uint64_t)sectors*heads*cylinders*512U;*method=dcp_disk_u16(h+90);return true;
}
static bool dcp_disk_rdc_block(const uint8_t *p,size_t size,uint8_t *dst,size_t cap,size_t *written,xx_pd_struct *pd) {
    size_t at=0,out=0;unsigned mask=0,flags=0;
    while(at<size) {
        unsigned token,kind,low,length,distance,i;
        if(pd && xx_pd_is_stopped(pd)) return false;
        mask>>=1;
        if(!mask) {if(size-at<2U)return false;flags=dcp_disk_u16(p+at);at+=2;mask=0x8000U;}
        if(at==size)return false;token=p[at++];
        if(!(mask&flags)) {if(out==cap)return false;dst[out++]=(uint8_t)token;continue;}
        kind=token>>4;low=token&15U;
        if(at==size)return false;
        if(kind<2U) {
            if(!kind)length=low+3U;
            else {length=low+(unsigned)p[at++]*16U+19U;if(at==size)return false;}
            token=p[at++];if(length>cap-out)return false;
            for(i=0;i<length;++i)dst[out++]=(uint8_t)token;
        } else {
            distance=low+(unsigned)p[at++]*16U+3U;length=kind;
            if(kind==2U) {if(at==size)return false;length=(unsigned)p[at++]+16U;}
            if(distance>out || length>cap-out)return false;
            for(i=0;i<length;++i) {dst[out]=dst[out-distance];++out;}
        }
    }
    *written=out;return true;
}
static bool dcp_disk_dcp(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t expected;unsigned method;uint8_t *plain=NULL,*packed=NULL,h[2];
    int64_t size=pm_available(f),at=512;size_t out=0;bool result=false;
    if(f->file_type!=XX_FILE_TYPE_DCP_DISK || (pd && xx_pd_is_stopped(pd)) ||
       !dcp_disk_dcp_header(f,&expected,&method) || (uint64_t)sizeof(pm_member)*8U>dcp_disk_budget(f))return false;
    if(!method) {
        if((uint64_t)(size-512)!=expected || !pm_add(f,s,"floppy.ima",512,(int64_t)expected))return false;
        s->size=size;return true;
    }
    if(expected+32768U+(uint64_t)sizeof(pm_member)*8U>dcp_disk_budget(f))return false;
    plain=(uint8_t *)xx_mem_alloc((size_t)expected);packed=(uint8_t *)xx_mem_alloc(32768U);
    if(!plain || !packed)goto done;
    while(at<size) {
        unsigned word;size_t n,wrote=0,capacity;
        if((pd && xx_pd_is_stopped(pd)) || size-at<2 || !pm_read(f,at,h,2))goto done;
        at+=2;word=dcp_disk_u16(h);
        if(!word) {if(at!=size)goto done;break;}
        n=(word&0x8000U)?0x10000U-word:word;
        if(n>(uint64_t)(size-at) || !pm_read(f,at,packed,n))goto done;
        at+=(int64_t)n;capacity=(size_t)expected-out;
        if(word&0x8000U) {if(n>capacity)goto done;xx_mem_copy(plain+out,packed,n);wrote=n;}
        else {
            if(capacity>16384U)capacity=16384U;
            if(!dcp_disk_rdc_block(packed,n,plain+out,capacity,&wrote,pd))goto done;
        }
        out+=wrote;
    }
    if(out!=expected || !pm_add(f,s,"floppy.ima",512,size-512))goto done;
    s->items[0].memory=plain;plain=NULL;s->items[0].size=(int64_t)expected;s->items[0].compression_method=1;
    s->size=size;result=true;
done:
    xx_mem_free(plain);xx_mem_free(packed);return result;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd&&xx_pd_is_stopped(pd))return false;
    return dcp_disk_dcp(f,s,pd);
}

Abstractformat *xx_dcp_disk_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f)pm_init(f,d,base,XX_FILE_TYPE_DCP_DISK,"dcp");
    return f;
}
void xx_dcp_disk_free(Abstractformat *f) {
    if(f){xx_format_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_dcp_disk_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;int64_t cursor;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    {uint64_t plain;unsigned method;if(dcp_disk_dcp_header(&f,&plain,&method))type=XX_FILE_TYPE_DCP_DISK;}
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_dcp_disk_open(xx_io_device *d) {return xx_dcp_disk_create(d,0);}
static const xx_file_type_t xx_dcp_disk_types[]={XX_FILE_TYPE_DCP_DISK};
static const xx_format_search_desc xx_dcp_disk_desc={xx_dcp_disk_types,1,NULL,0,xx_dcp_disk_open,xx_dcp_disk_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(dcp_disk,xx_dcp_disk_desc)
