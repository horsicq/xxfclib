/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_mt2/xx_tracker_mt2.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_MT2
#define XX_FILE_TYPE_TRACKER_MT2 ((xx_file_type_t)812)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=388;unsigned patterns,orders,chn,i;uint16_t version;
 if(!e8_range(c,0,388) || !e8_eq(c,0,"MT20",4) || (version=pm_le16(c->b+8))<0x200 || version>0x201 || !(orders=pm_le16(c->b+106)) || orders>256 || pm_le16(c->b+108)>=orders || !(patterns=pm_le16(c->b+110)) || patterns>256 || !(chn=pm_le16(c->b+112)) || chn>64 || pm_le32(c->b+118) || pm_le16(c->b+122) || pm_le16(c->b+124) || pm_le16(c->b+382) || pm_le32(c->b+384))return false;
 for(i=0;i<orders;++i) {if(c->b[126+i]>=patterns)return false; } if(!e8_add(c,"headers-orders.bin",0,p))return false;
 for(i=0;i<patterns;++i){uint32_t z;unsigned rows;size_t n;if(!e8_range(c,p,6) || !(rows=pm_le16(c->b+p)) || rows>1024 || (z=pm_le32(c->b+p+2))!=(uint32_t)(rows*chn*7U))return false;n=(z+1U)&~1U;if(!e8_add(c,"pattern.bin",p,6U+n))return false;if(n>z && c->b[p+6+z])return false;p+=6U+n;}
 {size_t start=p;for(i=0;i<511;++i){if(!e8_range(c,p,36) || pm_le32(c->b+p+32))return false;p+=36;}if(!e8_add(c,"empty-instrument-sample-slots.bin",start,p-start))return false;}
 return p==c->n;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_mt2_init(xx_tracker_mt2*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MT2,"bin");}}
xx_tracker_mt2*xx_tracker_mt2_create(xx_io_device*d,int64_t b) {xx_tracker_mt2*r=(xx_tracker_mt2*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_mt2_init(r,d,b);return r;}
void xx_tracker_mt2_destroy(xx_tracker_mt2*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_mt2_free(xx_tracker_mt2*r) {if(r){xx_tracker_mt2_destroy(r);xx_mem_free(r);}}
bool xx_tracker_mt2_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_mt2_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
