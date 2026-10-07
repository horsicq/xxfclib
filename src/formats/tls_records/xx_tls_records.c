/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc8446.html */
#include "xxfclib/formats/tls_records/xx_tls_records.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_thirteenth_wrappers.h"

static bool vector(nh_blob *b,uint64_t *p,uint64_t end,unsigned width,uint64_t *start,uint64_t *n){if(!th_take(b,p,end,width))return false;*n=width==1?b->p[(size_t)*p-1]:xx_data_get_u16(b->p+(size_t)*p-2, 2, 0, true);*start=*p;return th_take(b,p,end,*n);}
static bool hello(nh_blob *b,uint64_t p,uint64_t end,unsigned type){uint64_t start,n;NH_NEED(th_take(b,&p,end,34)&&xx_data_get_u16(b->p+(size_t)p-34, 2, 0, true)==0x303&&vector(b,&p,end,1,&start,&n)&&n<=32);if(type==1){NH_NEED(vector(b,&p,end,2,&start,&n)&&n>=2&&!(n&1)&&vector(b,&p,end,1,&start,&n)&&n==1&&!b->p[(size_t)start]);}else NH_NEED(th_take(b,&p,end,3)&&xx_data_get_u16(b->p+(size_t)p-3, 2, 0, true)&&!b->p[(size_t)p-1]);NH_NEED(vector(b,&p,end,2,&start,&n)&&p==end&&n);p=start;uint16_t seen[256];unsigned count=0;while(p<end){NH_NEED(count<256&&th_take(b,&p,end,4));uint16_t code=xx_data_get_u16(b->p+(size_t)p-4, 2, 0, true),len=xx_data_get_u16(b->p+(size_t)p-2, 2, 0, true);for(unsigned i=0;i<count;++i)NH_NEED(seen[i]!=code);seen[count++]=code;start=p;NH_NEED(th_take(b,&p,end,len));if(code==43){if(type==2)NH_NEED(len==2&&xx_data_get_u16(b->p+(size_t)start, 2, 0, true)==0x304);else NH_NEED(len>=3&&b->p[(size_t)start]==len-1&&!(b->p[(size_t)start]&1));}else if(code==51){uint64_t q=start,limit=p;if(type==1){NH_NEED(vector(b,&q,p,2,&start,&n)&&q==p);q=start;}while(q<limit){NH_NEED(th_take(b,&q,limit,4));n=xx_data_get_u16(b->p+(size_t)q-2, 2, 0, true);NH_NEED(n&&th_take(b,&q,limit,n));if(type==2)NH_NEED(q==limit);}}else if(code==10||code==13||code==50){NH_NEED(len>=4&&xx_data_get_u16(b->p+(size_t)start, 2, 0, true)==len-2&&!((len-2)&1));}else if(code==11){NH_NEED(len>=2&&b->p[(size_t)start]==len-1);}else if(code==0){if(type==2)NH_NEED(!len);else{uint64_t q=start;NH_NEED(th_take(b,&q,p,2)&&xx_data_get_u16(b->p+(size_t)start, 2, 0, true)==len-2);while(q<p){NH_NEED(th_take(b,&q,p,3)&&!b->p[(size_t)q-3]);n=xx_data_get_u16(b->p+(size_t)q-2, 2, 0, true);NH_NEED(n&&ec_utf(b,q,n)&&th_take(b,&q,p,n));}}}}
 return p==end;done:return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=0,start,n,p,end;unsigned records=0,hellos=0,encrypted=0;bool ok=false;if(!nh_load(f,&b,pd))return false;while(at<b.n){start=at;NH_NEED(++records<=1024&&th_take(&b,&at,b.n,5));unsigned type=b.p[(size_t)start];uint16_t version=xx_data_get_u16(b.p+(size_t)start+1, 2, 0, true);n=xx_data_get_u16(b.p+(size_t)start+3, 2, 0, true);NH_NEED(version>=0x301&&version<=0x303&&n&&n<=16640&&(type==20||type==22||type==23));p=at;NH_NEED((type==23||n<=16384)&& (version==0x303||(records==1&&type==22))&&th_take(&b,&at,b.n,n)&&nh_add(f,s,&b,"record-header",start,5));end=at;
 if(type==22){NH_NEED(!encrypted);while(p<end){start=p;NH_NEED(th_take(&b,&p,end,4));unsigned h=b.p[(size_t)start];n=((uint64_t)b.p[(size_t)start+1]<<16)|((uint64_t)b.p[(size_t)start+2]<<8)|b.p[(size_t)start+3];NH_NEED((h==1||h==2)&&n&&eh_span(p,n,end)&&hello(&b,p,p+n,h)&&nh_add(f,s,&b,"hello-handshake",start,n+4));p+=n;++hellos;}}else if(type==20){NH_NEED(hellos&&end-p==1&&b.p[(size_t)p]==1&&nh_add(f,s,&b,"compatibility-ccs",p,1));}else{NH_NEED(hellos&&end-p>=17&&nh_add(f,s,&b,"encoded-protected-record",p,end-p));++encrypted;}}
 NH_NEED(hellos>=1&&encrypted>=1);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_tls_records_init(xx_tls_records *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TLS_RECORDS,"bin");}}
xx_tls_records *xx_tls_records_create(xx_io_device *d,int64_t b) {xx_tls_records *r=(xx_tls_records *)xx_mem_alloc(sizeof(*r));if(r)xx_tls_records_init(r,d,b);return r;}
void xx_tls_records_destroy(xx_tls_records *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tls_records_free(xx_tls_records *r) {if(r){xx_tls_records_destroy(r);xx_mem_free(r);}}
bool xx_tls_records_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tls_records_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
