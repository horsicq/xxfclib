/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * LPAK single-file LZSS stream: magic, BE32 plaintext size, then 4 KiB
 * absolute-window references with LSB-first flag bytes and lengths 3..18.
 */
#include "xxfclib/formats/lpak/xx_lpak.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t header[8],window[4096],*packed=NULL,*plain=NULL;
    size_t out=0,pos=0,expected,wp=4078,i;int64_t size=pm_available(f);bool ok=false;
    if(size<8 || size>134217728 || !pm_read(f,0,header,8) || xx_rt_memcmp(header,"LPAK",4))return false;
    expected=xx_data_get_u32(header+4, 4, 0, true);if(expected>268435456 || (size==8 && expected))return false;
    packed=(uint8_t *)xx_mem_alloc(size>8?(size_t)size-8:1);
    plain=(uint8_t *)xx_mem_alloc(expected?expected:1);
    if(!packed || !plain || !pm_read(f,8,packed,(size_t)size-8))goto done;
    xx_rt_memset(window,' ',sizeof(window));
    while(out<expected){
        unsigned flags,bit;
        if((pd && xx_pd_is_stopped(pd)) || pos>=(size_t)size-8)goto done;
        flags=packed[pos++];
        for(bit=0;bit<8 && out<expected;++bit){
            if(flags&(1U<<bit)){
                uint8_t value;if(pos>=(size_t)size-8)goto done;value=packed[pos++];
                plain[out++]=value;window[wp]=value;wp=(wp+1)&4095;
            }else{
                unsigned at,length;if((size_t)size-8-pos<2)goto done;
                at=packed[pos]|((unsigned)(packed[pos+1]&0xf0)<<4);length=(packed[pos+1]&15)+3;pos+=2;
                if(length>expected-out)goto done;
                for(i=0;i<length;++i){uint8_t value=window[(at+i)&4095];plain[out++]=value;window[wp]=value;wp=(wp+1)&4095;}
            }
        }
    }
    if(pos!=(size_t)size-8 || !pm_add(f,s,"payload",8,size-8))goto done;
    s->items[0].size=(int64_t)expected;s->items[0].memory=plain;plain=NULL;s->size=size;ok=true;
 done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
void xx_lpak_init(xx_lpak *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LPAK,"cmp");}}
xx_lpak *xx_lpak_create(xx_io_device *d,int64_t b){xx_lpak *r=(xx_lpak *)xx_mem_alloc(sizeof(*r));if(r)xx_lpak_init(r,d,b);return r;}
void xx_lpak_destroy(xx_lpak *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_lpak_free(xx_lpak *r){if(r){xx_lpak_destroy(r);xx_mem_free(r);}}
bool xx_lpak_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_lpak_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
