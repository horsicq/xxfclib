/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DCP floppy-image wrapper, including raw Unix disks and RDC compression.
 * Geometry and complete block decoding are validated before exposure.
 */
#include "xx_arc9_compat.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define AX_CAP (256U*1024U*1024U)
static uint16_t ax_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t ax_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t ax_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):AX_CAP;return n<AX_CAP?n:AX_CAP;
}
static bool ax_dcp_header(Abstractformat *f,uint64_t *plain,unsigned *method) {
    uint8_t h[96];unsigned sectors,heads,cylinders;
    if(pm_available(f)<512 || !pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h+92,"DCP\0",4) ||
       h[88]<4 || h[88]>32 || ax_u16(h+90)>1U) return false;
    sectors=ax_u16(h);heads=ax_u16(h+2);cylinders=ax_u16(h+4);
    if(!sectors || sectors>63 || !heads || heads>2 || !cylinders || cylinders>255) return false;
    *plain=(uint64_t)sectors*heads*cylinders*512U;*method=ax_u16(h+90);return true;
}
static bool ax_rdc_block(const uint8_t *p,size_t size,uint8_t *dst,size_t cap,size_t *written,xx_pd_struct *pd) {
    size_t at=0,out=0;unsigned mask=0,flags=0;
    while(at<size) {
        unsigned token,kind,low,length,distance,i;
        if(pd && xx_pd_is_stopped(pd)) return false;
        mask>>=1;
        if(!mask) {if(size-at<2U)return false;flags=ax_u16(p+at);at+=2;mask=0x8000U;}
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
static bool ax_dcp(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t expected;unsigned method;uint8_t *plain=NULL,*packed=NULL,h[2];
    int64_t size=pm_available(f),at=512;size_t out=0;bool result=false;
    if(f->file_type!=XX_FILE_TYPE_DCP_DISK || (pd && xx_pd_is_stopped(pd)) ||
       !ax_dcp_header(f,&expected,&method) || (uint64_t)sizeof(pm_member)*8U>ax_budget(f))return false;
    if(!method) {
        if((uint64_t)(size-512)!=expected || !pm_add(f,s,"floppy.ima",512,(int64_t)expected))return false;
        s->size=size;return true;
    }
    if(expected+32768U+(uint64_t)sizeof(pm_member)*8U>ax_budget(f))return false;
    plain=(uint8_t *)xx_mem_alloc((size_t)expected);packed=(uint8_t *)xx_mem_alloc(32768U);
    if(!plain || !packed)goto done;
    while(at<size) {
        unsigned word;size_t n,wrote=0,capacity;
        if((pd && xx_pd_is_stopped(pd)) || size-at<2 || !pm_read(f,at,h,2))goto done;
        at+=2;word=ax_u16(h);
        if(!word) {if(at!=size)goto done;break;}
        n=(word&0x8000U)?0x10000U-word:word;
        if(n>(uint64_t)(size-at) || !pm_read(f,at,packed,n))goto done;
        at+=(int64_t)n;capacity=(size_t)expected-out;
        if(word&0x8000U) {if(n>capacity)goto done;xx_mem_copy(plain+out,packed,n);wrote=n;}
        else {
            if(capacity>16384U)capacity=16384U;
            if(!ax_rdc_block(packed,n,plain+out,capacity,&wrote,pd))goto done;
        }
        out+=wrote;
    }
    if(out!=expected || !pm_add(f,s,"floppy.ima",512,size-512))goto done;
    s->items[0].memory=plain;plain=NULL;s->items[0].size=(int64_t)expected;s->items[0].compression_method=1;
    s->size=size;result=true;
done:
    xx_mem_free(plain);xx_mem_free(packed);return result;
}
typedef struct ax_buffer {uint8_t *data;size_t size,capacity,limit;} ax_buffer;
static ssize_t ax_buffer_write(xx_io_device *d,const void *p,size_t n) {
    ax_buffer *b=d->priv;
    if(n>b->limit-b->size)return -1;
    if(b->capacity-b->size<n) {
        size_t cap=b->capacity?b->capacity:(b->limit<65536U?b->limit:65536U);uint8_t *next;
        while(cap<b->size+n) {if(cap>b->limit/2U){cap=b->limit;break;}cap*=2U;}
        next=xx_mem_realloc(b->data,cap);if(!next)return -1;b->data=next;b->capacity=cap;
    }
    if(n)xx_mem_copy(b->data+b->size,p,n);b->size+=n;return (ssize_t)n;
}
static bool ax_sony_header(Abstractformat *f,uint8_t *mode) {
    uint8_t h[26];int64_t n=pm_available(f);
    if(n<512 || !pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h,"TCJN",4) || ax_u32(h+16) ||
       ax_u32(h+20)!=(uint64_t)(n-512) || h[25]>1U)return false;
    *mode=h[24];return true;
}
static bool ax_sony(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t mode,h[6],tail[4];ax_buffer b={0};xx_io_device sink;int64_t used=0,size=pm_available(f);
    uint64_t reserve=524288U+(uint64_t)sizeof(pm_member)*8U,budget=ax_budget(f);bool result=false;uint32_t adler;
    if((pd && xx_pd_is_stopped(pd)) || !ax_sony_header(f,&mode))return false;
    if(mode) {if(pd)xx_pd_set_error(pd,-1,"Sony Image continuation requires the initial .IMG volume");return false;}
    if(size<522 || !pm_read(f,512,h,6) || xx_mem_compare(h,"ZLIB",4) ||
       !xx_zlib_stream_header_is_valid(h+4,2) || budget<=reserve)return false;
    b.limit=(size_t)(budget-reserve);if(b.limit>64U*1024U*1024U)b.limit=64U*1024U*1024U;
    xx_mem_zero(&sink,sizeof(sink));sink.priv=&b;sink.write=ax_buffer_write;
    if(!xx_deflate_unpack_device_ex(f->device,f->base_address+518,size-518,&sink,false,0,NULL,0,&used,pd) ||
       used!=size-522 || !pm_read(f,518+used,tail,4) || (pd && xx_pd_is_stopped(pd)))goto done;
    adler=((uint32_t)tail[0]<<24)|((uint32_t)tail[1]<<16)|((uint32_t)tail[2]<<8)|tail[3];
    if(adler!=xx_zlib_stream_adler32(b.data,b.size) || !pm_add(f,s,"image.img",516,size-516))goto done;
    s->items[0].memory=b.data;b.data=NULL;s->items[0].size=(int64_t)b.size;s->items[0].compression_method=8;s->size=size;result=true;
done:
    xx_mem_free(b.data);return result;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(f->file_type==XX_FILE_TYPE_SONY_IMAGE)return ax_sony(f,s,pd);
    return ax_dcp(f,s,pd);
}
Abstractformat *xx_arc9_compat_create(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));if(f)pm_init(f,d,b,type,type==XX_FILE_TYPE_SONY_IMAGE?"img":"dcp");return f;
}
void xx_arc9_compat_free(Abstractformat *f) {if(f){xx_format_destroy(f);xx_mem_free(f);}}
bool xx_arc9_compat_probe(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat f;bool valid;pm_init(&f,d,b,type,"dcp");valid=pm_valid(&f,NULL);xx_format_destroy(&f);return valid;
}
xx_file_type_t xx_arc9_compat_detect(xx_io_device *d,int64_t b) {
    Abstractformat f;uint64_t plain;unsigned method;uint8_t mode;int64_t cursor;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d || b<0)return XX_FILE_TYPE_UNKNOWN;
    cursor=xx_io_tell(d);xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=b;
    if(ax_dcp_header(&f,&plain,&method))type=XX_FILE_TYPE_DCP_DISK;
    else if(ax_sony_header(&f,&mode))type=XX_FILE_TYPE_SONY_IMAGE;
    (void)xx_io_seek64(d,cursor,SEEK_SET);return type;
}
