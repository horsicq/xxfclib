/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/westwood_vqa.c
 * Westwood FORM/WVQA v1/2 with complete VQHD/FINF frame indexes and length-framed frame/audio chunks, including IFF padding and nested encoded VQ frame chunks. Original encoded components are exported; version3/high-color/unknown extensions and rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/westwood_vqa/xx_westwood_vqa.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) {uint8_t h[20];return n>=62&&ng_probe(f,n,h,sizeof(h))&&pm_tag(h,"FORM",4)&&pm_tag(h+8,"WVQA",4)&&pm_tag(h+12,"VQHD",4)&&xx_data_get_u32(h+16, 4, 0, true)==42;}
static bool vqa_frame(const uint8_t *b,uint64_t at,uint64_t end,bool *vectors) {
 while(at<end){uint64_t q,stop;uint32_t len;bool known;if(!ng_span(at,8,end))return false;len=xx_data_get_u32(b+at+4, 4, 0, true);q=at+8;if(!ng_span(q,len,end)||!len)return false;stop=q+len;known=pm_tag(b+at,"CBF0",4)||pm_tag(b+at,"CBFZ",4)||pm_tag(b+at,"CBP0",4)||pm_tag(b+at,"CBPZ",4)||pm_tag(b+at,"CPL0",4)||pm_tag(b+at,"CPLZ",4)||pm_tag(b+at,"VPT0",4)||pm_tag(b+at,"VPTZ",4);if(!known)return false;if(pm_tag(b+at,"VPT0",4)||pm_tag(b+at,"VPTZ",4))*vectors=true;if(pm_tag(b+at,"CPL0",4)&&(len%3||len>768))return false;if(len&1U){if(stop>=end||b[stop])return false;++stop;}at=stop;
 }return at==end;
}
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t version=xx_data_get_u16(b+20, 2, 0, false),frames=xx_data_get_u16(b+24, 2, 0, false),width=xx_data_get_u16(b+26, 2, 0, false),height=xx_data_get_u16(b+28, 2, 0, false),rate=xx_data_get_u16(b+44, 2, 0, false),channels=b[46],bits=b[47],seen=0,index=0;uint64_t at=62,table,previous=0;uint32_t bw=b[30],bh=b[31];bool haveindex=false;
 if((uint64_t)xx_data_get_u32(b+4, 4, 0, true)+8!=n||(version!=1&&version!=2)||!frames||frames>2000||!width||width>4096||!height||height>4096||!bw||bw>8||!bh||bh>8||width%bw||height%bh||!b[32]||b[32]>30||!xx_data_get_u16(b+34, 2, 0, false)||xx_data_get_u16(b+34, 2, 0, false)>256||channels>2||(bits&&bits!=8&&bits!=16)||(rate&&rate<8000))return false;
 if(!ng_emit(f,s,"vqa_header.bin",0,62,n)) {return false; } table=0;
 while(at<n){uint64_t q,end,next;uint32_t len;if(ng_stop(pd)||!ng_span(at,8,n))return false;len=xx_data_get_u32(b+at+4, 4, 0, true);q=at+8;if(!ng_span(q,len,n))return false;end=q+len;next=end+(len&1U);if(next>n||(len&1U&&b[end]))return false;
  if(pm_tag(b+at,"FINF",4)){uint32_t i;if(haveindex||seen||len!=(uint64_t)frames*4)return false;table=q;haveindex=true;for(i=0;i<frames;++i){uint64_t offset=(uint64_t)(xx_data_get_u32(b+q+(uint64_t)i*4, 4, 0, false)&0x3fffffffU)*2;if(offset<next||offset>=n||(i&&offset<=previous))return false;previous=offset;}}
  else {if(!haveindex)return false;if(index<frames){uint64_t target=(uint64_t)(xx_data_get_u32(b+table+(uint64_t)index*4, 4, 0, false)&0x3fffffffU)*2;if(target<at)return false;if(target==at)++index;}
   if(pm_tag(b+at,"VQFR",4)||pm_tag(b+at,"VQFL",4)){bool vectors=false;if(!vqa_frame(b,q,end,&vectors))return false;if(pm_tag(b+at,"VQFR",4)){if(!vectors||++seen>frames||index<seen)return false;}}
   else if(pm_tag(b+at,"SND0",4)||pm_tag(b+at,"SND1",4)||pm_tag(b+at,"SND2",4)){if(!len||!rate||!channels||!bits)return false;if(pm_tag(b+at,"SND0",4)&&len%(channels*(bits/8)))return false;if(pm_tag(b+at,"SND1",4)&&(len<4||!xx_data_get_u16(b+q, 2, 0, false)||(uint32_t)xx_data_get_u16(b+q+2, 2, 0, false)+4!=len))return false;}
   else return false;
  }
  if(!ng_emit(f,s,"vqa_encoded_chunk.bin",at,next-at,n)) {return false; } at=next;
 }
 if(!haveindex||seen!=frames||index!=frames) {return false; } s->size=(int64_t)n;return true;
}

void xx_westwood_vqa_init(xx_westwood_vqa *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_WESTWOOD_VQA,"vqa");}}
xx_westwood_vqa *xx_westwood_vqa_create(xx_io_device *d,int64_t at) {xx_westwood_vqa *r=(xx_westwood_vqa *)xx_mem_alloc(sizeof(*r));if(r)xx_westwood_vqa_init(r,d,at);return r;}
void xx_westwood_vqa_destroy(xx_westwood_vqa *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_westwood_vqa_free(xx_westwood_vqa *r) {if(r){xx_westwood_vqa_destroy(r);xx_mem_free(r);}}
bool xx_westwood_vqa_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_westwood_vqa_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
