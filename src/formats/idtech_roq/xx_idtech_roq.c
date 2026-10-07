/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/idroqdec.c
 * RoQ video with complete info, VQ codebook/video and optional mono/stereo DPCM chunk extents. Geometry, codebook counts and VQ command references are bounded. Original coded chunks are exported; packet nesting, unknown chunks and video/audio rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/idtech_roq/xx_idtech_roq.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
typedef struct roq_scan {const uint8_t *b,*cells,*quads;uint64_t p,end;unsigned flags,left;bool first;} roq_scan;
static bool roq_block(roq_scan *q,unsigned size) {
 unsigned code,j;if(!q->left){if(!ng_span(q->p,2,q->end))return false;q->flags=xx_data_get_u16(q->b+q->p, 2, 0, false);q->p+=2;q->left=8;}code=(q->flags>>((--q->left)*2))&3;
 if(code==0)return !q->first;
 if(code==1){if(q->first||q->p>=q->end)return false;++q->p;return true;}
 if(code==2){if(q->p>=q->end||!q->quads[q->b[q->p++]])return false;return true;}
 if(size==8){for(j=0;j<4;++j)if(!roq_block(q,4))return false;return true;}
 for(j=0;j<4;++j) {if(q->p>=q->end||!q->cells[q->b[q->p++]])return false; } return true;
}
static bool ng_quick(Abstractformat *f,uint64_t n) { uint8_t h[8];return ng_probe(f,n,h,8)&&xx_data_get_u16(h, 2, 0, false)==0x1084&&xx_data_get_u32(h+2, 4, 0, false)==0xffffffffU&&xx_data_get_u16(h+6, 2, 0, false)>0&&xx_data_get_u16(h+6, 2, 0, false)<=120; }
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t at=8;uint8_t cells[256],quads[256];unsigned width=0,height=0,frames=0,ch=0,chunks=0;char label[40];xx_mem_zero(cells,sizeof(cells));xx_mem_zero(quads,sizeof(quads));
 if(n<24||xx_data_get_u16(b, 2, 0, false)!=0x1084||xx_data_get_u32(b+2, 4, 0, false)!=0xffffffffU||!xx_data_get_u16(b+6, 2, 0, false)||xx_data_get_u16(b+6, 2, 0, false)>120||!ng_emit(f,s,"header.bin",0,8,n))return false;
 while(at<n){uint32_t size,type,arg;uint64_t end;unsigned i,j;if(ng_stop(pd)||!ng_span(at,8,n))return false;type=xx_data_get_u16(b+at, 2, 0, false);size=xx_data_get_u32(b+at+2, 4, 0, false);arg=xx_data_get_u16(b+at+6, 2, 0, false);end=at+8+size;if(!ng_span(at+8,size,n))return false;
  if(type==0x1001){if(width||size!=8||arg||!(width=xx_data_get_u16(b+at+8, 2, 0, false))||width>4096||width%16||!(height=xx_data_get_u16(b+at+10, 2, 0, false))||height>4096||height%16||(uint64_t)width*height>16777216||xx_data_get_u16(b+at+12, 2, 0, false)!=8||xx_data_get_u16(b+at+14, 2, 0, false)!=4)return false;}
  else if(type==0x1002){unsigned nc=arg>>8,nq=arg&255;uint64_t p=at+8;if(!width)return false;if(!nc)nc=256;if(!nq&&size>nc*6)nq=256;if(size!=nc*6+nq*4)return false;for(i=0;i<nc;++i)cells[i]=1;p+=nc*6;for(i=0;i<nq;++i){for(j=0;j<4;++j)if(!cells[b[p++]])return false;quads[i]=1;}}
  else if(type==0x1011){roq_scan q;uint32_t blocks;if(!width||!size)return false;xx_mem_zero(&q,sizeof(q));q.b=b;q.p=at+8;q.end=end;q.cells=cells;q.quads=quads;q.first=frames==0;blocks=(width/16)*(height/16)*4;for(i=0;i<blocks;++i)if(ng_stop(pd)||!roq_block(&q,8))return false;if(q.p!=end)return false;++frames;}
  else if(type==0x1020||type==0x1021){unsigned channels=type==0x1021?2:1;if(!size||size%channels||(ch&&ch!=channels))return false;ch=channels;}
  else return false;
  xx_rt_snprintf(label,sizeof(label),"chunk-%u-%04x.roq",chunks++,type);if(!ng_emit(f,s,label,at,end-at,n))return false;at=end;
 }s->size=(int64_t)at;return frames>0;
}

void xx_idtech_roq_init(xx_idtech_roq *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_IDTECH_ROQ,"roq");}}
xx_idtech_roq *xx_idtech_roq_create(xx_io_device *d,int64_t at) {xx_idtech_roq *r=(xx_idtech_roq *)xx_mem_alloc(sizeof(*r));if(r)xx_idtech_roq_init(r,d,at);return r;}
void xx_idtech_roq_destroy(xx_idtech_roq *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_idtech_roq_free(xx_idtech_roq *r) {if(r){xx_idtech_roq_destroy(r);xx_mem_free(r);}}
bool xx_idtech_roq_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_idtech_roq_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
