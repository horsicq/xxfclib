/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc4271.html */
#include "xxfclib/formats/bgp_messages/xx_bgp_messages.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fourteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=0;unsigned count=0;bool ok=false;if(!nh_load(f,&b,pd))return false;while(at<b.n){uint64_t start=at;NH_NEED(++count<=2&&th_take(&b,&at,b.n,19));for(unsigned i=0;i<16;++i)NH_NEED(b.p[(size_t)start+i]==255);uint64_t n=xx_data_get_u16(b.p+(size_t)start+16, 2, 0, true);unsigned type=b.p[(size_t)start+18];NH_NEED(n>=19&&n<=4096&&nh_span(&b,start,n));uint64_t end=start+n;if(count==1){NH_NEED(type==1&&n>=29&&b.p[(size_t)at]==4);uint16_t hold=xx_data_get_u16(b.p+(size_t)at+3, 2, 0, true);NH_NEED(!hold||hold>=3);uint64_t olen=b.p[(size_t)at+9];NH_NEED(olen==n-29&&nh_add(f,s,&b,"bgp-open",start,29));at+=10;while(at<end){uint64_t p=at;NH_NEED(th_take(&b,&at,end,2));unsigned t=b.p[(size_t)p],len=b.p[(size_t)p+1];NH_NEED(t==2&&len&&th_take(&b,&at,end,len));uint64_t c=p+2;while(c<at){NH_NEED(th_take(&b,&c,at,2));unsigned cl=b.p[(size_t)c-1];NH_NEED(th_take(&b,&c,at,cl));}NH_NEED(nh_add(f,s,&b,"capabilities",p,at-p));}}else NH_NEED(type==4&&n==19&&nh_add(f,s,&b,"bgp-keepalive",start,19));at=end;}NH_NEED(count==2);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_bgp_messages_init(xx_bgp_messages *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_BGP_MESSAGES,"bin");}}
xx_bgp_messages *xx_bgp_messages_create(xx_io_device *d,int64_t b){xx_bgp_messages *r=(xx_bgp_messages *)xx_mem_alloc(sizeof(*r));if(r)xx_bgp_messages_init(r,d,b);return r;}
void xx_bgp_messages_destroy(xx_bgp_messages *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_bgp_messages_free(xx_bgp_messages *r){if(r){xx_bgp_messages_destroy(r);xx_mem_free(r);}}
bool xx_bgp_messages_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_bgp_messages_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
