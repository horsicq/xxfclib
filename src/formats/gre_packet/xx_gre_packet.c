/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2784.html */
#include "xxfclib/formats/gre_packet/xx_gre_packet.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=4;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=32);unsigned flags=pm_be16(b.p),type=pm_be16(b.p+2);NH_NEED(!(flags&~0xb000U)&&(type==0x0800||type==0x86dd));if(flags&0x8000){NH_NEED(th_take(&b,&at,b.n,4)&&!pm_be16(b.p+6)&&f15_checksum(&b,0,b.n,0));}if(flags&0x2000)NH_NEED(th_take(&b,&at,b.n,4));if(flags&0x1000)NH_NEED(th_take(&b,&at,b.n,4));NH_NEED((unsigned)(b.p[(size_t)at]>>4)==(type==0x0800?4U:6U)&&nh_add(f,s,&b,"gre-header",0,at)&&f15_ip_add(f,s,&b,at,b.n-at));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_gre_packet_init(xx_gre_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_GRE_PACKET,"bin");}}
xx_gre_packet *xx_gre_packet_create(xx_io_device *d,int64_t b){xx_gre_packet *r=(xx_gre_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_gre_packet_init(r,d,b);return r;}
void xx_gre_packet_destroy(xx_gre_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gre_packet_free(xx_gre_packet *r){if(r){xx_gre_packet_destroy(r);xx_mem_free(r);}}
bool xx_gre_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_gre_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
