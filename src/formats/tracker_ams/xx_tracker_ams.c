/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_ams/xx_tracker_ams.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_AMS
#define XX_FILE_TYPE_TRACKER_AMS ((xx_file_type_t)809)
#endif
static bool e8_parse(e8_blob*c) {
 unsigned samples,patterns,orders,chn,i;size_t p,headers;uint32_t lens[255];
 if(!e8_range(c,0,18) || !e8_eq(c,0,"Extreme",7) || c->b[8]!=1 || c->b[7]>2 || c->b[9]&0xe0 || (samples=c->b[10])>255 || !(patterns=pm_le16(c->b+11)) || patterns>1024 || !(orders=pm_le16(c->b+13)) || orders>256)return false;chn=(c->b[9]&31U)+1U;p=18U+pm_le16(c->b+16);
 for(i=0;i<samples;++i){uint8_t flags;if(!e8_range(c,p,17))return false;flags=c->b[p+16];lens[i]=pm_le32(c->b+p);if(flags&~0x84U || c->b[p+15]>127 || pm_le32(c->b+p+4)>pm_le32(c->b+p+8) || pm_le32(c->b+p+8)>lens[i] || ((flags&0x84) && lens[i]>E8_LIMIT/2U))return false;if(flags&0x84)lens[i]*=2U;p+=17;}
 if(!e8_strings(c,&p,1U+samples+chn+patterns) || !e8_range(c,p,2))return false;{unsigned z=pm_le16(c->b+p);p+=2;if(!e8_range(c,p,z))return false;p+=z;}if(!e8_range(c,p,2U*orders))return false;for(i=0;i<orders;++i)if(pm_le16(c->b+p+2U*i)>=patterns)return false;p+=2U*orders;headers=p;if(!e8_add(c,"headers-text-orders.bin",0,headers))return false;
 for(i=0;i<patterns;++i){uint32_t z;if(!e8_range(c,p,4) || !(z=pm_le32(c->b+p)) || !e8_range(c,p+4,z))return false;if(!e8_add(c,"encoded-pattern.bin",p,4U+z))return false;p+=4U+z;}
 for(i=0;i<samples;++i){if(!e8_range(c,p,lens[i]))return false;if(lens[i] && !e8_add(c,"sample.bin",p,lens[i]))return false;p+=lens[i];}return p==c->n;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_ams_init(xx_tracker_ams*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_AMS,"bin");}}
xx_tracker_ams*xx_tracker_ams_create(xx_io_device*d,int64_t b) {xx_tracker_ams*r=(xx_tracker_ams*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_ams_init(r,d,b);return r;}
void xx_tracker_ams_destroy(xx_tracker_ams*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_ams_free(xx_tracker_ams*r) {if(r){xx_tracker_ams_destroy(r);xx_mem_free(r);}}
bool xx_tracker_ams_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_ams_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
