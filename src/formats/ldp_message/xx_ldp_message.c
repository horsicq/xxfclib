/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5036.html */
#include "xxfclib/formats/ldp_message/xx_ldp_message.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=10;unsigned count=0;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=26&&b.n<=4096&&pm_be16(b.p)==1&&pm_be16(b.p+2)==b.n-4&&f16_ip4(b.p+4,false)&&!pm_be16(b.p+8)&&nh_add(f,s,&b,"ldp-pdu-header",0,10));while(at<b.n){uint64_t start=at;NH_NEED(++count<=128&&th_take(&b,&at,b.n,8)&&pm_be16(b.p+(size_t)start)==0x0100);uint64_t n=pm_be16(b.p+(size_t)start+2),end=start+4+n;NH_NEED(n>=12&&end<=b.n&&nh_add(f,s,&b,"hello-message-header",start,8));unsigned mask=0,tlvs=0;while(at<end){uint64_t p=at;NH_NEED(++tlvs<=3&&th_take(&b,&at,end,4));unsigned type=pm_be16(b.p+(size_t)p),z=pm_be16(b.p+(size_t)p+2),bit=type==0x400?1:type==0x401?2:type==0x402?4:0;NH_NEED((tlvs!=1||type==0x400)&&bit&&!(mask&bit)&&z==4&&th_take(&b,&at,end,z));mask|=bit;if(type==0x400)NH_NEED(!(pm_be16(b.p+(size_t)p+6)&0x3fff));if(type==0x401)NH_NEED(f16_ip4(b.p+(size_t)p+4,false));NH_NEED(nh_add(f,s,&b,"hello-tlv-fields",p,4)&&nh_add(f,s,&b,"hello-tlv-value",p+4,z));}NH_NEED(mask&1);}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ldp_message_init(xx_ldp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LDP_MESSAGE,"bin");}}
xx_ldp_message *xx_ldp_message_create(xx_io_device *d,int64_t b){xx_ldp_message *r=(xx_ldp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_ldp_message_init(r,d,b);return r;}
void xx_ldp_message_destroy(xx_ldp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ldp_message_free(xx_ldp_message *r){if(r){xx_ldp_message_destroy(r);xx_mem_free(r);}}
bool xx_ldp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ldp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
