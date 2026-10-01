/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc8489.html */
#include "xxfclib/formats/stun_message/xx_stun_message.h"
#include "../xx_thirteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=20,n,start;unsigned attrs=0;bool fingerprint=false,integrity=false,ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=20 && !(b.p[0]&192) && pm_be32(b.p+4)==0x2112a442 && pm_be16(b.p+2)==b.n-20 && !((b.n-20)&3));uint16_t type=pm_be16(b.p);NH_NEED(type==1||type==0x101||type==0x111||type==0x11);NH_NEED(nh_add(f,s,&b,"stun-header",0,20));while(at<b.n){start=at;NH_NEED(++attrs<=256&&!fingerprint&&th_take(&b,&at,b.n,4));type=pm_be16(b.p+(size_t)start);n=pm_be16(b.p+(size_t)start+2);uint64_t value=at;NH_NEED(th_take(&b,&at,b.n,n));
 if(type==6||type==0x8022||type==0x14||type==0x15){NH_NEED(!integrity&&n&&n<=763&&ec_utf(&b,value,n));}else if(type==8){NH_NEED(!integrity&&n==20);integrity=true;}else if(type==0x8028){NH_NEED(n==4&&at==b.n&&ec_crc(&b,0,start,pm_be32(b.p+(size_t)value)^0x5354554eU,false));fingerprint=true;}else if(type==1||type==0x20||type==0x8023){NH_NEED(!integrity&&(n==8||n==20)&&!b.p[(size_t)value]&&b.p[(size_t)value+1]==(n==8?1:2));}else if(type==9){NH_NEED(!integrity&&n>=4&&!b.p[(size_t)value]&&!b.p[(size_t)value+1]&&b.p[(size_t)value+2]>=3&&b.p[(size_t)value+2]<=6&&b.p[(size_t)value+3]<=99&&ec_utf(&b,value+4,n-4));}else if(type==0xa){NH_NEED(!integrity&&n&&!(n&1));}else NH_NEED(false);
 NH_NEED(th_take(&b,&at,b.n,(4-(n&3))&3)&&nh_add(f,s,&b,"attribute",start,at-start));}NH_NEED(attrs>0);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_stun_message_init(xx_stun_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_STUN_MESSAGE,"bin");}}
xx_stun_message *xx_stun_message_create(xx_io_device *d,int64_t b) {xx_stun_message *r=(xx_stun_message *)xx_mem_alloc(sizeof(*r));if(r)xx_stun_message_init(r,d,b);return r;}
void xx_stun_message_destroy(xx_stun_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_stun_message_free(xx_stun_message *r) {if(r){xx_stun_message_destroy(r);xx_mem_free(r);}}
bool xx_stun_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_stun_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
