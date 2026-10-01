/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/bink.c
 * Bink1 revisions b/f/g/h/i, complete dimensions/rate, bounded audio descriptors, increasing physical frame index and per-frame audio/video extents. Original header/index and complete encoded frames are exported; Bink2/SMUSH wrappers, unknown revisions and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/rad_bink/xx_rad_bink.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) {uint8_t h[44];return ng_probe(f,n,h,sizeof(h))&&pm_tag(h,"BIK",3)&&(h[3]=='b'||h[3]=='f'||h[3]=='g'||h[3]=='h'||h[3]=='i');}
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t frames=pm_le32(b+8),largest=pm_le32(b+12),tracks=pm_le32(b+40),i,j;uint64_t table,first,at;
 if((uint64_t)pm_le32(b+4)+8!=n||!frames||frames>4094||!largest||largest>n||pm_le32(b+16)!=frames||!pm_le32(b+20)||pm_le32(b+20)>8192||!pm_le32(b+24)||pm_le32(b+24)>8192||!pm_le32(b+28)||!pm_le32(b+32)||tracks>16)return false;
 table=44+(uint64_t)tracks*12;if(!ng_span(table,(uint64_t)frames*4,n))return false;
 for(i=0;i<tracks;++i){uint32_t rate=pm_le16(b+44+tracks*4+i*4),flags=pm_le16(b+46+tracks*4+i*4);if(!pm_le32(b+44+i*4)||!rate||(flags&~0x7000U))return false;for(j=0;j<i;++j)if(pm_le32(b+44+tracks*8+i*4)==pm_le32(b+44+tracks*8+j*4))return false;}
 first=pm_le32(b+table)&~1U;at=table+(uint64_t)frames*4;if(first==at+4){if(!ng_span(at,4,n)||pm_le32(b+at)!=n)return false;}else if(first!=at)return false;
 if(first>=n||!ng_emit(f,s,"bink_header_index.bin",0,first,n))return false;
 for(i=0;i<frames;++i){uint64_t begin=pm_le32(b+table+(uint64_t)i*4)&~1U,end=i+1<frames?(pm_le32(b+table+(uint64_t)(i+1)*4)&~1U):n,q=begin;if(ng_stop(pd)||begin<first||end<=begin||end>n||end-begin>largest)return false;
  for(j=0;j<tracks;++j){uint32_t size;if(!ng_span(q,4,end))return false;size=pm_le32(b+q);q+=4;if(!ng_span(q,size,end)||(size&&size<4))return false;if(size&&(!pm_le32(b+q)||pm_le32(b+q)>pm_le32(b+44+j*4)))return false;q+=size;}
  if(q>=end||!ng_emit(f,s,"bink_encoded_frame.bin",begin,end-begin,n))return false;
 }
 s->size=(int64_t)n;return true;
}

void xx_rad_bink_init(xx_rad_bink *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_RAD_BINK,"bik");}}
xx_rad_bink *xx_rad_bink_create(xx_io_device *d,int64_t at) {xx_rad_bink *r=(xx_rad_bink *)xx_mem_alloc(sizeof(*r));if(r)xx_rad_bink_init(r,d,at);return r;}
void xx_rad_bink_destroy(xx_rad_bink *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_rad_bink_free(xx_rad_bink *r) {if(r){xx_rad_bink_destroy(r);xx_mem_free(r);}}
bool xx_rad_bink_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_rad_bink_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
