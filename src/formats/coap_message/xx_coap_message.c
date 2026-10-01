/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc7252.html */
#include "xxfclib/formats/coap_message/xx_coap_message.h"
#include "../xx_thirteenth_wrappers.h"

static bool optnum(nh_blob *b,uint64_t *at,unsigned nib,uint64_t *value){if(nib<13){*value=nib;return true;}if(nib==13){if(!th_take(b,at,b->n,1))return false;*value=13+b->p[(size_t)*at-1];return true;}if(nib==14){if(!th_take(b,at,b->n,2))return false;*value=269+pm_be16(b->p+(size_t)*at-2);return true;}return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at,n,number=0,start,delta;unsigned opts=0;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=8 && (b.p[0]>>6)==1 && (b.p[0]&15)<=8);unsigned token=b.p[0]&15,code=b.p[1],type=(b.p[0]>>4)&3;NH_NEED((code>=1&&code<=4)||((code>>5)>=2&&(code>>5)<=5&&(code&31)<=31));NH_NEED(type!=3&&(code>4||type<=1));at=4;NH_NEED(th_take(&b,&at,b.n,token)&&nh_add(f,s,&b,"coap-header-token",0,at));while(at<b.n&&b.p[(size_t)at]!=255){start=at;NH_NEED(++opts<=256&&th_take(&b,&at,b.n,1));uint8_t h=b.p[(size_t)start];NH_NEED(optnum(&b,&at,h>>4,&delta)&&optnum(&b,&at,h&15,&n)&&number+delta<=65535);number+=delta;uint64_t value=at;NH_NEED(th_take(&b,&at,b.n,n));
 switch(number){case 1:NH_NEED(n<=8);break;case 3:case 8:case 11:case 15:case 20:case 35:case 39:NH_NEED(n<=1034&&ec_utf(&b,value,n));break;case 4:NH_NEED(n>=1&&n<=8);break;case 5:NH_NEED(!n);break;case 6:NH_NEED(n<=3);break;case 7:case 12:case 17:NH_NEED(n<=2);break;case 14:case 28:case 60:NH_NEED(n<=4);break;case 23:case 27:NH_NEED(n<=3&&(!n||(b.p[(size_t)at-1]&7)!=7));break;default:NH_NEED(false);}
 NH_NEED(nh_add(f,s,&b,"option",start,at-start));}NH_NEED(opts>0);if(at<b.n){NH_NEED(th_take(&b,&at,b.n,1)&&at<b.n&&nh_add(f,s,&b,"payload",at,b.n-at));at=b.n;}NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_coap_message_init(xx_coap_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_COAP_MESSAGE,"bin");}}
xx_coap_message *xx_coap_message_create(xx_io_device *d,int64_t b) {xx_coap_message *r=(xx_coap_message *)xx_mem_alloc(sizeof(*r));if(r)xx_coap_message_init(r,d,b);return r;}
void xx_coap_message_destroy(xx_coap_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_coap_message_free(xx_coap_message *r) {if(r){xx_coap_message_destroy(r);xx_mem_free(r);}}
bool xx_coap_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_coap_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
