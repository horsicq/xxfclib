/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/aliaspixdec.c
 * Alias/Wavefront PIX8/24-bit positive geometry and complete per-row RLE packets with exact physical EOF. Original encoded scanlines exported; no pixel/color conversion. Signatureless offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/alias_pix/xx_alias_pix.h"
#include "../wavefront_obj/xx_eleventh_media.h"
#include "xxfclib/data/xx_data.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[10];return n>=14&&pm_read(f,0,b,10)&&(xx_data_get_u16(b+8, 2, 0, true)==8||xx_data_get_u16(b+8, 2, 0, true)==24);}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t w=xx_data_get_u16(b, 2, 0, true),h=xx_data_get_u16(b+2, 2, 0, true),bits=xx_data_get_u16(b+8, 2, 0, true),y;uint64_t p=10;char label[64];
 if(!w||!h||w>16384||h>2048||(uint64_t)w*h>16777216||(bits!=8&&bits!=24)||!eg_emit(f,s,"descriptor.pix",0,10,n))return false;
 for(y=0;y<h;++y){uint64_t start=p;uint32_t x=0;
  while(x<w){uint32_t z;if(eg_stop(pd)||!eg_span(p,1+bits/8,n)||(z=b[p])==0||z>w-x)return false;p+=1+bits/8;x+=z;}
  xx_rt_snprintf(label,sizeof(label),"scanline-%u.pix",y);if(!eg_emit(f,s,label,start,p-start,n))return false;
 }
 if(p!=n) {return false; } s->size=(int64_t)n;return true;
}

void xx_alias_pix_init(xx_alias_pix *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ALIAS_PIX,"pix");}}
xx_alias_pix *xx_alias_pix_create(xx_io_device *d,int64_t at) {xx_alias_pix *r=(xx_alias_pix *)xx_mem_alloc(sizeof(*r));if(r)xx_alias_pix_init(r,d,at);return r;}
void xx_alias_pix_destroy(xx_alias_pix *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_alias_pix_free(xx_alias_pix *r) {if(r){xx_alias_pix_destroy(r);xx_mem_free(r);}}
bool xx_alias_pix_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_alias_pix_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
