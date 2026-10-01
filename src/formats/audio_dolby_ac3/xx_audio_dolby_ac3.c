/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/ac3_parser.c
 * Classic AC3 bsid8 raw streams with stable sample rate/channel mode and complete BSI optional fields. Exact declared frame sizes and both independent CRC16 regions are checked. Original complete encoded frames are exported; EAC3, other bitstream IDs, byte-swapped streams and audio decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/audio_dolby_ac3/xx_audio_dolby_ac3.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) { uint8_t h[7];return ng_probe(f,n,h,7)&&h[0]==0x0b&&h[1]==0x77&&(h[5]>>3)==8&&(h[4]>>6)<3&&(h[4]&63)<38; }
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 static const uint16_t rates[19]={32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640};
 static const uint16_t words441[19]={69,87,104,121,139,174,208,243,278,348,417,487,557,696,835,975,1114,1253,1393};
 uint64_t at=0;unsigned frames=0,sr=99,mode=99;char label[40];
 while(at<n){uint32_t v,acmod,lfe,code,rate,words,bytes,split,i;ng_bits q;
  if(ng_stop(pd)||!ng_span(at,7,n)||b[at]!=0x0b||b[at+1]!=0x77||(b[at+5]>>3)!=8||(rate=b[at+4]>>6)>=3||(code=b[at+4]&63)>=38)return false;
  words=rate==0?rates[code/2]*2U:rate==2?rates[code/2]*3U:words441[code/2]+(code&1);bytes=words*2;split=((bytes>>2)+(bytes>>4))*2;
  if(!ng_span(at,bytes,n)||ng_crc16(b+at+2,split-2)||ng_crc16(b+at+split,bytes-split))return false;
  q.b=b+at;q.bit=48;q.end=(uint64_t)split*8;
  if(!ng_bits_get(&q,3,&acmod))return false;
  if((acmod&1)&&acmod!=1&&!ng_bits_skip(&q,2))return false;
  if((acmod&4)&&!ng_bits_skip(&q,2))return false;
  if(acmod==2&&!ng_bits_skip(&q,2))return false;
  if(!ng_bits_get(&q,1,&lfe))return false;
  if(sr==99){sr=rate;mode=acmod|(lfe<<3);}else if(sr!=rate||mode!=(acmod|(lfe<<3)))return false;
  for(i=0;i<(acmod?1U:2U);++i){if(!ng_bits_skip(&q,5)||!ng_bits_get(&q,1,&v)||(v&&!ng_bits_skip(&q,8))||!ng_bits_get(&q,1,&v)||(v&&!ng_bits_skip(&q,8))||!ng_bits_get(&q,1,&v)||(v&&!ng_bits_skip(&q,7)))return false;}
  if(!ng_bits_skip(&q,2)||!ng_bits_get(&q,1,&v)||(v&&!ng_bits_skip(&q,14))||!ng_bits_get(&q,1,&v)||(v&&!ng_bits_skip(&q,14))||!ng_bits_get(&q,1,&v))return false;
  if(v){if(!ng_bits_get(&q,6,&v)||!ng_bits_skip(&q,((uint64_t)v+1)*8))return false;}
  xx_rt_snprintf(label,sizeof(label),"frame-%u.ac3",frames++);if(!ng_emit(f,s,label,at,bytes,n))return false;at+=bytes;
 }
 s->size=(int64_t)at;return frames>0;
}

void xx_audio_dolby_ac3_init(xx_audio_dolby_ac3 *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AUDIO_DOLBY_AC3,"ac3");}}
xx_audio_dolby_ac3 *xx_audio_dolby_ac3_create(xx_io_device *d,int64_t at) {xx_audio_dolby_ac3 *r=(xx_audio_dolby_ac3 *)xx_mem_alloc(sizeof(*r));if(r)xx_audio_dolby_ac3_init(r,d,at);return r;}
void xx_audio_dolby_ac3_destroy(xx_audio_dolby_ac3 *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_dolby_ac3_free(xx_audio_dolby_ac3 *r) {if(r){xx_audio_dolby_ac3_destroy(r);xx_mem_free(r);}}
bool xx_audio_dolby_ac3_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_audio_dolby_ac3_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
