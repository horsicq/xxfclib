/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc894.html */
#include "xxfclib/formats/ethernet_frame/xx_ethernet_frame.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fifteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=60&&nh_span(&b,0,14));unsigned type=xx_data_get_u16(b.p+12, 2, 0, true);NH_NEED((type==0x0800&&(b.p[14]>>4)==4)||(type==0x86dd&&(b.p[14]>>4)==6));NH_NEED(!(b.p[6]&1)&&!nh_zero(&b,6,6)&&nh_add(f,s,&b,"ethernet-header",0,14)&&f15_ip_add(f,s,&b,14,b.n-14));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ethernet_frame_init(xx_ethernet_frame *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ETHERNET_FRAME,"bin");}}
xx_ethernet_frame *xx_ethernet_frame_create(xx_io_device *d,int64_t b){xx_ethernet_frame *r=(xx_ethernet_frame *)xx_mem_alloc(sizeof(*r));if(r)xx_ethernet_frame_init(r,d,b);return r;}
void xx_ethernet_frame_destroy(xx_ethernet_frame *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ethernet_frame_free(xx_ethernet_frame *r){if(r){xx_ethernet_frame_destroy(r);xx_mem_free(r);}}
bool xx_ethernet_frame_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ethernet_frame_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
