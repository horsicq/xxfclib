/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2661.html */
#include "xxfclib/formats/l2tp_packet/xx_l2tp_packet.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=12;unsigned count=0;uint32_t mask=0;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=52&&b.n<=4096&&pm_be16(b.p)==0xc802&&pm_be16(b.p+2)==b.n&&!pm_be16(b.p+4)&&!pm_be16(b.p+6)&&nh_add(f,s,&b,"l2tp-control-header",0,12));while(at<b.n){uint64_t start=at;NH_NEED(++count<=10&&th_take(&b,&at,b.n,6));unsigned head=pm_be16(b.p+(size_t)start),n=head&1023,type=pm_be16(b.p+(size_t)start+4);NH_NEED(!(head&0x7c00)&&n>=6&&n<=261&&!pm_be16(b.p+(size_t)start+2)&&type<=10&&type!=1&&type!=5&&!(mask&(1U<<type)));mask|=1U<<type;NH_NEED(th_take(&b,&at,b.n,n-6));uint64_t p=start+6,z=n-6;if(type==0){NH_NEED(count==1&&(head&0x8000)&&z==2&&pm_be16(b.p+(size_t)p)==1);}else if(type==2){NH_NEED(z==2&&pm_be16(b.p+(size_t)p)==0x0100&&(head&0x8000));}else if(type==3||type==4){NH_NEED(z==4&&(pm_be32(b.p+(size_t)p)&~3U)==0);if(type==3)NH_NEED(head&0x8000);}else if(type==6){NH_NEED(z==2);}else if(type==7||type==8){NH_NEED(f15_ascii(&b,p,z,true));if(type==7)NH_NEED(head&0x8000);}else if(type==9||type==10){NH_NEED(z==2&&pm_be16(b.p+(size_t)p)>0);if(type==9)NH_NEED(head&0x8000);}else goto done;NH_NEED(nh_add(f,s,&b,"avp-fields",start,6)&&nh_add(f,s,&b,"avp-value",p,z));}NH_NEED((mask&0x28d)==0x28d);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_l2tp_packet_init(xx_l2tp_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_L2TP_PACKET,"bin");}}
xx_l2tp_packet *xx_l2tp_packet_create(xx_io_device *d,int64_t b){xx_l2tp_packet *r=(xx_l2tp_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_l2tp_packet_init(r,d,b);return r;}
void xx_l2tp_packet_destroy(xx_l2tp_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_l2tp_packet_free(xx_l2tp_packet *r){if(r){xx_l2tp_packet_destroy(r);xx_mem_free(r);}}
bool xx_l2tp_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_l2tp_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
