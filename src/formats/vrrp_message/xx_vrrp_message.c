/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc3768.html */
#include "xxfclib/formats/vrrp_message/xx_vrrp_message.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=20&&b.p[0]==33&&b.p[1]&&b.p[3]&&!b.p[4]&&b.p[5]&&b.n==16+(uint64_t)b.p[3]*4&&nh_zero(&b,b.n-8,8)&&f15_checksum(&b,0,b.n,0)&&nh_add(f,s,&b,"vrrp-header",0,8));for(uint64_t p=8;p<b.n-8;p+=4){NH_NEED(f16_ip4(b.p+(size_t)p,false));for(uint64_t q=8;q<p;q+=4)NH_NEED(pm_be32(b.p+(size_t)p)!=pm_be32(b.p+(size_t)q));NH_NEED(nh_add(f,s,&b,"virtual-ipv4-address",p,4));}NH_NEED(nh_add(f,s,&b,"reserved-authentication",b.n-8,8));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_vrrp_message_init(xx_vrrp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_VRRP_MESSAGE,"bin");}}
xx_vrrp_message *xx_vrrp_message_create(xx_io_device *d,int64_t b){xx_vrrp_message *r=(xx_vrrp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_vrrp_message_init(r,d,b);return r;}
void xx_vrrp_message_destroy(xx_vrrp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_vrrp_message_free(xx_vrrp_message *r){if(r){xx_vrrp_message_destroy(r);xx_mem_free(r);}}
bool xx_vrrp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_vrrp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
