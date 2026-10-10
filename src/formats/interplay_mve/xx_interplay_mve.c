/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/ipmovie.c
 * Interplay MVE standard26-byte preamble, typed chunk/opcode framing, bounded video/audio/palette initialization and complete shutdown/end chunks. Original encoded chunks are exported; video/audio codec commands and documented opaque opcodes18-21 remain encoded, and unknown opcode/version variants are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/interplay_mve/xx_interplay_mve.h"
#include "../common/xx_audiovisual_components.h"
static const uint8_t mve_magic[]={0x49,0x6e,0x74,0x65,0x72,0x70,0x6c,0x61,0x79,0x20,0x4d,0x56,0x45,0x20,0x46,0x69,0x6c,0x65,0x1a,0x00,0x1a,0x00,0x00,0x01,0x33,0x11};
static bool audiovisual_quick(Abstractformat *f,uint64_t n) {uint8_t h[26];return audiovisual_probe(f,n,h,sizeof(h))&&pm_tag(h,(const char *)mve_magic,26);}
static bool audiovisual_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t at=26;uint32_t width=0,height=0,audio=0,channels=0,bps=0,frames=0;bool shutdown=false,eos=false,ended=false;
 if(!audiovisual_emit(f,s,"mve_header.bin",0,26,n))return false;
 while(at<n){uint64_t q,end;uint32_t size,type;bool last=false;if(audiovisual_stop(pd)||!audiovisual_span(at,4,n)||ended)return false;size=xx_data_get_u16(b+at, 2, 0, false);type=xx_data_get_u16(b+at+2, 2, 0, false);q=at+4;if(type>5||!audiovisual_span(q,size,n))return false;end=q+size;if(shutdown&&type!=5)return false;
  while(q<end){uint32_t len,op,v;uint64_t p;if(!audiovisual_span(q,4,end)||last)return false;len=xx_data_get_u16(b+q, 2, 0, false);op=b[q+2];v=b[q+3];p=q+4;if(!audiovisual_span(p,len,end)||op>21)return false;
   switch(op){
    case 0:if(v||len||type!=4)return false;eos=true;break;
    case 1:if(v||len)return false;last=true;break;
    case 2:if(v||len!=6||!xx_data_get_u32(b+p, 4, 0, false)||!xx_data_get_u16(b+p+4, 2, 0, false))return false;break;
    case 3:if(v>1||len<6||len>10||!xx_data_get_u16(b+p+4, 2, 0, false))return false;audio=1;channels=(xx_data_get_u16(b+p+2, 2, 0, false)&1U)+1;bps=((xx_data_get_u16(b+p+2, 2, 0, false)>>1)&1U)+1;if(v==1&&(xx_data_get_u16(b+p+2, 2, 0, false)&4U))audio=2;break;
    case 4:if(v||len)return false;break;
    case 5:if(v>2||len!=4+v*2U||!xx_data_get_u16(b+p, 2, 0, false)||!xx_data_get_u16(b+p+2, 2, 0, false))return false;width=(uint32_t)xx_data_get_u16(b+p, 2, 0, false)*8;height=(uint32_t)xx_data_get_u16(b+p+2, 2, 0, false)*8;if(width>8192||height>8192||((v==2)&&xx_data_get_u16(b+p+6, 2, 0, false)>1))return false;break;
    case 6:case 16:case 17:if(v>3||len<14||!width||!height)return false;++frames;break;
    case 7:if(v>1||len!=6||!width)return false;break;
    case 8:if(v||!audio||len<6||!xx_data_get_u16(b+p+2, 2, 0, false)||!xx_data_get_u16(b+p+4, 2, 0, false))return false;if(audio==1&&len-6!=xx_data_get_u16(b+p+4, 2, 0, false))return false;if(audio==2&&len<6+channels)return false;if(xx_data_get_u16(b+p+4, 2, 0, false)%(channels*bps))return false;break;
    case 9:if(v||!audio||len!=6||!xx_data_get_u16(b+p+2, 2, 0, false)||!xx_data_get_u16(b+p+4, 2, 0, false))return false;break;
    case 10:if(v||len!=6||!xx_data_get_u16(b+p, 2, 0, false)||!xx_data_get_u16(b+p+2, 2, 0, false))return false;break;
    case 11:if(v||len<6)return false;break;
    case 12:{uint32_t start,count,i;if(v||len<4)return false;start=xx_data_get_u16(b+p, 2, 0, false);count=xx_data_get_u16(b+p+2, 2, 0, false);if(!count||start>256||count>256-start||len!=4+count*3)return false;for(i=4;i<len;++i)if(b[p+i]>63)return false;break;}
    case 13:if(v||!len)return false;break;
    case 14:case 15:if(v||!len||!width||len>(uint64_t)width*height)return false;break;
    case 18:case 19:case 20:case 21:if(v||len>4096)return false;break;
    default:return false;
   }q=p+len;
  }
  if(type==4){if(!eos||!last)return false;shutdown=true;}if(type==5){if(!shutdown||size)return false;ended=true;}
  if(!audiovisual_emit(f,s,"mve_encoded_chunk.bin",at,end-at,n)) {return false; } at=end;
 }
 if(!frames||!ended) {return false; } s->size=(int64_t)n;return true;
}

void xx_interplay_mve_init(xx_interplay_mve *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_INTERPLAY_MVE,"mve");}}
xx_interplay_mve *xx_interplay_mve_create(xx_io_device *d,int64_t at) {xx_interplay_mve *r=(xx_interplay_mve *)xx_mem_alloc(sizeof(*r));if(r)xx_interplay_mve_init(r,d,at);return r;}
void xx_interplay_mve_destroy(xx_interplay_mve *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_interplay_mve_free(xx_interplay_mve *r) {if(r){xx_interplay_mve_destroy(r);xx_mem_free(r);}}
bool xx_interplay_mve_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_interplay_mve_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
