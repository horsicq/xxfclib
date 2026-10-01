/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/flic.c
 * Autodesk FLI/FLC8-bit indexed animations with complete128-byte header, frame counts and bounded palette/BRUN/LC/SS2/COPY/BLACK image chunk grammar. Original header, standard bounded opaque PREFIX/PSTAMP metadata and encoded frames, including an optional ring frame, are exported; non8-bit depths, unknown chunks and rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/autodesk_flic/xx_autodesk_flic.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) {uint8_t h[128];return ng_probe(f,n,h,sizeof(h))&&(pm_le16(h+4)==0xaf11||pm_le16(h+4)==0xaf12);}
static bool flc_tail(const uint8_t *b,uint64_t q,uint64_t end) {return q<=end&&end-q<=1&&(q==end||b[q]==0);}
static bool flc_image(const uint8_t *b,uint64_t q,uint64_t end,uint32_t type,uint32_t width,uint32_t height,xx_pd_struct *pd) {
 uint32_t rows,y=0,i,x;if(type==13)return q==end;if(type==16)return end-q==(uint64_t)width*height;if(type==18)return end-q>=12;
 if(type==4||type==11){uint32_t colors=0;if(!ng_span(q,2,end))return false;rows=pm_le16(b+q);q+=2;for(i=0;i<rows;++i){uint32_t count,j;if(!ng_span(q,2,end))return false;colors+=b[q++];count=b[q++];if(!count)count=256;if(colors>256||count>256-colors||!ng_span(q,(uint64_t)count*3,end))return false;if(type==11)for(j=0;j<count*3;++j)if(b[q+j]>63)return false;q+=(uint64_t)count*3;colors+=count;}return flc_tail(b,q,end);}
 if(type!=7&&type!=12&&type!=15)return false;
 if(type==12){if(!ng_span(q,4,end))return false;y=pm_le16(b+q);rows=pm_le16(b+q+2);q+=4;if(y>height||rows>height-y)return false;}
 else if(type==7){if(!ng_span(q,2,end))return false;rows=pm_le16(b+q);q+=2;if(rows>height)return false;}else rows=height;
 while(rows){uint32_t packets;x=0;if(ng_stop(pd)||y>=height)return false;
  if(type==7){uint32_t op;if(!ng_span(q,2,end))return false;op=pm_le16(b+q);q+=2;if((op&0xc000U)==0xc000U){uint32_t skip=(uint32_t)(-(int32_t)(int16_t)op);if(skip>height-y)return false;y+=skip;continue;}if((op&0xc000U)==0x8000U)continue;if(op&0xc000U)return false;packets=op;}
  else {if(q>=end)return false;packets=b[q++];}
  if(type==15){while(x<width){int32_t run;uint32_t count;if(q>=end)return false;run=(int8_t)b[q++];if(!run)return false;count=(uint32_t)(run<0?-run:run);if(count>width-x||!ng_span(q,run>0?1:count,end))return false;q+=run>0?1:count;x+=count;}}
  else for(i=0;i<packets;++i){int32_t run;uint32_t count,unit=type==7?2U:1U;if(!ng_span(q,2,end))return false;x+=b[q++];run=(int8_t)b[q++];count=(uint32_t)(run<0?-run:run)*unit;if(x>width||count>width-x||!ng_span(q,run<0?unit:count,end))return false;q+=run<0?unit:count;x+=count;}
  ++y;--rows;
 }
 return flc_tail(b,q,end);
}
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t frames=pm_le16(b+6),width=pm_le16(b+8),height=pm_le16(b+10),seen=0;uint64_t at=128,first=0,second=0;bool prefix=false;
 if(pm_le32(b)!=n||!frames||frames>4093||!width||width>4096||!height||height>4096||pm_le16(b+12)!=8||pm_le16(b+14)>3||!(pm_le16(b+4)==0xaf11?pm_le16(b+16):pm_le32(b+16))||!ng_emit(f,s,"flic_header.bin",0,128,n))return false;
 while(at<n){uint32_t size,type;uint64_t q,end;if(ng_stop(pd)||!ng_span(at,6,n))return false;size=pm_le32(b+at);type=pm_le16(b+at+4);if(size<6||!ng_span(at,size,n))return false;end=at+size;
  if(type==0xf100){if(prefix||seen||size<12)return false;prefix=true;}
  else if(type==0xf1fa){uint32_t count,i;if(size<16||seen>frames)return false;if(!seen)first=at;if(seen==1)second=at;++seen;count=pm_le16(b+at+6);q=at+16;
   for(i=0;i<count;++i){uint32_t len,kind;if(!ng_span(q,6,end))return false;len=pm_le32(b+q);kind=pm_le16(b+q+4);if(len<6||!ng_span(q,len,end)||!flc_image(b,q+6,q+len,kind,width,height,pd))return false;q+=len;}if(q!=end)return false;
  }else return false;
  if(!ng_emit(f,s,type==0xf100?"flic_prefix.bin":"flic_encoded_frame.bin",at,size,n))return false;at=end;
 }
 if(seen!=frames&&seen!=frames+1)return false;if(pm_le16(b+4)==0xaf12&&((pm_le32(b+80)&&pm_le32(b+80)!=first)||(pm_le32(b+84)&&pm_le32(b+84)!=second)))return false;s->size=(int64_t)n;return true;
}

void xx_autodesk_flic_init(xx_autodesk_flic *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AUTODESK_FLIC,"flc");}}
xx_autodesk_flic *xx_autodesk_flic_create(xx_io_device *d,int64_t at) {xx_autodesk_flic *r=(xx_autodesk_flic *)xx_mem_alloc(sizeof(*r));if(r)xx_autodesk_flic_init(r,d,at);return r;}
void xx_autodesk_flic_destroy(xx_autodesk_flic *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_autodesk_flic_free(xx_autodesk_flic *r) {if(r){xx_autodesk_flic_destroy(r);xx_mem_free(r);}}
bool xx_autodesk_flic_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_autodesk_flic_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
