/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2408.html */
#include "xxfclib/formats/isakmp_message/xx_isakmp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool attributes(memory_blob *b,uint64_t p,uint64_t end){unsigned count=0;while(p<end){if(++count>256||!protocol_take(b,&p,end,4))return false;uint16_t t=xx_data_get_u16(b->p+(size_t)p-4, 2, 0, true),n=xx_data_get_u16(b->p+(size_t)p-2, 2, 0, true);if(!(t&32767))return false;if(!(t&32768)&&(!n||!protocol_take(b,&p,end,n)))return false;}return p==end;}
static bool proposals(memory_blob *b,uint64_t p,uint64_t end){unsigned count=0;while(p<end){uint64_t start=p;if(++count>256||!protocol_take(b,&p,end,8))return false;unsigned next=b->p[(size_t)start],spi=b->p[(size_t)start+6],nt=b->p[(size_t)start+7];uint64_t n=xx_data_get_u16(b->p+(size_t)start+2, 2, 0, true);if(b->p[(size_t)start+1]||n<8||!record_span(start,n,end)||!blob_span(b,start,n)||(next!=0&&next!=2)||!b->p[(size_t)start+4]||b->p[(size_t)start+5]!=1||spi||!nt)return false;uint64_t pe=start+n;unsigned tc=0;while(p<pe){uint64_t t=p;if(++tc>nt||!protocol_take(b,&p,pe,8))return false;unsigned tn=b->p[(size_t)t];uint64_t z=xx_data_get_u16(b->p+(size_t)t+2, 2, 0, true);if((tn!=0&&tn!=3)||b->p[(size_t)t+1]||!b->p[(size_t)t+4]||b->p[(size_t)t+5]!=1||!blob_zero(b,t+6,2)||z<8||!record_span(t,z,pe)||!attributes(b,t+8,t+z))return false;p=t+z;if((p==pe)!=(tn==0))return false;}if(tc!=nt||(pe==end)!=(next==0))return false;}return count>0&&p==end;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=48&&b.n<=65535&&b.p[16]==1&&b.p[17]==16&&b.p[18]==2&&b.p[19]==0&&blob_zero(&b,20,4)&&xx_data_get_u32(b.p+24, 4, 0, true)==b.n);BLOB_NEED(!b.p[28]&&!b.p[29]&&xx_data_get_u16(b.p+30, 2, 0, true)==b.n-28&&xx_data_get_u32(b.p+32, 4, 0, true)==1&&xx_data_get_u32(b.p+36, 4, 0, true)==1&&proposals(&b,40,b.n));BLOB_NEED(blob_add(f,s,&b,"isakmp-header",0,28)&&blob_add(f,s,&b,"sa-doi-situation",28,12));uint64_t at=40;while(at<b.n){uint64_t n=xx_data_get_u16(b.p+(size_t)at+2, 2, 0, true);BLOB_NEED(blob_add(f,s,&b,"proposal-transforms",at,n));at+=n;}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_isakmp_message_init(xx_isakmp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ISAKMP_MESSAGE,"bin");}}
xx_isakmp_message *xx_isakmp_message_create(xx_io_device *d,int64_t b){xx_isakmp_message *r=(xx_isakmp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_isakmp_message_init(r,d,b);return r;}
void xx_isakmp_message_destroy(xx_isakmp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_isakmp_message_free(xx_isakmp_message *r){if(r){xx_isakmp_message_destroy(r);xx_mem_free(r);}}
bool xx_isakmp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_isakmp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
