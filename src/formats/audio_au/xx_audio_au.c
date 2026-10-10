/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/au.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/audio_au/xx_audio_au.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !binary_range(at,n,(uint64_t)pm_available(f)) ||
       !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24]; uint32_t offset,declared,encoding,rate,channels,width;
    uint64_t n,measured=0; int64_t available; bool be;
    if(binary_stop(pd) || (available=pm_available(f))<25 || !pm_read(f,0,h,24))
        return false;
    if(!xx_rt_memcmp(h,".snd",4) || !xx_rt_memcmp(h,".sd\0",4))
        be=true;
    else if(!xx_rt_memcmp(h,"dns.",4) || !xx_rt_memcmp(h,"\0ds.",4))
        be=false;
    else return false;
    offset=xx_data_get_u32(h+4, 4, 0, be); declared=xx_data_get_u32(h+8, 4, 0, be);
    encoding=xx_data_get_u32(h+12, 4, 0, be); rate=xx_data_get_u32(h+16, 4, 0, be);
    channels=xx_data_get_u32(h+20, 4, 0, be);
    if(offset<24 || offset>available || encoding>29 || !rate ||
       rate>768000 || !channels || channels>256) return false;
    n=declared==UINT32_MAX ? (uint64_t)available-offset : declared;
    width=(encoding==1 || encoding==2 || encoding==27 || encoding==29) ? 1:
          encoding==3 || encoding==18 ? 2:encoding==4 ? 3:
          (encoding==5 || encoding==6) ? 4:encoding==7 ? 8:0;
    if(!n || (width && n%((uint64_t)width*channels)) ||
       !sm_emit(f,s,"au-descriptor.bin",0,offset,&measured) ||
       !sm_emit(f,s,"audio.bin",offset,n,&measured)) return false;
    s->size=(int64_t)measured; return true;
}

void xx_audio_au_init(xx_audio_au *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_AU,"au"); } }
xx_audio_au *xx_audio_au_create(xx_io_device *d,int64_t b) { xx_audio_au *r=(xx_audio_au *)xx_mem_alloc(sizeof(*r)); if(r) xx_audio_au_init(r,d,b); return r; }
void xx_audio_au_destroy(xx_audio_au *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_audio_au_free(xx_audio_au *r) { if(r) { xx_audio_au_destroy(r); xx_mem_free(r); } }
bool xx_audio_au_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_audio_au_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
