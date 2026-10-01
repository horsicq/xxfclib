/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_digi/xx_tracker_digi.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_DIGI
#define XX_FILE_TYPE_TRACKER_DIGI ((xx_file_type_t)810)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=1572;unsigned pat,ord,chn,i;uint32_t lens[31];
 if(!e8_range(c,0,1572) || !e8_eq(c,0,"DIGI Booster module",19) || c->b[20]!='V' || (c->b[24]<0x14 || c->b[24]>0x17) || !(chn=c->b[25]) || chn>8 || c->b[26]>1 || (ord=1U+c->b[47])>128)return false;pat=1U+c->b[46];for(i=0;i<ord;++i)if(c->b[48+i]>=pat)return false;
 for(i=0;i<31;++i){uint32_t start=pm_be32(c->b+300U+4U*i),loop=pm_be32(c->b+424U+4U*i);lens[i]=pm_be32(c->b+176U+4U*i);if(c->b[548+i]>64 || start>lens[i] || loop>lens[i]-start)return false;}
 if(!e8_add(c,"headers.bin",0,p))return false;for(i=0;i<pat;++i){size_t z=256U*chn;if(c->b[26]){unsigned j,events=0;if(!e8_range(c,p,66) || (z=pm_be16(c->b+p))<64)return false;for(j=0;j<64;++j){unsigned k,v=c->b[p+2+j];if(v&((1U<<(8U-chn))-1U))return false;for(k=0;k<chn;++k)if(v&(128U>>k))++events;}if(z!=64U+4U*events)return false;z+=2;}if(!e8_add(c,"pattern.bin",p,z))return false;p+=z;}for(i=0;i<31;++i){if(lens[i] && !e8_add(c,"sample.bin",p,lens[i]))return false;p+=lens[i];}return p==c->n;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_digi_init(xx_tracker_digi*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_DIGI,"bin");}}
xx_tracker_digi*xx_tracker_digi_create(xx_io_device*d,int64_t b) {xx_tracker_digi*r=(xx_tracker_digi*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_digi_init(r,d,b);return r;}
void xx_tracker_digi_destroy(xx_tracker_digi*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_digi_free(xx_tracker_digi*r) {if(r){xx_tracker_digi_destroy(r);xx_mem_free(r);}}
bool xx_tracker_digi_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_digi_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
