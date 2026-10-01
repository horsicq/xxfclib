/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_liquid/xx_tracker_liquid.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_LIQUID
#define XX_FILE_TYPE_TRACKER_LIQUID ((xx_file_type_t)806)
#endif
static bool e8_parse(e8_blob*c) {
 unsigned channels,patterns,instruments,orders,i;size_t p;uint16_t version;
 if(!e8_range(c,0,109) || !e8_eq(c,0,"Liquid Module:",14) || c->b[64]!=26 || (version=pm_le16(c->b+85))<0x100 || version>0x102 || !pm_le16(c->b+87) || !pm_le16(c->b+89) || !(channels=pm_le16(c->b+95)) || channels>64 || (pm_le32(c->b+97)&~3U) || !(patterns=pm_le16(c->b+101)) || patterns>256 || (instruments=pm_le16(c->b+103))>256 || !(orders=pm_le16(c->b+105)) || orders>256 || (p=pm_le16(c->b+107))<109U+2U*channels+orders || !e8_range(c,0,p))return false;
 for(i=0;i<channels;++i){unsigned pan=c->b[109+i];if((pan>64 && pan!=66) || c->b[109+channels+i]>64)return false;}for(i=0;i<orders;++i)if(c->b[109+2U*channels+i]>=patterns && c->b[109+2U*channels+i]!=255)return false;
 if(!e8_add(c,"headers-orders.bin",0,p))return false;
 for(i=0;i<patterns;++i){uint32_t z;unsigned rows;if(!e8_range(c,p,44) || !e8_eq(c,p,"LP\0\0",4) || !(rows=pm_le16(c->b+p+34)) || rows>256 || (z=pm_le32(c->b+p+36))>c->n-p-44 || pm_le32(c->b+p+40) || !e8_add(c,"encoded-pattern.bin",p,44U+z))return false;p+=44U+z;}
 for(i=0;i<instruments;++i){size_t hs;uint32_t raw,loop,start;unsigned flags,align;if(e8_eq(c,p,"????",4)){if(!e8_add(c,"empty-instrument.bin",p,4))return false;p+=4;continue;}if(!e8_range(c,p,144) || !e8_eq(c,p,"LDSS",4) || pm_le16(c->b+p+4)>0x102 || (hs=pm_le16(c->b+p+99))<144 || !e8_range(c,p,hs) || pm_le16(c->b+p+101) || c->b[p+93]>64 || (flags=c->b[p+94])&~7U)return false;raw=pm_le32(c->b+p+77);start=pm_le32(c->b+p+81);loop=pm_le32(c->b+p+85);align=((flags&1) ? 2U:1U)*((flags&2) ? 2U:1U);if(start>loop || loop>raw || raw%align || start%align || loop%align || !e8_add(c,"sample-header.bin",p,hs))return false;p+=hs;if(raw && !e8_add(c,"sample.bin",p,raw))return false;p+=raw;}
 return p==c->n;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_liquid_init(xx_tracker_liquid*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_LIQUID,"bin");}}
xx_tracker_liquid*xx_tracker_liquid_create(xx_io_device*d,int64_t b) {xx_tracker_liquid*r=(xx_tracker_liquid*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_liquid_init(r,d,b);return r;}
void xx_tracker_liquid_destroy(xx_tracker_liquid*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_liquid_free(xx_tracker_liquid*r) {if(r){xx_tracker_liquid_destroy(r);xx_mem_free(r);}}
bool xx_tracker_liquid_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_liquid_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
