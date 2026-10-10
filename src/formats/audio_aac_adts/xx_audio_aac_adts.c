/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/adts_header.c
 * MPEG4 AAC-LC ADTS, unprotected seven-byte headers and one raw-data block per frame, stable standard sample rate/channel configuration1-7. Complete declared frame extents are checked; original ADTS frames are exported. MPEG2, CRC-protected/multiple-block frames, explicit program configurations, ID3 wrappers and AAC element decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/audio_aac_adts/xx_audio_aac_adts.h"
#include "../common/xx_audiovisual_components.h"
static bool audiovisual_quick(Abstractformat *f,uint64_t n) { uint8_t h[7];return audiovisual_probe(f,n,h,7)&&h[0]==255&&h[1]==0xf1&&(h[2]>>6)==1&&((h[2]>>2)&15)<13; }
static bool audiovisual_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t at=0;unsigned frames=0,rate=99,channels=99;char label[40];
 while(at<n){uint32_t size,r,c;if(audiovisual_stop(pd)||!audiovisual_span(at,7,n)||b[at]!=255||b[at+1]!=0xf1||(b[at+2]>>6)!=1||(r=(b[at+2]>>2)&15)>=13||(c=((b[at+2]&1)<<2)|(b[at+3]>>6))<1||c>7||(b[at+6]&3))return false;
  size=((uint32_t)(b[at+3]&3)<<11)|((uint32_t)b[at+4]<<3)|(b[at+5]>>5);if(size<=7||!audiovisual_span(at,size,n))return false;
  if(rate==99){rate=r;channels=c;}else if(rate!=r||channels!=c)return false;
  xx_rt_snprintf(label,sizeof(label),"frame-%u.aac",frames++);if(!audiovisual_emit(f,s,label,at,size,n))return false;at+=size;
 }
 s->size=(int64_t)at;return frames>0;
}

void xx_audio_aac_adts_init(xx_audio_aac_adts *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AUDIO_AAC_ADTS,"aac");}}
xx_audio_aac_adts *xx_audio_aac_adts_create(xx_io_device *d,int64_t at) {xx_audio_aac_adts *r=(xx_audio_aac_adts *)xx_mem_alloc(sizeof(*r));if(r)xx_audio_aac_adts_init(r,d,at);return r;}
void xx_audio_aac_adts_destroy(xx_audio_aac_adts *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_aac_adts_free(xx_audio_aac_adts *r) {if(r){xx_audio_aac_adts_destroy(r);xx_mem_free(r);}}
bool xx_audio_aac_adts_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_audio_aac_adts_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
